#include "desktop/ListViewDebugPanel.h"
#include "app/AppFonts.h"
#include "desktop/LinDesktop.h"
#include "desktop/ListView.h"
#include "desktop/widgets/linwidget/WidgetManager.h"
#include "desktop/widgets/linwidget/ConfigManager.h"
#include "desktop/widgets/linwidget/WidgetPackage.h"
#include "desktop/widgets/linwidget/WidgetRegistryManager.h"
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
        "QTabBar::tab:selected { color: #0f6cbd; border-bottom: 3px solid #0f6cbd; font-weight: 600; }");
}

QString debugConfigPath()
{
    return AppPaths::configFilePath();
}

QString bundledLinWidgetsDir()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/LinWidgets"));
}

QString bundledLinWidgetRuntimeDir()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/LinWidgetRuntime"));
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
    copyDirectoryForFirstInstall(bundledLinWidgetRuntimeDir(), AppPaths::userLinWidgetRuntimeDir());

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
    setMinimumSize(860, 720);
    resize(960, 800);
    setStyleSheet(debugPanelStyleSheet());

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* headerContainer = new QWidget(this);
    headerContainer->setFixedHeight(136);
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

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(14);

    QPushButton* restartBtn = createActionButton("Restart", false, true);
    connect(restartBtn, &QPushButton::clicked, this, []() {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), QCoreApplication::arguments(), QCoreApplication::applicationDirPath());
        QCoreApplication::quit();
    });
    grid->addWidget(new FluentCard(u8"\uE777", "Restart", "Relaunch the application", restartBtn), 0, 0);

    QPushButton* settingsBtn = createActionButton("Open", true, false);
    connect(settingsBtn, &QPushButton::clicked, this, [this]() {
        if (m_listView) {
            m_listView->showSettings(nullptr);
        }
    });
    grid->addWidget(new FluentCard(u8"\uE713", "Settings", "Open the main settings window", settingsBtn), 0, 1);

    QPushButton* deskIconBtn = createActionButton("Open", false, false);
    connect(deskIconBtn, &QPushButton::clicked, this, []() {
        QProcess::startDetached("rundll32.exe", QStringList() << "shell32.dll,Control_RunDLL" << "desk.cpl,,0");
    });
    grid->addWidget(new FluentCard(u8"\uE71D", "Desktop Icons", "Native OS icon settings", deskIconBtn), 1, 0);

    QPushButton* qmlTestBtn = createActionButton("Verify", false, false);
    connect(qmlTestBtn, &QPushButton::clicked, this, []() {
        auto* w = new QmlTestWindow();
        w->show(); w->raise(); w->activateWindow();
    });
    grid->addWidget(new FluentCard(u8"\uE943", "QML Test", "Verify Qt Quick availability", qmlTestBtn), 1, 1);

    QPushButton* refreshBtn = createActionButton("Refresh", false, false);
    connect(refreshBtn, &QPushButton::clicked, this, [this](){
        if (m_listView) m_listView->onMenuRefresh();
    });
    grid->addWidget(new FluentCard(u8"\uE72C", "Refresh Layer", "Resort icons and re-render", refreshBtn), 2, 0);

    m_persistToggle = new FluentToggleSwitch();
    m_persistToggle->setChecked(m_listView ? m_listView->persistenceEnabled() : true, false);
    connect(m_persistToggle, &FluentToggleSwitch::toggled, this, [this](bool checked){
        if (m_listView) m_listView->setPersistenceEnabled(checked);
    });
    grid->addWidget(new FluentCard(u8"\uE71B", "Persistence", "Save box and icon data", m_persistToggle), 2, 1);

    QPushButton* exportBtn = createActionButton("Export", false, false);
    connect(exportBtn, &QPushButton::clicked, this, [this]() {
        QString path;
        if (DebugInfoExporter::exportToApplicationDir(m_listView, &path))
            QMessageBox::information(this, "Export", "Exported to " + path);
    });
    grid->addWidget(new FluentCard(u8"\uE8AB", "Export Debug", "Dump state to debug.json", exportBtn), 3, 0, 1, 2);

    m_openGLStatusLabel = new QLabel(page);
    m_openGLStatusLabel->setWordWrap(true);
    m_openGLStatusLabel->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 13px; color: #374151; line-height: 1.5;")
        .arg(AppFonts::uiCssFamily()));
    updateOpenGLStatus();
    grid->addWidget(new FluentCard(
        u8"\uE7F8",
        "OpenGL Status",
        "Shows the saved setting and whether this launch is using the OpenGL startup path.",
        m_openGLStatusLabel), 4, 0, 1, 2);

    QPushButton* openRoamingBtn = createActionButton("Open Roaming", false, false);
    connect(openRoamingBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::userConfigDir()));
    });
    grid->addWidget(new FluentCard(u8"\uE838", "Roaming Configs", "Open LinDesk AppData directory", openRoamingBtn), 7, 0);

    QPushButton* openLocalBtn = createActionButton("Open Local", false, false);
    connect(openLocalBtn, &QPushButton::clicked, this, []() {
        QString localPath = qEnvironmentVariable("LOCALAPPDATA") + "/LinDesk";
        QDir().mkpath(localPath);
        QDesktopServices::openUrl(QUrl::fromLocalFile(localPath));
    });
    grid->addWidget(new FluentCard(u8"\uE8A7", "Local Logs", "Open LinDesk LocalAppData directory", openLocalBtn), 7, 1);

    layout->addLayout(grid);
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
        for (const LinWidget::WidgetLayoutState& state : m_linWidgetManager->layoutStates()) {
            if (state.visible) {
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
        connect(m_linWidgetManager, &LinWidget::WidgetManager::widgetLaunched, this, [this](const QString& instanceId) {
            if (m_linWidgetsPageStatusLabel) {
                m_linWidgetsPageStatusLabel->setText("Widget loaded into WebView: " + instanceId);
            }
            linWidgetManager()->syncWidgetWindowLayer(instanceId);
            updateLinWidgetsButtons();
        });
        connect(m_linWidgetManager, &LinWidget::WidgetManager::widgetStopped, this, [this](const QString& instanceId) {
            if (m_linWidgetsPageStatusLabel) {
                m_linWidgetsPageStatusLabel->setText("Widget unloaded from WebView: " + instanceId);
            }
            const QString packageKey = m_linWidgetInstancesByPackage.key(instanceId);
            if (!packageKey.isEmpty()) {
                m_linWidgetInstancesByPackage.remove(packageKey);
            }
            const QString registryId = m_linWidgetInstancesById.key(instanceId);
            if (!registryId.isEmpty()) {
                m_linWidgetInstancesById.remove(registryId);
            }
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
    if (m_linWidgetManager) {
        m_linWidgetManager->syncAllWidgetWindowLayers();
    }

    int visibleCount = 0;
    for (const LinWidget::WidgetRegistryEntry& entry : registry.entries()) {
        if (!entry.enabled || !QFileInfo::exists(entry.packagePath)) {
            continue;
        }

        auto* item = new QListWidgetItem(entry.name);
        item->setData(Qt::UserRole, entry.packagePath);
        item->setData(Qt::UserRole + 1, entry.id);
        item->setData(Qt::UserRole + 2, entry.runtimeType);
        item->setData(Qt::UserRole + 3, entry.defaultSize);
        item->setData(Qt::UserRole + 4, entry.isDevelopment);
        const QString permissionsText = entry.permissionsGranted.isEmpty()
            ? QStringLiteral("none")
            : entry.permissionsGranted.join(QStringLiteral(", "));
        const QString mode = entry.isDevelopment ? QStringLiteral("development") : QStringLiteral("installed");
        const QString author = entry.author.isEmpty() ? QStringLiteral("Unknown author") : entry.author;
        const QString description = entry.description.isEmpty() ? QStringLiteral("No description") : entry.description;
        item->setText(QStringLiteral("%1\n%2  by %3  %4x%5  %6  mode: %7  autoLoad: %8  permissions: %9\n%10\n%11")
            .arg(entry.name,
                 entry.id,
                 author,
                 QString::number(entry.defaultSize.width()),
                 QString::number(entry.defaultSize.height()),
                 entry.runtimeType,
                 mode,
                 entry.autoLoad ? QStringLiteral("true") : QStringLiteral("false"),
                 permissionsText,
                 description,
                 entry.packagePath));
        if (entry.isDevelopment) {
            m_developmentLinWidgetsList->addItem(item);
        } else {
            m_installedLinWidgetsList->addItem(item);
        }
        ++visibleCount;
    }

    if (m_linWidgetsPageStatusLabel) {
        m_linWidgetsPageStatusLabel->setText(QStringLiteral("Found %1 registered widget package(s).").arg(visibleCount));
    }
    if (m_linWidgetManager) {
        m_linWidgetInstancesByPackage.clear();
        m_linWidgetInstancesById.clear();
        for (const LinWidget::WidgetLayoutState& state : m_linWidgetManager->layoutStates()) {
            if (state.visible) {
                m_linWidgetInstancesByPackage.insert(linWidgetPackageKey(state.packagePath), state.instanceId);
                LinWidget::WidgetManifest manifest;
                if (LinWidget::WidgetPackage::loadManifest(state.packagePath, &manifest, nullptr)) {
                    m_linWidgetInstancesById.insert(
                        linWidgetRegistryKey(manifest.id, state.instanceId.startsWith(QStringLiteral("dev:"))),
                        state.instanceId);
                }
            }
        }
    }
    updateLinWidgetsButtons();
    updateLinWidgetLogViewer();
}

void ListViewDebugPanel::updateLinWidgetsButtons()
{
    const QString packagePath = selectedLinWidgetPackagePath();
    const QString registryKey = selectedLinWidgetRegistryKey();
    const bool hasSelection = !packagePath.isEmpty();
    const bool running = hasSelection &&
        (m_linWidgetInstancesById.contains(registryKey) ||
         m_linWidgetInstancesByPackage.contains(linWidgetPackageKey(packagePath)));
    if (m_linWidgetsStartButton) {
        m_linWidgetsStartButton->setEnabled(hasSelection && !running);
    }
    if (m_linWidgetsStopButton) {
        m_linWidgetsStopButton->setEnabled(running);
    }
    if (m_linWidgetsOpenFolderButton) {
        m_linWidgetsOpenFolderButton->setEnabled(hasSelection);
    }
    if (m_linWidgetsOpenLogsButton) {
        m_linWidgetsOpenLogsButton->setEnabled(true);
    }
    if (m_linWidgetsPackageButton) {
        m_linWidgetsPackageButton->setEnabled(hasSelection && selectedLinWidgetIsDevelopment() && !running);
    }
    if (m_linWidgetsResetBundledButton) {
        m_linWidgetsResetBundledButton->setEnabled(QDir(bundledLinWidgetsDir()).exists());
    }
    if (m_linWidgetsRemoveDevButton) {
        m_linWidgetsRemoveDevButton->setEnabled(hasSelection && selectedLinWidgetIsDevelopment() && !running);
    }
    if (m_linWidgetsDeleteButton) {
        m_linWidgetsDeleteButton->setEnabled(hasSelection);
    }
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
        m_linWidgetsLogView->setPlainText(QStringLiteral("Log file has not been created yet:\n%1").arg(logPath));
        return;
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

QString ListViewDebugPanel::selectedLinWidgetPackagePath() const
{
    QListWidget* list = activeLinWidgetList();
    if (!list || !list->currentItem()) {
        return QString();
    }
    return list->currentItem()->data(Qt::UserRole).toString();
}

QString ListViewDebugPanel::selectedLinWidgetRegistryId() const
{
    QListWidget* list = activeLinWidgetList();
    if (!list || !list->currentItem()) {
        return QString();
    }
    return list->currentItem()->data(Qt::UserRole + 1).toString();
}

QString ListViewDebugPanel::selectedLinWidgetRegistryKey() const
{
    return linWidgetRegistryKey(selectedLinWidgetRegistryId(), selectedLinWidgetIsDevelopment());
}

QString ListViewDebugPanel::selectedLinWidgetInstanceIdForLog() const
{
    const QString registryKey = selectedLinWidgetRegistryKey();
    if (m_linWidgetInstancesById.contains(registryKey)) {
        return m_linWidgetInstancesById.value(registryKey);
    }

    const QString packagePath = selectedLinWidgetPackagePath();
    if (m_linWidgetInstancesByPackage.contains(linWidgetPackageKey(packagePath))) {
        return m_linWidgetInstancesByPackage.value(linWidgetPackageKey(packagePath));
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
    return list && list->currentItem() && list->currentItem()->data(Qt::UserRole + 4).toBool();
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

QWidget* ListViewDebugPanel::createLinWidgetsPage()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(18);

    layout->addWidget(createPageTitleLabel("LinWidgets"));
    layout->addWidget(createPageSubtitleLabel("Manage installed widgets and development projects. Development projects run directly from their source folders with hot reload."));

    QSettings settings(debugConfigPath(), QSettings::IniFormat);
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
    layout->addWidget(new FluentCard(u8"\uE9D9",
                                     "WebView Bounds Visualization",
                                     "Show the full shared WebView layer for debugging. Empty desktop clicks pass through when this is off.",
                                     m_linWidgetBoundsToggle));

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
        "  min-height: 58px;"
        "  padding: 10px 12px;"
        "  border-radius: 8px;"
        "}"
        "QListWidget::item:hover { background: #f3f7fb; }"
        "QListWidget::item:selected { background: #dbeafe; color: #0f172a; }")
        .arg(AppFonts::uiCssFamily());

    auto* contentLayout = new QHBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(16);

    auto* listsPanel = new QWidget(page);
    auto* listsLayout = new QVBoxLayout(listsPanel);
    listsLayout->setContentsMargins(0, 0, 0, 0);
    listsLayout->setSpacing(12);

    listsLayout->addWidget(createPageSubtitleLabel("Installed Widgets"));
    m_installedLinWidgetsList = new QListWidget(page);
    m_installedLinWidgetsList->setMinimumHeight(220);
    m_installedLinWidgetsList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_installedLinWidgetsList->setStyleSheet(listStyle);
    listsLayout->addWidget(m_installedLinWidgetsList, 1);

    listsLayout->addWidget(createPageSubtitleLabel("Development Projects"));
    m_developmentLinWidgetsList = new QListWidget(page);
    m_developmentLinWidgetsList->setMinimumHeight(220);
    m_developmentLinWidgetsList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_developmentLinWidgetsList->setStyleSheet(listStyle);
    listsLayout->addWidget(m_developmentLinWidgetsList, 1);

    auto* logPanel = new QWidget(page);
    auto* logLayout = new QVBoxLayout(logPanel);
    logLayout->setContentsMargins(0, 0, 0, 0);
    logLayout->setSpacing(10);
    m_linWidgetsLogTitleLabel = createPageSubtitleLabel("Live Log");
    logLayout->addWidget(m_linWidgetsLogTitleLabel);
    m_linWidgetsLogView = new QPlainTextEdit(page);
    m_linWidgetsLogView->setReadOnly(true);
    m_linWidgetsLogView->setMinimumWidth(420);
    m_linWidgetsLogView->setMinimumHeight(480);
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

    contentLayout->addWidget(listsPanel, 1);
    contentLayout->addWidget(logPanel, 1);
    layout->addLayout(contentLayout, 1);

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

    auto* actions = new QHBoxLayout();
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(10);

    auto* refreshButton = createActionButton("Refresh", false, false);
    m_linWidgetsAddDevButton = createActionButton("Add Dev Project", false, false);
    m_linWidgetsStartButton = createActionButton("Load to WebView", true, false);
    m_linWidgetsStopButton = createActionButton("Unload from WebView", false, true);
    m_linWidgetsOpenFolderButton = createActionButton("Open Folder", false, false);
    m_linWidgetsOpenLogsButton = createActionButton("Open Logs", false, false);
    m_linWidgetsPackageButton = createActionButton("Package to Roaming", false, false);
    m_linWidgetsResetBundledButton = createActionButton("Reset Bundled", false, true);
    m_linWidgetsRemoveDevButton = createActionButton("Remove Dev", false, true);
    m_linWidgetsDeleteButton = createActionButton("Delete Widget", false, true);
    actions->addWidget(refreshButton);
    actions->addWidget(m_linWidgetsAddDevButton);
    actions->addWidget(m_linWidgetsStartButton);
    actions->addWidget(m_linWidgetsStopButton);
    actions->addWidget(m_linWidgetsPackageButton);
    actions->addWidget(m_linWidgetsRemoveDevButton);
    actions->addWidget(m_linWidgetsOpenFolderButton);
    actions->addWidget(m_linWidgetsOpenLogsButton);
    actions->addWidget(m_linWidgetsResetBundledButton);
    actions->addWidget(m_linWidgetsDeleteButton);
    actions->addStretch();
    layout->addLayout(actions);

    m_linWidgetsPageStatusLabel = new QLabel(page);
    m_linWidgetsPageStatusLabel->setWordWrap(true);
    m_linWidgetsPageStatusLabel->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 13px; color: #374151;")
        .arg(AppFonts::uiCssFamily()));
    layout->addWidget(m_linWidgetsPageStatusLabel);

    connect(refreshButton, &QPushButton::clicked, this, &ListViewDebugPanel::reloadLinWidgetsList);
    connect(m_installedLinWidgetsList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current) {
        if (current && m_developmentLinWidgetsList) {
            m_developmentLinWidgetsList->clearSelection();
            m_developmentLinWidgetsList->setCurrentItem(nullptr);
        }
        updateLinWidgetsButtons();
        updateLinWidgetLogViewer();
    });
    connect(m_developmentLinWidgetsList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current) {
        if (current && m_installedLinWidgetsList) {
            m_installedLinWidgetsList->clearSelection();
            m_installedLinWidgetsList->setCurrentItem(nullptr);
        }
        updateLinWidgetsButtons();
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
    connect(m_linWidgetsStartButton, &QPushButton::clicked, this, [this]() {
        const QString packagePath = selectedLinWidgetPackagePath();
        const QString registryId = selectedLinWidgetRegistryId();
        const bool isDevelopment = selectedLinWidgetIsDevelopment();
        const QString registryKey = linWidgetRegistryKey(registryId, isDevelopment);
    if (packagePath.isEmpty()) {
        return;
    }

        LinWidget::WidgetManifest manifest;
        QString errorMessage;
        if (!LinWidget::WidgetPackage::loadManifest(packagePath, &manifest, &errorMessage)) {
            m_linWidgetsPageStatusLabel->setText("Load to WebView failed: " + errorMessage);
            return;
        }

        QString instanceId;
        const QRect geometry(260, 220, manifest.width, manifest.height);
        LinWidget::WidgetManager* manager = linWidgetManager();
        if (!manager || !manager->launchWidgetPackage(packagePath, geometry, isDevelopment, isDevelopment, &instanceId, &errorMessage)) {
            m_linWidgetsPageStatusLabel->setText("Load to WebView failed: " + errorMessage);
            return;
        }

        LinWidget::WidgetRegistryManager registry;
        registry.load();
        registry.setLastRunState(registryId, isDevelopment, QStringLiteral("running"));
        registry.save();

        m_linWidgetInstancesById.insert(registryKey, instanceId);
        m_linWidgetInstancesByPackage.insert(linWidgetPackageKey(packagePath), instanceId);
        m_lastLinWidgetInstanceId = instanceId;
        m_linWidgetsPageStatusLabel->setText("Loaded " + manifest.name + " into WebView. Instance: " + instanceId);
        updateLinWidgetsButtons();
        updateLinWidgetLogViewer();
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
        copyDirectoryForFirstInstall(bundledLinWidgetRuntimeDir(), AppPaths::userLinWidgetRuntimeDir());
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
            if (LinWidget::WidgetManager* manager = linWidgetManager()) {
                manager->removeWidgetState(runningInstanceId);
            }
            m_linWidgetInstancesById.remove(registryKey);
            m_linWidgetInstancesByPackage.remove(linWidgetPackageKey(packagePath));
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
    connect(m_linWidgetsStopButton, &QPushButton::clicked, this, [this]() {
        const QString packagePath = selectedLinWidgetPackagePath();
        const QString registryId = selectedLinWidgetRegistryId();
        const bool isDevelopment = selectedLinWidgetIsDevelopment();
        const QString registryKey = linWidgetRegistryKey(registryId, isDevelopment);
        const QString instanceId = m_linWidgetInstancesById.value(
            registryKey,
            m_linWidgetInstancesByPackage.value(linWidgetPackageKey(packagePath)));
        if (instanceId.isEmpty()) {
            updateLinWidgetsButtons();
            return;
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
        m_linWidgetsPageStatusLabel->setText("Unloaded widget from WebView: " + instanceId);
        updateLinWidgetsButtons();
        updateLinWidgetLogViewer();
    });

    reloadLinWidgetsList();
    updateLinWidgetLogViewer();
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
