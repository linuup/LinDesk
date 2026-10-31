#include "ui/settings/pages/LinWidgetsSettingsPage.h"
#include "ui/settings/SettingsUICommon.h"
#include "desktop/widgets/linwidget/ConfigManager.h"
#include "desktop/widgets/linwidget/LinWidgetProjectTools.h"
#include "desktop/widgets/runtime_export.h"
#include "desktop/runtime/RuntimeInstaller.h"
#include "desktop/widgets/linwidget/WidgetPackage.h"
#include "desktop/widgets/linwidget/WidgetRegistryManager.h"
#include "desktop/ListView.h"
#include "desktop/LinDesktop.h"
#include "app/LanguageManager.h"
#include "app/AppFonts.h"
#include "services/AppPaths.h"
#include "ui/Fluent/FluentToggleSwitch.h"
#include "ui/Fluent/FluentButton.h"
#include "ui/Fluent/FluentCheckBox.h"
#include "ui/Fluent/FluentComboBox.h"
#include "ui/Fluent/FluentCard.h"
#include "ui/Fluent/FluentDialog.h"
#include "ui/Fluent/FluentInput.h"
#include "ui/Fluent/FluentBaseWidget.h"
#include <QInputDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QStackedWidget>
#include <QListWidget>
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextCursor>
#include <QDateTime>
#include <QMessageBox>
#include <QSettings>

using namespace SettingsUICommon;

LinWidgetsSettingsPage::LinWidgetsSettingsPage(QWidget* parent)
    : QWidget(parent)
{
    initUI();
}

LinWidgetsSettingsPage::~LinWidgetsSettingsPage()
{
    if (m_linWidgetsLogWatcher) {
        delete m_linWidgetsLogWatcher;
        m_linWidgetsLogWatcher = nullptr;
    }
    if (m_linWidgetsLogReloadTimer) {
        delete m_linWidgetsLogReloadTimer;
        m_linWidgetsLogReloadTimer = nullptr;
    }
}

void LinWidgetsSettingsPage::reloadPage()
{
    reloadLinWidgetsSettingsPage();
}

void LinWidgetsSettingsPage::updateTranslations()
{
    if (m_linWidgetsPageTitle) {
        m_linWidgetsPageTitle->setText(tr("LinWidgets"));
    }
}

void LinWidgetsSettingsPage::openDrawer(QWidget* content, const QString& title, int width)
{
    if (auto* base = dynamic_cast<FluentBaseWidget*>(window())) {
        base->openDrawer(content, title, width);
    }
}

void LinWidgetsSettingsPage::closeDrawer()
{
    if (auto* base = dynamic_cast<FluentBaseWidget*>(window())) {
        base->closeDrawer();
    }
}


class MenuListMousePressFilter : public QObject {
public:
    MenuListMousePressFilter(QListWidget* list, std::function<bool(const QPoint&, Qt::KeyboardModifiers)> mousePressHandler)
        : QObject(list), m_list(list), m_mousePressHandler(std::move(mousePressHandler))
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (!m_list || watched != m_list->viewport() || event->type() != QEvent::MouseButtonPress) {
            return QObject::eventFilter(watched, event);
        }

        const QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        if (m_mousePressHandler && m_mousePressHandler(mouseEvent->pos(), mouseEvent->modifiers())) {
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QListWidget* m_list = nullptr;
    std::function<bool(const QPoint&, Qt::KeyboardModifiers)> m_mousePressHandler;
};

static QWidget* createSettingsLinWidgetRow(QListWidget* list,
                                          QListWidgetItem* item,
                                          const QString& id,
                                          const QString& name,
                                          const QString& version,
                                          const QString& description,
                                          const QString& iconPath,
                                          const QString& badge,
                                          QWidget* parent,
                                          std::function<void(QListWidgetItem*)> openHandler = nullptr)
{
    QWidget* row = new QWidget(parent);
    row->setObjectName(QStringLiteral("linWidgetStoreRow"));
    row->setAttribute(Qt::WA_StyledBackground, true);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(20, 14, 20, 14);
    rowLayout->setSpacing(18);

    rowLayout->addWidget(createWidgetIconLabel(id, iconPath, row));

    auto* titleLayout = new QVBoxLayout();
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(4);
    auto* nameRow = new QHBoxLayout();
    nameRow->setContentsMargins(0, 0, 0, 0);
    nameRow->setSpacing(8);
    auto* nameLabel = new QLabel(name.isEmpty() ? id : name, row);
    nameLabel->setStyleSheet(QStringLiteral("color:#10182d;font-size:15px;font-weight:800;background:transparent;"));
    nameRow->addWidget(nameLabel);
    if (!badge.isEmpty()) {
        auto* badgeLabel = new QLabel(badge, row);
        badgeLabel->setStyleSheet(QStringLiteral("color:#1771e6;background:#eaf3ff;border-radius:5px;padding:2px 7px;font-size:11px;font-weight:800;"));
        nameRow->addWidget(badgeLabel);
    }
    nameRow->addStretch();
    auto* descriptionLabel = new QLabel(description.trimmed().isEmpty() ? QObject::tr("No description") : description.trimmed(), row);
    descriptionLabel->setWordWrap(true);
    descriptionLabel->setStyleSheet(QStringLiteral("color:#7a8498;font-size:12px;font-weight:600;background:transparent;"));
    titleLayout->addLayout(nameRow);
    titleLayout->addWidget(descriptionLabel);
    rowLayout->addLayout(titleLayout, 2);

    rowLayout->addWidget(createWidgetMetaLabel(QObject::tr("Version"), version.isEmpty() ? QStringLiteral("unknown") : version, row));

    row->installEventFilter(list);
    const QList<QWidget*> children = row->findChildren<QWidget*>();
    for (QWidget* child : children) {
        if (!qobject_cast<QAbstractButton*>(child)) {
            child->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        }
    }
    if (openHandler) {
        row->setCursor(Qt::PointingHandCursor);
        row->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        row->installEventFilter(new MenuListMousePressFilter(list, [item, openHandler](const QPoint&, Qt::KeyboardModifiers) {
            openHandler(item);
            return true;
        }));
    }

    return row;
}

namespace {
QString runtimeLinWidgetsDirForSettings()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/LinWidgets"));
}

QString marketplaceFilePathForSettings()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/LinWidgetMarketplace/marketplace.json"));
}

QString safeLinWidgetFileName(QString id)
{
    for (QChar& ch : id) {
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('.') && ch != QLatin1Char('-') && ch != QLatin1Char('_')) {
            ch = QLatin1Char('_');
        }
    }
    return id.isEmpty() ? QStringLiteral("widget") : id;
}

bool settingsSkipLinWidgetCopyEntry(const QString& name)
{
    return name == QStringLiteral(".git") ||
        name == QStringLiteral("node_modules") ||
        name.compare(QStringLiteral("cache"), Qt::CaseInsensitive) == 0 ||
        name.compare(QStringLiteral("tmp"), Qt::CaseInsensitive) == 0 ||
        name.compare(QStringLiteral("temp"), Qt::CaseInsensitive) == 0;
}

bool settingsCopyLinWidgetDirectory(const QString& sourcePath, const QString& targetPath, QString* errorMessage)
{
    const QDir source(sourcePath);
    if (!source.exists()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Source widget directory does not exist: %1").arg(sourcePath);
        }
        return false;
    }
    if (!QDir().mkpath(targetPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot create widget directory: %1").arg(targetPath);
        }
        return false;
    }

    const QFileInfoList entries = source.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries, QDir::Name);
    for (const QFileInfo& entry : entries) {
        if (settingsSkipLinWidgetCopyEntry(entry.fileName())) {
            continue;
        }
        const QString targetEntryPath = QDir(targetPath).filePath(entry.fileName());
        if (entry.isDir()) {
            if (!settingsCopyLinWidgetDirectory(entry.absoluteFilePath(), targetEntryPath, errorMessage)) {
                return false;
            }
        } else {
            QFile::remove(targetEntryPath);
            if (!QFile::copy(entry.absoluteFilePath(), targetEntryPath)) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Cannot copy widget file: %1").arg(entry.absoluteFilePath());
                }
                return false;
            }
        }
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool settingsRemoveDirectory(const QString& path, QString* errorMessage)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        if (errorMessage) {
            errorMessage->clear();
        }
        return true;
    }
    if (!info.isDir() || !QDir(path).removeRecursively()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot remove directory: %1").arg(path);
        }
        return false;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

QString settingsInstalledTargetForManifest(const LinWidget::WidgetManifest& manifest)
{
    QString safe = manifest.id.trimmed();
    for (QChar& ch : safe) {
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('.') && ch != QLatin1Char('-') && ch != QLatin1Char('_')) {
            ch = QLatin1Char('_');
        }
    }
    if (safe.isEmpty()) {
        safe = QStringLiteral("widget");
    }
    return QDir(AppPaths::userLinWidgetsDir()).filePath(safe);
}

QString safeLinWidgetInstanceFileNameForSettings(QString instanceId)
{
    for (QChar& ch : instanceId) {
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('.') && ch != QLatin1Char('-') && ch != QLatin1Char('_')) {
            ch = QLatin1Char('_');
        }
    }
    return instanceId.isEmpty() ? QStringLiteral("widget") : instanceId;
}

bool settingsEnsureDefaultLinWidgetsInstalled()
{
    // Do not auto-install any default widgets on first launch.
    const QString targetRoot = AppPaths::userLinWidgetsDir();
    const QString markerPath = QDir(targetRoot).filePath(QStringLiteral(".defaults-installed"));
    if (QFileInfo::exists(markerPath)) {
        return true;
    }

    QDir().mkpath(targetRoot);
    QFile marker(markerPath);
    if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        marker.write("1\n");
    }
    return true;
}

bool settingsRemoveLinWidgetLayoutState(const QString& instanceId, const QString& packagePath)
{
    QVector<LinWidget::WidgetLayoutState> states;
    QString errorMessage;
    if (!LinWidget::ConfigManager::loadLayout(LinWidget::ConfigManager::defaultConfigPath(), &states, &errorMessage)) {
        return false;
    }

    const QString packageKey = QDir::cleanPath(QFileInfo(packagePath).absoluteFilePath()).toLower();
    QVector<LinWidget::WidgetLayoutState> keptStates;
    keptStates.reserve(states.size());
    for (const LinWidget::WidgetLayoutState& state : states) {
        const QString statePackageKey = QDir::cleanPath(QFileInfo(state.packagePath).absoluteFilePath()).toLower();
        if (state.instanceId == instanceId || (!packageKey.isEmpty() && statePackageKey == packageKey)) {
            continue;
        }
        keptStates.append(state);
    }
    return LinWidget::ConfigManager::saveLayout(LinWidget::ConfigManager::defaultConfigPath(), keptStates, nullptr);
}

constexpr int SettingsLinWidgetPackagePathRole = Qt::UserRole;
constexpr int SettingsLinWidgetRegistryIdRole = Qt::UserRole + 1;
constexpr int SettingsLinWidgetRuntimeTypeRole = Qt::UserRole + 2;
constexpr int SettingsLinWidgetDefaultSizeRole = Qt::UserRole + 3;
constexpr int SettingsLinWidgetDevelopmentRole = Qt::UserRole + 4;
constexpr int SettingsLinWidgetNameRole = Qt::UserRole + 5;
constexpr int SettingsLinWidgetVersionRole = Qt::UserRole + 6;
constexpr int SettingsLinWidgetAuthorRole = Qt::UserRole + 7;
constexpr int SettingsLinWidgetDescriptionRole = Qt::UserRole + 8;
constexpr int SettingsLinWidgetAutoLoadRole = Qt::UserRole + 9;
constexpr int SettingsLinWidgetGrantedPermissionsRole = Qt::UserRole + 10;
constexpr int SettingsLinWidgetRequestedPermissionsRole = Qt::UserRole + 11;
constexpr int SettingsLinWidgetSourcePathRole = Qt::UserRole + 12;
constexpr int SettingsLinWidgetCategoryRole = Qt::UserRole + 13;
constexpr int SettingsLinWidgetTrustedRole = Qt::UserRole + 14;
constexpr int SettingsLinWidgetOfficialInstalledPathRole = Qt::UserRole + 15;
constexpr int SettingsLinWidgetManifestValidRole = Qt::UserRole + 16;
constexpr int SettingsLinWidgetManifestErrorRole = Qt::UserRole + 17;
constexpr int SettingsLinWidgetIconPathRole = Qt::UserRole + 18;
constexpr int SettingsLinWidgetMultiInstanceRole = Qt::UserRole + 19;

QString settingsPermissionsText(const QStringList& permissions)
{
    return permissions.isEmpty() ? QStringLiteral("none") : permissions.join(QStringLiteral(", "));
}

QString settingsPermissionDescription(const QString& permission)
{
    const QString normalized = permission.trimmed().toLower();
    if (normalized == QStringLiteral("network")) {
        return QObject::tr("Allow this widget to access external network services.");
    }
    if (normalized == QStringLiteral("storage")) {
        return QObject::tr("Allow this widget to save local browser state.");
    }
    if (normalized == QStringLiteral("desktop")) {
        return QObject::tr("Allow this widget to use future privileged desktop APIs.");
    }
    return QObject::tr("Allow this widget to use this permission.");
}

QStringList promptLinWidgetInstallPermissions(QWidget* parent, const LinWidget::WidgetManifest& manifest, bool* accepted)
{
    QStringList requestedPermissions = manifest.permissions;
    requestedPermissions.removeDuplicates();
    requestedPermissions.sort();
    if (accepted) {
        *accepted = false;
    }

    FluentDialog dialog(parent);
    dialog.setDialogTitle(QObject::tr("Install LinWidget"));
    dialog.setMessage(QObject::tr("%1 requests the following permissions. Checked permissions will be granted during installation.")
                          .arg(manifest.name.isEmpty() ? manifest.id : manifest.name));

    QVector<QPair<QString, FluentCheckBox*>> permissionChecks;
    if (requestedPermissions.isEmpty()) {
        auto* emptyLabel = new QLabel(QObject::tr("This widget does not request sensitive permissions."), &dialog);
        emptyLabel->setWordWrap(true);
        emptyLabel->setStyleSheet(QStringLiteral("color:#66738c;font-size:13px;font-weight:600;background:transparent;"));
        dialog.contentLayout()->addWidget(emptyLabel);
    } else {
        for (const QString& permission : requestedPermissions) {
            auto* row = new QWidget(&dialog);
            row->setMinimumWidth(0);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 6, 0, 6);
            rowLayout->setSpacing(10);

            auto* check = new FluentCheckBox(row);
            check->setChecked(true);
            auto* label = new QLabel(QStringLiteral("<b>%1</b><br><span style='color:#66738c;'>%2</span>")
                                         .arg(permission.toHtmlEscaped(), settingsPermissionDescription(permission).toHtmlEscaped()),
                                     row);
            label->setTextFormat(Qt::RichText);
            label->setWordWrap(true);
            label->setMinimumWidth(0);
            label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            rowLayout->addWidget(check, 0, Qt::AlignTop);
            rowLayout->addWidget(label, 1, Qt::AlignVCenter);
            dialog.contentLayout()->addWidget(row);
            permissionChecks.append(qMakePair(permission, check));
        }
    }

    bool confirmed = false;
    dialog.addActionButton(QObject::tr("Install"), FluentDialog::ActionType::Primary, [&]() {
        confirmed = true;
    });
    dialog.addActionButton(QObject::tr("Cancel"), FluentDialog::ActionType::Secondary);
    dialog.exec();

    if (!confirmed) {
        return QStringList();
    }

    QStringList grantedPermissions;
    for (const auto& pair : permissionChecks) {
        if (pair.second && pair.second->isChecked()) {
            grantedPermissions.append(pair.first);
        }
    }
    grantedPermissions.removeDuplicates();
    grantedPermissions.sort();
    if (accepted) {
        *accepted = true;
    }
    return grantedPermissions;
}

QString settingsWidgetStateText(const QHash<QString, QString>& runningById,
                                const QHash<QString, QString>& runningByPackage,
                                const QHash<QString, int>& runningCountById,
                                const QString& registryKey,
                                const QString& packageKey,
                                bool multiInstance)
{
    if (multiInstance) {
        const int count = runningCountById.value(registryKey, 0);
        return count > 0
            ? QStringLiteral("running %1/3").arg(count)
            : QStringLiteral("stopped");
    }
    return runningById.contains(registryKey) || runningByPackage.contains(packageKey)
        ? QStringLiteral("running")
        : QStringLiteral("stopped");
}

QString settingsWidgetItemText(const QString& name,
                               const QString& id,
                               const QString& version,
                               bool isDevelopment,
                               bool autoLoad,
                               const QString& path,
                               const QString& state)
{
    return QStringLiteral("%1\nid: %2\nversion: %3\nmode: %4    state: %5    autoLoad: %6\npath: %7")
        .arg(name.isEmpty() ? id : name)
        .arg(id)
        .arg(version.isEmpty() ? QStringLiteral("unknown") : version)
        .arg(isDevelopment ? QStringLiteral("dev") : QStringLiteral("installed"))
        .arg(state)
        .arg(autoLoad ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(path);
}
}

void LinWidgetsSettingsPage::initUI()
{
    auto* page = this;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(54, 34, 54, 36);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignTop);

    m_linWidgetsPageTitle = createPageTitle(tr("LinWidgets"), page);
    m_linWidgetsPageTitle->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(m_linWidgetsPageTitle, 0, Qt::AlignLeft);
    layout->addSpacing(8);

    layout->addWidget(createPageSubtitle(tr("Make your desktop more powerful and beautiful"), page), 0, Qt::AlignLeft);
    layout->addSpacing(24);

    auto* topCards = new QWidget(page);
    auto* topCardsLayout = new QHBoxLayout(topCards);
    topCardsLayout->setContentsMargins(0, 0, 0, 0);
    topCardsLayout->setSpacing(16);

    auto* runtimePanel = createPanel(topCards);
    runtimePanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* runtimeLayout = new QHBoxLayout(runtimePanel);
    runtimeLayout->setContentsMargins(22, 18, 22, 18);
    runtimeLayout->setSpacing(14);
    auto* runtimeIcon = new QLabel(glyph(0xE7B8), runtimePanel);
    runtimeIcon->setAlignment(Qt::AlignCenter);
    runtimeIcon->setFixedSize(46, 46);
    runtimeIcon->setStyleSheet(QStringLiteral(
        "QLabel {"
        " color:#1771e6;"
        " background:#eef6ff;"
        " border:1px solid #cfe2ff;"
        " border-radius:12px;"
        " font-family:'Segoe Fluent Icons','Segoe MDL2 Assets';"
        " font-size:20px;"
        "}"));
    runtimeLayout->addWidget(runtimeIcon, 0, Qt::AlignVCenter);
    auto* runtimeText = new QVBoxLayout();
    runtimeText->setContentsMargins(0, 0, 0, 0);
    runtimeText->setSpacing(4);
    runtimeText->addWidget(createCardTitle(tr("Widget Runtime"), runtimePanel));
    runtimeText->addWidget(createCardSubtitle(tr("Manage LinWidget runtime environment. Closing it will release widget resources."), runtimePanel));
    runtimeLayout->addLayout(runtimeText, 1);
    m_linWidgetsRuntimeToggle = new FluentToggleSwitch(runtimePanel);
    QSettings settings(settingsConfigPath(), QSettings::IniFormat);
    m_linWidgetsRuntimeToggle->setChecked(settings.value("LinWidgets/Enabled", true).toBool(), false);
    runtimeLayout->addWidget(m_linWidgetsRuntimeToggle, 0, Qt::AlignVCenter);
    topCardsLayout->addWidget(runtimePanel, 1);

    layout->addWidget(topCards);
    layout->addSpacing(14);

    auto* subNavContainer = new QWidget(page);
    subNavContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* subNavLayout = new QHBoxLayout(subNavContainer);
    subNavLayout->setContentsMargins(0, 0, 0, 0);
    subNavLayout->setSpacing(8);

    auto* segmentedBar = new QWidget(subNavContainer);
    segmentedBar->setObjectName(QStringLiteral("linWidgetSubsystemBar"));
    segmentedBar->setFixedHeight(38);
    segmentedBar->setStyleSheet(QStringLiteral(
        "QWidget#linWidgetSubsystemBar {"
        "  background: #f1f5f9;"
        "  border: 1px solid #e2e8f0;"
        "  border-radius: 8px;"
        "}"));
    auto* segmentedLayout = new QHBoxLayout(segmentedBar);
    segmentedLayout->setContentsMargins(3, 3, 3, 3);
    segmentedLayout->setSpacing(4);

    m_subsystemWidgetsBtn = new QPushButton(tr("Desktop Widgets"), segmentedBar);
    m_subsystemWidgetsBtn->setCursor(Qt::PointingHandCursor);
    m_subsystemWidgetsBtn->setFixedHeight(30);

    m_subsystemWallpaperBtn = new QPushButton(tr("Live Wallpaper"), segmentedBar);
    m_subsystemWallpaperBtn->setCursor(Qt::PointingHandCursor);
    m_subsystemWallpaperBtn->setFixedHeight(30);

    auto updateSubsystemButtons = [this](int activeIndex) {
        auto styleBtn = [](QPushButton* btn, bool active) {
            btn->setStyleSheet(active ? QStringLiteral(
                "QPushButton {"
                "  background: #ffffff;"
                "  color: #0f172a;"
                "  font-weight: 700;"
                "  font-size: 13px;"
                "  border: 1px solid #cbd5e1;"
                "  border-radius: 6px;"
                "  padding: 0 16px;"
                "}"
            ) : QStringLiteral(
                "QPushButton {"
                "  background: transparent;"
                "  color: #64748b;"
                "  font-weight: 500;"
                "  font-size: 13px;"
                "  border: none;"
                "  border-radius: 6px;"
                "  padding: 0 16px;"
                "}"
                "QPushButton:hover {"
                "  background: rgba(255, 255, 255, 0.6);"
                "  color: #1e293b;"
                "}"
            ));
        };
        styleBtn(m_subsystemWidgetsBtn, activeIndex == 0);
        styleBtn(m_subsystemWallpaperBtn, activeIndex == 1);
        if (m_linWidgetSubsystemStack) {
            m_linWidgetSubsystemStack->setCurrentIndex(activeIndex);
        }
        if (activeIndex == 1) {
            refreshWallpaperSubsystemState();
        }
    };

    connect(m_subsystemWidgetsBtn, &QPushButton::clicked, this, [updateSubsystemButtons]() {
        updateSubsystemButtons(0);
    });
    connect(m_subsystemWallpaperBtn, &QPushButton::clicked, this, [updateSubsystemButtons]() {
        updateSubsystemButtons(1);
    });

    segmentedLayout->addWidget(m_subsystemWidgetsBtn);
    segmentedLayout->addWidget(m_subsystemWallpaperBtn);
    subNavLayout->addWidget(segmentedBar);
    subNavLayout->addStretch();

    layout->addWidget(subNavContainer);
    layout->addSpacing(10);

    m_linWidgetSubsystemStack = new QStackedWidget(page);
    layout->addWidget(m_linWidgetSubsystemStack, 1);

    m_linWidgetsTabs = new QTabWidget(m_linWidgetSubsystemStack);
    m_linWidgetsTabs->setDocumentMode(true);
    m_linWidgetsTabs->setObjectName(QStringLiteral("linWidgetsTabs"));
    m_linWidgetSubsystemStack->addWidget(m_linWidgetsTabs);

    m_wallpaperSubsystemPage = createWallpaperSubsystemPage(m_linWidgetSubsystemStack);
    m_linWidgetSubsystemStack->addWidget(m_wallpaperSubsystemPage);

    updateSubsystemButtons(0);

    auto createListPage = [this, page](QListWidget*& list, const QString& title, const QString& subtitle) {
        auto* tab = new QWidget(page);
        auto* tabLayout = new QVBoxLayout(tab);
        tabLayout->setContentsMargins(0, 16, 0, 0);
        tabLayout->setSpacing(0);
        auto* panel = createPanel(tab);
        auto* panelLayout = new QVBoxLayout(panel);
        panelLayout->setContentsMargins(22, 18, 22, 18);
        panelLayout->setSpacing(10);
        panelLayout->addWidget(createCardTitle(title, panel));
        panelLayout->addWidget(createCardSubtitle(subtitle, panel));
        list = new QListWidget(panel);
        list->setObjectName(QStringLiteral("linWidgetSettingsList"));
        list->setSelectionMode(QAbstractItemView::SingleSelection);
        list->setMinimumHeight(320);
        list->setWordWrap(true);
        list->setMouseTracking(true);
        panelLayout->addWidget(list, 1);
        tabLayout->addWidget(panel, 1);
        return tab;
    };

    m_linWidgetsTabs->addTab(createListPage(m_officialLinWidgetsList,
                                            tr("Official"),
                                            tr("Install widgets provided by LinDesk.")),
                             tr("Official"));
    m_linWidgetsTabs->addTab(createListPage(m_marketplaceLinWidgetsList,
                                            tr("Marketplace"),
                                            tr("Browse the local market source, online discovery capability will be opened later.")),
                             tr("Marketplace"));

    auto* installedTab = new QWidget(page);
    auto* installedLayout = new QGridLayout(installedTab);
    installedLayout->setContentsMargins(0, 16, 0, 0);
    installedLayout->setHorizontalSpacing(16);
    installedLayout->setVerticalSpacing(0);
    auto* installedListPanel = createPanel(installedTab);
    auto* installedListLayout = new QVBoxLayout(installedListPanel);
    installedListLayout->setContentsMargins(22, 18, 22, 18);
    installedListLayout->setSpacing(10);
    installedListLayout->addWidget(createCardTitle(tr("Installed"), installedListPanel));
    installedListLayout->addWidget(createCardSubtitle(tr("Manage widgets that have been copied to the Roaming directory."), installedListPanel));
    m_installedLinWidgetsList = new QListWidget(installedListPanel);
    m_installedLinWidgetsList->setObjectName(QStringLiteral("linWidgetSettingsList"));
    m_installedLinWidgetsList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_installedLinWidgetsList->setMinimumHeight(340);
    m_installedLinWidgetsList->setWordWrap(true);
    m_installedLinWidgetsList->setMouseTracking(true);
    installedListLayout->addWidget(m_installedLinWidgetsList, 1);
    installedLayout->addWidget(installedListPanel, 0, 0, 1, 1);

    auto* inspectorPanel = createPanel(installedTab);
    auto* inspectorLayout = new QVBoxLayout(inspectorPanel);
    inspectorLayout->setContentsMargins(22, 18, 22, 18);
    inspectorLayout->setSpacing(10);
    inspectorLayout->addWidget(createCardTitle(tr("Inspector"), inspectorPanel));
    m_linWidgetsInspectorLabel = createCardSubtitle(tr("Select a widget to inspect details."), inspectorPanel);
    inspectorLayout->addWidget(m_linWidgetsInspectorLabel);
    inspectorLayout->addWidget(createDivider(inspectorPanel));
    inspectorLayout->addWidget(createCardSubtitle(tr("Permissions"), inspectorPanel));
    auto* permissionRow = new QHBoxLayout();
    permissionRow->setContentsMargins(0, 0, 0, 0);
    permissionRow->setSpacing(10);
    m_linWidgetNetworkPermissionCheck = new QCheckBox(QStringLiteral("network"), inspectorPanel);
    m_linWidgetStoragePermissionCheck = new QCheckBox(QStringLiteral("storage"), inspectorPanel);
    m_linWidgetDesktopPermissionCheck = new QCheckBox(QStringLiteral("desktop"), inspectorPanel);
    permissionRow->addWidget(m_linWidgetNetworkPermissionCheck);
    permissionRow->addWidget(m_linWidgetStoragePermissionCheck);
    permissionRow->addWidget(m_linWidgetDesktopPermissionCheck);
    permissionRow->addStretch();
    inspectorLayout->addLayout(permissionRow);
    inspectorLayout->addWidget(createDivider(inspectorPanel));
    inspectorLayout->addWidget(createCardSubtitle(tr("Actions"), inspectorPanel));
    auto* actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(8);
    m_linWidgetLoadButton = createSettingsActionButton(tr("Load"), inspectorPanel, true);
    m_linWidgetUnloadButton = createSettingsActionButton(tr("Unload"), inspectorPanel);
    m_linWidgetReloadButton = createSettingsActionButton(tr("Reload"), inspectorPanel);
    m_linWidgetOpenFolderButton = createSettingsActionButton(tr("Open Folder"), inspectorPanel);
    m_linWidgetDeleteButton = createSettingsActionButton(tr("Uninstall"), inspectorPanel, false, true);
    actionRow->addWidget(m_linWidgetLoadButton);
    actionRow->addWidget(m_linWidgetUnloadButton);
    actionRow->addWidget(m_linWidgetReloadButton);
    actionRow->addWidget(m_linWidgetOpenFolderButton);
    actionRow->addWidget(m_linWidgetDeleteButton);
    inspectorLayout->addLayout(actionRow);
    inspectorLayout->addWidget(createDivider(inspectorPanel));
    inspectorLayout->addWidget(createCardTitle(tr("Live Log"), inspectorPanel));
    m_linWidgetsLogView = new QPlainTextEdit(inspectorPanel);
    m_linWidgetsLogView->setReadOnly(true);
    m_linWidgetsLogView->setMinimumHeight(170);
    m_linWidgetsLogView->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background: #0f172a; color: #dbeafe; border: none; border-radius: 8px; padding: 8px; font-family: Consolas, 'Cascadia Mono', monospace; font-size: 12px; }"));
    inspectorLayout->addWidget(m_linWidgetsLogView, 1);
    inspectorLayout->addWidget(createCardTitle(tr("Recent Errors"), inspectorPanel));
    m_linWidgetsErrorsLabel = createCardSubtitle(tr("No errors."), inspectorPanel);
    inspectorLayout->addWidget(m_linWidgetsErrorsLabel);
    inspectorPanel->hide();
    installedLayout->addWidget(inspectorPanel, 0, 1, 1, 1);
    installedLayout->setColumnStretch(0, 1);
    installedLayout->setColumnStretch(1, 0);
    m_linWidgetsTabs->addTab(installedTab, tr("Installed"));

    auto* advancedTab = new QWidget(page);
    auto* advancedLayout = new QGridLayout(advancedTab);
    advancedLayout->setContentsMargins(0, 16, 0, 0);
    advancedLayout->setHorizontalSpacing(16);
    advancedLayout->setVerticalSpacing(0);
    auto* developmentListPanel = createPanel(advancedTab);
    auto* developmentListLayout = new QVBoxLayout(developmentListPanel);
    developmentListLayout->setContentsMargins(22, 18, 22, 18);
    developmentListLayout->setSpacing(10);
    developmentListLayout->addWidget(createCardTitle(tr("Development"), developmentListPanel));
    developmentListLayout->addWidget(createCardSubtitle(tr("Run widgets from source code directory and export local packages."), developmentListPanel));
    m_developmentLinWidgetsList = new QListWidget(developmentListPanel);
    m_developmentLinWidgetsList->setObjectName(QStringLiteral("linWidgetSettingsList"));
    m_developmentLinWidgetsList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_developmentLinWidgetsList->setWordWrap(true);
    m_developmentLinWidgetsList->setMinimumHeight(340);
    m_developmentLinWidgetsList->setMouseTracking(true);
    developmentListLayout->addWidget(m_developmentLinWidgetsList, 1);
    advancedLayout->addWidget(developmentListPanel, 0, 0, 1, 1);
    auto* advancedPanel = createPanel(advancedTab);
    auto* advancedPanelLayout = new QVBoxLayout(advancedPanel);
    advancedPanelLayout->setContentsMargins(22, 18, 22, 18);
    advancedPanelLayout->setSpacing(0);
    advancedPanelLayout->addWidget(createCardTitle(tr("Advanced Tools"), advancedPanel));
    advancedPanelLayout->addWidget(createCardSubtitle(tr("Create, add, run and export development widgets."), advancedPanel));
    advancedPanelLayout->addSpacing(12);
    m_linWidgetAddDevButton = createSettingsActionButton(tr("Add Development Project"), advancedPanel);
    m_linWidgetNewProjectButton = createSettingsActionButton(tr("New Widget Project"), advancedPanel);
    m_linWidgetExportZipButton = createSettingsActionButton(tr("Export Zip Package"), advancedPanel);
    m_linWidgetRemoveDevButton = createSettingsActionButton(tr("Remove Development Record"), advancedPanel, false, true);
    auto* openLogsButton = createSettingsActionButton(tr("Open Logs Directory"), advancedPanel);
    auto* openRoamingButton = createSettingsActionButton(tr("Open Roaming Directory"), advancedPanel);
    advancedPanelLayout->addWidget(m_linWidgetAddDevButton);
    advancedPanelLayout->addSpacing(8);
    advancedPanelLayout->addWidget(m_linWidgetNewProjectButton);
    advancedPanelLayout->addSpacing(8);
    advancedPanelLayout->addWidget(m_linWidgetExportZipButton);
    advancedPanelLayout->addSpacing(8);
    advancedPanelLayout->addWidget(m_linWidgetRemoveDevButton);
    advancedPanelLayout->addWidget(createDivider(advancedPanel));
    advancedPanelLayout->addWidget(openLogsButton);
    advancedPanelLayout->addSpacing(8);
    advancedPanelLayout->addWidget(openRoamingButton);
    advancedPanelLayout->addStretch();
    advancedLayout->addWidget(advancedPanel, 0, 1, 1, 1);
    advancedLayout->setColumnStretch(0, 1);
    advancedLayout->setColumnStretch(1, 1);
    advancedTab->hide();

    auto* footerPanel = createPanel(page);
    footerPanel->setGraphicsEffect(nullptr);
    auto* footerLayout = new QHBoxLayout(footerPanel);
    footerLayout->setContentsMargins(18, 12, 18, 12);
    footerLayout->setSpacing(12);
    m_linWidgetsStatusLabel = createCardSubtitle(QString(), footerPanel);
    footerLayout->addWidget(m_linWidgetsStatusLabel, 1);
    auto* footerRefreshButton = createSettingsActionButton(tr("Refresh"), footerPanel);
    footerLayout->addWidget(footerRefreshButton, 0, Qt::AlignVCenter);
    layout->addSpacing(12);
    layout->addWidget(footerPanel);

    m_linWidgetsLogWatcher = new QFileSystemWatcher(page);
    m_linWidgetsLogReloadTimer = new QTimer(page);
    m_linWidgetsLogReloadTimer->setSingleShot(true);
    m_linWidgetsLogReloadTimer->setInterval(120);
    connect(m_linWidgetsLogWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
        if (!path.isEmpty() && QFileInfo::exists(path) && !m_linWidgetsLogWatcher->files().contains(path)) {
            m_linWidgetsLogWatcher->addPath(path);
        }
        if (m_linWidgetsLogReloadTimer) {
            m_linWidgetsLogReloadTimer->start();
        }
    });
    connect(m_linWidgetsLogReloadTimer, &QTimer::timeout, this, &LinWidgetsSettingsPage::refreshLinWidgetSettingsInspector);

    connect(m_linWidgetsRuntimeToggle, &FluentToggleSwitch::toggled, this, [this](bool checked) {
        QSettings s(settingsConfigPath(), QSettings::IniFormat);
        s.setValue("LinWidgets/Enabled", checked);
        s.sync();
        auto& bridge = LinWidgetRuntimeBridge::instance();
        if (bridge.isLoaded()) {
            bridge.setEnabled(checked);
            if (checked) {
                bridge.restoreFromConfig(QString());
            }
        }
        reloadLinWidgetsSettingsPage();
    });
    connect(footerRefreshButton, &QPushButton::clicked, this, &LinWidgetsSettingsPage::reloadLinWidgetsSettingsPage);
    connect(openLogsButton, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::linWidgetLogsDir()));
    });
    connect(openRoamingButton, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::userLinWidgetsDir()));
    });
    connect(m_installedLinWidgetsList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current) {
        if (current && m_developmentLinWidgetsList) {
            m_developmentLinWidgetsList->clearSelection();
        }
        refreshLinWidgetSettingsButtons();
        refreshLinWidgetSettingsInspector();
    });
    connect(m_developmentLinWidgetsList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current) {
        if (current && m_installedLinWidgetsList) {
            m_installedLinWidgetsList->clearSelection();
        }
        refreshLinWidgetSettingsButtons();
        refreshLinWidgetSettingsInspector();
    });
    connect(m_installedLinWidgetsList, &QListWidget::itemClicked, this, &LinWidgetsSettingsPage::selectAndOpenLinWidgetSettingsItem);
    connect(m_developmentLinWidgetsList, &QListWidget::itemClicked, this, &LinWidgetsSettingsPage::selectAndOpenLinWidgetSettingsItem);
    connect(m_officialLinWidgetsList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        selectAndOpenLinWidgetSettingsItem(item);
    });
    connect(m_marketplaceLinWidgetsList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        selectAndOpenLinWidgetSettingsItem(item);
    });
    connect(m_linWidgetLoadButton, &QPushButton::clicked, this, &LinWidgetsSettingsPage::loadSelectedSettingsLinWidget);
    connect(m_linWidgetUnloadButton, &QPushButton::clicked, this, &LinWidgetsSettingsPage::unloadSelectedSettingsLinWidget);
    connect(m_linWidgetReloadButton, &QPushButton::clicked, this, &LinWidgetsSettingsPage::reloadSelectedSettingsLinWidget);
    connect(m_linWidgetOpenFolderButton, &QPushButton::clicked, this, [this]() {
        const QString path = selectedSettingsLinWidgetPackagePath();
        if (!path.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        }
    });
    connect(m_linWidgetDeleteButton, &QPushButton::clicked, this, &LinWidgetsSettingsPage::uninstallSelectedSettingsLinWidget);
    connect(m_linWidgetAddDevButton, &QPushButton::clicked, this, &LinWidgetsSettingsPage::addDevelopmentSettingsLinWidget);
    connect(m_linWidgetNewProjectButton, &QPushButton::clicked, this, &LinWidgetsSettingsPage::createDevelopmentSettingsLinWidget);
    connect(m_linWidgetExportZipButton, &QPushButton::clicked, this, &LinWidgetsSettingsPage::exportDevelopmentSettingsLinWidget);
    connect(m_linWidgetRemoveDevButton, &QPushButton::clicked, this, [this]() {
        auto& registry = LinWidget::WidgetRegistryManager::instance();
        registry.load();
        registry.removeDevelopmentProject(selectedSettingsLinWidgetRegistryId());
        registry.save();
        reloadLinWidgetsSettingsPage();
    });
    connect(m_linWidgetNetworkPermissionCheck, &QCheckBox::toggled, this, [this](bool checked) {
        setSelectedSettingsLinWidgetPermission(QStringLiteral("network"), checked);
    });
    connect(m_linWidgetStoragePermissionCheck, &QCheckBox::toggled, this, [this](bool checked) {
        setSelectedSettingsLinWidgetPermission(QStringLiteral("storage"), checked);
    });
    connect(m_linWidgetDesktopPermissionCheck, &QCheckBox::toggled, this, [this](bool checked) {
        setSelectedSettingsLinWidgetPermission(QStringLiteral("desktop"), checked);
    });

    reloadLinWidgetsSettingsPage();
}

QWidget* LinWidgetsSettingsPage::createWallpaperSubsystemPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 10, 0, 0);
    layout->setSpacing(16);

    auto* activePanel = createPanel(page);
    activePanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* activeLayout = new QVBoxLayout(activePanel);
    activeLayout->setContentsMargins(22, 18, 22, 18);
    activeLayout->setSpacing(12);

    auto* activeTopRow = new QHBoxLayout();
    activeTopRow->setContentsMargins(0, 0, 0, 0);
    activeTopRow->setSpacing(14);

    m_wallpaperPreviewLabel = new QLabel(activePanel);
    m_wallpaperPreviewLabel->setAlignment(Qt::AlignCenter);
    m_wallpaperPreviewLabel->setFixedSize(52, 52);
    m_wallpaperPreviewLabel->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  color: #7c3aed;"
        "  background: #f5f3ff;"
        "  border: 1px solid #ddd6fe;"
        "  border-radius: 12px;"
        "  font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets';"
        "  font-size: 24px;"
        "}"));
    m_wallpaperPreviewLabel->setText(glyph(0xEB9F));
    activeTopRow->addWidget(m_wallpaperPreviewLabel, 0, Qt::AlignVCenter);

    auto* activeTextLayout = new QVBoxLayout();
    activeTextLayout->setContentsMargins(0, 0, 0, 0);
    activeTextLayout->setSpacing(3);

    m_wallpaperActiveTitle = createCardTitle(tr("Live Wallpaper"), activePanel);
    m_wallpaperActiveDesc = createCardSubtitle(tr("No live wallpaper currently active."), activePanel);
    activeTextLayout->addWidget(m_wallpaperActiveTitle);
    activeTextLayout->addWidget(m_wallpaperActiveDesc);
    activeTopRow->addLayout(activeTextLayout, 1);

    m_wallpaperStopBtn = createSettingsActionButton(tr("Stop Wallpaper"), activePanel, false, true);
    m_wallpaperStopBtn->setEnabled(false);
    connect(m_wallpaperStopBtn, &QPushButton::clicked, this, &LinWidgetsSettingsPage::stopActiveWallpaper);
    activeTopRow->addWidget(m_wallpaperStopBtn, 0, Qt::AlignVCenter);
    activeLayout->addLayout(activeTopRow);

    layout->addWidget(activePanel, 0);

    auto* galleryPanel = createPanel(page);
    galleryPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* galleryLayout = new QVBoxLayout(galleryPanel);
    galleryLayout->setContentsMargins(22, 18, 22, 18);
    galleryLayout->setSpacing(10);

    galleryLayout->addWidget(createCardTitle(tr("Wallpaper Library"), galleryPanel));
    galleryLayout->addWidget(createCardSubtitle(tr("Select an official or imported live wallpaper to apply to your desktop."), galleryPanel));

    m_wallpaperList = new QListWidget(galleryPanel);
    m_wallpaperList->setObjectName(QStringLiteral("linWidgetSettingsList"));
    m_wallpaperList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_wallpaperList->setMinimumHeight(280);
    m_wallpaperList->setWordWrap(true);
    m_wallpaperList->setMouseTracking(true);
    galleryLayout->addWidget(m_wallpaperList, 1);

    layout->addWidget(galleryPanel, 1);
    return page;
}

void LinWidgetsSettingsPage::refreshWallpaperSubsystemState()
{
    auto& bridge = LinWidgetRuntimeBridge::instance();
    const bool hasActive = bridge.isLoaded() && bridge.hasActiveWallpaper();
    const QString activeId = hasActive ? bridge.activeWallpaperId() : QString();
    const QString activePath = hasActive ? bridge.activeWallpaperPackagePath() : QString();

    if (m_wallpaperStopBtn) {
        m_wallpaperStopBtn->setEnabled(hasActive);
    }

    if (m_wallpaperActiveTitle && m_wallpaperActiveDesc) {
        if (hasActive) {
            LinWidget::WidgetManifest manifest;
            QString name = activeId;
            if (!activePath.isEmpty() && LinWidget::WidgetPackage::loadManifest(activePath, &manifest, nullptr)) {
                name = manifest.name.isEmpty() ? activeId : manifest.name;
            }
            m_wallpaperActiveTitle->setText(tr("%1 (Active)").arg(name));
            m_wallpaperActiveDesc->setText(tr("Live wallpaper is currently running on the background layer."));
        } else {
            m_wallpaperActiveTitle->setText(tr("Live Wallpaper (Inactive)"));
            m_wallpaperActiveDesc->setText(tr("No live wallpaper currently active. Your desktop uses Windows wallpaper."));
        }
    }
}

void LinWidgetsSettingsPage::applyWallpaperPackage(const QString& packagePath, bool isDevelopment)
{
    Q_UNUSED(isDevelopment);
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (!bridge.isLoaded()) {
        return;
    }
    bridge.startWallpaper(packagePath);
    refreshWallpaperSubsystemState();
    reloadLinWidgetsSettingsPage();
}

void LinWidgetsSettingsPage::stopActiveWallpaper()
{
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (bridge.isLoaded()) {
        bridge.stopWallpaper(true);
    }
    refreshWallpaperSubsystemState();
    reloadLinWidgetsSettingsPage();
}

void LinWidgetsSettingsPage::reloadLinWidgetsSettingsPage()
{
    if (!m_officialLinWidgetsList || !m_marketplaceLinWidgetsList || !m_installedLinWidgetsList || !m_developmentLinWidgetsList) {
        return;
    }

    const QString selectedKey = selectedSettingsLinWidgetRegistryKey();
    settingsEnsureDefaultLinWidgetsInstalled();
    if (!m_preserveLinWidgetDrawerOnReload) {
        closeDrawer();
    }

    m_officialLinWidgetsList->clear();
    m_marketplaceLinWidgetsList->clear();
    m_installedLinWidgetsList->clear();
    m_developmentLinWidgetsList->clear();
    if (m_wallpaperList) {
        m_wallpaperList->clear();
    }

    auto& registry = LinWidget::WidgetRegistryManager::instance();
    QString registryError;
    registry.load(&registryError);
    registry.syncOfficialWidgetsIfOutdated(runtimeLinWidgetsDirForSettings(), AppPaths::userLinWidgetsDir());
    registry.syncFromFolder(AppPaths::userLinWidgetsDir(), &registryError);
    registry.save(&registryError);

    refreshLinWidgetSettingsRunningMaps();

    QHash<QString, LinWidget::WidgetRegistryEntry> installedEntriesById;
    int installedCount = 0;
    int developmentCount = 0;
    for (const LinWidget::WidgetRegistryEntry& entry : registry.entries()) {
        if (!entry.enabled || !QFileInfo::exists(entry.packagePath)) {
            continue;
        }

        if (!entry.isDevelopment) {
            installedEntriesById.insert(entry.id.trimmed().toLower(), entry);
        }

        LinWidget::WidgetManifest manifest;
        QString manifestError;
        const bool manifestOk = LinWidget::WidgetPackage::loadManifest(entry.packagePath, &manifest, &manifestError);
        const QString iconPath = manifestOk
            ? settingsResolveWidgetIconPath(entry.packagePath, manifest.icon)
            : settingsResolveWidgetIconPath(entry.packagePath, entry.icon);
        const QString registryKey = settingsLinWidgetRegistryKey(entry.id, entry.isDevelopment);
        const QString packageKey = settingsLinWidgetPackageKey(entry.packagePath);
        const QString state = settingsWidgetStateText(m_linWidgetInstancesById,
                                                      m_linWidgetInstancesByPackage,
                                                      m_linWidgetInstanceCountsById,
                                                      registryKey,
                                                      packageKey,
                                                      manifestOk && manifest.multiInstance);

        auto* item = new QListWidgetItem();
        item->setData(SettingsLinWidgetPackagePathRole, entry.packagePath);
        item->setData(SettingsLinWidgetRegistryIdRole, entry.id);
        item->setData(SettingsLinWidgetRuntimeTypeRole, entry.runtimeType);
        item->setData(SettingsLinWidgetDefaultSizeRole, entry.defaultSize);
        item->setData(SettingsLinWidgetDevelopmentRole, entry.isDevelopment);
        item->setData(SettingsLinWidgetNameRole, entry.name);
        item->setData(SettingsLinWidgetVersionRole, entry.version);
        item->setData(SettingsLinWidgetAuthorRole, entry.author);
        item->setData(SettingsLinWidgetDescriptionRole, entry.description);
        item->setData(SettingsLinWidgetAutoLoadRole, entry.autoLoad);
        item->setData(SettingsLinWidgetGrantedPermissionsRole, entry.permissionsGranted);
        item->setData(SettingsLinWidgetRequestedPermissionsRole, manifest.permissions);
        item->setData(SettingsLinWidgetSourcePathRole, entry.sourcePath);
        item->setData(SettingsLinWidgetManifestValidRole, manifestOk);
        item->setData(SettingsLinWidgetManifestErrorRole, manifestError);
        item->setData(SettingsLinWidgetIconPathRole, iconPath);
        item->setData(SettingsLinWidgetMultiInstanceRole, manifestOk && manifest.multiInstance);
        const QString displayText = manifestOk
            ? settingsWidgetItemText(entry.name,
                                     entry.id,
                                     entry.version,
                                     entry.isDevelopment,
                                     entry.autoLoad,
                                     entry.packagePath,
                                     state)
            : tr("%1\nManifest error: %2\n%3").arg(entry.name.isEmpty() ? entry.id : entry.name, manifestError, entry.packagePath);
        item->setText(QString());
        item->setToolTip(displayText);

        const bool isWallpaper = (entry.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0) ||
                                 (manifestOk && manifest.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0);
        if (isWallpaper) {
            // Avoid letting stale AppData copy shadow the official bundled wallpaper gallery item
            if (entry.id.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0 &&
                !entry.isDevelopment &&
                !entry.packagePath.contains(QStringLiteral("resources/LinWidgets"), Qt::CaseInsensitive)) {
                continue;
            }
            if (m_wallpaperList) {
                m_wallpaperList->addItem(item);
                auto* row = createSettingsLinWidgetRow(m_wallpaperList,
                                                       item,
                                                       entry.id,
                                                       entry.name,
                                                       entry.version,
                                                       entry.description,
                                                       iconPath,
                                                       entry.isDevelopment ? tr("Development") : tr("Installed"),
                                                       m_wallpaperList,
                                                       nullptr);
                auto* rowLayout = qobject_cast<QHBoxLayout*>(row->layout());
                auto& bridge = LinWidgetRuntimeBridge::instance();
                const bool isCurrentlyActive = bridge.isLoaded() && bridge.hasActiveWallpaper() &&
                    (bridge.activeWallpaperId().compare(entry.id, Qt::CaseInsensitive) == 0 ||
                     bridge.activeWallpaperId().compare(QStringLiteral("wallpaper:") + entry.id, Qt::CaseInsensitive) == 0);

                auto* applyButton = createSettingsActionButton(isCurrentlyActive ? tr("Active") : tr("Apply Wallpaper"), row, !isCurrentlyActive);
                applyButton->setEnabled(!isCurrentlyActive);
                rowLayout->addWidget(applyButton);
                connect(applyButton, &QPushButton::clicked, this, [this, path = entry.packagePath, isDev = entry.isDevelopment]() {
                    applyWallpaperPackage(path, isDev);
                });

                if (!entry.packagePath.contains(QStringLiteral("resources/LinWidgets"), Qt::CaseInsensitive)) {
                    auto* uninstallButton = createSettingsActionButton(tr("Uninstall"), row, false, true);
                    rowLayout->addWidget(uninstallButton);
                    connect(uninstallButton, &QPushButton::clicked, this, [this, id = entry.id, path = entry.packagePath]() {
                        auto& bridge = LinWidgetRuntimeBridge::instance();
                        if (bridge.isLoaded() && bridge.hasActiveWallpaper()) {
                            const QString activeId = bridge.activeWallpaperId();
                            if (activeId.compare(id, Qt::CaseInsensitive) == 0 ||
                                activeId.compare(QStringLiteral("wallpaper:") + id, Qt::CaseInsensitive) == 0) {
                                bridge.stopWallpaper(true);
                            }
                        }
                        auto& reg = LinWidget::WidgetRegistryManager::instance();
                        reg.load();
                        reg.removeById(id);
                        reg.save();
                        QString error;
                        settingsRemoveDirectory(path, &error);
                        reloadLinWidgetsSettingsPage();
                    });
                }

                item->setSizeHint(QSize(0, 88));
                m_wallpaperList->setItemWidget(item, row);
            }
            continue;
        }

        if (entry.isDevelopment) {
            m_developmentLinWidgetsList->addItem(item);
            auto* row = createSettingsLinWidgetRow(m_developmentLinWidgetsList,
                                                   item,
                                                   entry.id,
                                                   entry.name,
                                                   entry.version,
                                                   entry.description,
                                                   iconPath,
                                                   tr("Development"),
                                                   m_developmentLinWidgetsList,
                                                   [this](QListWidgetItem* clickedItem) {
                                                       selectAndOpenLinWidgetSettingsItem(clickedItem);
                                                   });
            item->setSizeHint(QSize(0, 88));
            m_developmentLinWidgetsList->setItemWidget(item, row);
            ++developmentCount;
            if (!selectedKey.isEmpty() && selectedKey == registryKey) {
                m_developmentLinWidgetsList->setCurrentItem(item);
            }
        } else {
            m_installedLinWidgetsList->addItem(item);
            auto* row = createSettingsLinWidgetRow(m_installedLinWidgetsList,
                                                   item,
                                                   entry.id,
                                                   entry.name,
                                                   entry.version,
                                                   entry.description,
                                                   iconPath,
                                                   tr("Installed"),
                                                   m_installedLinWidgetsList,
                                                   [this](QListWidgetItem* clickedItem) {
                                                       selectAndOpenLinWidgetSettingsItem(clickedItem);
                                                   });
            auto* rowLayout = qobject_cast<QHBoxLayout*>(row->layout());
            const int instanceCount = m_linWidgetInstanceCountsById.value(registryKey, 0);
            const bool rowRunning = state == QStringLiteral("running");
            const QString closeButtonText = rowRunning ? tr("Close") : tr("Load");
            auto* closeButton = createSettingsActionButton(closeButtonText, row);
            auto* uninstallButton = createSettingsActionButton(tr("Uninstall"), row, false, true);
            closeButton->setEnabled(manifestOk &&
                                    (!m_linWidgetsRuntimeToggle || m_linWidgetsRuntimeToggle->checked()) &&
                                    (manifest.multiInstance ? instanceCount < 3 : true));
            uninstallButton->setEnabled(true);
            rowLayout->addWidget(closeButton);
            rowLayout->addWidget(uninstallButton);
            connect(closeButton, &QPushButton::clicked, this, [this, item, rowRunning]() {
                if (m_installedLinWidgetsList) {
                    m_installedLinWidgetsList->setCurrentItem(item);
                }
                if (rowRunning) {
                    unloadSelectedSettingsLinWidget();
                } else {
                    loadSelectedSettingsLinWidget();
                }
            });
            connect(uninstallButton, &QPushButton::clicked, this, [this, item]() {
                if (m_installedLinWidgetsList) {
                    m_installedLinWidgetsList->setCurrentItem(item);
                }
                uninstallSelectedSettingsLinWidget();
            });
            item->setSizeHint(QSize(0, 88));
            m_installedLinWidgetsList->setItemWidget(item, row);
            ++installedCount;
            if (!selectedKey.isEmpty() && selectedKey == registryKey) {
                m_installedLinWidgetsList->setCurrentItem(item);
            }
        }
    }

    const QDir officialDir(runtimeLinWidgetsDirForSettings());
    QFileInfoList officialEntries = officialDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    std::stable_sort(officialEntries.begin(), officialEntries.end(), [&installedEntriesById](const QFileInfo& left, const QFileInfo& right) {
        LinWidget::WidgetManifest leftManifest;
        LinWidget::WidgetManifest rightManifest;
        const bool leftOk = LinWidget::WidgetPackage::loadManifest(left.absoluteFilePath(), &leftManifest, nullptr);
        const bool rightOk = LinWidget::WidgetPackage::loadManifest(right.absoluteFilePath(), &rightManifest, nullptr);
        const bool leftInstalled = leftOk && installedEntriesById.contains(leftManifest.id.trimmed().toLower());
        const bool rightInstalled = rightOk && installedEntriesById.contains(rightManifest.id.trimmed().toLower());
        if (leftInstalled != rightInstalled) {
            return leftInstalled;
        }
        const QString leftName = leftOk && !leftManifest.name.trimmed().isEmpty() ? leftManifest.name : left.fileName();
        const QString rightName = rightOk && !rightManifest.name.trimmed().isEmpty() ? rightManifest.name : right.fileName();
        return QString::localeAwareCompare(leftName, rightName) < 0;
    });
    for (const QFileInfo& entry : officialEntries) {
        LinWidget::WidgetManifest manifest;
        QString errorMessage;
        if (!LinWidget::WidgetPackage::loadManifest(entry.absoluteFilePath(), &manifest, &errorMessage)) {
            continue;
        }
        if (manifest.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0) {
            if (m_wallpaperList) {
                bool alreadyInWallpaperList = false;
                for (int i = 0; i < m_wallpaperList->count(); ++i) {
                    if (m_wallpaperList->item(i)->data(SettingsLinWidgetRegistryIdRole).toString().compare(manifest.id, Qt::CaseInsensitive) == 0) {
                        alreadyInWallpaperList = true;
                        break;
                    }
                }
                if (!alreadyInWallpaperList) {
                    const QString wallIconPath = settingsResolveWidgetIconPath(entry.absoluteFilePath(), manifest.icon);
                    auto* wallItem = new QListWidgetItem();
                    wallItem->setData(SettingsLinWidgetPackagePathRole, entry.absoluteFilePath());
                    wallItem->setData(SettingsLinWidgetRegistryIdRole, manifest.id);
                    wallItem->setData(SettingsLinWidgetNameRole, manifest.name);
                    wallItem->setData(SettingsLinWidgetVersionRole, manifest.version);
                    wallItem->setData(SettingsLinWidgetAuthorRole, manifest.author);
                    wallItem->setData(SettingsLinWidgetDescriptionRole, manifest.description);
                    wallItem->setData(SettingsLinWidgetIconPathRole, wallIconPath);
                    m_wallpaperList->addItem(wallItem);
                    auto* row = createSettingsLinWidgetRow(m_wallpaperList,
                                                           wallItem,
                                                           manifest.id,
                                                           manifest.name,
                                                           manifest.version,
                                                           manifest.description,
                                                           wallIconPath,
                                                           tr("Official"),
                                                           m_wallpaperList,
                                                           nullptr);
                    auto* rowLayout = qobject_cast<QHBoxLayout*>(row->layout());
                    auto& bridge = LinWidgetRuntimeBridge::instance();
                    const bool isCurrentlyActive = bridge.isLoaded() && bridge.hasActiveWallpaper() &&
                        (bridge.activeWallpaperId().compare(manifest.id, Qt::CaseInsensitive) == 0 ||
                         bridge.activeWallpaperId().compare(QStringLiteral("wallpaper:") + manifest.id, Qt::CaseInsensitive) == 0);

                    auto* applyButton = createSettingsActionButton(isCurrentlyActive ? tr("Active") : tr("Apply Wallpaper"), row, !isCurrentlyActive);
                    applyButton->setEnabled(!isCurrentlyActive);
                    rowLayout->addWidget(applyButton);
                    connect(applyButton, &QPushButton::clicked, this, [this, path = entry.absoluteFilePath()]() {
                        applyWallpaperPackage(path, false);
                    });
                    wallItem->setSizeHint(QSize(0, 88));
                    m_wallpaperList->setItemWidget(wallItem, row);
                }
            }
            continue;
        }
        const QString iconPath = settingsResolveWidgetIconPath(entry.absoluteFilePath(), manifest.icon);
        const LinWidget::WidgetRegistryEntry installed = installedEntriesById.value(manifest.id.trimmed().toLower());
        const bool isInstalled = !installed.id.isEmpty() && QFileInfo::exists(installed.packagePath);
        const QString installedKey = settingsLinWidgetRegistryKey(manifest.id, false);
        const QString packageKey = isInstalled ? settingsLinWidgetPackageKey(installed.packagePath) : QString();
        const QString state = isInstalled
            ? settingsWidgetStateText(m_linWidgetInstancesById,
                                      m_linWidgetInstancesByPackage,
                                      m_linWidgetInstanceCountsById,
                                      installedKey,
                                      packageKey,
                                      manifest.multiInstance)
            : QStringLiteral("not installed");

        auto* item = new QListWidgetItem();
        item->setData(SettingsLinWidgetPackagePathRole, entry.absoluteFilePath());
        item->setData(SettingsLinWidgetRegistryIdRole, manifest.id);
        item->setData(SettingsLinWidgetRuntimeTypeRole, LinWidget::runtimeTypeToString(manifest.type));
        item->setData(SettingsLinWidgetDefaultSizeRole, QSize(manifest.width, manifest.height));
        item->setData(SettingsLinWidgetDevelopmentRole, false);
        item->setData(SettingsLinWidgetNameRole, manifest.name);
        item->setData(SettingsLinWidgetVersionRole, manifest.version);
        item->setData(SettingsLinWidgetAuthorRole, manifest.author);
        item->setData(SettingsLinWidgetDescriptionRole, manifest.description);
        item->setData(SettingsLinWidgetAutoLoadRole, manifest.autoLoad);
        item->setData(SettingsLinWidgetRequestedPermissionsRole, manifest.permissions);
        item->setData(SettingsLinWidgetOfficialInstalledPathRole, installed.packagePath);
        item->setData(SettingsLinWidgetIconPathRole, iconPath);
        item->setData(SettingsLinWidgetMultiInstanceRole, manifest.multiInstance);
        const QString officialDisplayText = QStringLiteral("%1\nid: %2\nversion: %3    author: %4\npermissions: %5\nstate: %6\n%7")
            .arg(manifest.name)
            .arg(manifest.id)
            .arg(manifest.version.isEmpty() ? QStringLiteral("unknown") : manifest.version)
            .arg(manifest.author.isEmpty() ? QStringLiteral("Unknown") : manifest.author)
            .arg(settingsPermissionsText(manifest.permissions))
            .arg(state)
            .arg(manifest.description.isEmpty() ? entry.absoluteFilePath() : manifest.description);
        item->setText(QString());
        item->setToolTip(officialDisplayText);
        m_officialLinWidgetsList->addItem(item);

        auto* row = createSettingsLinWidgetRow(m_officialLinWidgetsList,
                                               item,
                                               manifest.id,
                                               manifest.name,
                                               manifest.version,
                                               manifest.description,
                                               iconPath,
                                               tr("Official"),
                                               m_officialLinWidgetsList,
                                               [this](QListWidgetItem* clickedItem) {
                                                   selectAndOpenLinWidgetSettingsItem(clickedItem);
                                               });
        auto* rowLayout = qobject_cast<QHBoxLayout*>(row->layout());
        if (!isInstalled) {
            auto* installButton = createSettingsActionButton(tr("Install"), row, true);
            rowLayout->addWidget(installButton);
            connect(installButton, &QPushButton::clicked, this, [this, path = entry.absoluteFilePath()]() {
                installOfficialSettingsLinWidget(path);
            });
        } else {
            auto* uninstallButton = createSettingsActionButton(tr("Uninstall"), row, false, true);
            uninstallButton->setEnabled(true);
            rowLayout->addWidget(uninstallButton);
            connect(uninstallButton, &QPushButton::clicked, this, [this, id = manifest.id]() {
                m_linWidgetsTabs->setCurrentIndex(2);
                for (int i = 0; i < m_installedLinWidgetsList->count(); ++i) {
                    QListWidgetItem* installedItem = m_installedLinWidgetsList->item(i);
                    if (installedItem && installedItem->data(SettingsLinWidgetRegistryIdRole).toString().compare(id, Qt::CaseInsensitive) == 0) {
                        m_installedLinWidgetsList->setCurrentItem(installedItem);
                        break;
                    }
                }
                uninstallSelectedSettingsLinWidget();
            });
        }
        item->setSizeHint(QSize(0, 88));
        m_officialLinWidgetsList->setItemWidget(item, row);
    }

    QFile marketplaceFile(marketplaceFilePathForSettings());
    if (!marketplaceFile.exists() || !marketplaceFile.open(QIODevice::ReadOnly)) {
        auto* item = new QListWidgetItem(tr("Marketplace is coming soon."));
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        m_marketplaceLinWidgetsList->addItem(item);
    } else {
        const QJsonDocument document = QJsonDocument::fromJson(marketplaceFile.readAll());
        const QJsonArray widgets = document.isArray()
            ? document.array()
            : document.object().value(QStringLiteral("widgets")).toArray();
        if (widgets.isEmpty()) {
            auto* item = new QListWidgetItem(tr("Marketplace is coming soon."));
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            m_marketplaceLinWidgetsList->addItem(item);
        }
        for (const QJsonValue& value : widgets) {
            const QJsonObject object = value.toObject();
            const QString id = object.value(QStringLiteral("id")).toString();
            const QString sourcePath = object.value(QStringLiteral("sourcePath")).toString();
            const QString marketplaceIconPath = object.value(QStringLiteral("icon")).toString();
            QStringList permissions;
            const QJsonArray permissionsArray = object.value(QStringLiteral("permissions")).toArray();
            for (const QJsonValue& permissionValue : permissionsArray) {
                const QString permission = permissionValue.toString().trimmed();
                if (!permission.isEmpty()) {
                    permissions.append(permission);
                }
            }

            auto* item = new QListWidgetItem();
            item->setData(SettingsLinWidgetPackagePathRole, sourcePath);
            item->setData(SettingsLinWidgetRegistryIdRole, id);
            item->setData(SettingsLinWidgetNameRole, object.value(QStringLiteral("name")).toString(id));
            item->setData(SettingsLinWidgetVersionRole, object.value(QStringLiteral("version")).toString());
            item->setData(SettingsLinWidgetAuthorRole, object.value(QStringLiteral("author")).toString());
            item->setData(SettingsLinWidgetDescriptionRole, object.value(QStringLiteral("description")).toString());
            item->setData(SettingsLinWidgetRequestedPermissionsRole, permissions);
            item->setData(SettingsLinWidgetCategoryRole, object.value(QStringLiteral("category")).toString());
            item->setData(SettingsLinWidgetTrustedRole, object.value(QStringLiteral("trusted")).toBool());
            item->setData(SettingsLinWidgetIconPathRole, settingsResolveWidgetIconPath(sourcePath, marketplaceIconPath));
            const QString marketplaceDisplayText = QStringLiteral("%1\nid: %2\nversion: %3    category: %4    trusted: %5\npermissions: %6\n%7")
                .arg(item->data(SettingsLinWidgetNameRole).toString())
                .arg(id)
                .arg(item->data(SettingsLinWidgetVersionRole).toString().isEmpty() ? QStringLiteral("unknown") : item->data(SettingsLinWidgetVersionRole).toString())
                .arg(item->data(SettingsLinWidgetCategoryRole).toString().isEmpty() ? QStringLiteral("General") : item->data(SettingsLinWidgetCategoryRole).toString())
                .arg(item->data(SettingsLinWidgetTrustedRole).toBool() ? QStringLiteral("true") : QStringLiteral("false"))
                .arg(settingsPermissionsText(permissions))
                .arg(item->data(SettingsLinWidgetDescriptionRole).toString().isEmpty() ? sourcePath : item->data(SettingsLinWidgetDescriptionRole).toString());
            item->setText(QString());
            item->setToolTip(marketplaceDisplayText);
            m_marketplaceLinWidgetsList->addItem(item);

            auto* row = createSettingsLinWidgetRow(m_marketplaceLinWidgetsList,
                                                   item,
                                                   id,
                                                   item->data(SettingsLinWidgetNameRole).toString(),
                                                   item->data(SettingsLinWidgetVersionRole).toString(),
                                                   item->data(SettingsLinWidgetDescriptionRole).toString(),
                                                   item->data(SettingsLinWidgetIconPathRole).toString(),
                                                   tr("Marketplace"),
                                                   m_marketplaceLinWidgetsList,
                                                   [this](QListWidgetItem* clickedItem) {
                                                       selectAndOpenLinWidgetSettingsItem(clickedItem);
                                                   });
            auto* rowLayout = qobject_cast<QHBoxLayout*>(row->layout());
            auto* installButton = createSettingsActionButton(tr("Install"), row, true);
            installButton->setEnabled(QFileInfo(sourcePath).isDir());
            rowLayout->addWidget(installButton);
            connect(installButton, &QPushButton::clicked, this, [this, sourcePath]() {
                LinWidget::WidgetManifest manifest;
                QString errorMessage;
                if (!LinWidget::WidgetPackage::loadManifest(sourcePath, &manifest, &errorMessage)) {
                    if (m_linWidgetsStatusLabel) {
                        m_linWidgetsStatusLabel->setText(tr("Marketplace install failed: %1").arg(errorMessage));
                    }
                    return;
                }
                bool accepted = false;
                const QStringList grantedPermissions = promptLinWidgetInstallPermissions(this, manifest, &accepted);
                if (!accepted) {
                    if (m_linWidgetsStatusLabel) {
                        m_linWidgetsStatusLabel->setText(tr("Marketplace install canceled: %1").arg(manifest.name));
                    }
                    return;
                }
                const QString targetPath = settingsInstalledTargetForManifest(manifest);
                if (!settingsRemoveDirectory(targetPath, &errorMessage) ||
                    !settingsCopyLinWidgetDirectory(sourcePath, targetPath, &errorMessage)) {
                    if (m_linWidgetsStatusLabel) {
                        m_linWidgetsStatusLabel->setText(tr("Marketplace install failed: %1").arg(errorMessage));
                    }
                    return;
                }
                auto& reg = LinWidget::WidgetRegistryManager::instance();
                reg.load();
                reg.upsertFromPackage(targetPath, &errorMessage);
                reg.setPermissionsGranted(manifest.id, false, grantedPermissions);
                reg.save();
                QString instanceId;
                QString launchError;
                const bool launched = launchInstalledSettingsLinWidget(targetPath, manifest, &instanceId, &launchError);
                if (m_linWidgetsStatusLabel) {
                    m_linWidgetsStatusLabel->setText(launched
                        ? tr("Installed and loaded marketplace widget: %1").arg(manifest.name)
                        : tr("Installed marketplace widget: %1\nLoad skipped: %2").arg(manifest.name, launchError));
                }
                reloadLinWidgetsSettingsPage();
            });
            item->setSizeHint(QSize(0, 88));
            m_marketplaceLinWidgetsList->setItemWidget(item, row);
        }
    }

    if (m_linWidgetsStatusLabel) {
        const QString summary = tr("Installed %1    Development %2")
            .arg(installedCount)
            .arg(developmentCount);
        m_linWidgetsStatusLabel->setText(registryError.isEmpty()
            ? summary
            : tr("%1\nManifest errors:\n%2").arg(summary, registryError));
    }
    refreshLinWidgetSettingsButtons();
    refreshLinWidgetSettingsInspector();
}

void LinWidgetsSettingsPage::refreshLinWidgetSettingsButtons()
{
    refreshLinWidgetSettingsRunningMaps();
    refreshWallpaperSubsystemState();
    const QString packagePath = selectedSettingsLinWidgetPackagePath();
    const bool hasSelection = !packagePath.isEmpty();
    const bool running = !selectedSettingsLinWidgetInstanceId().isEmpty();
    const bool isDevelopment = selectedSettingsLinWidgetIsDevelopment();
    const bool runtimeEnabled = !m_linWidgetsRuntimeToggle || m_linWidgetsRuntimeToggle->checked();
    QListWidget* activeList = activeSettingsLinWidgetList();
    QListWidgetItem* activeItem = activeList ? activeList->currentItem() : nullptr;
    const bool manifestValid = !activeItem || activeItem->data(SettingsLinWidgetManifestValidRole).toBool();
    const bool multiInstance = activeItem && activeItem->data(SettingsLinWidgetMultiInstanceRole).toBool();
    const int instanceCount = m_linWidgetInstanceCountsById.value(selectedSettingsLinWidgetRegistryKey(), 0);

    if (m_linWidgetLoadButton) {
        m_linWidgetLoadButton->setText(multiInstance ? tr("Add") : tr("Load"));
        m_linWidgetLoadButton->setEnabled(hasSelection &&
                                          runtimeEnabled &&
                                          manifestValid &&
                                          ((multiInstance && instanceCount < 3) || (!multiInstance && !running)));
    }
    if (m_linWidgetUnloadButton) {
        m_linWidgetUnloadButton->setEnabled(hasSelection && running && runtimeEnabled);
        m_linWidgetUnloadButton->setText(multiInstance ? tr("Close One") : tr("Close"));
    }
    if (m_linWidgetReloadButton) {
        m_linWidgetReloadButton->setEnabled(hasSelection && running && runtimeEnabled);
    }
    if (m_linWidgetOpenFolderButton) {
        m_linWidgetOpenFolderButton->setEnabled(hasSelection);
    }
    if (m_linWidgetDeleteButton) {
        m_linWidgetDeleteButton->setEnabled(hasSelection && !isDevelopment);
    }
    if (m_linWidgetExportZipButton) {
        m_linWidgetExportZipButton->setEnabled(hasSelection && isDevelopment && !running);
    }
    if (m_linWidgetRemoveDevButton) {
        m_linWidgetRemoveDevButton->setEnabled(hasSelection && isDevelopment && !running);
    }

    const QStringList granted = activeItem
        ? activeItem->data(SettingsLinWidgetGrantedPermissionsRole).toStringList()
        : QStringList();
    auto updatePermissionCheck = [&](QCheckBox* check, const QString& permission) {
        if (!check) {
            return;
        }
        const QSignalBlocker blocker(check);
        check->setChecked(isDevelopment || granted.contains(permission));
        check->setEnabled(hasSelection && !isDevelopment);
    };
    updatePermissionCheck(m_linWidgetNetworkPermissionCheck, QStringLiteral("network"));
    updatePermissionCheck(m_linWidgetStoragePermissionCheck, QStringLiteral("storage"));
    updatePermissionCheck(m_linWidgetDesktopPermissionCheck, QStringLiteral("desktop"));
}

void LinWidgetsSettingsPage::refreshLinWidgetSettingsInspector()
{
    QListWidget* list = activeSettingsLinWidgetList();
    QListWidgetItem* item = list ? list->currentItem() : nullptr;
    if (!item) {
        if (m_linWidgetsInspectorLabel) {
            m_linWidgetsInspectorLabel->setText(tr("Select an installed widget or development project."));
        }
        if (m_linWidgetsLogView) {
            m_linWidgetsLogView->setPlainText(tr("Select a widget to view its log."));
        }
        if (m_linWidgetsErrorsLabel) {
            m_linWidgetsErrorsLabel->setText(tr("No errors."));
        }
        return;
    }

    const QString id = item->data(SettingsLinWidgetRegistryIdRole).toString();
    const QString name = item->data(SettingsLinWidgetNameRole).toString();
    const QString version = item->data(SettingsLinWidgetVersionRole).toString();
    const QString author = item->data(SettingsLinWidgetAuthorRole).toString();
    const QString description = item->data(SettingsLinWidgetDescriptionRole).toString();
    const QString packagePath = item->data(SettingsLinWidgetPackagePathRole).toString();
    const QString iconPath = item->data(SettingsLinWidgetIconPathRole).toString();
    const bool isDevelopment = item->data(SettingsLinWidgetDevelopmentRole).toBool();
    const QStringList requested = item->data(SettingsLinWidgetRequestedPermissionsRole).toStringList();
    const QStringList granted = item->data(SettingsLinWidgetGrantedPermissionsRole).toStringList();
    QStringList missing;
    for (const QString& permission : requested) {
        if (!granted.contains(permission)) {
            missing.append(permission);
        }
    }
    const QString instanceId = selectedSettingsLinWidgetInstanceId();
    const QString logInstanceId = instanceId.isEmpty()
        ? settingsLinWidgetRegistryKey(id, isDevelopment)
        : instanceId;

    if (m_linWidgetsInspectorLabel) {
        m_linWidgetsInspectorLabel->setText(QStringLiteral(
            "name: %1\n"
            "id: %2\n"
            "version: %3\n"
            "mode: %4\n"
            "state: %5\n"
            "autoLoad: %6\n"
            "requested permissions: %7\n"
            "granted permissions: %8\n"
            "missing permissions: %9\n"
            "author: %10\n"
            "description: %11\n"
            "path: %12")
            .arg(name.isEmpty() ? id : name)
            .arg(id)
            .arg(version.isEmpty() ? QStringLiteral("unknown") : version)
            .arg(isDevelopment ? QStringLiteral("dev") : QStringLiteral("installed"))
            .arg(instanceId.isEmpty() ? QStringLiteral("stopped") : QStringLiteral("running"))
            .arg(item->data(SettingsLinWidgetAutoLoadRole).toBool() ? QStringLiteral("true") : QStringLiteral("false"))
            .arg(settingsPermissionsText(requested))
            .arg(isDevelopment ? QStringLiteral("network, storage, desktop") : settingsPermissionsText(granted))
            .arg(isDevelopment || missing.isEmpty() ? QStringLiteral("none") : missing.join(QStringLiteral(", ")))
            .arg(author.isEmpty() ? QStringLiteral("Unknown") : author)
            .arg(description.isEmpty() ? QStringLiteral("No description") : description)
            .arg(packagePath));
    }

    const QString logPath = settingsLinWidgetLogPath(logInstanceId);
    if (m_linWidgetsLogWatcher && m_currentLinWidgetLogPath != logPath) {
        const QStringList watchedFiles = m_linWidgetsLogWatcher->files();
        if (!watchedFiles.isEmpty()) {
            m_linWidgetsLogWatcher->removePaths(watchedFiles);
        }
        m_currentLinWidgetLogPath = logPath;
        if (!logPath.isEmpty() && QFileInfo::exists(logPath)) {
            m_linWidgetsLogWatcher->addPath(logPath);
        }
    }

    QString logText;
    if (logPath.isEmpty()) {
        logText = tr("No log instance.");
    } else {
        QFile file(logPath);
        if (!file.exists()) {
            const QString sharedLogPath = settingsLinWidgetLogPath(QStringLiteral("__shared__"));
            if (QFileInfo::exists(sharedLogPath)) {
                file.setFileName(sharedLogPath);
            } else {
                logText = tr("Log file has not been created yet:\n%1").arg(logPath);
            }
        }
        if (logText.isEmpty()) {
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                logText = tr("Cannot open log file:\n%1").arg(file.fileName());
            } else {
                QByteArray content = file.readAll();
                constexpr int maxLogBytes = 200 * 1024;
                if (content.size() > maxLogBytes) {
                    content = content.right(maxLogBytes);
                }
                logText = QString::fromUtf8(content);
            }
        }
    }
    if (m_linWidgetsLogView) {
        m_linWidgetsLogView->setPlainText(logText);
        QTextCursor cursor = m_linWidgetsLogView->textCursor();
        cursor.movePosition(QTextCursor::End);
        m_linWidgetsLogView->setTextCursor(cursor);
    }

    QStringList errorLines;
    const QStringList lines = logText.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        const QString lower = line.toLower();
        if (lower.contains(QStringLiteral("permission denied")) ||
            lower.contains(QStringLiteral("error")) ||
            lower.contains(QStringLiteral("failed")) ||
            lower.contains(QStringLiteral("js.rejection"))) {
            errorLines.append(line);
        }
    }
    if (m_linWidgetsErrorsLabel) {
        if (errorLines.isEmpty()) {
            m_linWidgetsErrorsLabel->setText(tr("No errors."));
        } else {
            while (errorLines.size() > 8) {
                errorLines.removeFirst();
            }
            m_linWidgetsErrorsLabel->setText(errorLines.join(QLatin1Char('\n')));
        }
    }
}

void LinWidgetsSettingsPage::openLinWidgetSettingsDrawer(QListWidgetItem* item)
{
    if (!item || !(item->flags() & Qt::ItemIsEnabled)) {
        return;
    }

    QListWidget* ownerList = item->listWidget();
    const bool isOfficial = ownerList == m_officialLinWidgetsList;
    const bool isMarketplace = ownerList == m_marketplaceLinWidgetsList;
    const QString id = item->data(SettingsLinWidgetRegistryIdRole).toString();
    const QString name = item->data(SettingsLinWidgetNameRole).toString();
    const QString version = item->data(SettingsLinWidgetVersionRole).toString();
    const QString author = item->data(SettingsLinWidgetAuthorRole).toString();
    const QString description = item->data(SettingsLinWidgetDescriptionRole).toString();
    const QString packagePath = item->data(SettingsLinWidgetPackagePathRole).toString();
    const QString iconPath = item->data(SettingsLinWidgetIconPathRole).toString();
    const bool isDevelopment = item->data(SettingsLinWidgetDevelopmentRole).toBool();
    const QStringList requested = item->data(SettingsLinWidgetRequestedPermissionsRole).toStringList();
    const QStringList granted = item->data(SettingsLinWidgetGrantedPermissionsRole).toStringList();
    const bool multiInstance = item->data(SettingsLinWidgetMultiInstanceRole).toBool();
    const QString registryKey = settingsLinWidgetRegistryKey(id, isDevelopment);
    const QString packageKey = settingsLinWidgetPackageKey(isOfficial ? item->data(SettingsLinWidgetOfficialInstalledPathRole).toString() : packagePath);
    const QString instanceId = m_linWidgetInstancesById.value(registryKey, m_linWidgetInstancesByPackage.value(packageKey));
    const bool running = !instanceId.isEmpty();
    const bool installed = !item->data(SettingsLinWidgetOfficialInstalledPathRole).toString().isEmpty() || (!isOfficial && !isMarketplace);

    auto* content = new QWidget();
    content->setMinimumWidth(0);
    content->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto* hero = new QWidget(content);
    hero->setMinimumWidth(0);
    hero->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* heroLayout = new QHBoxLayout(hero);
    heroLayout->setContentsMargins(0, 0, 0, 0);
    heroLayout->setSpacing(18);
    heroLayout->addWidget(createWidgetIconLabel(id, iconPath, hero, 64));
    auto* heroText = new QVBoxLayout();
    heroText->setContentsMargins(0, 0, 0, 0);
    heroText->setSpacing(6);
    auto* title = new QLabel(name.isEmpty() ? id : name, hero);
    title->setWordWrap(true);
    title->setMinimumWidth(0);
    title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    title->setStyleSheet(QStringLiteral("color:#101827;font-size:24px;font-weight:800;background:transparent;"));
    auto* subtitle = new QLabel(description.isEmpty() ? tr("LinWidget 桌面小组件。") : description, hero);
    subtitle->setWordWrap(true);
    subtitle->setMinimumWidth(0);
    subtitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    subtitle->setStyleSheet(QStringLiteral("color:#66738c;font-size:14px;font-weight:500;background:transparent;line-height:150%;"));
    heroText->addWidget(title);
    heroText->addWidget(subtitle);
    heroLayout->addLayout(heroText, 1);

    QPushButton* primaryButton = nullptr;
    if (isOfficial && !installed) {
        primaryButton = createSettingsActionButton(tr("install"), hero, true);
        connect(primaryButton, &QPushButton::clicked, this, [this, packagePath]() {
            installOfficialSettingsLinWidget(packagePath);
            closeDrawer();
        });
    } else if (isOfficial && installed) {
        primaryButton = createSettingsActionButton(tr("manage"), hero, true);
        connect(primaryButton, &QPushButton::clicked, this, [this, id]() {
            if (m_installedLinWidgetsList) {
                for (int i = 0; i < m_installedLinWidgetsList->count(); ++i) {
                    QListWidgetItem* installedItem = m_installedLinWidgetsList->item(i);
                    if (installedItem && installedItem->data(SettingsLinWidgetRegistryIdRole).toString().compare(id, Qt::CaseInsensitive) == 0) {
                        m_linWidgetsTabs->setCurrentIndex(2);
                        m_installedLinWidgetsList->setCurrentItem(installedItem);
                        openLinWidgetSettingsDrawer(installedItem);
                        return;
                    }
                }
            }
        });
    } else if (isMarketplace) {
        primaryButton = createSettingsActionButton(tr("install"), hero, true);
        primaryButton->setEnabled(QFileInfo(packagePath).isDir());
    } else {
        primaryButton = createSettingsActionButton(multiInstance ? tr("add") : (running ? tr("close") : tr("load")), hero, true);
        primaryButton->setEnabled(!multiInstance || m_linWidgetInstanceCountsById.value(registryKey, 0) < 3);
        connect(primaryButton, &QPushButton::clicked, this, [this, running, multiInstance]() {
            m_preserveLinWidgetDrawerOnReload = true;
            if (multiInstance) {
                loadSelectedSettingsLinWidget();
            } else {
                running ? unloadSelectedSettingsLinWidget() : loadSelectedSettingsLinWidget();
            }
            m_preserveLinWidgetDrawerOnReload = false;
        });
    }
    heroLayout->addWidget(primaryButton);
    layout->addWidget(hero);

    auto* details = createPanel(content);
    details->setGraphicsEffect(nullptr);
    details->setMinimumWidth(0);
    details->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(22, 20, 22, 20);
    detailsLayout->setSpacing(0);
    detailsLayout->addWidget(createCardTitle(tr("Details"), details));

    auto addDetailRow = [&](const QString& label, const QString& value) {
        auto* row = new QWidget(details);
        row->setMinimumWidth(0);
        row->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 11, 0, 11);
        rowLayout->setSpacing(16);
        auto* left = new QLabel(label, row);
        left->setStyleSheet(QStringLiteral("color:#7a8498;font-size:13px;font-weight:600;background:transparent;"));
        left->setFixedWidth(110);
        auto* right = new QLabel(value.isEmpty() ? QStringLiteral("unknown") : value, row);
        right->setWordWrap(true);
        right->setMinimumWidth(0);
        right->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        right->setTextInteractionFlags(Qt::TextSelectableByMouse);
        right->setStyleSheet(QStringLiteral("color:#1f2a44;font-size:13px;font-weight:600;background:transparent;"));
        rowLayout->addWidget(left);
        rowLayout->addWidget(right, 1);
        detailsLayout->addWidget(row);
        detailsLayout->addWidget(createDivider(details));
    };

    addDetailRow(tr("version"), version.isEmpty() ? QStringLiteral("unknown") : version);
    addDetailRow(tr("author"), author.isEmpty() ? QStringLiteral("LinDesk") : author);
    addDetailRow(tr("permission"), settingsPermissionsText(requested));
    addDetailRow(tr("status"), multiInstance && running
        ? tr("running %1/3").arg(m_linWidgetInstanceCountsById.value(registryKey, 0))
        : (running ? tr("running") : (installed ? tr("installed") : tr("uninstall"))));
    addDetailRow(tr("description"), description.isEmpty() ? tr("no description") : description);

    if (isOfficial && installed) {
        layout->addSpacing(4);
        auto* note = createCardSubtitle(tr("This widget is already installed. Open the installed instance to manage permissions and runtime state."), details);
        note->setWordWrap(true);
        note->setMinimumWidth(0);
        note->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        detailsLayout->addWidget(note);
    }

    QString logTextForDrawer;
    QString errorsTextForDrawer = tr("no error");
    QStringList deniedPermissions;
    if (!isOfficial && !isMarketplace) {
        const QString logInstanceId = instanceId.isEmpty()
            ? settingsLinWidgetRegistryKey(id, isDevelopment)
            : instanceId;
        const QString logPath = settingsLinWidgetLogPath(logInstanceId);
        QFile logFile(logPath);
        if (!logFile.exists()) {
            const QString sharedLogPath = settingsLinWidgetLogPath(QStringLiteral("__shared__"));
            if (QFileInfo::exists(sharedLogPath)) {
                logFile.setFileName(sharedLogPath);
            } else {
                logTextForDrawer = tr("log file not created: \n%1").arg(logPath);
            }
        }
        if (logTextForDrawer.isEmpty()) {
            if (!logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                logTextForDrawer = tr("failed to open log file: \n%1").arg(logFile.fileName());
            } else {
                QByteArray contentBytes = logFile.readAll();
                constexpr int maxDrawerLogBytes = 120 * 1024;
                if (contentBytes.size() > maxDrawerLogBytes) {
                    contentBytes = contentBytes.right(maxDrawerLogBytes);
                }
                logTextForDrawer = QString::fromUtf8(contentBytes);
            }
        }
        QStringList errorLines;
        for (const QString& line : logTextForDrawer.split(QLatin1Char('\n'))) {
            const QString lower = line.toLower();
            if (lower.contains(QStringLiteral("permission denied")) ||
                lower.contains(QStringLiteral("error")) ||
                lower.contains(QStringLiteral("failed")) ||
                lower.contains(QStringLiteral("js.rejection"))) {
                errorLines.append(line);
            }
        }
        while (errorLines.size() > 8) {
            errorLines.removeFirst();
        }
        if (!errorLines.isEmpty()) {
            errorsTextForDrawer = errorLines.join(QLatin1Char('\n'));
        }
        QStringList missingRequestedPermissions;
        for (const QString& permission : requested) {
            const QString normalizedPermission = permission.trimmed().toLower();
            if (!normalizedPermission.isEmpty() &&
                !granted.contains(normalizedPermission) &&
                !missingRequestedPermissions.contains(normalizedPermission)) {
                missingRequestedPermissions.append(normalizedPermission);
            }
        }
        deniedPermissions = missingRequestedPermissions;
    }

    if (!isOfficial && !isMarketplace) {
        if (!isDevelopment && !deniedPermissions.isEmpty()) {
            auto* hint = new QWidget(details);
            hint->setObjectName(QStringLiteral("linWidgetPermissionHint"));
            hint->setAttribute(Qt::WA_StyledBackground, true);
            hint->setStyleSheet(QStringLiteral(
                "QWidget#linWidgetPermissionHint {"
                " background:#fff8e6;"
                " border:1px solid #fde7a5;"
                " border-radius:12px;"
                "}"));
            auto* hintLayout = new QHBoxLayout(hint);
            hintLayout->setContentsMargins(14, 12, 14, 12);
            hintLayout->setSpacing(12);

            auto* icon = new QLabel(glyph(0xE7BA), hint);
            icon->setAlignment(Qt::AlignCenter);
            icon->setFixedSize(30, 30);
            icon->setStyleSheet(QStringLiteral(
                "QLabel {"
                " color:#b45309;"
                " background:#ffedd5;"
                " border-radius:8px;"
                " font-family:'Segoe Fluent Icons','Segoe MDL2 Assets';"
                " font-size:15px;"
                "}"));
            hintLayout->addWidget(icon, 0, Qt::AlignTop);

            auto* body = new QVBoxLayout();
            body->setContentsMargins(0, 0, 0, 0);
            body->setSpacing(10);
            auto* text = new QLabel(tr("The widget request was intercepted by the permission system. Please authorize %1 and reload.").arg(deniedPermissions.join(QStringLiteral(", "))), hint);
            text->setWordWrap(true);
            text->setMinimumWidth(0);
            text->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            text->setStyleSheet(QStringLiteral("color:#7c4a03;font-size:13px;font-weight:600;background:transparent;"));
            body->addWidget(text);

            auto* grantRow = new QHBoxLayout();
            grantRow->setContentsMargins(0, 0, 0, 0);
            grantRow->setSpacing(8);
            for (const QString& permission : deniedPermissions) {
                auto* grantButton = createSettingsActionButton(tr("grant %1").arg(permission), hint, true);
                connect(grantButton, &QPushButton::clicked, this, [this, id, packagePath, registryKey, permission]() {
                    m_preserveLinWidgetDrawerOnReload = true;
                    setSettingsLinWidgetPermission(id, packagePath, registryKey, permission, true);
                    m_preserveLinWidgetDrawerOnReload = false;
                });
                grantRow->addWidget(grantButton, 0, Qt::AlignVCenter);
            }
            grantRow->addStretch();
            body->addLayout(grantRow);
            hintLayout->addLayout(body, 1);
            detailsLayout->addSpacing(12);
            detailsLayout->addWidget(hint);
        }
        detailsLayout->addSpacing(8);
        detailsLayout->addWidget(createCardTitle(tr("permissions"), details));
        auto* permissionGrid = new QWidget(details);
        permissionGrid->setMinimumWidth(0);
        permissionGrid->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto* permissionLayout = new QVBoxLayout(permissionGrid);
        permissionLayout->setContentsMargins(0, 12, 0, 12);
        permissionLayout->setSpacing(10);
        auto makePermissionToggle = [&](const QString& permission) {
            auto* toggle = new QWidget(details);
            toggle->setMinimumWidth(0);
            toggle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            auto* toggleLayout = new QHBoxLayout(toggle);
            toggleLayout->setContentsMargins(0, 0, 0, 0);
            toggleLayout->setSpacing(8);
            auto* check = new FluentCheckBox(toggle);
            check->setChecked(isDevelopment || granted.contains(permission));
            check->setEnabled(!isDevelopment);
            auto* label = new QLabel(permission, toggle);
            label->setStyleSheet(QStringLiteral("color:#1f2a44;font-size:13px;font-weight:600;background:transparent;"));
            label->setEnabled(!isDevelopment);
            label->setMinimumWidth(0);
            label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            toggleLayout->addWidget(check, 0, Qt::AlignVCenter);
            toggleLayout->addWidget(label, 1, Qt::AlignVCenter);
            connect(check, &FluentCheckBox::toggled, this, [this, id, packagePath, registryKey, permission](bool checked) {
                m_preserveLinWidgetDrawerOnReload = true;
                setSettingsLinWidgetPermission(id, packagePath, registryKey, permission, checked);
                m_preserveLinWidgetDrawerOnReload = false;
            });
            permissionLayout->addWidget(toggle);
        };
        makePermissionToggle(QStringLiteral("network"));
        makePermissionToggle(QStringLiteral("storage"));
        makePermissionToggle(QStringLiteral("desktop"));
        detailsLayout->addWidget(permissionGrid);

        auto* actionPanel = new QWidget(details);
        actionPanel->setMinimumWidth(0);
        actionPanel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto* actionRow = new QVBoxLayout(actionPanel);
        actionRow->setContentsMargins(0, 4, 0, 12);
        actionRow->setSpacing(8);
        auto* reloadButton = createSettingsActionButton(tr("reload"), details);
        auto* folderButton = createSettingsActionButton(tr("open folder"), details);
        auto* deleteButton = createSettingsActionButton(tr("uninstall"), details, false, true);
        reloadButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        folderButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        deleteButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        reloadButton->setEnabled(running);
        deleteButton->setEnabled(!isDevelopment);
        connect(reloadButton, &QPushButton::clicked, this, [this]() {
            m_preserveLinWidgetDrawerOnReload = true;
            reloadSelectedSettingsLinWidget();
            m_preserveLinWidgetDrawerOnReload = false;
        });
        connect(folderButton, &QPushButton::clicked, this, [packagePath]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(packagePath));
        });
        connect(deleteButton, &QPushButton::clicked, this, &LinWidgetsSettingsPage::uninstallSelectedSettingsLinWidget);
        actionRow->addWidget(reloadButton);
        actionRow->addWidget(folderButton);
        actionRow->addWidget(deleteButton);
        detailsLayout->addWidget(actionPanel);
    }

    auto* previewTitle = createCardTitle(tr("preview"), details);
    detailsLayout->addWidget(previewTitle);
    auto* preview = new QLabel(details);
    preview->setAlignment(Qt::AlignCenter);
    preview->setMinimumHeight(150);
    preview->setMinimumWidth(0);
    preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    preview->setWordWrap(true);
    preview->setText(tr("%1 preview").arg(name.isEmpty() ? id : name));
    preview->setStyleSheet(QStringLiteral(
        "QLabel {"
        " background:#ffffff;"
        " border:1px solid #e5edf7;"
        " border-radius:14px;"
        " color:#66738c;"
        " font-size:13px;"
        " font-weight:600;"
        "}"));
    detailsLayout->addSpacing(10);
    detailsLayout->addWidget(preview);

    if (!isOfficial && !isMarketplace) {
        detailsLayout->addSpacing(12);
        detailsLayout->addWidget(createCardTitle(tr("Recent errors"), details));
        auto* errorsLabel = createCardSubtitle(errorsTextForDrawer, details);
        errorsLabel->setMinimumWidth(0);
        errorsLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        detailsLayout->addWidget(errorsLabel);
    }

    layout->addWidget(details);
    layout->addStretch();
    openDrawer(content, QString(), 620);
}

void LinWidgetsSettingsPage::selectAndOpenLinWidgetSettingsItem(QListWidgetItem* item)
{
    if (!item || !(item->flags() & Qt::ItemIsEnabled)) {
        return;
    }

    QListWidget* list = item->listWidget();
    if (!list) {
        return;
    }

    if (list == m_installedLinWidgetsList && m_developmentLinWidgetsList) {
        m_developmentLinWidgetsList->clearSelection();
        m_developmentLinWidgetsList->setCurrentRow(-1);
    } else if (list == m_developmentLinWidgetsList && m_installedLinWidgetsList) {
        m_installedLinWidgetsList->clearSelection();
        m_installedLinWidgetsList->setCurrentRow(-1);
    } else if (list != m_installedLinWidgetsList && list != m_developmentLinWidgetsList) {
        if (m_installedLinWidgetsList) {
            m_installedLinWidgetsList->clearSelection();
            m_installedLinWidgetsList->setCurrentRow(-1);
        }
        if (m_developmentLinWidgetsList) {
            m_developmentLinWidgetsList->clearSelection();
            m_developmentLinWidgetsList->setCurrentRow(-1);
        }
    }

    if (list->currentItem() != item) {
        list->setCurrentItem(item);
    }
    refreshLinWidgetSettingsButtons();
    refreshLinWidgetSettingsInspector();
    openLinWidgetSettingsDrawer(item);
}

void LinWidgetsSettingsPage::refreshLinWidgetSettingsRunningMaps()
{
    m_linWidgetInstancesByPackage.clear();
    m_linWidgetInstancesById.clear();
    m_linWidgetInstanceCountsById.clear();

    auto& bridge = LinWidgetRuntimeBridge::instance();
    bool loadedFromRuntime = false;
    if (bridge.isLoaded()) {
        QJsonObject req;
        req.insert(QStringLiteral("action"), QStringLiteral("list"));
        const QJsonObject resp = bridge.queryWidgetManager(req);
        const QJsonArray widgets = resp.value(QStringLiteral("widgets")).toArray();
        if (!widgets.isEmpty()) {
            loadedFromRuntime = true;
            for (const QJsonValue& val : widgets) {
                const QJsonObject obj = val.toObject();
                const QString regId = obj.value(QStringLiteral("id")).toString().trimmed().toLower();
                const QString packagePath = obj.value(QStringLiteral("packagePath")).toString();
                const bool isDev = obj.value(QStringLiteral("isDevelopment")).toBool();
                const QString key = settingsLinWidgetRegistryKey(regId, isDev);
                const QJsonArray runningIds = obj.value(QStringLiteral("runningInstanceIds")).toArray();
                if (!runningIds.isEmpty()) {
                    for (const QJsonValue& idVal : runningIds) {
                        const QString instId = idVal.toString();
                        m_linWidgetInstancesById.insert(key, instId);
                        if (!packagePath.isEmpty()) {
                            m_linWidgetInstancesByPackage.insert(settingsLinWidgetPackageKey(packagePath), instId);
                        }
                    }
                    m_linWidgetInstanceCountsById.insert(key, runningIds.size());
                }
            }
        }
    }

    if (!loadedFromRuntime) {
        QVector<LinWidget::WidgetLayoutState> states;
        LinWidget::ConfigManager::loadLayout(LinWidget::ConfigManager::defaultConfigPath(), &states, nullptr);

        for (const LinWidget::WidgetLayoutState& state : states) {
            if (!state.visible) {
                continue;
            }
            m_linWidgetInstancesByPackage.insert(settingsLinWidgetPackageKey(state.packagePath), state.instanceId);
            const bool isDevelopment = state.isDevelopment || state.instanceId.startsWith(QStringLiteral("dev:"));
            QString registryId = state.registryId;
            LinWidget::WidgetManifest manifest;
            if (registryId.isEmpty() && LinWidget::WidgetPackage::loadManifest(state.packagePath, &manifest, nullptr)) {
                registryId = manifest.id;
            }
            if (!registryId.isEmpty()) {
                const QString key = settingsLinWidgetRegistryKey(registryId, isDevelopment);
                m_linWidgetInstancesById.insert(key, state.instanceId);
                m_linWidgetInstanceCountsById.insert(key, m_linWidgetInstanceCountsById.value(key, 0) + 1);
            }
        }
    }

    if (bridge.isLoaded() && bridge.hasActiveWallpaper()) {
        const QString activePath = bridge.activeWallpaperPackagePath();
        const QString activeId = bridge.activeWallpaperId();
        const bool isDev = bridge.activeWallpaperIsDevelopment();
        const QString wallInstanceId = QStringLiteral("wallpaper:") + activeId;
        m_linWidgetInstancesByPackage.insert(settingsLinWidgetPackageKey(activePath), wallInstanceId);
        const QString key = settingsLinWidgetRegistryKey(activeId, isDev);
        m_linWidgetInstancesById.insert(key, wallInstanceId);
        m_linWidgetInstanceCountsById.insert(key, m_linWidgetInstanceCountsById.value(key, 0) + 1);
    }
}

void LinWidgetsSettingsPage::loadSelectedSettingsLinWidget()
{
    const QString packagePath = selectedSettingsLinWidgetPackagePath();
    const QString registryId = selectedSettingsLinWidgetRegistryId();
    const bool isDevelopment = selectedSettingsLinWidgetIsDevelopment();
    if (packagePath.isEmpty()) {
        return;
    }
    if (QListWidget* list = activeSettingsLinWidgetList()) {
        if (QListWidgetItem* item = list->currentItem()) {
            if (!item->data(SettingsLinWidgetManifestValidRole).toBool()) {
                if (m_linWidgetsStatusLabel) {
                    m_linWidgetsStatusLabel->setText(tr("Load blocked: %1").arg(item->data(SettingsLinWidgetManifestErrorRole).toString()));
                }
                refreshLinWidgetSettingsButtons();
                return;
            }
        }
    }

    LinWidget::WidgetManifest manifest;
    QString errorMessage;
    if (!LinWidget::WidgetPackage::loadManifest(packagePath, &manifest, &errorMessage)) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Load failed: %1").arg(errorMessage));
        }
        return;
    }

    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (!bridge.isLoaded()) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Runtime not loaded"));
        }
        return;
    }
    const bool launched = bridge.startWidgetPackage(packagePath);
    if (!launched) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Load failed"));
        }
        return;
    }

    auto& registry = LinWidget::WidgetRegistryManager::instance();
    registry.load();
    registry.setLastRunState(registryId, isDevelopment, QStringLiteral("running"));
    registry.save();

    if (m_linWidgetsStatusLabel) {
        m_linWidgetsStatusLabel->setText(tr("Loaded widget: %1").arg(manifest.name));
    }
    reloadLinWidgetsSettingsPage();
}

void LinWidgetsSettingsPage::unloadSelectedSettingsLinWidget()
{
    const QString registryId = selectedSettingsLinWidgetRegistryId();
    const bool isDevelopment = selectedSettingsLinWidgetIsDevelopment();
    const QString registryKey = selectedSettingsLinWidgetRegistryKey();
    QString instanceId = selectedSettingsLinWidgetInstanceId();
    const QString packagePath = selectedSettingsLinWidgetPackagePath();

    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (bridge.isLoaded()) {
        QJsonObject req;
        req.insert(QStringLiteral("action"), QStringLiteral("list"));
        const QJsonObject resp = bridge.queryWidgetManager(req);
        const QJsonArray widgets = resp.value(QStringLiteral("widgets")).toArray();
        QStringList instancesToStop;
        for (const QJsonValue& val : widgets) {
            const QJsonObject obj = val.toObject();
            if (obj.value(QStringLiteral("id")).toString().compare(registryId, Qt::CaseInsensitive) == 0 &&
                obj.value(QStringLiteral("isDevelopment")).toBool() == isDevelopment) {
                const QJsonArray runningIds = obj.value(QStringLiteral("runningInstanceIds")).toArray();
                for (const QJsonValue& idVal : runningIds) {
                    instancesToStop.append(idVal.toString());
                }
            }
        }
        if (instancesToStop.isEmpty() && !instanceId.isEmpty()) {
            instancesToStop.append(instanceId);
        }
        if (instancesToStop.isEmpty() && !registryKey.isEmpty()) {
            instancesToStop.append(registryKey);
        }

        for (const QString& id : instancesToStop) {
            bridge.stopWidget(id);
        }
    }

    auto& registry = LinWidget::WidgetRegistryManager::instance();
    registry.load();
    if (!registryId.isEmpty()) {
        registry.setLastRunState(registryId, isDevelopment, QStringLiteral("stopped"));
        registry.save();
    }

    m_linWidgetInstancesById.remove(registryKey);
    m_linWidgetInstanceCountsById.remove(registryKey);
    if (!packagePath.isEmpty()) {
        m_linWidgetInstancesByPackage.remove(settingsLinWidgetPackageKey(packagePath));
    }

    if (m_linWidgetsStatusLabel) {
        m_linWidgetsStatusLabel->setText(tr("Unloaded widget: %1").arg(!registryId.isEmpty() ? registryId : instanceId));
    }
    reloadLinWidgetsSettingsPage();
}

void LinWidgetsSettingsPage::reloadSelectedSettingsLinWidget()
{
    const QString instanceId = selectedSettingsLinWidgetInstanceId();
    if (instanceId.isEmpty()) {
        refreshLinWidgetSettingsButtons();
        return;
    }
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (bridge.isLoaded()) {
        if (bridge.restartWidget(instanceId)) {
            if (m_linWidgetsStatusLabel) {
                m_linWidgetsStatusLabel->setText(tr("Reloaded widget: %1").arg(instanceId));
            }
        }
    }
    refreshLinWidgetSettingsInspector();
    refreshLinWidgetSettingsButtons();
}

void LinWidgetsSettingsPage::uninstallSelectedSettingsLinWidget()
{
    const QString packagePath = selectedSettingsLinWidgetPackagePath();
    const QString registryId = selectedSettingsLinWidgetRegistryId();
    const bool isDevelopment = selectedSettingsLinWidgetIsDevelopment();
    if (packagePath.isEmpty() || registryId.isEmpty()) {
        refreshLinWidgetSettingsButtons();
        return;
    }

    bool confirmed = true;
    if (!isDevelopment) {
        FluentDialog dialog(this);
        dialog.setDialogTitle(tr("卸载小组件"));
        dialog.setMessage(tr("确定要卸载这个小组件和它的保存记录吗？\n%1").arg(packagePath));
        dialog.addActionButton(tr("卸载"), FluentDialog::ActionType::Primary, [&confirmed]() {
            confirmed = true;
        });
        dialog.addActionButton(tr("取消"), FluentDialog::ActionType::Secondary);
        confirmed = false;
        dialog.exec();
    }
    if (!confirmed) {
        return;
    }

    const QString stableInstanceId = settingsLinWidgetRegistryKey(registryId, isDevelopment);
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (bridge.isLoaded()) {
        bridge.removeWidgetState(stableInstanceId);
    }

    if (isDevelopment) {
        auto& registry = LinWidget::WidgetRegistryManager::instance();
        registry.load();
        registry.removeDevelopmentProject(registryId);
        registry.save();
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Removed development record: %1").arg(registryId));
        }
        reloadLinWidgetsSettingsPage();
        return;
    }

    settingsRemoveLinWidgetLayoutState(stableInstanceId, packagePath);

    auto& registry = LinWidget::WidgetRegistryManager::instance();
    registry.load();
    registry.removeById(registryId);
    registry.save();

    QString errorMessage;
    if (!settingsRemoveDirectory(packagePath, &errorMessage) && m_linWidgetsStatusLabel) {
        m_linWidgetsStatusLabel->setText(tr("Widget record removed, folder delete failed: %1").arg(errorMessage));
    } else if (m_linWidgetsStatusLabel) {
        m_linWidgetsStatusLabel->setText(tr("Uninstalled installed widget: %1").arg(registryId));
    }
    reloadLinWidgetsSettingsPage();
}

void LinWidgetsSettingsPage::installOfficialSettingsLinWidget(const QString& packagePath)
{
    LinWidget::WidgetManifest manifest;
    QString errorMessage;
    if (!LinWidget::WidgetPackage::loadManifest(packagePath, &manifest, &errorMessage)) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Official install failed: %1").arg(errorMessage));
        }
        return;
    }
    bool accepted = false;
    const QStringList grantedPermissions = promptLinWidgetInstallPermissions(this, manifest, &accepted);
    if (!accepted) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Official install canceled: %1").arg(manifest.name));
        }
        return;
    }
    const QString targetPath = settingsInstalledTargetForManifest(manifest);
    if (!QFileInfo::exists(targetPath) &&
        !settingsCopyLinWidgetDirectory(packagePath, targetPath, &errorMessage)) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Official install failed: %1").arg(errorMessage));
        }
        return;
    }
    auto& registry = LinWidget::WidgetRegistryManager::instance();
    registry.load();
    registry.upsertFromPackage(targetPath, &errorMessage);
    registry.setPermissionsGranted(manifest.id, false, grantedPermissions);
    registry.save();
    QString instanceId;
    QString launchError;
    const bool launched = launchInstalledSettingsLinWidget(targetPath, manifest, &instanceId, &launchError);
    if (m_linWidgetsStatusLabel) {
        m_linWidgetsStatusLabel->setText(launched
            ? tr("Installed and loaded official widget: %1").arg(manifest.name)
            : tr("Installed official widget: %1\nLoad skipped: %2").arg(manifest.name, launchError));
    }
    reloadLinWidgetsSettingsPage();
}

bool LinWidgetsSettingsPage::launchInstalledSettingsLinWidget(const QString& packagePath,
                                                 const LinWidget::WidgetManifest& manifest,
                                                 QString* instanceId,
                                                 QString* errorMessage)
{
    QSettings settings(settingsConfigPath(), QSettings::IniFormat);
    if (!settings.value("LinWidgets/Enabled", true).toBool()) {
        if (errorMessage) {
            *errorMessage = tr("LinWidget runtime is disabled.");
        }
        return false;
    }

    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (!bridge.isLoaded()) {
        if (errorMessage) {
            *errorMessage = tr("LinWidget runtime is not available.");
        }
        return false;
    }

    const bool launched = bridge.startWidgetPackage(packagePath);
    if (!launched) {
        if (errorMessage) {
            *errorMessage = tr("Failed to launch widget package.");
        }
        return false;
    }

    auto& registry = LinWidget::WidgetRegistryManager::instance();
    registry.load();
    registry.setLastRunState(manifest.id, false, QStringLiteral("running"));
    registry.save();
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

void LinWidgetsSettingsPage::resetOfficialSettingsLinWidget(const QString& packagePath)
{
    LinWidget::WidgetManifest manifest;
    QString errorMessage;
    if (!LinWidget::WidgetPackage::loadManifest(packagePath, &manifest, &errorMessage)) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Official reset failed: %1").arg(errorMessage));
        }
        return;
    }
    const QString targetPath = settingsInstalledTargetForManifest(manifest);
    if (!settingsRemoveDirectory(targetPath, &errorMessage) ||
        !settingsCopyLinWidgetDirectory(packagePath, targetPath, &errorMessage)) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Official reset failed: %1").arg(errorMessage));
        }
        return;
    }
    auto& registry = LinWidget::WidgetRegistryManager::instance();
    registry.load();
    registry.upsertFromPackage(targetPath, &errorMessage);
    registry.save();
    if (m_linWidgetsStatusLabel) {
        m_linWidgetsStatusLabel->setText(tr("Reset official widget: %1").arg(manifest.name));
    }
    reloadLinWidgetsSettingsPage();
}

void LinWidgetsSettingsPage::addDevelopmentSettingsLinWidget()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Select LinWidget development project"));
    if (dir.isEmpty()) {
        return;
    }
    auto& registry = LinWidget::WidgetRegistryManager::instance();
    QString errorMessage;
    registry.load();
    if (!registry.addDevelopmentProject(dir, &errorMessage) || !registry.save(&errorMessage)) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Add development project failed: %1").arg(errorMessage));
        }
        return;
    }
    if (m_linWidgetsStatusLabel) {
        m_linWidgetsStatusLabel->setText(tr("Added development project: %1").arg(dir));
    }
    reloadLinWidgetsSettingsPage();
}

void LinWidgetsSettingsPage::createDevelopmentSettingsLinWidget()
{
    const QString targetDir = QFileDialog::getExistingDirectory(this, tr("Choose parent folder for new widget project"));
    if (targetDir.isEmpty()) {
        return;
    }
    bool ok = false;
    const QString id = QInputDialog::getText(this, tr("Widget Id"), tr("Use reverse domain style, for example com.example.weather"), QLineEdit::Normal, QStringLiteral("com.example.widget"), &ok).trimmed();
    if (!ok || id.isEmpty()) {
        return;
    }
    const QString name = QInputDialog::getText(this, tr("Widget Name"), tr("Display name"), QLineEdit::Normal, QStringLiteral("New Widget"), &ok).trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    const QString author = QInputDialog::getText(this, tr("Author"), tr("Author"), QLineEdit::Normal, qEnvironmentVariable("USERNAME"), &ok).trimmed();
    if (!ok) {
        return;
    }
    const QString description = QInputDialog::getText(this, tr("Description"), tr("Short description"), QLineEdit::Normal, QStringLiteral("A LinWidget project."), &ok).trimmed();
    if (!ok) {
        return;
    }

    LinWidget::WidgetProjectMetadata metadata;
    metadata.id = id;
    metadata.name = name;
    metadata.author = author;
    metadata.description = description;

    const QString projectDir = QDir(targetDir).filePath(safeLinWidgetFileName(id));
    QString errorMessage;
    if (!LinWidget::LinWidgetProjectTools::createProject(projectDir, metadata, &errorMessage)) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Create project failed: %1").arg(errorMessage));
        }
        return;
    }

    auto& registry = LinWidget::WidgetRegistryManager::instance();
    registry.load();
    registry.addDevelopmentProject(projectDir, &errorMessage);
    registry.save();
    if (m_linWidgetsStatusLabel) {
        m_linWidgetsStatusLabel->setText(tr("Created development project: %1").arg(projectDir));
    }
    reloadLinWidgetsSettingsPage();
}

void LinWidgetsSettingsPage::exportDevelopmentSettingsLinWidget()
{
    const QString packagePath = selectedSettingsLinWidgetPackagePath();
    if (packagePath.isEmpty() || !selectedSettingsLinWidgetIsDevelopment() || !selectedSettingsLinWidgetInstanceId().isEmpty()) {
        refreshLinWidgetSettingsButtons();
        return;
    }
    const QString id = selectedSettingsLinWidgetRegistryId();
    const QString outputPath = QFileDialog::getSaveFileName(this,
                                                           tr("Export LinWidget Zip"),
                                                           QDir::home().filePath(safeLinWidgetFileName(id) + QStringLiteral(".zip")),
                                                           tr("Zip package (*.zip)"));
    if (outputPath.isEmpty()) {
        return;
    }
    QString errorMessage;
    if (!LinWidget::LinWidgetProjectTools::exportProjectZip(packagePath, outputPath, &errorMessage)) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Export zip failed: %1").arg(errorMessage));
        }
        return;
    }
    if (m_linWidgetsStatusLabel) {
        m_linWidgetsStatusLabel->setText(tr("Exported zip package: %1").arg(outputPath));
    }
}

void LinWidgetsSettingsPage::deleteSelectedSettingsLinWidget()
{
    uninstallSelectedSettingsLinWidget();
}

void LinWidgetsSettingsPage::setSelectedSettingsLinWidgetPermission(const QString& permission, bool granted)
{
    QListWidget* list = activeSettingsLinWidgetList();
    QListWidgetItem* item = list ? list->currentItem() : nullptr;
    if (!item) {
        refreshLinWidgetSettingsButtons();
        return;
    }

    const QString registryId = item->data(SettingsLinWidgetRegistryIdRole).toString();
    const QString packagePath = item->data(SettingsLinWidgetPackagePathRole).toString();
    const bool isDevelopment = item->data(SettingsLinWidgetDevelopmentRole).toBool();
    if (registryId.trimmed().isEmpty() || isDevelopment) {
        refreshLinWidgetSettingsButtons();
        return;
    }

    setSettingsLinWidgetPermission(registryId,
                                   packagePath,
                                   settingsLinWidgetRegistryKey(registryId, false),
                                   permission,
                                   granted);
}

void LinWidgetsSettingsPage::setSettingsLinWidgetPermission(const QString& registryId,
                                               const QString& packagePath,
                                               const QString& selectedKey,
                                               const QString& permission,
                                               bool granted)
{
    const QString normalizedId = registryId.trimmed().toLower();
    if (normalizedId.isEmpty()) {
        refreshLinWidgetSettingsButtons();
        return;
    }

    auto& registry = LinWidget::WidgetRegistryManager::instance();
    QString registryError;
    if (!registry.load(&registryError)) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Save permission failed: %1").arg(registryError));
        }
        refreshLinWidgetSettingsButtons();
        return;
    }

    LinWidget::WidgetRegistryEntry entry = registry.entryById(normalizedId);
    QStringList permissions = entry.permissionsGranted;
    const QString normalized = permission.trimmed().toLower();
    if (granted && !permissions.contains(normalized)) {
        permissions.append(normalized);
    } else if (!granted) {
        permissions.removeAll(normalized);
    }
    permissions.sort();

    if (!registry.setPermissionsGranted(normalizedId, false, permissions)) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Save permission failed: widget record not found."));
        }
        refreshLinWidgetSettingsButtons();
        return;
    }
    if (!registry.save(&registryError)) {
        if (m_linWidgetsStatusLabel) {
            m_linWidgetsStatusLabel->setText(tr("Save permission failed: %1").arg(registryError));
        }
        refreshLinWidgetSettingsButtons();
        return;
    }

    const QString stableSelectedKey = selectedKey.isEmpty()
        ? settingsLinWidgetRegistryKey(normalizedId, false)
        : selectedKey;
    const QString packageKey = settingsLinWidgetPackageKey(packagePath.isEmpty() ? entry.packagePath : packagePath);
    const QString instanceId = m_linWidgetInstancesById.value(stableSelectedKey, m_linWidgetInstancesByPackage.value(packageKey));
    if (!instanceId.isEmpty()) {
        LinWidgetRuntimeBridge::instance().restartWidget(instanceId);
    }
    reloadLinWidgetsSettingsPage();
    if (!stableSelectedKey.isEmpty()) {
        auto restoreSelection = [this, stableSelectedKey](QListWidget* targetList) -> QListWidgetItem* {
            if (!targetList) {
                return nullptr;
            }
            for (int i = 0; i < targetList->count(); ++i) {
                QListWidgetItem* candidate = targetList->item(i);
                if (!candidate) {
                    continue;
                }
                const QString candidateKey = settingsLinWidgetRegistryKey(
                    candidate->data(SettingsLinWidgetRegistryIdRole).toString(),
                    candidate->data(SettingsLinWidgetDevelopmentRole).toBool());
                if (candidateKey == stableSelectedKey) {
                    targetList->setCurrentItem(candidate);
                    return candidate;
                }
            }
            return nullptr;
        };
        QListWidgetItem* restoredItem = restoreSelection(m_installedLinWidgetsList);
        if (!restoredItem) {
            restoredItem = restoreSelection(m_developmentLinWidgetsList);
        }
        if (m_preserveLinWidgetDrawerOnReload && restoredItem) {
            openLinWidgetSettingsDrawer(restoredItem);
        }
    }
    refreshLinWidgetSettingsButtons();
    refreshLinWidgetSettingsInspector();
}

QListWidget* LinWidgetsSettingsPage::activeSettingsLinWidgetList() const
{
    if (m_linWidgetSubsystemStack && m_linWidgetSubsystemStack->currentIndex() == 1) {
        if (m_wallpaperList && m_wallpaperList->currentItem()) {
            return m_wallpaperList;
        }
    }

    const int tabIndex = m_linWidgetsTabs ? m_linWidgetsTabs->currentIndex() : -1;
    if (tabIndex == 0 && m_officialLinWidgetsList && m_officialLinWidgetsList->currentItem()) {
        return m_officialLinWidgetsList;
    }
    if (tabIndex == 1 && m_marketplaceLinWidgetsList && m_marketplaceLinWidgetsList->currentItem()) {
        return m_marketplaceLinWidgetsList;
    }
    if (tabIndex == 2 && m_installedLinWidgetsList && m_installedLinWidgetsList->currentItem()) {
        return m_installedLinWidgetsList;
    }
    if (tabIndex == 3 && m_developmentLinWidgetsList && m_developmentLinWidgetsList->currentItem()) {
        return m_developmentLinWidgetsList;
    }

    if (m_installedLinWidgetsList && !m_installedLinWidgetsList->selectedItems().isEmpty() && m_installedLinWidgetsList->currentItem()) {
        return m_installedLinWidgetsList;
    }
    if (m_officialLinWidgetsList && !m_officialLinWidgetsList->selectedItems().isEmpty() && m_officialLinWidgetsList->currentItem()) {
        return m_officialLinWidgetsList;
    }
    if (m_developmentLinWidgetsList && !m_developmentLinWidgetsList->selectedItems().isEmpty() && m_developmentLinWidgetsList->currentItem()) {
        return m_developmentLinWidgetsList;
    }
    if (m_wallpaperList && !m_wallpaperList->selectedItems().isEmpty() && m_wallpaperList->currentItem()) {
        return m_wallpaperList;
    }
    if (m_marketplaceLinWidgetsList && !m_marketplaceLinWidgetsList->selectedItems().isEmpty() && m_marketplaceLinWidgetsList->currentItem()) {
        return m_marketplaceLinWidgetsList;
    }

    if (m_installedLinWidgetsList && m_installedLinWidgetsList->currentItem()) {
        return m_installedLinWidgetsList;
    }
    if (m_officialLinWidgetsList && m_officialLinWidgetsList->currentItem()) {
        return m_officialLinWidgetsList;
    }
    if (m_developmentLinWidgetsList && m_developmentLinWidgetsList->currentItem()) {
        return m_developmentLinWidgetsList;
    }
    if (m_wallpaperList && m_wallpaperList->currentItem()) {
        return m_wallpaperList;
    }
    return nullptr;
}

QString LinWidgetsSettingsPage::selectedSettingsLinWidgetPackagePath() const
{
    QListWidget* list = activeSettingsLinWidgetList();
    return list && list->currentItem()
        ? list->currentItem()->data(SettingsLinWidgetPackagePathRole).toString()
        : QString();
}

QString LinWidgetsSettingsPage::selectedSettingsLinWidgetRegistryId() const
{
    QListWidget* list = activeSettingsLinWidgetList();
    return list && list->currentItem()
        ? list->currentItem()->data(SettingsLinWidgetRegistryIdRole).toString()
        : QString();
}

bool LinWidgetsSettingsPage::selectedSettingsLinWidgetIsDevelopment() const
{
    QListWidget* list = activeSettingsLinWidgetList();
    return list && list->currentItem() && list->currentItem()->data(SettingsLinWidgetDevelopmentRole).toBool();
}

QString LinWidgetsSettingsPage::selectedSettingsLinWidgetInstanceId() const
{
    const QString registryKey = selectedSettingsLinWidgetRegistryKey();
    if (m_linWidgetInstancesById.contains(registryKey)) {
        return m_linWidgetInstancesById.value(registryKey);
    }

    const QString packageKey = settingsLinWidgetPackageKey(selectedSettingsLinWidgetPackagePath());
    if (m_linWidgetInstancesByPackage.contains(packageKey)) {
        return m_linWidgetInstancesByPackage.value(packageKey);
    }
    return QString();
}

QString LinWidgetsSettingsPage::selectedSettingsLinWidgetRegistryKey() const
{
    return settingsLinWidgetRegistryKey(selectedSettingsLinWidgetRegistryId(), selectedSettingsLinWidgetIsDevelopment());
}

QString LinWidgetsSettingsPage::settingsLinWidgetRegistryKey(const QString& id, bool isDevelopment) const
{
    return QStringLiteral("%1:%2").arg(isDevelopment ? QStringLiteral("dev") : QStringLiteral("installed"),
                                       id.trimmed().toLower());
}

QString LinWidgetsSettingsPage::settingsLinWidgetPackageKey(const QString& packagePath) const
{
    const QFileInfo info(packagePath);
    const QString path = info.exists() ? info.absoluteFilePath() : packagePath;
    return QDir::cleanPath(path).toLower();
}

QString LinWidgetsSettingsPage::settingsLinWidgetLogPath(const QString& instanceId) const
{
    if (instanceId.isEmpty()) {
        return QString();
    }
    return QDir(AppPaths::linWidgetLogsDir()).filePath(safeLinWidgetInstanceFileNameForSettings(instanceId) + QStringLiteral(".log"));
}
