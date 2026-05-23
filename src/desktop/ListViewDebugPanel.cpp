#include "desktop/ListViewDebugPanel.h"
#include "app/AppFonts.h"
#include "desktop/LinDesktop.h"
#include "desktop/ListView.h"
#include "desktop/widgets/linwidget/WidgetManager.h"
#include "desktop/widgets/linwidget/ConfigManager.h"
#include "desktop/widgets/linwidget/WidgetPackage.h"
#include "desktop/widgets/linwidget/WidgetRegistryManager.h"
#include "desktop/widgets/linwidget/WidgetSettingsDialog.h"
#include "ui/QmlTestWindow.h"
#include "ui/SoftDesktopTextPreview.h"
#include "ui/SoftDesktopTextRenderer.h"
#include "ui/Fluent/FluentToggleSwitch.h"
#include "ui/Fluent/FluentCard.h"
#include "services/AppPaths.h"
#include "services/DebugInfoExporter.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QDesktopServices>
#include <QUrl>
#include <QVBoxLayout>
#include <QTimer>

#include <QListWidget>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTextCursor>
#include <QPainterPath>
#include <QResizeEvent>
#include <QToolButton>

// Custom delegate to draw the Fluent Design 2 style sidebar
#include <QTabBar>
#include <QStackedWidget>
#include <QPainterPath>

namespace {
QString debugPanelStyleSheet()
{
    return QStringLiteral(
        "QWidget { background: #f5f7fb; }"
        "QScrollArea { background: transparent; border: none; }"
        "QScrollBar:vertical { background: transparent; width: 10px; margin: 4px; }"
        "QScrollBar::handle:vertical { background: rgba(31, 41, 55, 0.18); border-radius: 5px; min-height: 36px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; height: 0px; }"
        "QTabBar::tab {"
        "   height: 40px; min-width: 128px;"
        "   padding: 0 18px; margin-right: 10px;"
        "   background: transparent; border: none;"
        "   font-family: 'Microsoft YaHei UI'; font-size: 14px; font-weight: 500; color: #6b7280;"
        "}"
        "QTabBar::tab:hover { color: #111827; }"
        "QTabBar::tab:selected { color: #0f6cbd; border-bottom: 3px solid #0f6cbd; font-weight: 600; }"
        "QToolTip {"
        "   color: #111827;"
        "   background-color: #ffffff;"
        "   border: 1px solid rgba(15, 23, 42, 0.16);"
        "   padding: 6px 8px;"
        "   border-radius: 6px;"
        "}");
}

QString debugConfigPath()
{
    return AppPaths::configFilePath();
}

QString bundledLinWidgetsDir()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/LinWidgets"));
}

QString userLinWidgetsDir()
{
    return AppPaths::userLinWidgetsDir();
}

QString safeLinWidgetInstanceFileName(QString instanceId)
{
    for (QChar& ch : instanceId) {
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('.') && ch != QLatin1Char('-') && ch != QLatin1Char('_')) {
            ch = QLatin1Char('_');
        }
    }
    return instanceId.isEmpty() ? QStringLiteral("widget") : instanceId;
}

constexpr int LinWidgetPackagePathRole = Qt::UserRole;
constexpr int LinWidgetRegistryIdRole = Qt::UserRole + 1;
constexpr int LinWidgetRuntimeTypeRole = Qt::UserRole + 2;
constexpr int LinWidgetDefaultSizeRole = Qt::UserRole + 3;
constexpr int LinWidgetDevelopmentRole = Qt::UserRole + 4;
constexpr int LinWidgetNameRole = Qt::UserRole + 5;
constexpr int LinWidgetVersionRole = Qt::UserRole + 6;
constexpr int LinWidgetAuthorRole = Qt::UserRole + 7;
constexpr int LinWidgetDescriptionRole = Qt::UserRole + 8;
constexpr int LinWidgetAutoLoadRole = Qt::UserRole + 9;
constexpr int LinWidgetPermissionsRole = Qt::UserRole + 10;
constexpr int LinWidgetRequestedPermissionsRole = Qt::UserRole + 11;
constexpr int LinWidgetDisplayTextRole = Qt::UserRole + 12;
constexpr int LinWidgetSettingsRole = Qt::UserRole + 13;

bool shouldSkipLinWidgetInstallEntry(const QString& name)
{
    return name == QStringLiteral(".webview2") ||
        name == QStringLiteral(".git") ||
        name == QStringLiteral("node_modules") ||
        name.compare(QStringLiteral("cache"), Qt::CaseInsensitive) == 0 ||
        name.compare(QStringLiteral("tmp"), Qt::CaseInsensitive) == 0 ||
        name.compare(QStringLiteral("temp"), Qt::CaseInsensitive) == 0;
}

bool copyDirectoryForFirstInstall(const QString& sourcePath, const QString& targetPath)
{
    const QDir sourceDir(sourcePath);
    if (!sourceDir.exists()) {
        return false;
    }

    QDir().mkpath(targetPath);
    bool copiedAny = false;
    const QFileInfoList entries = sourceDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& entry : entries) {
        if (shouldSkipLinWidgetInstallEntry(entry.fileName())) {
            continue;
        }

        const QString targetEntryPath = QDir(targetPath).filePath(entry.fileName());
        if (entry.isDir()) {
            copiedAny = copyDirectoryForFirstInstall(entry.absoluteFilePath(), targetEntryPath) || copiedAny;
            continue;
        }
        if (QFileInfo::exists(targetEntryPath)) {
            QFile::remove(targetEntryPath);
        }
        copiedAny = QFile::copy(entry.absoluteFilePath(), targetEntryPath) || copiedAny;
    }
    return copiedAny;
}

bool clearDirectoryForBundledReset(const QString& targetPath)
{
    const QDir targetDir(targetPath);
    if (!targetDir.exists()) {
        return true;
    }

    const QString root = QDir::cleanPath(QFileInfo(targetPath).absoluteFilePath());
    const QString rootWithSeparator = root.endsWith(QLatin1Char('/')) || root.endsWith(QLatin1Char('\\'))
        ? root
        : root + QDir::separator();
    if (root.isEmpty() || root == QDir::rootPath()) {
        return false;
    }

    bool ok = true;
    const QFileInfoList entries = targetDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name);
    for (const QFileInfo& entry : entries) {
        if (!QDir::cleanPath(entry.absoluteFilePath()).startsWith(rootWithSeparator, Qt::CaseInsensitive)) {
            ok = false;
            continue;
        }
        if (entry.isDir()) {
            ok = QDir(entry.absoluteFilePath()).removeRecursively() && ok;
        } else {
            ok = QFile::remove(entry.absoluteFilePath()) && ok;
        }
    }
    return ok;
}

bool removeLinWidgetDirectory(const QString& targetPath, QString* errorMessage = nullptr)
{
    const QFileInfo targetInfo(targetPath);
    const QString cleanTarget = QDir::cleanPath(targetInfo.absoluteFilePath());
    if (!targetInfo.exists()) {
        if (errorMessage) {
            errorMessage->clear();
        }
        return true;
    }
    if (!targetInfo.isDir() || cleanTarget.isEmpty() || cleanTarget == QDir::rootPath()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid widget directory: %1").arg(targetPath);
        }
        return false;
    }
    if (!QDir(cleanTarget).removeRecursively()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to delete widget directory: %1").arg(cleanTarget);
        }
        return false;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool resetBundledLinWidget(const QString& widgetFolderName, QString* errorMessage = nullptr)
{
    const QString sourcePath = QDir(bundledLinWidgetsDir()).filePath(widgetFolderName);
    const QString targetPath = QDir(userLinWidgetsDir()).filePath(widgetFolderName);
    if (!QDir(sourcePath).exists()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Bundled widget source does not exist: %1").arg(sourcePath);
        }
        return false;
    }
    if (!clearDirectoryForBundledReset(targetPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to clear existing widget folder: %1").arg(targetPath);
        }
        return false;
    }
    if (!copyDirectoryForFirstInstall(sourcePath, targetPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to copy bundled widget: %1").arg(sourcePath);
        }
        return false;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

QStringList resetAllBundledLinWidgets(QString* errorMessage = nullptr)
{
    const QDir bundledDir(bundledLinWidgetsDir());
    if (!bundledDir.exists()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Bundled widgets source does not exist: %1").arg(bundledLinWidgetsDir());
        }
        return {};
    }

    QStringList resetFolders;
    const QFileInfoList entries = bundledDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& entry : entries) {
        QString itemError;
        if (!resetBundledLinWidget(entry.fileName(), &itemError)) {
            if (errorMessage) {
                *errorMessage = itemError;
            }
            return {};
        }
        resetFolders.append(entry.fileName());
    }

    const QString markerPath = QDir(userLinWidgetsDir()).filePath(QStringLiteral(".defaults-installed"));
    QFile marker(markerPath);
    if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        marker.write("1\n");
    }

    if (errorMessage) {
        errorMessage->clear();
    }
    return resetFolders;
}

void ensureDefaultLinWidgetsInstalled()
{
    const QDir bundledDir(bundledLinWidgetsDir());
    if (!bundledDir.exists()) {
        return;
    }

    const QString targetRoot = userLinWidgetsDir();
    const QString markerPath = QDir(targetRoot).filePath(QStringLiteral(".defaults-installed"));
    if (QFileInfo::exists(markerPath)) {
        return;
    }

    const QFileInfoList entries = bundledDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& entry : entries) {
        const QString targetPath = QDir(targetRoot).filePath(entry.fileName());
        if (QFileInfo::exists(targetPath)) {
            continue;
        }
        copyDirectoryForFirstInstall(entry.absoluteFilePath(), targetPath);
    }

    QFile marker(markerPath);
    if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        marker.write("1\n");
    }
}

void removeLinWidgetLayoutState(const QString& instanceId, const QString& packagePath)
{
    QVector<LinWidget::WidgetLayoutState> states;
    QString errorMessage;
    if (!LinWidget::ConfigManager::loadLayout(LinWidget::ConfigManager::defaultConfigPath(), &states, &errorMessage)) {
        return;
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
    LinWidget::ConfigManager::saveLayout(LinWidget::ConfigManager::defaultConfigPath(), keptStates, nullptr);
}
}

ListViewDebugPanel::ListViewDebugPanel(ListView* listView, QWidget* parent)
    : QWidget(parent), m_listView(listView)
{
    initUI();
}

void ListViewDebugPanel::initUI() {
    setWindowTitle("ListView Debug Console");
    setAttribute(Qt::WA_DeleteOnClose, false);
    setMinimumSize(520, 520);
    resize(960, 800);
    setStyleSheet(debugPanelStyleSheet());

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* headerContainer = new QWidget(this);
    headerContainer->setMinimumHeight(126);
    headerContainer->setStyleSheet("background-color: #ffffff; border-bottom: 1px solid #e5e7eb;");
    auto* headerLayout = new QVBoxLayout(headerContainer);
    headerLayout->setContentsMargins(32, 22, 32, 12);
    headerLayout->setSpacing(8);

    auto* titleLabel = new QLabel("Debug Console", headerContainer);
    titleLabel->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 28px; font-weight: 600; color: #111827;")
        .arg(AppFonts::uiCssFamily()));
    headerLayout->addWidget(titleLabel);

    auto* subtitleLabel = new QLabel("Inspect desktop behavior, tune layout values, and jump to app settings.", headerContainer);
    subtitleLabel->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 13px; color: #6b7280;")
        .arg(AppFonts::uiCssFamily()));
    headerLayout->addWidget(subtitleLabel);

    m_tabBar = new QTabBar(headerContainer);
    m_tabBar->addTab("General Tools");
    m_tabBar->addTab("LinWidgets");
    m_tabBar->addTab("Layout Settings");
    m_tabBar->addTab("Text Diagnostics");
    m_tabBar->setDrawBase(false);
    m_tabBar->setUsesScrollButtons(true);
    m_tabBar->setExpanding(false);
    headerLayout->addWidget(m_tabBar);
    mainLayout->addWidget(headerContainer);

    m_contentStack = new QStackedWidget(this);
    m_contentStack->addWidget(wrapScrollablePage(createGeneralPage()));
    m_contentStack->addWidget(wrapScrollablePage(createLinWidgetsPage()));
    m_contentStack->addWidget(wrapScrollablePage(createLayoutPage()));
    m_contentStack->addWidget(wrapScrollablePage(createTextDiagnosticsPage()));
    mainLayout->addWidget(m_contentStack);

    connect(m_tabBar, &QTabBar::currentChanged, m_contentStack, &QStackedWidget::setCurrentIndex);
    m_tabBar->setCurrentIndex(0);
    updateAdaptiveLayout();

    // Initialize values
    if (m_listView) {
        m_rowSlider->setValue(m_listView->rowSpacing());
        m_colSlider->setValue(m_listView->colSpacing());
    }
    m_largeGlowSlider->setValue(6);
    m_tightGlowSlider->setValue(2);
    m_glowAlphaSlider->setValue(210);
    m_innerAlphaSlider->setValue(155);
    m_glowOffsetXSlider->setValue(0);
    m_glowOffsetYSlider->setValue(1);
    m_innerOffsetXSlider->setValue(0);
    m_innerOffsetYSlider->setValue(1);
    
    updateLabels();
    applyTextPreviewOptions();
}

void ListViewDebugPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateAdaptiveLayout();
}

void ListViewDebugPanel::updateAdaptiveLayout()
{
    if (m_generalGrid) {
        const int availableWidth = width();
        const int columnCount = availableWidth < 760 ? 1 : 2;
        for (QWidget* card : m_generalCards) {
            m_generalGrid->removeWidget(card);
        }
        int row = 0;
        int column = 0;
        for (QWidget* card : m_generalCards) {
            if (m_generalWideCards.value(card, false)) {
                if (column != 0) {
                    ++row;
                    column = 0;
                }
                m_generalGrid->addWidget(card, row, 0, 1, columnCount);
                ++row;
                column = 0;
                continue;
            }
            m_generalGrid->addWidget(card, row, column);
            ++column;
            if (column >= columnCount) {
                column = 0;
                ++row;
            }
        }
    }

    if (m_linWidgetsActionGroupsGrid) {
        const int columnCount = width() < 760 ? 1 : width() < 1080 ? 2 : 4;
        for (QWidget* group : m_linWidgetActionGroups) {
            m_linWidgetsActionGroupsGrid->removeWidget(group);
        }
        for (int i = 0; i < m_linWidgetActionGroups.size(); ++i) {
            QWidget* group = m_linWidgetActionGroups.at(i);
            m_linWidgetsActionGroupsGrid->addWidget(group, i / columnCount, i % columnCount);
        }
    }

    if (m_linWidgetsContentGrid && m_linWidgetsListsPanel && m_linWidgetsLogPanel) {
        m_linWidgetsContentGrid->removeWidget(m_linWidgetsListsPanel);
        m_linWidgetsContentGrid->removeWidget(m_linWidgetsLogPanel);
        if (width() < 820) {
            m_linWidgetsContentGrid->addWidget(m_linWidgetsListsPanel, 0, 0);
            m_linWidgetsContentGrid->addWidget(m_linWidgetsLogPanel, 1, 0);
            m_linWidgetsContentGrid->setColumnStretch(0, 1);
            m_linWidgetsContentGrid->setColumnStretch(1, 0);
        } else {
            m_linWidgetsContentGrid->addWidget(m_linWidgetsListsPanel, 0, 0);
            m_linWidgetsContentGrid->addWidget(m_linWidgetsLogPanel, 0, 1);
            m_linWidgetsContentGrid->setColumnStretch(0, 1);
            m_linWidgetsContentGrid->setColumnStretch(1, 1);
        }
    }

    if (m_linWidgetsCompactSwitchGrid) {
        const int columnCount = width() < 680 ? 1 : 3;
        for (QWidget* item : m_linWidgetsCompactSwitchItems) {
            m_linWidgetsCompactSwitchGrid->removeWidget(item);
        }
        for (int i = 0; i < m_linWidgetsCompactSwitchItems.size(); ++i) {
            m_linWidgetsCompactSwitchGrid->addWidget(m_linWidgetsCompactSwitchItems.at(i), i / columnCount, i % columnCount);
        }
        for (int column = 0; column < 3; ++column) {
            m_linWidgetsCompactSwitchGrid->setColumnStretch(column, column < columnCount ? 1 : 0);
        }
    }
}

QPushButton* ListViewDebugPanel::createActionButton(const QString& text, bool accent, bool destructive) const
{
    auto* button = new QPushButton(text);
    button->setFixedHeight(34);
    button->setMinimumWidth(88);

    QString background = QStringLiteral("#ffffff");
    QString foreground = QStringLiteral("#1f2937");
    QString border = QStringLiteral("rgba(15, 23, 42, 0.14)");
    QString hover = QStringLiteral("#f3f4f6");
    if (accent) {
        background = QStringLiteral("#0f6cbd");
        foreground = QStringLiteral("#ffffff");
        border = QStringLiteral("#0f6cbd");
        hover = QStringLiteral("#115ea3");
    } else if (destructive) {
        background = QStringLiteral("#d92d20");
        foreground = QStringLiteral("#ffffff");
        border = QStringLiteral("#d92d20");
        hover = QStringLiteral("#b42318");
    }

    button->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  min-width: 88px; padding: 0 14px; border-radius: 8px;"
        "  border: 1px solid %1; background: %2; color: %3;"
        "  font-family: %4; font-size: 13px; font-weight: 600;"
        "}"
        "QPushButton:hover { background: %5; }"
        "QPushButton:pressed { padding-top: 1px; }")
        .arg(border, background, foreground, AppFonts::uiCssFamily(), hover));
    return button;
}

QWidget* ListViewDebugPanel::createSliderSettingRow(const QString& labelText,
                                                    int minValue,
                                                    int maxValue,
                                                    QSlider*& slider,
                                                    QLabel*& valueLabel)
{
    auto* container = new QWidget();
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* label = new QLabel(labelText, container);
    label->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 13px; font-weight: 600; color: #111827;")
        .arg(AppFonts::uiCssFamily()));
    layout->addWidget(label);

    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(12);

    slider = new QSlider(Qt::Horizontal, container);
    slider->setRange(minValue, maxValue);
    row->addWidget(slider, 1);

    valueLabel = new QLabel("0", container);
    valueLabel->setFixedWidth(36);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    valueLabel->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 12px; color: #6b7280;")
        .arg(AppFonts::uiCssFamily()));
    row->addWidget(valueLabel);

    layout->addLayout(row);
    return container;
}

QLabel* ListViewDebugPanel::createPageTitleLabel(const QString& text) const
{
    auto* label = new QLabel(text);
    label->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 24px; font-weight: 600; color: #111827;")
        .arg(AppFonts::uiCssFamily()));
    return label;
}

QLabel* ListViewDebugPanel::createPageSubtitleLabel(const QString& text) const
{
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    label->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 13px; color: #6b7280;")
        .arg(AppFonts::uiCssFamily()));
    return label;
}

QWidget* ListViewDebugPanel::wrapScrollablePage(QWidget* content) {
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("background: transparent;");
    scroll->setWidget(content);
    return scroll;
}

QWidget* ListViewDebugPanel::createGeneralPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(20);

    layout->addWidget(createPageTitleLabel("General Tools"));
    layout->addWidget(createPageSubtitleLabel("Common debug actions and quick entry points for desktop diagnostics."));

    m_generalGrid = new QGridLayout();
    m_generalGrid->setHorizontalSpacing(14);
    m_generalGrid->setVerticalSpacing(14);
    m_generalGrid->setColumnStretch(0, 1);
    m_generalGrid->setColumnStretch(1, 1);
    auto addGeneralCard = [this](QWidget* card, bool wide = false) {
        m_generalCards.append(card);
        m_generalWideCards.insert(card, wide);
    };

    QPushButton* restartBtn = createActionButton("Restart", false, true);
    connect(restartBtn, &QPushButton::clicked, this, []() {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), QCoreApplication::arguments(), QCoreApplication::applicationDirPath());
        QCoreApplication::quit();
    });
    addGeneralCard(new FluentCard(u8"\uE777", "Restart", "Relaunch the application", restartBtn));

    QPushButton* settingsBtn = createActionButton("Open", true, false);
    connect(settingsBtn, &QPushButton::clicked, this, [this]() {
        if (m_listView) {
            m_listView->showSettings(nullptr);
        }
    });
    addGeneralCard(new FluentCard(u8"\uE713", "Settings", "Open the main settings window", settingsBtn));

    QPushButton* deskIconBtn = createActionButton("Open", false, false);
    connect(deskIconBtn, &QPushButton::clicked, this, []() {
        QProcess::startDetached("rundll32.exe", QStringList() << "shell32.dll,Control_RunDLL" << "desk.cpl,,0");
    });
    addGeneralCard(new FluentCard(u8"\uE71D", "Desktop Icons", "Native OS icon settings", deskIconBtn));

    QPushButton* qmlTestBtn = createActionButton("Verify", false, false);
    connect(qmlTestBtn, &QPushButton::clicked, this, []() {
        auto* w = new QmlTestWindow();
        w->show(); w->raise(); w->activateWindow();
    });
    addGeneralCard(new FluentCard(u8"\uE943", "QML Test", "Verify Qt Quick availability", qmlTestBtn));

    QPushButton* refreshBtn = createActionButton("Refresh", false, false);
    connect(refreshBtn, &QPushButton::clicked, this, [this](){
        if (m_listView) m_listView->onMenuRefresh();
    });
    addGeneralCard(new FluentCard(u8"\uE72C", "Refresh Layer", "Resort icons and re-render", refreshBtn));

    m_persistToggle = new FluentToggleSwitch();
    m_persistToggle->setChecked(m_listView ? m_listView->persistenceEnabled() : true, false);
    connect(m_persistToggle, &FluentToggleSwitch::toggled, this, [this](bool checked){
        if (m_listView) m_listView->setPersistenceEnabled(checked);
    });
    addGeneralCard(new FluentCard(u8"\uE71B", "Persistence", "Save box and icon data", m_persistToggle));

    QPushButton* exportBtn = createActionButton("Export", false, false);
    connect(exportBtn, &QPushButton::clicked, this, [this]() {
        QString path;
        if (DebugInfoExporter::exportToApplicationDir(m_listView, &path))
            QMessageBox::information(this, "Export", "Exported to " + path);
    });
    addGeneralCard(new FluentCard(u8"\uE8AB", "Export Debug", "Dump state to debug.json", exportBtn), true);

    m_openGLStatusLabel = new QLabel(page);
    m_openGLStatusLabel->setWordWrap(true);
    m_openGLStatusLabel->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 13px; color: #374151; line-height: 1.5;")
        .arg(AppFonts::uiCssFamily()));
    updateOpenGLStatus();
    addGeneralCard(new FluentCard(
        u8"\uE7F8",
        "OpenGL Status",
        "Shows the saved setting and whether this launch is using the OpenGL startup path.",
        m_openGLStatusLabel), true);

    QPushButton* openRoamingBtn = createActionButton("Open Roaming", false, false);
    connect(openRoamingBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::userConfigDir()));
    });
    addGeneralCard(new FluentCard(u8"\uE838", "Roaming Configs", "Open LinDesk AppData directory", openRoamingBtn));

    QPushButton* openLocalBtn = createActionButton("Open Local", false, false);
    connect(openLocalBtn, &QPushButton::clicked, this, []() {
        QString localPath = qEnvironmentVariable("LOCALAPPDATA") + "/LinDesk";
        QDir().mkpath(localPath);
        QDesktopServices::openUrl(QUrl::fromLocalFile(localPath));
    });
    addGeneralCard(new FluentCard(u8"\uE8A7", "Local Logs", "Open LinDesk LocalAppData directory", openLocalBtn));

    layout->addLayout(m_generalGrid);
    updateAdaptiveLayout();
    layout->addStretch();
    return page;
}

LinWidget::WidgetManager* ListViewDebugPanel::linWidgetManager()
{
    if (!m_linWidgetManager) {
        m_linWidgetManager = m_listView ? m_listView->linWidgetManager() : nullptr;
        if (!m_linWidgetManager) {
            return nullptr;
        }
        QSettings settings(debugConfigPath(), QSettings::IniFormat);
        m_linWidgetManager->setEnabled(settings.value("LinWidgets/Enabled", true).toBool());
        refreshLinWidgetRunningMaps();
        connect(m_linWidgetManager, &LinWidget::WidgetManager::widgetLaunched, this, [this](const QString& instanceId) {
            if (m_linWidgetsPageStatusLabel) {
                m_linWidgetsPageStatusLabel->setText("Widget loaded into WebView: " + instanceId);
            }
            linWidgetManager()->syncWidgetWindowLayer(instanceId);
            refreshLinWidgetRunningMaps();
            updateSelectedLinWidgetItemText();
            updateLinWidgetsButtons();
        });
        connect(m_linWidgetManager, &LinWidget::WidgetManager::widgetStopped, this, [this](const QString& instanceId) {
            if (m_linWidgetsPageStatusLabel) {
                m_linWidgetsPageStatusLabel->setText("Widget unloaded from WebView: " + instanceId);
            }
            refreshLinWidgetRunningMaps();
            updateSelectedLinWidgetItemText();
            updateLinWidgetLogViewer();
            updateLinWidgetsButtons();
        });
        connect(m_linWidgetManager, &LinWidget::WidgetManager::widgetError, this, [this](const QString& instanceId, const QString& errorMessage) {
            const QString prefix = instanceId.isEmpty() ? QStringLiteral("LinWidget") : instanceId;
            if (m_linWidgetsPageStatusLabel) {
                m_linWidgetsPageStatusLabel->setText(prefix + ": " + errorMessage);
            }
            updateLinWidgetsButtons();
        });
    }
    return m_linWidgetManager;
}

void ListViewDebugPanel::reloadLinWidgetsList()
{
    if (!m_installedLinWidgetsList || !m_developmentLinWidgetsList) {
        return;
    }

    ensureDefaultLinWidgetsInstalled();
    m_installedLinWidgetsList->clear();
    m_developmentLinWidgetsList->clear();

    LinWidget::WidgetRegistryManager registry;
    QString registryError;
    registry.load(&registryError);
    registry.syncFromFolder(userLinWidgetsDir(), &registryError);
    registry.save(&registryError);
    refreshLinWidgetRunningMaps();
    if (m_linWidgetManager) {
        m_linWidgetManager->syncAllWidgetWindowLayers();
    }

    int visibleCount = 0;
    for (const LinWidget::WidgetRegistryEntry& entry : registry.entries()) {
        if (!entry.enabled || !QFileInfo::exists(entry.packagePath)) {
            continue;
        }

        auto* item = new QListWidgetItem(entry.name);
        item->setData(LinWidgetPackagePathRole, entry.packagePath);
        item->setData(LinWidgetRegistryIdRole, entry.id);
        item->setData(LinWidgetRuntimeTypeRole, entry.runtimeType);
        item->setData(LinWidgetDefaultSizeRole, entry.defaultSize);
        item->setData(LinWidgetDevelopmentRole, entry.isDevelopment);
        item->setData(LinWidgetNameRole, entry.name);
        item->setData(LinWidgetVersionRole, entry.version);
        item->setData(LinWidgetAuthorRole, entry.author);
        item->setData(LinWidgetDescriptionRole, entry.description);
        item->setData(LinWidgetAutoLoadRole, entry.autoLoad);
        LinWidget::WidgetManifest manifest;
        LinWidget::WidgetPackage::loadManifest(entry.packagePath, &manifest, nullptr);
        item->setData(LinWidgetSettingsRole, manifest.settings);
        const QString permissionsText = entry.permissionsGranted.isEmpty()
            ? QStringLiteral("none")
            : entry.permissionsGranted.join(QStringLiteral(", "));
        item->setData(LinWidgetPermissionsRole, permissionsText);
        item->setData(LinWidgetRequestedPermissionsRole, manifest.permissions);
        updateLinWidgetItemText(item);
        if (entry.isDevelopment) {
            m_developmentLinWidgetsList->addItem(item);
            m_developmentLinWidgetsList->setItemWidget(item, createLinWidgetItemWidget(item));
        } else {
            m_installedLinWidgetsList->addItem(item);
            m_installedLinWidgetsList->setItemWidget(item, createLinWidgetItemWidget(item));
        }
        ++visibleCount;
    }

    if (m_linWidgetsPageStatusLabel) {
        m_linWidgetsPageStatusLabel->setText(QStringLiteral("Found %1 registered widget package(s).").arg(visibleCount));
    }
    updateLinWidgetsButtons();
    updateLinWidgetPermissionControls();
    updateLinWidgetLogViewer();
}

void ListViewDebugPanel::updateLinWidgetsButtons()
{
    updateAllLinWidgetItemWidgets();

    const QString packagePath = selectedLinWidgetPackagePath();
    const bool hasSelection = !packagePath.isEmpty();
    const bool running = hasSelection && selectedLinWidgetIsRunning();
    const bool isDevelopment = selectedLinWidgetIsDevelopment();
    const bool runtimeEnabled = !m_linWidgetManager || m_linWidgetManager->isEnabled();
    if (m_linWidgetsOpenFolderButton) {
        m_linWidgetsOpenFolderButton->setEnabled(hasSelection);
    }
    if (m_linWidgetsOpenLogsButton) {
        m_linWidgetsOpenLogsButton->setEnabled(true);
    }
    if (m_linWidgetsPackageButton) {
        m_linWidgetsPackageButton->setEnabled(hasSelection && isDevelopment && !running);
    }
    if (m_linWidgetsResetBundledButton) {
        m_linWidgetsResetBundledButton->setEnabled(QDir(bundledLinWidgetsDir()).exists());
    }
    if (m_linWidgetsRemoveDevButton) {
        m_linWidgetsRemoveDevButton->setEnabled(hasSelection && isDevelopment && !running);
    }
    if (m_linWidgetsDeleteButton) {
        m_linWidgetsDeleteButton->setEnabled(hasSelection && !isDevelopment && !running);
    }
}

void ListViewDebugPanel::updateLinWidgetPermissionControls()
{
    QListWidget* list = activeLinWidgetList();
    QListWidgetItem* item = list ? list->currentItem() : nullptr;
    const bool hasSelection = item != nullptr;
    const bool isDevelopment = selectedLinWidgetIsDevelopment();
    const QStringList requested = selectedLinWidgetRequestedPermissions();
    const QStringList granted = selectedLinWidgetGrantedPermissions();
    auto updateCheck = [&](QCheckBox* check, const QString& permission) {
        if (!check) {
            return;
        }
        const QSignalBlocker blocker(check);
        check->setChecked(isDevelopment || granted.contains(permission));
        check->setEnabled(hasSelection && !isDevelopment);
    };
    updateCheck(m_linWidgetNetworkPermissionCheck, QStringLiteral("network"));
    updateCheck(m_linWidgetStoragePermissionCheck, QStringLiteral("storage"));
    updateCheck(m_linWidgetDesktopPermissionCheck, QStringLiteral("desktop"));
    if (!m_linWidgetPermissionsLabel) {
        return;
    }
    QStringList missing;
    for (const QString& permission : requested) {
        if (!granted.contains(permission)) {
            missing.append(permission);
        }
    }
    m_linWidgetPermissionsLabel->setText(QStringLiteral("requested: %1\ngranted: %2\nmissing: %3")
        .arg(requested.isEmpty() ? QStringLiteral("none") : requested.join(QStringLiteral(", ")))
        .arg(isDevelopment ? QStringLiteral("network, storage, desktop") : (granted.isEmpty() ? QStringLiteral("none") : granted.join(QStringLiteral(", "))))
        .arg(isDevelopment || missing.isEmpty() ? QStringLiteral("none") : missing.join(QStringLiteral(", "))));
}

void ListViewDebugPanel::updateLinWidgetLogViewer()
{
    if (!m_linWidgetsLogView || !m_linWidgetsLogWatcher) {
        return;
    }

    const QString instanceId = selectedLinWidgetInstanceIdForLog();
    const QString logPath = linWidgetLogFilePathForInstance(instanceId);
    if (m_currentLinWidgetLogPath != logPath) {
        const QStringList watchedFiles = m_linWidgetsLogWatcher->files();
        if (!watchedFiles.isEmpty()) {
            m_linWidgetsLogWatcher->removePaths(watchedFiles);
        }
        m_currentLinWidgetLogPath = logPath;
        if (!logPath.isEmpty() && QFileInfo::exists(logPath)) {
            m_linWidgetsLogWatcher->addPath(logPath);
        }
    }

    if (m_linWidgetsLogTitleLabel) {
        m_linWidgetsLogTitleLabel->setText(instanceId.isEmpty()
            ? QStringLiteral("Live Log")
            : QStringLiteral("Live Log: %1").arg(instanceId));
    }

    if (logPath.isEmpty()) {
        m_linWidgetsLogView->setPlainText(QStringLiteral("Select or start a widget to view its instance log."));
        return;
    }

    QFile file(logPath);
    if (!file.exists()) {
        const QString sharedLogPath = linWidgetLogFilePathForInstance(QStringLiteral("__shared__"));
        if (instanceId != QStringLiteral("__shared__") && QFileInfo::exists(sharedLogPath)) {
            file.setFileName(sharedLogPath);
        } else {
            m_linWidgetsLogView->setPlainText(QStringLiteral("Log file has not been created yet:\n%1").arg(logPath));
            return;
        }
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_linWidgetsLogView->setPlainText(QStringLiteral("Cannot open log file:\n%1").arg(logPath));
        return;
    }

    QByteArray content = file.readAll();
    constexpr int maxLogBytes = 200 * 1024;
    if (content.size() > maxLogBytes) {
        content = content.right(maxLogBytes);
    }
    m_linWidgetsLogView->setPlainText(QString::fromUtf8(content));
    QTextCursor cursor = m_linWidgetsLogView->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_linWidgetsLogView->setTextCursor(cursor);
}

void ListViewDebugPanel::refreshLinWidgetRunningMaps()
{
    m_linWidgetInstancesByPackage.clear();
    m_linWidgetInstancesById.clear();

    LinWidget::WidgetManager* manager = m_linWidgetManager ? m_linWidgetManager : linWidgetManager();
    if (!manager) {
        return;
    }

    for (const LinWidget::WidgetLayoutState& state : manager->layoutStates()) {
        if (!state.visible) {
            continue;
        }
        m_linWidgetInstancesByPackage.insert(linWidgetPackageKey(state.packagePath), state.instanceId);
        LinWidget::WidgetManifest manifest;
        if (LinWidget::WidgetPackage::loadManifest(state.packagePath, &manifest, nullptr)) {
            m_linWidgetInstancesById.insert(
                linWidgetRegistryKey(manifest.id, state.instanceId.startsWith(QStringLiteral("dev:"))),
                state.instanceId);
        }
        m_lastLinWidgetInstanceId = state.instanceId;
    }
}

QString ListViewDebugPanel::selectedLinWidgetRunningInstanceId() const
{
    const QString registryKey = selectedLinWidgetRegistryKey();
    if (m_linWidgetInstancesById.contains(registryKey)) {
        return m_linWidgetInstancesById.value(registryKey);
    }

    const QString packagePath = selectedLinWidgetPackagePath();
    if (m_linWidgetInstancesByPackage.contains(linWidgetPackageKey(packagePath))) {
        return m_linWidgetInstancesByPackage.value(linWidgetPackageKey(packagePath));
    }

    return QString();
}

QString ListViewDebugPanel::selectedLinWidgetPackagePath() const
{
    QListWidget* list = activeLinWidgetList();
    if (!list || !list->currentItem()) {
        return QString();
    }
    return list->currentItem()->data(LinWidgetPackagePathRole).toString();
}

QString ListViewDebugPanel::selectedLinWidgetRegistryId() const
{
    QListWidget* list = activeLinWidgetList();
    if (!list || !list->currentItem()) {
        return QString();
    }
    return list->currentItem()->data(LinWidgetRegistryIdRole).toString();
}

QString ListViewDebugPanel::selectedLinWidgetRegistryKey() const
{
    return linWidgetRegistryKey(selectedLinWidgetRegistryId(), selectedLinWidgetIsDevelopment());
}

QString ListViewDebugPanel::selectedLinWidgetInstanceIdForLog() const
{
    const QString runningInstanceId = selectedLinWidgetRunningInstanceId();
    if (!runningInstanceId.isEmpty()) {
        return runningInstanceId;
    }

    const QString registryId = selectedLinWidgetRegistryId().trimmed().toLower();
    if (registryId.isEmpty()) {
        return QString();
    }
    return QStringLiteral("%1:%2").arg(selectedLinWidgetIsDevelopment() ? QStringLiteral("dev") : QStringLiteral("installed"),
                                       registryId);
}

QString ListViewDebugPanel::linWidgetLogFilePathForInstance(const QString& instanceId) const
{
    if (instanceId.isEmpty()) {
        return QString();
    }
    return QDir(AppPaths::linWidgetLogsDir()).filePath(safeLinWidgetInstanceFileName(instanceId) + QStringLiteral(".log"));
}

bool ListViewDebugPanel::selectedLinWidgetIsDevelopment() const
{
    QListWidget* list = activeLinWidgetList();
    return list && list->currentItem() && list->currentItem()->data(LinWidgetDevelopmentRole).toBool();
}

bool ListViewDebugPanel::selectedLinWidgetIsRunning() const
{
    return !selectedLinWidgetRunningInstanceId().isEmpty();
}

void ListViewDebugPanel::updateLinWidgetItemText(QListWidgetItem* item) const
{
    if (!item) {
        return;
    }

    const QString id = item->data(LinWidgetRegistryIdRole).toString();
    const bool isDevelopment = item->data(LinWidgetDevelopmentRole).toBool();
    const QString registryKey = linWidgetRegistryKey(id, isDevelopment);
    const QString packagePath = item->data(LinWidgetPackagePathRole).toString();
    const bool running = m_linWidgetInstancesById.contains(registryKey) ||
        m_linWidgetInstancesByPackage.contains(linWidgetPackageKey(packagePath));
    const QSize defaultSize = item->data(LinWidgetDefaultSizeRole).toSize();
    const QString name = item->data(LinWidgetNameRole).toString();
    const QString version = item->data(LinWidgetVersionRole).toString();
    const QString runtimeType = item->data(LinWidgetRuntimeTypeRole).toString();
    const QString author = item->data(LinWidgetAuthorRole).toString().isEmpty()
        ? QStringLiteral("Unknown author")
        : item->data(LinWidgetAuthorRole).toString();
    const QString description = item->data(LinWidgetDescriptionRole).toString().isEmpty()
        ? QStringLiteral("No description")
        : item->data(LinWidgetDescriptionRole).toString();
    const QString permissions = item->data(LinWidgetPermissionsRole).toString().isEmpty()
        ? QStringLiteral("none")
        : item->data(LinWidgetPermissionsRole).toString();

    const QString displayText = QStringLiteral("%1\n%2")
        .arg(name)
        .arg(packagePath);
    item->setData(LinWidgetDisplayTextRole, displayText);
    item->setText(QString());
    item->setToolTip(QStringLiteral(
        "name: %1\n"
        "id: %2\n"
        "version: %3\n"
        "mode: %4\n"
        "state: %5\n"
        "autoLoad: %6\n"
        "runtime: %7\n"
        "size: %8x%9\n"
        "permissions: %10\n"
        "author: %11\n"
        "description: %12\n"
        "path: %13")
        .arg(name)
        .arg(id)
        .arg(version.isEmpty() ? QStringLiteral("unknown") : version)
        .arg(isDevelopment ? QStringLiteral("dev") : QStringLiteral("installed"))
        .arg(running ? QStringLiteral("running") : QStringLiteral("stopped"))
        .arg(item->data(LinWidgetAutoLoadRole).toBool() ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(runtimeType)
        .arg(defaultSize.width())
        .arg(defaultSize.height())
        .arg(permissions)
        .arg(author)
        .arg(description)
        .arg(packagePath));
}

void ListViewDebugPanel::updateSelectedLinWidgetItemText()
{
    if (QListWidget* list = activeLinWidgetList()) {
        QListWidgetItem* item = list->currentItem();
        updateLinWidgetItemText(item);
        if (item) {
            if (QWidget* widget = list->itemWidget(item)) {
                widget->deleteLater();
            }
            list->setItemWidget(item, createLinWidgetItemWidget(item));
        }
    }
}

void ListViewDebugPanel::updateAllLinWidgetItemWidgets()
{
    auto updateList = [this](QListWidget* list) {
        if (!list) {
            return;
        }
        for (int i = 0; i < list->count(); ++i) {
            QListWidgetItem* item = list->item(i);
            updateLinWidgetItemText(item);
            QWidget* widget = list->itemWidget(item);
            if (!widget) {
                list->setItemWidget(item, createLinWidgetItemWidget(item));
                continue;
            }
            const QString id = item->data(LinWidgetRegistryIdRole).toString();
            const bool isDevelopment = item->data(LinWidgetDevelopmentRole).toBool();
            const QString packagePath = item->data(LinWidgetPackagePathRole).toString();
            const bool running = m_linWidgetInstancesById.contains(linWidgetRegistryKey(id, isDevelopment)) ||
                m_linWidgetInstancesByPackage.contains(linWidgetPackageKey(packagePath));
            const bool runtimeEnabled = !m_linWidgetManager || m_linWidgetManager->isEnabled();
            if (QToolButton* button = widget->findChild<QToolButton*>(QStringLiteral("linWidgetItemAction"))) {
                button->setText(running ? QStringLiteral("Unload") : QStringLiteral("Load"));
                button->setEnabled(runtimeEnabled);
                button->setToolTip(item->toolTip());
            }
            if (QToolButton* reloadButton = widget->findChild<QToolButton*>(QStringLiteral("linWidgetItemReload"))) {
                reloadButton->setEnabled(runtimeEnabled && running);
                reloadButton->setToolTip(QStringLiteral("Reload this widget"));
            }
            if (QToolButton* settingsButton = widget->findChild<QToolButton*>(QStringLiteral("linWidgetItemSettings"))) {
                const QString settingsPath = item->data(LinWidgetSettingsRole).toString().trimmed();
                const bool hasSettings = !settingsPath.isEmpty() && QFileInfo::exists(QDir(packagePath).filePath(settingsPath));
                settingsButton->setVisible(!settingsPath.isEmpty());
                settingsButton->setEnabled(hasSettings);
                settingsButton->setToolTip(hasSettings ? QStringLiteral("Open widget settings") : QStringLiteral("Settings page is missing"));
            }
            widget->setToolTip(item->toolTip());
            const QList<QLabel*> labels = widget->findChildren<QLabel*>();
            for (QLabel* label : labels) {
                label->setText(item->data(LinWidgetDisplayTextRole).toString());
                label->setToolTip(item->toolTip());
            }
        }
    };
    updateList(m_installedLinWidgetsList);
    updateList(m_developmentLinWidgetsList);
}

bool ListViewDebugPanel::loadSelectedLinWidget()
{
    const QString packagePath = selectedLinWidgetPackagePath();
    const QString registryId = selectedLinWidgetRegistryId();
    const bool isDevelopment = selectedLinWidgetIsDevelopment();
    const QString registryKey = linWidgetRegistryKey(registryId, isDevelopment);
    if (packagePath.isEmpty()) {
        return false;
    }

    LinWidget::WidgetManifest manifest;
    QString errorMessage;
    if (!LinWidget::WidgetPackage::loadManifest(packagePath, &manifest, &errorMessage)) {
        if (m_linWidgetsPageStatusLabel) {
            m_linWidgetsPageStatusLabel->setText("Load to WebView failed: " + errorMessage);
        }
        return false;
    }

    QString instanceId;
    const QRect geometry(260, 220, manifest.width, manifest.height);
    LinWidget::WidgetManager* manager = linWidgetManager();
    if (!manager || !manager->launchWidgetPackage(packagePath, geometry, isDevelopment, isDevelopment, &instanceId, &errorMessage)) {
        if (m_linWidgetsPageStatusLabel) {
            m_linWidgetsPageStatusLabel->setText("Load to WebView failed: " + errorMessage);
        }
        return false;
    }

    LinWidget::WidgetRegistryManager registry;
    registry.load();
    registry.setLastRunState(registryId, isDevelopment, QStringLiteral("running"));
    registry.save();

    m_linWidgetInstancesById.insert(registryKey, instanceId);
    m_linWidgetInstancesByPackage.insert(linWidgetPackageKey(packagePath), instanceId);
    m_lastLinWidgetInstanceId = instanceId;
    if (m_linWidgetsPageStatusLabel) {
        m_linWidgetsPageStatusLabel->setText("Loaded " + manifest.name + " into WebView. Instance: " + instanceId);
    }
    updateSelectedLinWidgetItemText();
    updateLinWidgetsButtons();
    updateLinWidgetLogViewer();
    return true;
}

bool ListViewDebugPanel::unloadSelectedLinWidget()
{
    const QString packagePath = selectedLinWidgetPackagePath();
    const QString registryId = selectedLinWidgetRegistryId();
    const bool isDevelopment = selectedLinWidgetIsDevelopment();
    const QString registryKey = linWidgetRegistryKey(registryId, isDevelopment);
    const QString instanceId = m_linWidgetInstancesById.value(
        registryKey,
        m_linWidgetInstancesByPackage.value(linWidgetPackageKey(packagePath)));
    if (instanceId.isEmpty()) {
        updateLinWidgetsButtons();
        return false;
    }

    if (LinWidget::WidgetManager* manager = linWidgetManager()) {
        manager->stopWidget(instanceId);
    }
    LinWidget::WidgetRegistryManager registry;
    registry.load();
    registry.setLastRunState(registryId, isDevelopment, QStringLiteral("stopped"));
    registry.save();

    m_linWidgetInstancesById.remove(registryKey);
    m_linWidgetInstancesByPackage.remove(linWidgetPackageKey(packagePath));
    if (m_linWidgetsPageStatusLabel) {
        m_linWidgetsPageStatusLabel->setText("Unloaded widget from WebView: " + instanceId);
    }
    updateSelectedLinWidgetItemText();
    updateLinWidgetsButtons();
    updateLinWidgetLogViewer();
    return true;
}

QWidget* ListViewDebugPanel::createLinWidgetItemWidget(QListWidgetItem* item)
{
    auto* row = new QWidget();
    row->setToolTip(item ? item->toolTip() : QString());
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(10);

    auto* textLabel = new QLabel(item ? item->data(LinWidgetDisplayTextRole).toString() : QString(), row);
    textLabel->setWordWrap(true);
    textLabel->setToolTip(row->toolTip());
    textLabel->setStyleSheet(QStringLiteral(
        "font-family: %1;"
        "font-size: 12px;"
        "font-weight: 500;"
        "color: #111827;"
        "background: transparent;")
        .arg(AppFonts::uiCssFamily()));
    layout->addWidget(textLabel, 1);

    auto* actionButton = new QToolButton(row);
    actionButton->setObjectName(QStringLiteral("linWidgetItemAction"));
    actionButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    actionButton->setCursor(Qt::PointingHandCursor);
    actionButton->setToolTip(row->toolTip());
    actionButton->setStyleSheet(QStringLiteral(
        "QToolButton {"
        "  min-width: 68px;"
        "  height: 28px;"
        "  padding: 0 10px;"
        "  border-radius: 7px;"
        "  border: 1px solid rgba(15, 23, 42, 0.14);"
        "  background: #ffffff;"
        "  color: #0f6cbd;"
        "  font-family: %1;"
        "  font-size: 12px;"
        "  font-weight: 700;"
        "}"
        "QToolButton:hover { background: #eef6ff; }"
        "QToolButton:disabled { color: #9ca3af; background: #f3f4f6; }")
        .arg(AppFonts::uiCssFamily()));
    layout->addWidget(actionButton, 0, Qt::AlignVCenter);

    auto* reloadButton = new QToolButton(row);
    reloadButton->setObjectName(QStringLiteral("linWidgetItemReload"));
    reloadButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    reloadButton->setCursor(Qt::PointingHandCursor);
    reloadButton->setText(QStringLiteral("Reload"));
    reloadButton->setToolTip(QStringLiteral("Reload this widget"));
    reloadButton->setStyleSheet(actionButton->styleSheet());
    layout->addWidget(reloadButton, 0, Qt::AlignVCenter);

    auto* settingsButton = new QToolButton(row);
    settingsButton->setObjectName(QStringLiteral("linWidgetItemSettings"));
    settingsButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    settingsButton->setCursor(Qt::PointingHandCursor);
    settingsButton->setText(QStringLiteral("Settings"));
    settingsButton->setToolTip(QStringLiteral("Open widget settings"));
    settingsButton->setStyleSheet(actionButton->styleSheet());
    layout->addWidget(settingsButton, 0, Qt::AlignVCenter);

    auto selectItem = [this, item]() {
        if (!item) {
            return false;
        }
        QListWidget* list = item->listWidget();
        if (!list) {
            return false;
        }
        list->setCurrentItem(item);
        if (m_installedLinWidgetsList && list == m_installedLinWidgetsList && m_developmentLinWidgetsList) {
            m_developmentLinWidgetsList->clearSelection();
            m_developmentLinWidgetsList->setCurrentItem(nullptr);
        } else if (m_developmentLinWidgetsList && list == m_developmentLinWidgetsList && m_installedLinWidgetsList) {
            m_installedLinWidgetsList->clearSelection();
            m_installedLinWidgetsList->setCurrentItem(nullptr);
        }
        return true;
    };

    auto runAction = [this, selectItem]() {
        if (!selectItem()) {
            return;
        }
        if (selectedLinWidgetIsRunning()) {
            unloadSelectedLinWidget();
        } else {
            loadSelectedLinWidget();
        }
    };
    connect(actionButton, &QToolButton::clicked, this, runAction);
    connect(reloadButton, &QToolButton::clicked, this, [this, selectItem]() {
        if (!selectItem()) {
            return;
        }
        const QString instanceId = selectedLinWidgetRunningInstanceId();
        if (instanceId.isEmpty()) {
            updateLinWidgetsButtons();
            return;
        }
        LinWidget::WidgetManager* manager = linWidgetManager();
        if (!manager || !manager->restartWidget(instanceId)) {
            if (m_linWidgetsPageStatusLabel) {
                m_linWidgetsPageStatusLabel->setText("Reload Widget failed: " + instanceId);
            }
            return;
        }
        if (m_linWidgetsPageStatusLabel) {
            m_linWidgetsPageStatusLabel->setText("Reloaded widget iframe: " + instanceId);
        }
        updateLinWidgetLogViewer();
        updateLinWidgetsButtons();
    });
    connect(settingsButton, &QToolButton::clicked, this, [this, item, selectItem]() {
        if (!item || !selectItem()) {
            return;
        }
        const QString packagePath = item->data(LinWidgetPackagePathRole).toString();
        LinWidget::WidgetManifest manifest;
        QString errorMessage;
        if (!LinWidget::WidgetPackage::loadManifest(packagePath, &manifest, &errorMessage)) {
            if (m_linWidgetsPageStatusLabel) {
                m_linWidgetsPageStatusLabel->setText("Open widget settings failed: " + errorMessage);
            }
            return;
        }
        auto* dialog = new LinWidget::WidgetSettingsDialog(
            packagePath,
            manifest,
            selectedLinWidgetGrantedPermissions(),
            item->data(LinWidgetDevelopmentRole).toBool(),
            selectedLinWidgetInstanceIdForLog(),
            this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->initialize(&errorMessage);
        connect(dialog, &QDialog::finished, this, [this]() {
            const QString instanceId = selectedLinWidgetRunningInstanceId();
            if (!instanceId.isEmpty()) {
                if (LinWidget::WidgetManager* manager = linWidgetManager()) {
                    manager->restartWidget(instanceId);
                }
                updateLinWidgetLogViewer();
                updateLinWidgetsButtons();
            }
        });
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
        if (!errorMessage.isEmpty() && m_linWidgetsPageStatusLabel) {
            m_linWidgetsPageStatusLabel->setText(errorMessage);
        }
    });

    const QString id = item ? item->data(LinWidgetRegistryIdRole).toString() : QString();
    const bool isDevelopment = item && item->data(LinWidgetDevelopmentRole).toBool();
    const QString packagePath = item ? item->data(LinWidgetPackagePathRole).toString() : QString();
    const bool running = m_linWidgetInstancesById.contains(linWidgetRegistryKey(id, isDevelopment)) ||
        m_linWidgetInstancesByPackage.contains(linWidgetPackageKey(packagePath));
    const bool runtimeEnabled = !m_linWidgetManager || m_linWidgetManager->isEnabled();
    actionButton->setText(running ? QStringLiteral("Unload") : QStringLiteral("Load"));
    actionButton->setEnabled(runtimeEnabled);
    reloadButton->setEnabled(runtimeEnabled && running);
    const QString settingsPath = item ? item->data(LinWidgetSettingsRole).toString().trimmed() : QString();
    const bool hasSettings = item && !settingsPath.isEmpty() && QFileInfo::exists(QDir(packagePath).filePath(settingsPath));
    settingsButton->setVisible(!settingsPath.isEmpty());
    settingsButton->setEnabled(hasSettings);
    settingsButton->setToolTip(hasSettings ? QStringLiteral("Open widget settings") : QStringLiteral("Settings page is missing"));

    return row;
}

QListWidget* ListViewDebugPanel::activeLinWidgetList() const
{
    if (m_developmentLinWidgetsList && m_developmentLinWidgetsList->currentItem()) {
        return m_developmentLinWidgetsList;
    }
    if (m_installedLinWidgetsList && m_installedLinWidgetsList->currentItem()) {
        return m_installedLinWidgetsList;
    }
    return nullptr;
}

QString ListViewDebugPanel::linWidgetPackageKey(const QString& packagePath) const
{
    const QFileInfo info(packagePath);
    const QString path = info.exists() ? info.absoluteFilePath() : packagePath;
    return QDir::cleanPath(path).toLower();
}

QString ListViewDebugPanel::linWidgetRegistryKey(const QString& id, bool isDevelopment) const
{
    return QStringLiteral("%1:%2").arg(isDevelopment ? QStringLiteral("dev") : QStringLiteral("installed"),
                                       id.trimmed().toLower());
}

QStringList ListViewDebugPanel::selectedLinWidgetRequestedPermissions() const
{
    QListWidget* list = activeLinWidgetList();
    if (!list || !list->currentItem()) {
        return {};
    }
    QStringList permissions = list->currentItem()->data(LinWidgetRequestedPermissionsRole).toStringList();
    for (QString& permission : permissions) {
        permission = permission.trimmed().toLower();
    }
    permissions.removeDuplicates();
    return permissions;
}

QStringList ListViewDebugPanel::selectedLinWidgetGrantedPermissions() const
{
    QListWidget* list = activeLinWidgetList();
    if (!list || !list->currentItem()) {
        return {};
    }
    const QString text = list->currentItem()->data(LinWidgetPermissionsRole).toString();
    if (text.isEmpty() || text == QStringLiteral("none")) {
        return {};
    }
    QStringList permissions = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (QString& permission : permissions) {
        permission = permission.trimmed().toLower();
    }
    permissions.removeDuplicates();
    return permissions;
}

void ListViewDebugPanel::setSelectedLinWidgetGrantedPermission(const QString& permission, bool granted)
{
    QListWidget* list = activeLinWidgetList();
    QListWidgetItem* item = list ? list->currentItem() : nullptr;
    if (!item || selectedLinWidgetIsDevelopment()) {
        updateLinWidgetPermissionControls();
        return;
    }

    QStringList permissions = selectedLinWidgetGrantedPermissions();
    const QString normalized = permission.trimmed().toLower();
    if (granted && !permissions.contains(normalized)) {
        permissions.append(normalized);
    } else if (!granted) {
        permissions.removeAll(normalized);
    }
    permissions.sort();

    LinWidget::WidgetRegistryManager registry;
    registry.load();
    registry.setPermissionsGranted(selectedLinWidgetRegistryId(), selectedLinWidgetIsDevelopment(), permissions);
    registry.save();

    item->setData(LinWidgetPermissionsRole, permissions.isEmpty() ? QStringLiteral("none") : permissions.join(QStringLiteral(", ")));
    updateSelectedLinWidgetItemText();
    updateLinWidgetPermissionControls();
    const QString instanceId = selectedLinWidgetRunningInstanceId();
    if (!instanceId.isEmpty()) {
        if (LinWidget::WidgetManager* manager = linWidgetManager()) {
            manager->restartWidget(instanceId);
        }
    }
}

void ListViewDebugPanel::updateLinWidgetLayerDiagnostics()
{
    if (!m_linWidgetLayerDiagnosticsLabel) {
        return;
    }
    LinWidget::WidgetManager* manager = linWidgetManager();
    if (!manager) {
        m_linWidgetLayerDiagnosticsLabel->setText(QStringLiteral("LinWidget manager is not available."));
        return;
    }
    const LinWidget::WidgetManager::LayerDiagnostics diagnostics = manager->verifyWidgetLayerAndClickThrough();
    const bool pass = diagnostics.hasListViewWindow &&
        diagnostics.hasHostWindow &&
        diagnostics.syncSucceeded &&
        diagnostics.hostAboveListView &&
        diagnostics.hostRectMatchesListView &&
        diagnostics.emptyPointPassesThrough &&
        diagnostics.widgetPointHitsHost &&
        diagnostics.regionRectCount == diagnostics.visibleWidgetCount;
    m_linWidgetLayerDiagnosticsLabel->setText(QStringLiteral("%1\n\n%2")
        .arg(pass ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
             manager->layerDiagnosticsText(diagnostics)));
    if (m_linWidgetsPageStatusLabel) {
        m_linWidgetsPageStatusLabel->setText(pass
            ? QStringLiteral("Layer and click through check passed.")
            : QStringLiteral("Layer and click through check found a mismatch. See diagnostics above."));
    }
}

QWidget* ListViewDebugPanel::createLinWidgetsPage()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(18);

    layout->addWidget(createPageTitleLabel("LinWidgets"));
    layout->addWidget(createPageSubtitleLabel("Manage installed widgets and development projects. Development projects run directly from their source folders with hot reload."));

    QSettings settings(debugConfigPath(), QSettings::IniFormat);
    auto* compactSwitchPanel = new QWidget(page);
    compactSwitchPanel->setObjectName(QStringLiteral("linWidgetCompactSwitchPanel"));
    compactSwitchPanel->setAttribute(Qt::WA_StyledBackground, true);
    compactSwitchPanel->setStyleSheet(QStringLiteral(
        "QWidget#linWidgetCompactSwitchPanel {"
        "  background: #ffffff;"
        "  border: 1px solid rgba(15, 23, 42, 0.10);"
        "  border-radius: 10px;"
        "}"));
    m_linWidgetsCompactSwitchGrid = new QGridLayout(compactSwitchPanel);
    m_linWidgetsCompactSwitchGrid->setContentsMargins(12, 8, 12, 8);
    m_linWidgetsCompactSwitchGrid->setHorizontalSpacing(16);
    m_linWidgetsCompactSwitchGrid->setVerticalSpacing(8);
    auto addCompactSwitch = [this, compactSwitchPanel](const QString& name,
                                                       const QString& tooltip,
                                                       FluentToggleSwitch* toggle) {
        auto* item = new QWidget(compactSwitchPanel);
        item->setToolTip(tooltip);
        item->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto* itemLayout = new QHBoxLayout(item);
        itemLayout->setContentsMargins(0, 0, 0, 0);
        itemLayout->setSpacing(6);

        auto* label = new QLabel(name, item);
        label->setToolTip(tooltip);
        label->setStyleSheet(QStringLiteral(
            "font-family: %1; font-size: 12px; font-weight: 600; color: #374151;")
            .arg(AppFonts::uiCssFamily()));

        toggle->setParent(item);
        toggle->setToolTip(tooltip);
        itemLayout->addWidget(label, 0, Qt::AlignVCenter);
        itemLayout->addWidget(toggle, 0, Qt::AlignVCenter);
        itemLayout->addStretch();
        m_linWidgetsCompactSwitchItems.append(item);
    };

    m_linWidgetEnabledToggle = new FluentToggleSwitch(page);
    m_linWidgetEnabledToggle->setChecked(settings.value("LinWidgets/Enabled", true).toBool(), false);
    connect(m_linWidgetEnabledToggle, &FluentToggleSwitch::toggled, this, [this](bool checked) {
        QSettings s(debugConfigPath(), QSettings::IniFormat);
        s.setValue("LinWidgets/Enabled", checked);
        s.sync();

        if (LinWidget::WidgetManager* manager = linWidgetManager()) {
            manager->setEnabled(checked);
            if (checked) {
                QString errorMessage;
                manager->restoreFromConfig(QString(), &errorMessage);
                if (m_linWidgetsPageStatusLabel) {
                    m_linWidgetsPageStatusLabel->setText(errorMessage.isEmpty()
                        ? QStringLiteral("LinWidgets runtime enabled.")
                        : QStringLiteral("LinWidgets runtime enabled, restore warning: %1").arg(errorMessage));
                }
            } else if (m_linWidgetsPageStatusLabel) {
                m_linWidgetsPageStatusLabel->setText(QStringLiteral("LinWidgets runtime disabled. WidgetHost and IPC resources released."));
            }
        }

        refreshLinWidgetRunningMaps();
        updateSelectedLinWidgetItemText();
        updateLinWidgetsButtons();
        updateLinWidgetLogViewer();
    });
    addCompactSwitch(QStringLiteral("Runtime"),
                     QStringLiteral("Master switch for the shared WebView runtime. Turning it off unloads widgets and stops WidgetHost."),
                     m_linWidgetEnabledToggle);

    m_linWidgetBoundsToggle = new FluentToggleSwitch(page);
    m_linWidgetBoundsToggle->setChecked(settings.value("LinWidgets/ShowWebViewBounds", false).toBool(), false);
    connect(m_linWidgetBoundsToggle, &FluentToggleSwitch::toggled, this, [this](bool checked) {
        QSettings s(debugConfigPath(), QSettings::IniFormat);
        s.setValue("LinWidgets/ShowWebViewBounds", checked);
        s.sync();
        if (LinWidget::WidgetManager* manager = linWidgetManager()) {
            manager->setShowWebViewBounds(checked);
        }
        if (m_linWidgetsPageStatusLabel) {
            m_linWidgetsPageStatusLabel->setText(checked
                ? QStringLiteral("LinWidget WebView bounds visualization enabled.")
                : QStringLiteral("LinWidget WebView bounds visualization disabled."));
        }
    });
    addCompactSwitch(QStringLiteral("WebView Background"),
                     QStringLiteral("Show the full shared WebView layer for debugging. Empty desktop clicks pass through when this is off."),
                     m_linWidgetBoundsToggle);

    m_linWidgetBlockedCellsToggle = new FluentToggleSwitch(page);
    m_linWidgetBlockedCellsToggle->setChecked(settings.value("LinWidgets/ShowBlockedCells", false).toBool(), false);
    connect(m_linWidgetBlockedCellsToggle, &FluentToggleSwitch::toggled, this, [this](bool checked) {
        QSettings s(debugConfigPath(), QSettings::IniFormat);
        s.setValue("LinWidgets/ShowBlockedCells", checked);
        s.sync();
        if (m_listView) {
            m_listView->setShowLinWidgetBlockedCells(checked);
        }
        if (m_linWidgetsPageStatusLabel) {
            m_linWidgetsPageStatusLabel->setText(checked
                ? QStringLiteral("LinWidget blocked grid cells overlay enabled.")
                : QStringLiteral("LinWidget blocked grid cells overlay disabled."));
        }
    });
    addCompactSwitch(QStringLiteral("Occupied Cells"),
                     QStringLiteral("Draw the desktop grid cells reserved by visible WebView widgets."),
                     m_linWidgetBlockedCellsToggle);
    layout->addWidget(compactSwitchPanel);
    updateAdaptiveLayout();

    auto* layerCheckPanel = new QWidget(page);
    layerCheckPanel->setObjectName(QStringLiteral("linWidgetLayerCheckPanel"));
    layerCheckPanel->setAttribute(Qt::WA_StyledBackground, true);
    layerCheckPanel->setStyleSheet(QStringLiteral(
        "QWidget#linWidgetLayerCheckPanel {"
        "  background: #ffffff;"
        "  border: 1px solid rgba(15, 23, 42, 0.10);"
        "  border-radius: 10px;"
        "}"));
    auto* layerCheckLayout = new QVBoxLayout(layerCheckPanel);
    layerCheckLayout->setContentsMargins(14, 12, 14, 12);
    layerCheckLayout->setSpacing(10);
    auto* layerCheckTitle = new QLabel(QStringLiteral("Layer And Click Through Check"), layerCheckPanel);
    layerCheckTitle->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 13px; font-weight: 700; color: #111827;")
        .arg(AppFonts::uiCssFamily()));
    layerCheckLayout->addWidget(layerCheckTitle);
    m_linWidgetLayerDiagnosticsLabel = new QLabel(layerCheckPanel);
    m_linWidgetLayerDiagnosticsLabel->setWordWrap(true);
    m_linWidgetLayerDiagnosticsLabel->setText(QStringLiteral("Run the check after loading a widget."));
    m_linWidgetLayerDiagnosticsLabel->setStyleSheet(QStringLiteral(
        "font-family: Consolas, 'Cascadia Mono', monospace; font-size: 12px; color: #334155;"));
    layerCheckLayout->addWidget(m_linWidgetLayerDiagnosticsLabel);
    auto* layerButtonsRow = new QHBoxLayout();
    layerButtonsRow->setContentsMargins(0, 0, 0, 0);
    layerButtonsRow->setSpacing(8);
    auto* runLayerCheckButton = createActionButton(QStringLiteral("Run Check"), true, false);
    auto* resyncLayerButton = createActionButton(QStringLiteral("Resync Layer"), false, false);
    auto* toggleBoundsButton = createActionButton(QStringLiteral("Toggle Bounds Visualization"), false, false);
    layerButtonsRow->addWidget(runLayerCheckButton);
    layerButtonsRow->addWidget(resyncLayerButton);
    layerButtonsRow->addWidget(toggleBoundsButton);
    layerButtonsRow->addStretch();
    layerCheckLayout->addLayout(layerButtonsRow);
    layout->addWidget(layerCheckPanel);

    const QString listStyle = QStringLiteral(
        "QListWidget {"
        "  background: #ffffff;"
        "  border: 1px solid rgba(15, 23, 42, 0.10);"
        "  border-radius: 10px;"
        "  padding: 8px;"
        "  font-family: %1;"
        "  color: #111827;"
        "}"
        "QListWidget::item {"
        "  min-height: 74px;"
        "  padding: 4px;"
        "  border-radius: 8px;"
        "}"
        "QListWidget::item:hover { background: #f3f7fb; }"
        "QListWidget::item:selected { background: #dbeafe; color: #0f172a; }")
        .arg(AppFonts::uiCssFamily());

    m_linWidgetsContentGrid = new QGridLayout();
    m_linWidgetsContentGrid->setContentsMargins(0, 0, 0, 0);
    m_linWidgetsContentGrid->setHorizontalSpacing(16);
    m_linWidgetsContentGrid->setVerticalSpacing(16);

    m_linWidgetsListsPanel = new QWidget(page);
    auto* listsLayout = new QVBoxLayout(m_linWidgetsListsPanel);
    listsLayout->setContentsMargins(0, 0, 0, 0);
    listsLayout->setSpacing(12);

    auto* installedHeader = new QWidget(m_linWidgetsListsPanel);
    auto* installedHeaderLayout = new QHBoxLayout(installedHeader);
    installedHeaderLayout->setContentsMargins(0, 0, 0, 0);
    installedHeaderLayout->setSpacing(8);
    installedHeaderLayout->addWidget(createPageSubtitleLabel("Installed Widgets"), 1);
    auto* installedRefreshButton = createActionButton("Refresh", false, false);
    installedRefreshButton->setFixedWidth(82);
    installedHeaderLayout->addWidget(installedRefreshButton, 0, Qt::AlignRight);
    listsLayout->addWidget(installedHeader);
    m_installedLinWidgetsList = new QListWidget(page);
    m_installedLinWidgetsList->setMinimumHeight(220);
    m_installedLinWidgetsList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_installedLinWidgetsList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_installedLinWidgetsList->setMouseTracking(true);
    m_installedLinWidgetsList->setStyleSheet(listStyle);
    listsLayout->addWidget(m_installedLinWidgetsList, 1);

    auto* devHeader = new QWidget(m_linWidgetsListsPanel);
    auto* devHeaderLayout = new QHBoxLayout(devHeader);
    devHeaderLayout->setContentsMargins(0, 0, 0, 0);
    devHeaderLayout->setSpacing(8);
    devHeaderLayout->addWidget(createPageSubtitleLabel("Development Projects"), 1);
    auto* devRefreshButton = createActionButton("Refresh", false, false);
    devRefreshButton->setFixedWidth(82);
    devHeaderLayout->addWidget(devRefreshButton, 0, Qt::AlignRight);
    listsLayout->addWidget(devHeader);
    m_developmentLinWidgetsList = new QListWidget(page);
    m_developmentLinWidgetsList->setMinimumHeight(220);
    m_developmentLinWidgetsList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_developmentLinWidgetsList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_developmentLinWidgetsList->setMouseTracking(true);
    m_developmentLinWidgetsList->setStyleSheet(listStyle);
    listsLayout->addWidget(m_developmentLinWidgetsList, 1);

    m_linWidgetsLogPanel = new QWidget(page);
    auto* logLayout = new QVBoxLayout(m_linWidgetsLogPanel);
    logLayout->setContentsMargins(0, 0, 0, 0);
    logLayout->setSpacing(10);
    m_linWidgetsLogTitleLabel = createPageSubtitleLabel("Live Log");
    logLayout->addWidget(m_linWidgetsLogTitleLabel);
    m_linWidgetsLogView = new QPlainTextEdit(page);
    m_linWidgetsLogView->setReadOnly(true);
    m_linWidgetsLogView->setMinimumWidth(260);
    m_linWidgetsLogView->setMinimumHeight(320);
    m_linWidgetsLogView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_linWidgetsLogView->setStyleSheet(QStringLiteral(
        "QPlainTextEdit {"
        "  background: #0f172a;"
        "  color: #dbeafe;"
        "  border: 1px solid rgba(15, 23, 42, 0.16);"
        "  border-radius: 10px;"
        "  padding: 10px;"
        "  font-family: Consolas, 'Cascadia Mono', monospace;"
        "  font-size: 12px;"
        "}"));
    logLayout->addWidget(m_linWidgetsLogView, 1);

    m_linWidgetPermissionsLabel = new QLabel(page);
    m_linWidgetPermissionsLabel->setWordWrap(true);
    m_linWidgetPermissionsLabel->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 12px; color: #374151;")
        .arg(AppFonts::uiCssFamily()));
    logLayout->addWidget(m_linWidgetPermissionsLabel);

    auto* permissionRow = new QHBoxLayout();
    permissionRow->setContentsMargins(0, 0, 0, 0);
    permissionRow->setSpacing(10);
    m_linWidgetNetworkPermissionCheck = new QCheckBox(QStringLiteral("network"), page);
    m_linWidgetStoragePermissionCheck = new QCheckBox(QStringLiteral("storage"), page);
    m_linWidgetDesktopPermissionCheck = new QCheckBox(QStringLiteral("desktop"), page);
    permissionRow->addWidget(m_linWidgetNetworkPermissionCheck);
    permissionRow->addWidget(m_linWidgetStoragePermissionCheck);
    permissionRow->addWidget(m_linWidgetDesktopPermissionCheck);
    permissionRow->addStretch();
    logLayout->addLayout(permissionRow);

    layout->addLayout(m_linWidgetsContentGrid, 1);

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
    connect(m_linWidgetsLogReloadTimer, &QTimer::timeout, this, &ListViewDebugPanel::updateLinWidgetLogViewer);
    connect(m_linWidgetNetworkPermissionCheck, &QCheckBox::toggled, this, [this](bool checked) {
        setSelectedLinWidgetGrantedPermission(QStringLiteral("network"), checked);
    });
    connect(m_linWidgetStoragePermissionCheck, &QCheckBox::toggled, this, [this](bool checked) {
        setSelectedLinWidgetGrantedPermission(QStringLiteral("storage"), checked);
    });
    connect(m_linWidgetDesktopPermissionCheck, &QCheckBox::toggled, this, [this](bool checked) {
        setSelectedLinWidgetGrantedPermission(QStringLiteral("desktop"), checked);
    });
    connect(runLayerCheckButton, &QPushButton::clicked, this, &ListViewDebugPanel::updateLinWidgetLayerDiagnostics);
    connect(resyncLayerButton, &QPushButton::clicked, this, [this]() {
        if (LinWidget::WidgetManager* manager = linWidgetManager()) {
            manager->syncAllWidgetWindowLayers();
        }
        updateLinWidgetLayerDiagnostics();
    });
    connect(toggleBoundsButton, &QPushButton::clicked, this, [this]() {
        if (!m_linWidgetBoundsToggle) {
            return;
        }
        m_linWidgetBoundsToggle->setChecked(!m_linWidgetBoundsToggle->checked(), true);
        updateLinWidgetLayerDiagnostics();
    });

    m_linWidgetsActionGroupsGrid = new QGridLayout();
    m_linWidgetsActionGroupsGrid->setContentsMargins(0, 0, 0, 0);
    m_linWidgetsActionGroupsGrid->setHorizontalSpacing(12);
    m_linWidgetsActionGroupsGrid->setVerticalSpacing(12);

    auto* refreshButton = createActionButton("Refresh List", false, false);
    m_linWidgetsAddDevButton = createActionButton("Add Dev Project", false, false);
    m_linWidgetsOpenFolderButton = createActionButton("Open Folder", false, false);
    m_linWidgetsOpenLogsButton = createActionButton("Open Logs", false, false);
    m_linWidgetsPackageButton = createActionButton("Package to Roaming", false, false);
    m_linWidgetsResetBundledButton = createActionButton("Reset Bundled", false, true);
    m_linWidgetsRemoveDevButton = createActionButton("Remove Dev", false, true);
    m_linWidgetsDeleteButton = createActionButton("Delete Widget", false, true);

    auto createActionGroup = [this, page](const QString& title, const QList<QPushButton*>& buttons) {
        auto* group = new QWidget(page);
        group->setObjectName(QStringLiteral("linWidgetActionGroup"));
        group->setAttribute(Qt::WA_StyledBackground, true);
        group->setStyleSheet(QStringLiteral(
            "QWidget#linWidgetActionGroup {"
            "  background: #ffffff;"
            "  border: 1px solid rgba(15, 23, 42, 0.10);"
            "  border-radius: 10px;"
            "}"));

        auto* groupLayout = new QVBoxLayout(group);
        groupLayout->setContentsMargins(12, 10, 12, 12);
        groupLayout->setSpacing(8);

        auto* titleLabel = new QLabel(title, group);
        titleLabel->setStyleSheet(QStringLiteral(
            "font-family: %1; font-size: 12px; font-weight: 700; color: #4b5563;")
            .arg(AppFonts::uiCssFamily()));
        groupLayout->addWidget(titleLabel);

        auto* buttonGrid = new QGridLayout();
        buttonGrid->setContentsMargins(0, 0, 0, 0);
        buttonGrid->setHorizontalSpacing(8);
        buttonGrid->setVerticalSpacing(8);
        for (int i = 0; i < buttons.size(); ++i) {
            QPushButton* button = buttons.at(i);
            button->setParent(group);
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            buttonGrid->addWidget(button, i / 2, i % 2);
        }
        groupLayout->addLayout(buttonGrid);
        groupLayout->addStretch();
        group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        return group;
    };

    m_linWidgetActionGroups = {
        createActionGroup(QStringLiteral("List"), { refreshButton, m_linWidgetsAddDevButton }),
        createActionGroup(QStringLiteral("Files"), { m_linWidgetsOpenFolderButton, m_linWidgetsOpenLogsButton, m_linWidgetsPackageButton }),
        createActionGroup(QStringLiteral("Remove / Reset"), { m_linWidgetsRemoveDevButton, m_linWidgetsResetBundledButton, m_linWidgetsDeleteButton })
    };
    layout->addLayout(m_linWidgetsActionGroupsGrid);
    updateAdaptiveLayout();

    m_linWidgetsPageStatusLabel = new QLabel(page);
    m_linWidgetsPageStatusLabel->setWordWrap(true);
    m_linWidgetsPageStatusLabel->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 13px; color: #374151;")
        .arg(AppFonts::uiCssFamily()));
    layout->addWidget(m_linWidgetsPageStatusLabel);

    connect(installedRefreshButton, &QPushButton::clicked, this, &ListViewDebugPanel::reloadLinWidgetsList);
    connect(devRefreshButton, &QPushButton::clicked, this, &ListViewDebugPanel::reloadLinWidgetsList);
    connect(refreshButton, &QPushButton::clicked, this, &ListViewDebugPanel::reloadLinWidgetsList);
    connect(m_installedLinWidgetsList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current) {
        if (current && m_developmentLinWidgetsList) {
            m_developmentLinWidgetsList->clearSelection();
            m_developmentLinWidgetsList->setCurrentItem(nullptr);
        }
        updateLinWidgetsButtons();
        updateLinWidgetPermissionControls();
        updateLinWidgetLogViewer();
    });
    connect(m_developmentLinWidgetsList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current) {
        if (current && m_installedLinWidgetsList) {
            m_installedLinWidgetsList->clearSelection();
            m_installedLinWidgetsList->setCurrentItem(nullptr);
        }
        updateLinWidgetsButtons();
        updateLinWidgetPermissionControls();
        updateLinWidgetLogViewer();
    });
    connect(m_linWidgetsAddDevButton, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, "Select LinWidget development project");
        if (dir.isEmpty()) {
            return;
        }
        LinWidget::WidgetRegistryManager registry;
        QString errorMessage;
        registry.load();
        if (!registry.addDevelopmentProject(dir, &errorMessage)) {
            m_linWidgetsPageStatusLabel->setText("Add dev project failed: " + errorMessage);
            return;
        }
        registry.save();
        m_linWidgetsPageStatusLabel->setText("Development project added.");
        reloadLinWidgetsList();
    });
    connect(m_linWidgetsOpenFolderButton, &QPushButton::clicked, this, [this]() {
        const QString packagePath = selectedLinWidgetPackagePath();
        if (!packagePath.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(packagePath));
        }
    });
    connect(m_linWidgetsOpenLogsButton, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::linWidgetLogsDir()));
    });
    connect(m_linWidgetsPackageButton, &QPushButton::clicked, this, [this]() {
        LinWidget::WidgetRegistryManager registry;
        QString installedPath;
        QString errorMessage;
        registry.load();
        if (!registry.installDevelopmentProject(selectedLinWidgetRegistryId(), &installedPath, &errorMessage)) {
            m_linWidgetsPageStatusLabel->setText("Package to Roaming failed: " + errorMessage);
            return;
        }
        registry.save();
        m_linWidgetsPageStatusLabel->setText("Packaged widget to Roaming: " + installedPath);
        reloadLinWidgetsList();
    });
    connect(m_linWidgetsResetBundledButton, &QPushButton::clicked, this, [this]() {
        QString errorMessage;
        const QStringList resetFolders = resetAllBundledLinWidgets(&errorMessage);
        if (resetFolders.isEmpty() && !errorMessage.isEmpty()) {
            m_linWidgetsPageStatusLabel->setText("Reset bundled widget failed: " + errorMessage);
            return;
        }
        LinWidget::WidgetRegistryManager registry;
        registry.load();
        for (const QString& folderName : resetFolders) {
            registry.upsertFromPackage(QDir(userLinWidgetsDir()).filePath(folderName), &errorMessage);
        }
        registry.save();
        m_linWidgetsPageStatusLabel->setText(QStringLiteral("Reset %1 bundled widget(s) from runtime resources.").arg(resetFolders.size()));
        reloadLinWidgetsList();
    });
    connect(m_linWidgetsRemoveDevButton, &QPushButton::clicked, this, [this]() {
        LinWidget::WidgetRegistryManager registry;
        registry.load();
        registry.removeDevelopmentProject(selectedLinWidgetRegistryId());
        registry.save();
        m_linWidgetsPageStatusLabel->setText("Removed development project from registry.");
        reloadLinWidgetsList();
    });
    connect(m_linWidgetsDeleteButton, &QPushButton::clicked, this, [this]() {
        const QString packagePath = selectedLinWidgetPackagePath();
        const QString registryId = selectedLinWidgetRegistryId();
        const bool isDevelopment = selectedLinWidgetIsDevelopment();
        const QString registryKey = linWidgetRegistryKey(registryId, isDevelopment);
        if (packagePath.isEmpty() || registryId.isEmpty()) {
            return;
        }

        const QString modeText = isDevelopment ? QStringLiteral("development project") : QStringLiteral("installed widget");
        const int confirm = QMessageBox::warning(
            this,
            QStringLiteral("Delete LinWidget"),
            QStringLiteral("Delete this %1?\n\n%2\n\nThis removes the registry record, saved layout state, and the widget folder.")
                .arg(modeText, packagePath),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (confirm != QMessageBox::Yes) {
            return;
        }

        const QString runningInstanceId = m_linWidgetInstancesById.value(
            registryKey,
            m_linWidgetInstancesByPackage.value(linWidgetPackageKey(packagePath)));
        if (!runningInstanceId.isEmpty()) {
            m_linWidgetsPageStatusLabel->setText("Unload the widget from WebView before deleting it.");
            updateLinWidgetsButtons();
            return;
        }
        removeLinWidgetLayoutState(registryKey, packagePath);

        LinWidget::WidgetRegistryManager registry;
        QString errorMessage;
        registry.load();
        if (isDevelopment) {
            registry.removeDevelopmentProject(registryId);
        } else {
            registry.removeById(registryId);
        }
        registry.save();

        if (!removeLinWidgetDirectory(packagePath, &errorMessage)) {
            m_linWidgetsPageStatusLabel->setText("Deleted registry record, but folder delete failed: " + errorMessage);
            reloadLinWidgetsList();
            return;
        }

        m_linWidgetsPageStatusLabel->setText("Deleted widget: " + registryId + ". Folder: " + packagePath);
        reloadLinWidgetsList();
    });
    reloadLinWidgetsList();
    updateLinWidgetLogViewer();
    updateLinWidgetLayerDiagnostics();
    return page;
}

QWidget* ListViewDebugPanel::createLayoutPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(24);

    layout->addWidget(createPageTitleLabel("Layout & Behavior"));
    layout->addWidget(createPageSubtitleLabel("Adjust desktop spacing and desktop embedding behavior for visual debugging."));

    LinDesktop* desk = m_listView ? qobject_cast<LinDesktop*>(m_listView->parentWidget()) : nullptr;
    m_embedToggle = new FluentToggleSwitch();
    if (desk) m_embedToggle->setChecked(desk->isEmbedded(), false);
    connect(m_embedToggle, &FluentToggleSwitch::toggled, this, [desk](bool checked){
        if (desk) { if (checked) desk->embedToDesktop(); else desk->unembedFromDesktop(); }
    });
    layout->addWidget(new FluentCard(u8"\uE7F4", "Embedded Desktop Mode", "Attach window to WorkerW layer", m_embedToggle));

    layout->addWidget(createSliderSettingRow("Row Spacing", 0, 40, m_rowSlider, m_rowValueLabel));

    connect(m_rowSlider, &QSlider::valueChanged, this, [this](int v) {
        updateLabels();
        if (m_listView) m_listView->setDebugGridSpacing(v, m_colSlider->value());
    });

    layout->addWidget(createSliderSettingRow("Column Spacing", 0, 30, m_colSlider, m_colValueLabel));

    connect(m_colSlider, &QSlider::valueChanged, this, [this](int v) {
        updateLabels();
        if (m_listView) m_listView->setDebugGridSpacing(m_rowSlider->value(), v);
    });

    layout->addStretch();
    return page;
}

QWidget* ListViewDebugPanel::createTextDiagnosticsPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(16);

    layout->addWidget(createPageTitleLabel("Text Rendering Diagnostics"));
    layout->addWidget(createPageSubtitleLabel("Preview the desktop text renderer and tune glow and shadow parameters."));

    m_textPreview = new SoftDesktopTextPreview();
    QFont previewFont("Segoe UI");
    previewFont.setPixelSize(36);
    m_textPreview->setPreviewFont(previewFont);
    m_textPreview->setText("New Text Document.txt");
    m_textPreview->setMinimumHeight(100);
    layout->addWidget(m_textPreview);

    auto addSlider = [&](const QString& label, int min, int max, QSlider*& slider, QLabel*& val) {
        layout->addWidget(createSliderSettingRow(label, min, max, slider, val));
        connect(slider, &QSlider::valueChanged, this, [this](int){ 
            updateLabels(); 
            applyTextPreviewOptions(); 
        });
    };

    addSlider("Large Glow Radius", 0, 16, m_largeGlowSlider, m_largeGlowValueLabel);
    addSlider("Tight Glow Radius", 0, 12, m_tightGlowSlider, m_tightGlowValueLabel);
    addSlider("Glow Alpha", 0, 255, m_glowAlphaSlider, m_glowAlphaValueLabel);
    addSlider("Inner Shadow Alpha", 0, 255, m_innerAlphaSlider, m_innerAlphaValueLabel);
    addSlider("Glow Offset X", -8, 8, m_glowOffsetXSlider, m_glowOffsetXValueLabel);
    addSlider("Glow Offset Y", -8, 8, m_glowOffsetYSlider, m_glowOffsetYValueLabel);
    addSlider("Inner Offset X", -8, 8, m_innerOffsetXSlider, m_innerOffsetXValueLabel);
    addSlider("Inner Offset Y", -8, 8, m_innerOffsetYSlider, m_innerOffsetYValueLabel);

    layout->addStretch();
    return page;
}

void ListViewDebugPanel::updateLabels()
{
    updateOpenGLStatus();

    if (m_rowValueLabel) {
        m_rowValueLabel->setText(QString::number(m_rowSlider ? m_rowSlider->value() : 0));
    }
    if (m_colValueLabel) {
        m_colValueLabel->setText(QString::number(m_colSlider ? m_colSlider->value() : 0));
    }
    if (m_largeGlowValueLabel) {
        m_largeGlowValueLabel->setText(QString::number(m_largeGlowSlider ? m_largeGlowSlider->value() : 0));
    }
    if (m_tightGlowValueLabel) {
        m_tightGlowValueLabel->setText(QString::number(m_tightGlowSlider ? m_tightGlowSlider->value() : 0));
    }
    if (m_glowAlphaValueLabel) {
        m_glowAlphaValueLabel->setText(QString::number(m_glowAlphaSlider ? m_glowAlphaSlider->value() : 0));
    }
    if (m_innerAlphaValueLabel) {
        m_innerAlphaValueLabel->setText(QString::number(m_innerAlphaSlider ? m_innerAlphaSlider->value() : 0));
    }
    if (m_glowOffsetXValueLabel) {
        m_glowOffsetXValueLabel->setText(QString::number(m_glowOffsetXSlider ? m_glowOffsetXSlider->value() : 0));
    }
    if (m_glowOffsetYValueLabel) {
        m_glowOffsetYValueLabel->setText(QString::number(m_glowOffsetYSlider ? m_glowOffsetYSlider->value() : 0));
    }
    if (m_innerOffsetXValueLabel) {
        m_innerOffsetXValueLabel->setText(QString::number(m_innerOffsetXSlider ? m_innerOffsetXSlider->value() : 0));
    }
    if (m_innerOffsetYValueLabel) {
        m_innerOffsetYValueLabel->setText(QString::number(m_innerOffsetYSlider ? m_innerOffsetYSlider->value() : 0));
    }
}

void ListViewDebugPanel::updateOpenGLStatus()
{
    if (!m_openGLStatusLabel) {
        return;
    }

    QSettings settings(debugConfigPath(), QSettings::IniFormat);
    const bool configuredEnabled = settings.value("General/UseOpenGL", false).toBool();
    const bool activeForCurrentLaunch = QCoreApplication::testAttribute(Qt::AA_UseDesktopOpenGL);

    QStringList lines;
    lines << QString("Configured in settings: %1").arg(configuredEnabled ? "Enabled" : "Disabled");
    lines << QString("Active for current launch: %1").arg(activeForCurrentLaunch ? "Yes" : "No");

    if (configuredEnabled != activeForCurrentLaunch) {
        lines << "State mismatch detected. A restart is still needed.";
    } else if (configuredEnabled) {
        lines << "OpenGL startup acceleration is active.";
    } else {
        lines << "The application is running on the default Qt rendering path.";
    }

    m_openGLStatusLabel->setText(lines.join("\n"));
}

void ListViewDebugPanel::applyTextPreviewOptions()
{
    if (!m_textPreview) {
        return;
    }

    SoftDesktopTextRenderer::Options options = m_textPreview->rendererOptions();
    options.textColor = QColor(255, 255, 255);
    options.glowColor = QColor(0, 0, 0, m_glowAlphaSlider ? m_glowAlphaSlider->value() : 210);
    options.innerShadowColor = QColor(0, 0, 0, m_innerAlphaSlider ? m_innerAlphaSlider->value() : 155);
    options.largeGlowRadius = m_largeGlowSlider ? m_largeGlowSlider->value() : 6;
    options.tightGlowRadius = m_tightGlowSlider ? m_tightGlowSlider->value() : 2;
    options.glowOffset = QPoint(m_glowOffsetXSlider ? m_glowOffsetXSlider->value() : 0,
                                m_glowOffsetYSlider ? m_glowOffsetYSlider->value() : 1);
    options.innerShadowOffset = QPoint(m_innerOffsetXSlider ? m_innerOffsetXSlider->value() : 0,
                                       m_innerOffsetYSlider ? m_innerOffsetYSlider->value() : 1);
    options.lineSpacingAdjust = -2;
    options.alignment = Qt::AlignHCenter | Qt::AlignVCenter;
    m_textPreview->setRendererOptions(options);
}
