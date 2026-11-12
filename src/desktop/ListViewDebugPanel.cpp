#include "desktop/ListViewDebugPanel.h"
#include "app/Version.h"
#include "app/AppFonts.h"
#include "desktop/LinDesktop.h"
#include "desktop/ListView.h"
#include "desktop/widgets/runtime_export.h"
#include "desktop/runtime/RuntimeInstaller.h"
#include "desktop/widgets/linwidget/ConfigManager.h"
#include "desktop/widgets/linwidget/WidgetPackage.h"
#include "desktop/widgets/linwidget/WidgetRegistryManager.h"
#include "ui/Fluent/FluentToggleSwitch.h"
#include "ui/Fluent/FluentCard.h"
#include "services/AppPaths.h"
#include "services/DebugInfoExporter.h"
#include "ai/AICoreService.h"
#include "ui/hub/WallpaperStudioWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include "ui/Fluent/FluentDialog.h"
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include "FluentSlider.h"
#include <QDesktopServices>
#include <QUrl>
#include <QVBoxLayout>
#include <QTimer>
#include <QClipboard>
#include <QGuiApplication>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <QListWidget>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTextCursor>
#include <QPainterPath>
#include <QResizeEvent>
#include <QToolButton>
#include <QUuid>

// Custom delegate to draw the Fluent Design 2 style sidebar
#include <QTabBar>
#include <QStackedWidget>
#include <QPainterPath>

namespace {
QString debugPanelStyleSheet()
{
    return QStringLiteral(
        "QWidget { "
        "  background: #f8fafc; "
        "  font-family: %1; "
        "} "
        "QScrollArea { background: transparent; border: none; } "
        "QScrollBar:vertical { "
        "  background: transparent; "
        "  width: 8px; "
        "  margin: 2px; "
        "} "
        "QScrollBar::handle:vertical { "
        "  background: rgba(100, 116, 139, 0.28); "
        "  border-radius: 4px; "
        "  min-height: 36px; "
        "} "
        "QScrollBar::handle:vertical:hover { "
        "  background: rgba(100, 116, 139, 0.48); "
        "} "
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical, "
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { "
        "  background: transparent; height: 0px; "
        "} "
        "QScrollBar:horizontal { "
        "  background: transparent; "
        "  height: 8px; "
        "  margin: 2px; "
        "} "
        "QScrollBar::handle:horizontal { "
        "  background: rgba(100, 116, 139, 0.28); "
        "  border-radius: 4px; "
        "  min-width: 36px; "
        "} "
        "QScrollBar::handle:horizontal:hover { "
        "  background: rgba(100, 116, 139, 0.48); "
        "} "
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal, "
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { "
        "  background: transparent; width: 0px; "
        "} "
        "QTabBar#debugSegmentedTabBar { "
        "  background: #f1f5f9; "
        "  border: 1px solid rgba(15, 23, 42, 0.08); "
        "  border-radius: 9px; "
        "  padding: 3px; "
        "} "
        "QTabBar#debugSegmentedTabBar::tab { "
        "  height: 30px; "
        "  min-width: 96px; "
        "  padding: 0 14px; "
        "  margin: 0 2px; "
        "  background: transparent; "
        "  border: none; "
        "  border-radius: 6px; "
        "  font-family: %1; "
        "  font-size: 12px; "
        "  font-weight: 500; "
        "  color: #475569; "
        "} "
        "QTabBar#debugSegmentedTabBar::tab:hover { "
        "  background: rgba(255, 255, 255, 0.70); "
        "  color: #0f172a; "
        "} "
        "QTabBar#debugSegmentedTabBar::tab:selected { "
        "  background: #ffffff; "
        "  color: #0f6cbd; "
        "  font-weight: 600; "
        "  border: 1px solid rgba(15, 23, 42, 0.08); "
        "} "
        "QTabBar#linWidgetSubTabBar { "
        "  background: #f1f5f9; "
        "  border: 1px solid rgba(15, 23, 42, 0.08); "
        "  border-radius: 8px; "
        "  padding: 2px; "
        "} "
        "QTabBar#linWidgetSubTabBar::tab { "
        "  height: 26px; "
        "  min-width: 80px; "
        "  padding: 0 12px; "
        "  margin: 0 2px; "
        "  background: transparent; "
        "  border: none; "
        "  border-radius: 5px; "
        "  font-family: %1; "
        "  font-size: 11px; "
        "  font-weight: 500; "
        "  color: #475569; "
        "} "
        "QTabBar#linWidgetSubTabBar::tab:hover { "
        "  background: rgba(255, 255, 255, 0.70); "
        "  color: #0f172a; "
        "} "
        "QTabBar#linWidgetSubTabBar::tab:selected { "
        "  background: #ffffff; "
        "  color: #0f6cbd; "
        "  font-weight: 600; "
        "  border: 1px solid rgba(15, 23, 42, 0.06); "
        "} "
        "QSlider::groove:horizontal { "
        "  height: 4px; "
        "  background: #e2e8f0; "
        "  border-radius: 2px; "
        "} "
        "QSlider::sub-page:horizontal { "
        "  background: #0f6cbd; "
        "  border-radius: 2px; "
        "} "
        "QSlider::handle:horizontal { "
        "  background: #ffffff; "
        "  border: 2px solid #0f6cbd; "
        "  width: 14px; "
        "  height: 14px; "
        "  margin: -5px 0; "
        "  border-radius: 7px; "
        "} "
        "QSlider::handle:horizontal:hover { "
        "  background: #0f6cbd; "
        "  border: 2px solid #ffffff; "
        "} "
        "QLineEdit, QComboBox, QPlainTextEdit { "
        "  background: #ffffff; "
        "  border: 1px solid rgba(15, 23, 42, 0.12); "
        "  border-radius: 8px; "
        "  padding: 7px 10px; "
        "  font-family: %1; "
        "  font-size: 12px; "
        "  color: #0f172a; "
        "} "
        "QLineEdit:focus, QComboBox:focus, QPlainTextEdit:focus { "
        "  border: 1px solid #0f6cbd; "
        "  background: #ffffff; "
        "} "
        "QComboBox::drop-down { "
        "  width: 24px; "
        "  border: none; "
        "} "
        "QCheckBox { "
        "  font-family: %1; "
        "  font-size: 12px; "
        "  color: #334155; "
        "  spacing: 8px; "
        "} "
        "QCheckBox::indicator { "
        "  width: 16px; "
        "  height: 16px; "
        "  border: 1px solid rgba(15, 23, 42, 0.22); "
        "  border-radius: 4px; "
        "  background: #ffffff; "
        "} "
        "QCheckBox::indicator:hover { "
        "  border-color: #0f6cbd; "
        "} "
        "QCheckBox::indicator:checked { "
        "  background: #0f6cbd; "
        "  border-color: #0f6cbd; "
        "} "
        "QToolTip { "
        "  color: #0f172a; "
        "  background-color: #ffffff; "
        "  border: 1px solid rgba(15, 23, 42, 0.14); "
        "  padding: 6px 10px; "
        "  border-radius: 7px; "
        "  font-family: %1; "
        "  font-size: 12px; "
        "} "
    ).arg(AppFonts::uiCssFamily());
}

QString debugConfigPath()
{
    return AppPaths::configFilePath();
}

QString aiConfigPath()
{
    return QDir(AppPaths::userConfigDir()).filePath(QStringLiteral("ai_studio.json"));
}

QString bundledLinWidgetsDir()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/LinWidgets"));
}

QString userLinWidgetsDir()
{
    return AppPaths::userLinWidgetsDir();
}

QString defaultAIBaseUrl(int providerIndex)
{
    switch (providerIndex) {
    case 1:
        return QStringLiteral("https://api.anthropic.com/v1");
    case 2:
        return QStringLiteral("http://127.0.0.1:11434");
    case 3:
        return QStringLiteral("https://generativelanguage.googleapis.com/v1beta");
    case 4:
        return QStringLiteral("https://api.deepseek.com");
    case 5:
        return QStringLiteral("https://dashscope.aliyuncs.com/compatible-mode/v1");
    default:
        return QStringLiteral("https://api.openai.com/v1");
    }
}

QString defaultAIModel(int providerIndex)
{
    switch (providerIndex) {
    case 1:
        return QStringLiteral("claude-3-5-haiku-latest");
    case 2:
        return QStringLiteral("llama3.2");
    case 3:
        return QStringLiteral("gemini-2.5-flash");
    case 4:
        return QStringLiteral("deepseek-chat");
    case 5:
        return QStringLiteral("qwen-plus");
    default:
        return QStringLiteral("gpt-4o-mini");
    }
}

QString aiProviderSettingsPrefix(int providerIndex)
{
    switch (providerIndex) {
    case 1:
        return QStringLiteral("Claude");
    case 2:
        return QStringLiteral("Ollama");
    case 3:
        return QStringLiteral("Google");
    case 4:
        return QStringLiteral("DeepSeek");
    case 5:
        return QStringLiteral("Qwen");
    default:
        return QStringLiteral("OpenAI");
    }
}

int aiProviderIndexFromName(const QString& providerName)
{
    const QString normalized = providerName.trimmed();
    for (int i = 0; i <= 5; ++i) {
        if (aiProviderSettingsPrefix(i).compare(normalized, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return 0;
}

QStringList defaultAIModels(int providerIndex)
{
    switch (providerIndex) {
    case 1:
        return {
            QStringLiteral("claude-3-5-haiku-latest"),
            QStringLiteral("claude-3-5-sonnet-latest")
        };
    case 2:
        return {
            QStringLiteral("llama3.2"),
            QStringLiteral("llama3.1"),
            QStringLiteral("qwen2.5")
        };
    case 3:
        return {
            QStringLiteral("gemini-2.5-flash"),
            QStringLiteral("gemini-2.5-pro"),
            QStringLiteral("gemini-1.5-flash")
        };
    case 4:
        return {
            QStringLiteral("deepseek-chat"),
            QStringLiteral("deepseek-reasoner")
        };
    case 5:
        return {
            QStringLiteral("qwen-plus"),
            QStringLiteral("qwen-turbo"),
            QStringLiteral("qwen-max")
        };
    default:
        return {
            QStringLiteral("gpt-4o-mini"),
            QStringLiteral("gpt-4o"),
            QStringLiteral("gpt-4.1-mini")
        };
    }
}

QJsonObject loadAIConfigRoot()
{
    QFile file(aiConfigPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return QJsonObject();
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.object();
}

void saveAIConfigRoot(const QJsonObject& root)
{
    const QString path = aiConfigPath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
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
constexpr int LinWidgetManifestValidRole = Qt::UserRole + 14;
constexpr int LinWidgetManifestErrorRole = Qt::UserRole + 15;

bool shouldSkipLinWidgetInstallEntry(const QString& name)
{
    return name == QStringLiteral(".git") ||
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
    // Do not auto-install any default widgets on first launch.
    const QString targetRoot = userLinWidgetsDir();
    const QString markerPath = QDir(targetRoot).filePath(QStringLiteral(".defaults-installed"));
    if (QFileInfo::exists(markerPath)) {
        return;
    }

    QDir().mkpath(targetRoot);
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
QWidget* createSectionHeaderWidget(const QString& iconChar, const QString& titleText, const QString& descText)
{
    auto* header = new QWidget();
    auto* layout = new QHBoxLayout(header);
    layout->setContentsMargins(2, 6, 2, 2);
    layout->setSpacing(8);

    auto* iconLabel = new QLabel(iconChar, header);
    iconLabel->setStyleSheet(QStringLiteral(
        "font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets';"
        "font-size: 15px; color: #0f6cbd; background: transparent;"
    ));
    layout->addWidget(iconLabel);

    auto* titleLabel = new QLabel(titleText, header);
    titleLabel->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 13px; font-weight: 700; color: #1e293b; background: transparent;"
    ).arg(AppFonts::uiCssFamily()));
    layout->addWidget(titleLabel);

    if (!descText.isEmpty()) {
        auto* dotLabel = new QLabel(QStringLiteral("·"), header);
        dotLabel->setStyleSheet(QStringLiteral("color: #94a3b8; font-weight: bold; background: transparent;"));
        layout->addWidget(dotLabel);

        auto* descLabel = new QLabel(descText, header);
        descLabel->setStyleSheet(QStringLiteral(
            "font-family: %1; font-size: 12px; color: #64748b; background: transparent;"
        ).arg(AppFonts::uiCssFamily()));
        layout->addWidget(descLabel);
    }

    layout->addStretch();
    return header;
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
    setMinimumSize(680, 560);
    resize(1020, 820);
    setStyleSheet(debugPanelStyleSheet());

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* headerContainer = new QWidget(this);
    headerContainer->setObjectName(QStringLiteral("debugHeaderContainer"));
    headerContainer->setFixedHeight(66);
    headerContainer->setStyleSheet(QStringLiteral(
        "QWidget#debugHeaderContainer {"
        "  background: #ffffff;"
        "  border-bottom: 1px solid rgba(15, 23, 42, 0.08);"
        "}"));
    auto* headerLayout = new QHBoxLayout(headerContainer);
    headerLayout->setContentsMargins(24, 12, 24, 12);
    headerLayout->setSpacing(16);

    auto* titleContainer = new QWidget(headerContainer);
    auto* titleLayout = new QHBoxLayout(titleContainer);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(10);

    auto* iconBadge = new QLabel(titleContainer);
    iconBadge->setFixedSize(36, 36);
    iconBadge->setAlignment(Qt::AlignCenter);
    iconBadge->setText(QString::fromUtf8(u8"\uEBE8"));
    iconBadge->setStyleSheet(QStringLiteral(
        "background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #0f6cbd, stop:1 #115ea3);"
        "color: #ffffff;"
        "font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets';"
        "font-size: 18px;"
        "border-radius: 9px;"
    ));
    titleLayout->addWidget(iconBadge);

    auto* textContainer = new QWidget(titleContainer);
    auto* textLayout = new QVBoxLayout(textContainer);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(1);

    auto* titleLabel = new QLabel(QStringLiteral("Debug Console"), textContainer);
    titleLabel->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 16px; font-weight: 700; color: #0f172a;")
        .arg(AppFonts::uiCssFamily()));
    textLayout->addWidget(titleLabel);

    auto* subtitleLabel = new QLabel(QStringLiteral("LinDesk Developer & Diagnostics"), textContainer);
    subtitleLabel->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 11px; color: #64748b;")
        .arg(AppFonts::uiCssFamily()));
    textLayout->addWidget(subtitleLabel);

    titleLayout->addWidget(textContainer);
    headerLayout->addWidget(titleContainer);

    headerLayout->addStretch(1);

    m_tabBar = new QTabBar(headerContainer);
    m_tabBar->setObjectName(QStringLiteral("debugSegmentedTabBar"));
    m_tabBar->addTab(QString::fromUtf8(u8"\uE777  General"));
    m_tabBar->addTab(QString::fromUtf8(u8"\uEB9F  壁纸与组件"));
    m_tabBar->addTab(QString::fromUtf8(u8"\uE945  AI Studio"));
    m_tabBar->addTab(QString::fromUtf8(u8"\uE7F4  Layout"));
    m_tabBar->setDrawBase(false);
    m_tabBar->setUsesScrollButtons(true);
    m_tabBar->setExpanding(false);
    headerLayout->addWidget(m_tabBar, 0, Qt::AlignVCenter);

    mainLayout->addWidget(headerContainer);

    m_contentStack = new QStackedWidget(this);
    m_contentStack->addWidget(wrapScrollablePage(createGeneralPage()));
    m_contentStack->addWidget(wrapScrollablePage(createLinWidgetsPage()));
    m_contentStack->addWidget(wrapScrollablePage(createAIPage()));
    m_contentStack->addWidget(wrapScrollablePage(createLayoutPage()));
    mainLayout->addWidget(m_contentStack, 1);

    connect(m_tabBar, &QTabBar::currentChanged, m_contentStack, &QStackedWidget::setCurrentIndex);
    m_tabBar->setCurrentIndex(0);
    updateAdaptiveLayout();

    // Initialize values
    if (m_listView) {
        m_rowSlider->setValue(m_listView->rowSpacing());
        m_colSlider->setValue(m_listView->colSpacing());
    }
    
    updateLabels();
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

        for (const GeneralSection& sec : m_generalSections) {
            if (sec.header) {
                m_generalGrid->removeWidget(sec.header);
            }
            for (QWidget* card : sec.cards) {
                m_generalGrid->removeWidget(card);
            }
        }

        int row = 0;
        for (const GeneralSection& sec : m_generalSections) {
            if (sec.header) {
                m_generalGrid->addWidget(sec.header, row, 0, 1, columnCount);
                ++row;
            }
            int column = 0;
            for (QWidget* card : sec.cards) {
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
            if (column != 0) {
                ++row;
            }
        }
    }

    if (m_linWidgetsContentGrid && m_linWidgetsListsPanel && m_linWidgetsLogPanel) {
        m_linWidgetsContentGrid->removeWidget(m_linWidgetsListsPanel);
        m_linWidgetsContentGrid->removeWidget(m_linWidgetsLogPanel);
        if (width() < 860) {
            m_linWidgetsContentGrid->addWidget(m_linWidgetsListsPanel, 0, 0);
            m_linWidgetsContentGrid->addWidget(m_linWidgetsLogPanel, 1, 0);
            m_linWidgetsContentGrid->setColumnStretch(0, 1);
            m_linWidgetsContentGrid->setColumnStretch(1, 0);
        } else {
            m_linWidgetsContentGrid->addWidget(m_linWidgetsListsPanel, 0, 0);
            m_linWidgetsContentGrid->addWidget(m_linWidgetsLogPanel, 0, 1);
            m_linWidgetsContentGrid->setColumnStretch(0, 4);
            m_linWidgetsContentGrid->setColumnStretch(1, 5);
        }
    }
}

QPushButton* ListViewDebugPanel::createActionButton(const QString& text, bool accent, bool destructive) const
{
    auto* button = new QPushButton(text);
    button->setFixedHeight(32);
    button->setMinimumWidth(80);
    button->setCursor(Qt::PointingHandCursor);

    QString background = QStringLiteral("#ffffff");
    QString foreground = QStringLiteral("#1e293b");
    QString border = QStringLiteral("rgba(15, 23, 42, 0.14)");
    QString hover = QStringLiteral("#f8fafc");
    if (accent) {
        background = QStringLiteral("#0f6cbd");
        foreground = QStringLiteral("#ffffff");
        border = QStringLiteral("#0f6cbd");
        hover = QStringLiteral("#115ea3");
    } else if (destructive) {
        background = QStringLiteral("#fee2e2");
        foreground = QStringLiteral("#dc2626");
        border = QStringLiteral("rgba(220, 38, 38, 0.2)");
        hover = QStringLiteral("#fecaca");
    }

    button->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  padding: 0 14px; border-radius: 7px;"
        "  border: 1px solid %1; background: %2; color: %3;"
        "  font-family: %4; font-size: 12px; font-weight: 600;"
        "}"
        "QPushButton:hover { background: %5; }"
        "QPushButton:pressed { padding-top: 1px; }"
        "QPushButton:disabled { color: #94a3b8; background: #f1f5f9; border-color: rgba(15, 23, 42, 0.08); }")
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
    layout->setSpacing(6);

    auto* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(8);

    auto* label = new QLabel(labelText, container);
    label->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 13px; font-weight: 600; color: #1e293b;")
        .arg(AppFonts::uiCssFamily()));
    topRow->addWidget(label);
    topRow->addStretch();

    valueLabel = new QLabel("0", container);
    valueLabel->setAlignment(Qt::AlignCenter);
    valueLabel->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 12px; font-weight: 600; color: #0f6cbd;"
        "background: #eff6ff; border: 1px solid #bfdbfe; border-radius: 6px; padding: 1px 8px;")
        .arg(AppFonts::uiCssFamily()));
    topRow->addWidget(valueLabel);
    layout->addLayout(topRow);

    slider = new FluentSlider(Qt::Horizontal, container);
    slider->setRange(minValue, maxValue);
    layout->addWidget(slider);

    return container;
}

QLabel* ListViewDebugPanel::createPageTitleLabel(const QString& text) const
{
    auto* label = new QLabel(text);
    label->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 20px; font-weight: 700; color: #0f172a;")
        .arg(AppFonts::uiCssFamily()));
    return label;
}

QLabel* ListViewDebugPanel::createPageSubtitleLabel(const QString& text) const
{
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    label->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 13px; color: #64748b; line-height: 1.4;")
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
    layout->setContentsMargins(28, 24, 28, 28);
    layout->setSpacing(20);

    layout->addWidget(createPageTitleLabel("General Tools"));
    layout->addWidget(createPageSubtitleLabel("Common debug actions, runtime toggles, storage directories, and crash diagnostics."));

    m_generalGrid = new QGridLayout();
    m_generalGrid->setHorizontalSpacing(12);
    m_generalGrid->setVerticalSpacing(10);
    m_generalGrid->setColumnStretch(0, 1);
    m_generalGrid->setColumnStretch(1, 1);

    m_generalSections.clear();
    m_generalCards.clear();
    m_generalWideCards.clear();

    auto registerCard = [this](QWidget* card, bool wide = false) {
        m_generalCards.append(card);
        m_generalWideCards.insert(card, wide);
        return card;
    };

    // Section 1: Quick Actions
    {
        GeneralSection s1;
        s1.header = createSectionHeaderWidget(QString::fromUtf8(u8"\uE777"), QStringLiteral("Quick Actions"), QStringLiteral("Restart, refresh, and core settings"));

        QPushButton* refreshBtn = createActionButton("Refresh", false, false);
        connect(refreshBtn, &QPushButton::clicked, this, [this](){
            if (m_listView) m_listView->onMenuRefresh();
        });
        s1.cards.append(registerCard(new FluentCard(u8"\uE72C", "Refresh Layer", "Resort icons and re-render desktop", refreshBtn)));

        QPushButton* restartBtn = createActionButton("Restart", false, true);
        connect(restartBtn, &QPushButton::clicked, this, []() {
            LinDesktop::relaunchApplication();
        });
        s1.cards.append(registerCard(new FluentCard(u8"\uE777", "Restart", "Relaunch the LinDesk application", restartBtn)));

        QPushButton* settingsBtn = createActionButton("Open", true, false);
        connect(settingsBtn, &QPushButton::clicked, this, [this]() {
            if (m_listView) {
                m_listView->showSettings(nullptr);
            }
        });
        s1.cards.append(registerCard(new FluentCard(u8"\uE713", "Settings", "Open the main settings dialog", settingsBtn)));

        QPushButton* deskIconBtn = createActionButton("Open", false, false);
        connect(deskIconBtn, &QPushButton::clicked, this, []() {
            QProcess::startDetached("rundll32.exe", QStringList() << "shell32.dll,Control_RunDLL" << "desk.cpl,,0");
        });
        s1.cards.append(registerCard(new FluentCard(u8"\uE71D", "Desktop Icons", "Native OS icon settings", deskIconBtn)));

        m_generalSections.append(s1);
    }

    // Section 2: Runtime & Graphics
    {
        GeneralSection s2;
        s2.header = createSectionHeaderWidget(QString::fromUtf8(u8"\uE7F4"), QStringLiteral("Desktop Runtime & Rendering"), QStringLiteral("Graphics path, persistence, and embedding"));

        m_persistToggle = new FluentToggleSwitch();
        m_persistToggle->setChecked(m_listView ? m_listView->persistenceEnabled() : true, false);
        connect(m_persistToggle, &FluentToggleSwitch::toggled, this, [this](bool checked){
            if (m_listView) m_listView->setPersistenceEnabled(checked);
        });
        s2.cards.append(registerCard(new FluentCard(u8"\uE71B", "Persistence", "Save box and icon layout data", m_persistToggle)));

        m_defaultWallpaperToggle = new FluentToggleSwitch();
        LinDesktop* desk = nullptr;
        if (m_listView) {
            QWidget* p = m_listView;
            while (p) {
                if (LinDesktop* d = qobject_cast<LinDesktop*>(p)) {
                    desk = d;
                    break;
                }
                p = p->parentWidget();
            }
        }
        m_defaultWallpaperToggle->setChecked(desk ? desk->showDefaultWallpaper() : true, false);
        connect(m_defaultWallpaperToggle, &FluentToggleSwitch::toggled, this, [this](bool checked){
            LinDesktop* desk = nullptr;
            if (m_listView) {
                QWidget* p = m_listView;
                while (p) {
                    if (LinDesktop* d = qobject_cast<LinDesktop*>(p)) {
                        desk = d;
                        break;
                    }
                    p = p->parentWidget();
                }
            }
            if (desk) {
                desk->setShowDefaultWallpaper(checked);
            }
        });
        s2.cards.append(registerCard(new FluentCard(u8"\uE7F4", "Default Wallpaper", "Toggle desktop default wallpaper background", m_defaultWallpaperToggle)));

        m_openGLStatusLabel = new QLabel(page);
        m_openGLStatusLabel->setWordWrap(true);
        m_openGLStatusLabel->setStyleSheet(QStringLiteral(
            "font-family: %1; font-size: 12px; font-weight: 500; color: #0f6cbd;"
            "background: #eff6ff; border: 1px solid #bfdbfe; border-radius: 6px; padding: 6px 10px;"
        ).arg(AppFonts::uiCssFamily()));
        updateOpenGLStatus();
        s2.cards.append(registerCard(new FluentCard(u8"\uE7F8", "OpenGL Acceleration", "Shows GPU startup acceleration status and active Qt rendering path", m_openGLStatusLabel), true));

        m_generalSections.append(s2);
    }

    // Section 3: Storage & Logs
    {
        GeneralSection s3;
        s3.header = createSectionHeaderWidget(QString::fromUtf8(u8"\uE8B7"), QStringLiteral("Storage & Log Paths"), QStringLiteral("Direct access to logs, dumps, and app data"));

        QPushButton* openLogsBtn = createActionButton("Open Logs", false, false);
        connect(openLogsBtn, &QPushButton::clicked, this, []() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::userLogsDir()));
        });
        s3.cards.append(registerCard(new FluentCard(u8"\uE8A7", "Application Logs", "Open logs directory (%LOCALAPPDATA%/LinDesk/logs)", openLogsBtn)));

        QPushButton* openCrashesBtn = createActionButton("Open Dumps", false, false);
        connect(openCrashesBtn, &QPushButton::clicked, this, []() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::userCrashDumpsDir()));
        });
        s3.cards.append(registerCard(new FluentCard(u8"\uE7BA", "Crash Dumps", "Open crash minidump directory (%LOCALAPPDATA%/LinDesk/logs/crash)", openCrashesBtn)));

        QPushButton* openRoamingBtn = createActionButton("Open Roaming", false, false);
        connect(openRoamingBtn, &QPushButton::clicked, this, []() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::userConfigDir()));
        });
        s3.cards.append(registerCard(new FluentCard(u8"\uE838", "Roaming Configs", "Open LinDesk AppData roaming directory", openRoamingBtn)));

        QPushButton* openLocalBtn = createActionButton("Open Local", false, false);
        connect(openLocalBtn, &QPushButton::clicked, this, []() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::userLocalConfigDir()));
        });
        s3.cards.append(registerCard(new FluentCard(u8"\uE8B7", "Local Data", "Open LinDesk LocalAppData directory", openLocalBtn)));

        QPushButton* exportBtn = createActionButton("Export", false, false);
        connect(exportBtn, &QPushButton::clicked, this, [this]() {
            QString path;
            if (DebugInfoExporter::exportToApplicationDir(m_listView, &path))
                FluentDialog::showInfo(this, "Export", "Exported to " + path);
        });
        s3.cards.append(registerCard(new FluentCard(u8"\uE8AB", "Export Debug Snapshot", "Dump full desktop state to debug.json", exportBtn), true));

        m_generalSections.append(s3);
    }

    // Section 4: Diagnostics & Tests
    {
        GeneralSection s4;
        s4.header = createSectionHeaderWidget(QString::fromUtf8(u8"\uE943"), QStringLiteral("Diagnostics & Crash Testing"), QStringLiteral("Minidump generation and crash testing"));

        QPushButton* crashBtn = createActionButton("Trigger Crash", false, true);
        connect(crashBtn, &QPushButton::clicked, this, []() {
            // Dereference a null pointer to force an active crash
            volatile int* p = nullptr;
            *p = 42;
        });
        s4.cards.append(registerCard(new FluentCard(u8"\uE7BA", "Trigger Crash", "Instantly trigger an active application crash (Minidump test)", crashBtn)));

        m_generalSections.append(s4);
    }

    layout->addLayout(m_generalGrid);
    updateAdaptiveLayout();
    layout->addStretch();
    return page;
}

void ListViewDebugPanel::reloadLinWidgetsList()
{
    if (!m_linWidgetsList) {
        return;
    }

    ensureDefaultLinWidgetsInstalled();
    m_linWidgetsList->clear();

    auto& registry = LinWidget::WidgetRegistryManager::instance();
    QString registryError;
    registry.load(&registryError);
    registry.syncFromFolder(userLinWidgetsDir(), &registryError);
    registry.save(&registryError);
    refreshLinWidgetRunningMaps();
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (bridge.isLoaded()) {
        bridge.syncAllWidgetWindowLayers();
    }

    int visibleCount = 0;
    int runningCount = 0;
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
        QString manifestError;
        const bool manifestOk = LinWidget::WidgetPackage::loadManifest(entry.packagePath, &manifest, &manifestError);
        item->setData(LinWidgetSettingsRole, manifest.settings);
        const QString permissionsText = entry.permissionsGranted.isEmpty()
            ? QStringLiteral("none")
            : entry.permissionsGranted.join(QStringLiteral(", "));
        item->setData(LinWidgetPermissionsRole, permissionsText);
        item->setData(LinWidgetRequestedPermissionsRole, manifest.permissions);
        item->setData(LinWidgetManifestValidRole, manifestOk);
        item->setData(LinWidgetManifestErrorRole, manifestError);

        const QString registryKey = linWidgetRegistryKey(entry.id, entry.isDevelopment);
        const bool running = m_linWidgetInstancesById.contains(registryKey) ||
            m_linWidgetInstancesByPackage.contains(linWidgetPackageKey(entry.packagePath));
        if (running) {
            ++runningCount;
        }

        updateLinWidgetItemText(item);
        m_linWidgetsList->addItem(item);
        m_linWidgetsList->setItemWidget(item, createLinWidgetItemWidget(item));
        ++visibleCount;
    }

    // 应用当前筛选过滤标签
    if (m_linWidgetFilterTabBar) {
        const int filterIdx = m_linWidgetFilterTabBar->currentIndex();
        for (int i = 0; i < m_linWidgetsList->count(); ++i) {
            QListWidgetItem* item = m_linWidgetsList->item(i);
            const bool isDev = item->data(LinWidgetDevelopmentRole).toBool();
            if (filterIdx == 1) {
                item->setHidden(isDev); // 仅官方
            } else if (filterIdx == 2) {
                item->setHidden(!isDev); // 仅开发工程
            } else {
                item->setHidden(false); // 全部
            }
        }
    }

    if (m_linWidgetsPageStatusLabel) {
        const QString statusText = registryError.isEmpty()
            ? QStringLiteral("已载入 %1 个小组件 (%2 个运行中) · 多进程沙箱正常").arg(visibleCount).arg(runningCount)
            : QStringLiteral("载入 %1 个小组件 (%2 个运行中) · 配置异常: %3").arg(visibleCount).arg(runningCount).arg(registryError);
        m_linWidgetsPageStatusLabel->setText(statusText);
    }

    updateLinWidgetsButtons();
    updateLinWidgetDetailsPanel();
}

void ListViewDebugPanel::updateLinWidgetsButtons()
{
    updateAllLinWidgetItemWidgets();

    const QString packagePath = selectedLinWidgetPackagePath();
    const bool hasSelection = !packagePath.isEmpty();
    const bool running = hasSelection && selectedLinWidgetIsRunning();
    const bool runtimeEnabled = LinWidgetRuntimeBridge::instance().isEnabled();

    if (m_linWidgetsOpenFolderButton) {
        m_linWidgetsOpenFolderButton->setEnabled(hasSelection);
    }
    if (m_linWidgetsOpenLogsButton) {
        m_linWidgetsOpenLogsButton->setEnabled(true);
    }
    if (m_linWidgetsDevToolsButton) {
        m_linWidgetsDevToolsButton->setEnabled(runtimeEnabled && running);
    }
    if (m_linWidgetsRestartButton) {
        m_linWidgetsRestartButton->setEnabled(runtimeEnabled && running);
    }
    if (m_linWidgetsSettingsButton) {
        m_linWidgetsSettingsButton->setEnabled(hasSelection);
    }
    if (m_linWidgetsDeleteButton) {
        m_linWidgetsDeleteButton->setEnabled(hasSelection && !running);
    }
    if (m_linWidgetsResetBundledButton) {
        m_linWidgetsResetBundledButton->setEnabled(QDir(bundledLinWidgetsDir()).exists());
    }

    if (m_linWidgetsRuntimeToggleButton) {
        const bool isInstalled = RuntimeInstaller::instance().isRuntimeInstalled();
        if (isInstalled) {
            m_linWidgetsRuntimeToggleButton->setText(QStringLiteral("卸载组件包"));
            m_linWidgetsRuntimeToggleButton->setToolTip(QStringLiteral("卸载 AppData 中的组件运行环境，模拟全新系统以测试远程下载与安装流程"));
            m_linWidgetsRuntimeToggleButton->setStyleSheet(QStringLiteral(
                "QPushButton {"
                "  background: #fee2e2; border: 1px solid #fca5a5; color: #b91c1c;"
                "  padding: 5px 12px; border-radius: 6px; font-size: 12px; font-weight: 500;"
                "}"
                "QPushButton:hover { background: #fecaca; border-color: #f87171; }"
                "QPushButton:pressed { background: #fca5a5; }"
            ));
        } else {
            m_linWidgetsRuntimeToggleButton->setText(QStringLiteral("安装组件包"));
            m_linWidgetsRuntimeToggleButton->setToolTip(QStringLiteral("一键从本地构建目录 (runtime/linwidget) 复制安装运行环境至 AppData，无需远程下载"));
            m_linWidgetsRuntimeToggleButton->setStyleSheet(QStringLiteral(
                "QPushButton {"
                "  background: #0067c0; border: 1px solid #005fb8; color: #ffffff;"
                "  padding: 5px 12px; border-radius: 6px; font-size: 12px; font-weight: 500;"
                "}"
                "QPushButton:hover { background: #1975c5; border-color: #1975c5; }"
                "QPushButton:pressed { background: #005299; }"
            ));
        }
        m_linWidgetsRuntimeToggleButton->setEnabled(true);
    }
    updateLinWidgetRuntimeInfoCard();
}

void ListViewDebugPanel::updateLinWidgetRuntimeInfoCard()
{
    if (!m_linWidgetRuntimeInfoLabel) {
        return;
    }

    auto& installer = RuntimeInstaller::instance();
    auto& bridge = LinWidgetRuntimeBridge::instance();
    const bool isInstalled = installer.isRuntimeInstalled();
    const bool isLoaded = bridge.isLoaded();
    const bool isEnabled = bridge.isEnabled();

    const QString runtimeDir = installer.runtimeDirectory();
    bool isLocalDev = false;
    const QString localBuild = installer.localBuildRuntimeDir();
    if (isInstalled && !localBuild.isEmpty() && QDir(runtimeDir) == QDir(localBuild)) {
        isLocalDev = true;
    }

    int runningCount = 0;
    if (isLoaded) {
        QJsonObject req;
        req.insert(QStringLiteral("action"), QStringLiteral("list"));
        const QJsonObject resp = bridge.queryWidgetManager(req);
        const QJsonArray widgets = resp.value(QStringLiteral("widgets")).toArray();
        for (const QJsonValue& val : widgets) {
            runningCount += val.toObject().value(QStringLiteral("runningInstanceIds")).toArray().size();
        }
    }

    QString html;
    QTextStream ts(&html);
    ts << QStringLiteral("<div style='line-height: 1.6; font-size: 12px; color: #334155;'>");
    ts << QStringLiteral("<table style='width: 100%; border-collapse: collapse; font-size: 12px;'>");

    // 状态行
    ts << QStringLiteral("<tr><td style='width: 80px; font-weight: 600; color: #64748b; padding: 2px 0;'>底座状态:</td><td>");
    if (isLoaded && isEnabled) {
        ts << QStringLiteral("<span style='background: #dcfce7; color: #15803d; font-weight: 600; padding: 1px 8px; border-radius: 4px;'>● 运行中 (Active)</span>");
    } else if (isInstalled) {
        ts << QStringLiteral("<span style='background: #eff6ff; color: #1e40af; font-weight: 600; padding: 1px 8px; border-radius: 4px;'>● 已安装 (就绪待命)</span>");
    } else {
        ts << QStringLiteral("<span style='background: #fef3c7; color: #b45309; font-weight: 600; padding: 1px 8px; border-radius: 4px;'>○ 未安装 (休眠)</span>");
    }
    ts << QStringLiteral(" &nbsp;<span style='color: #64748b;'>活跃实例: <b>%1</b></span></td></tr>").arg(runningCount);

    // 运行路径行
    ts << QStringLiteral("<tr><td style='font-weight: 600; color: #64748b; padding: 2px 0;'>运行目录:</td><td style='word-break: break-all; color: #0f172a;'>");
    if (isInstalled) {
        ts << QStringLiteral("<code style='font-family: Consolas, monospace; font-size: 11px;'>%1</code>").arg(runtimeDir);
        if (isLocalDev) {
            ts << QStringLiteral(" <span style='background: #ede9fe; color: #7c3aed; font-size: 10px; font-weight: 700; padding: 1px 6px; border-radius: 4px;'>本地开发构建</span>");
        }
    } else {
        ts << QStringLiteral("<span style='color: #94a3b8;'>未挂载运行环境 (可点击上方「安装组件包」快速装配)</span>");
    }
    ts << QStringLiteral("</td></tr>");

    // 版本与硬件加速行
    ts << QStringLiteral("<tr><td style='font-weight: 600; color: #64748b; padding: 2px 0;'>版本/加速:</td><td>");
    ts << QStringLiteral("CEF OSR %1 · 硬件图形加速: ").arg(installer.installedVersion().isEmpty() ? QStringLiteral("1.0.0") : installer.installedVersion());
    if (bridge.isCefOsrEnabled()) {
        ts << QStringLiteral("<b style='color: #15803d;'>已开启</b>");
    } else {
        ts << QStringLiteral("<span style='color: #64748b;'>已关闭</span>");
    }
    ts << QStringLiteral("</td></tr>");

    ts << QStringLiteral("</table></div>");
    m_linWidgetRuntimeInfoLabel->setText(html);
}

void ListViewDebugPanel::updateLinWidgetDetailsPanel()
{
    updateLinWidgetRuntimeInfoCard();

    const QString packagePath = selectedLinWidgetPackagePath();
    const QString registryId = selectedLinWidgetRegistryId();
    const bool isDevelopment = selectedLinWidgetIsDevelopment();
    const bool isRunning = selectedLinWidgetIsRunning();
    const QString instanceId = selectedLinWidgetRunningInstanceId();

    if (!m_linWidgetDetailsLabel) {
        return;
    }

    if (packagePath.isEmpty()) {
        m_linWidgetDetailsLabel->setText(QStringLiteral(
            "<div style='color: #94a3b8; padding: 8px 0; font-size: 12px;'>"
            "未选择组件。请从左侧列表中点击选择组件，以查看沙箱属性并进行调测。"
            "</div>"));
        if (m_linWidgetsDevToolsButton) m_linWidgetsDevToolsButton->setEnabled(false);
        if (m_linWidgetsRestartButton) m_linWidgetsRestartButton->setEnabled(false);
        if (m_linWidgetsSettingsButton) m_linWidgetsSettingsButton->setEnabled(false);
        if (m_linWidgetsOpenFolderButton) m_linWidgetsOpenFolderButton->setEnabled(false);
        if (m_linWidgetsDeleteButton) m_linWidgetsDeleteButton->setEnabled(false);
        return;
    }

    LinWidget::WidgetManifest manifest;
    LinWidget::WidgetPackage::loadManifest(packagePath, &manifest, nullptr);
    const QString name = manifest.name.isEmpty() ? registryId : manifest.name;
    const QString version = manifest.version.isEmpty() ? QStringLiteral("1.0.0") : manifest.version;
    const QString author = manifest.author.isEmpty() ? QStringLiteral("LinDesk") : manifest.author;
    const QString entry = manifest.entry.isEmpty() ? QStringLiteral("index.html") : manifest.entry;

    QString statusBadge;
    if (isRunning) {
        const QRect geom = LinWidgetRuntimeBridge::instance().widgetGeometry(instanceId);
        statusBadge = QStringLiteral(
            "<span style='background: #dcfce7; color: #15803d; font-weight: 600; padding: 2px 8px; border-radius: 4px;'>"
            "● 运行中</span> &nbsp; <span style='color: #475569;'>坐标: %1,%2 &nbsp;尺寸: %3×%4 px</span>")
            .arg(geom.x()).arg(geom.y()).arg(geom.width()).arg(geom.height());
    } else {
        statusBadge = QStringLiteral(
            "<span style='background: #f1f5f9; color: #64748b; font-weight: 600; padding: 2px 8px; border-radius: 4px;'>"
            "○ 已停止</span>");
    }

    QString permBadges;
    if (manifest.permissions.isEmpty()) {
        permBadges = QStringLiteral("<span style='color: #94a3b8;'>无特殊权限</span>");
    } else {
        for (const QString& perm : manifest.permissions) {
            permBadges += QStringLiteral(
                "<span style='background: #eff6ff; color: #1e40af; border: 1px solid #bfdbfe; font-size: 11px; font-weight: 600; padding: 1px 6px; border-radius: 4px; margin-right: 4px;'>%1</span>"
            ).arg(perm);
        }
    }

    const QString detailsHtml = QStringLiteral(
        "<div style='line-height: 1.6; font-size: 12px; color: #1e293b;'>"
        "  <div style='font-size: 14px; font-weight: 700; color: #0f172a; margin-bottom: 2px;'>%1 "
        "    <span style='font-size: 11px; font-weight: 500; color: #64748b;'>v%2 由 %3 开发</span>"
        "  </div>"
        "  <div style='margin-bottom: 6px;'>%4</div>"
        "  <table style='width: 100%; font-size: 11px; color: #475569; border-collapse: collapse;'>"
        "    <tr><td style='width: 80px; font-weight: 600;'>组件 ID:</td><td><code>%5</code></td></tr>"
        "    <tr><td style='font-weight: 600;'>工程类型:</td><td>%6</td></tr>"
        "    <tr><td style='font-weight: 600;'>入口文件:</td><td><code>%7</code></td></tr>"
        "    <tr><td style='font-weight: 600;'>已声明权限:</td><td>%8</td></tr>"
        "    <tr><td style='font-weight: 600;'>所在路径:</td><td style='word-break: break-all; color: #64748b;'>%9</td></tr>"
        "  </table>"
        "</div>"
    ).arg(name, version, author, statusBadge, registryId,
         isDevelopment ? QStringLiteral("<b style='color: #7c3aed;'>开发工程 (实时热重载)</b>") : QStringLiteral("<b style='color: #0284c7;'>官方内置组件</b>"),
         entry, permBadges, packagePath);

    m_linWidgetDetailsLabel->setText(detailsHtml);

    if (m_linWidgetsDevToolsButton) {
        m_linWidgetsDevToolsButton->setEnabled(isRunning);
    }
    if (m_linWidgetsRestartButton) {
        m_linWidgetsRestartButton->setEnabled(isRunning);
    }
    if (m_linWidgetsSettingsButton) {
        m_linWidgetsSettingsButton->setEnabled(true);
    }
    if (m_linWidgetsOpenFolderButton) {
        m_linWidgetsOpenFolderButton->setEnabled(true);
    }
    if (m_linWidgetsDeleteButton) {
        m_linWidgetsDeleteButton->setEnabled(!isRunning);
    }
}

void ListViewDebugPanel::refreshLinWidgetRunningMaps()
{
    m_linWidgetInstancesByPackage.clear();
    m_linWidgetInstancesById.clear();

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
                const QString key = linWidgetRegistryKey(regId, isDev);
                const QJsonArray runningIds = obj.value(QStringLiteral("runningInstanceIds")).toArray();
                if (!runningIds.isEmpty()) {
                    for (const QJsonValue& idVal : runningIds) {
                        const QString instId = idVal.toString();
                        m_linWidgetInstancesById.insert(key, instId);
                        if (!packagePath.isEmpty()) {
                            m_linWidgetInstancesByPackage.insert(linWidgetPackageKey(packagePath), instId);
                        }
                        m_lastLinWidgetInstanceId = instId;
                    }
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
        return QStringLiteral("__shared__");
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
    const QString manifestError = item->data(LinWidgetManifestErrorRole).toString();
    if (!item->data(LinWidgetManifestValidRole).toBool()) {
        const QString displayText = QStringLiteral("%1\n%2\n%3")
            .arg(name.isEmpty() ? id : name)
            .arg(QStringLiteral("Manifest error: %1").arg(manifestError))
            .arg(packagePath);
        item->setData(LinWidgetDisplayTextRole, displayText);
        item->setText(QString());
        item->setToolTip(displayText);
        return;
    }

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
    if (!m_linWidgetsList) {
        return;
    }
    for (int i = 0; i < m_linWidgetsList->count(); ++i) {
        QListWidgetItem* item = m_linWidgetsList->item(i);
        updateLinWidgetItemText(item);
        if (QWidget* oldWidget = m_linWidgetsList->itemWidget(item)) {
            oldWidget->deleteLater();
        }
        m_linWidgetsList->setItemWidget(item, createLinWidgetItemWidget(item));
    }
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
            m_linWidgetsPageStatusLabel->setText("Load failed: " + errorMessage);
        }
        return false;
    }

    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (!bridge.isLoaded()) {
        if (m_linWidgetsPageStatusLabel) {
            m_linWidgetsPageStatusLabel->setText("Load failed: widget runtime not loaded");
        }
        return false;
    }

    QString instanceId;
    if (!bridge.startWidgetPackage(packagePath)) {
        if (m_linWidgetsPageStatusLabel) {
            m_linWidgetsPageStatusLabel->setText("Load failed to start widget package");
        }
        return false;
    }
    QStringList restoredIds;
    restoredIds.append(manifest.id);

    auto& registry = LinWidget::WidgetRegistryManager::instance();
    registry.load();
    registry.setLastRunState(registryId, isDevelopment, QStringLiteral("running"));
    registry.save();

    m_linWidgetInstancesById.insert(registryKey, instanceId);
    m_linWidgetInstancesByPackage.insert(linWidgetPackageKey(packagePath), instanceId);
    m_lastLinWidgetInstanceId = instanceId;
    if (m_linWidgetsPageStatusLabel) {
        m_linWidgetsPageStatusLabel->setText(
            restoredIds.size() > 1
                ? QStringLiteral("Loaded %1 instance(s) of %2 (CEF OSR)").arg(restoredIds.size()).arg(manifest.name)
                : QStringLiteral("Loaded %1 (CEF OSR). Instance: %2").arg(manifest.name, instanceId));
    }
    updateSelectedLinWidgetItemText();
    updateLinWidgetsButtons();
    updateLinWidgetDetailsPanel();
    return true;
}

bool ListViewDebugPanel::unloadSelectedLinWidget()
{
    const QString packagePath = selectedLinWidgetPackagePath();
    const QString registryId = selectedLinWidgetRegistryId();
    const bool isDevelopment = selectedLinWidgetIsDevelopment();
    const QString registryKey = linWidgetRegistryKey(registryId, isDevelopment);

    const QString primaryInstanceId = m_linWidgetInstancesById.value(
        registryKey,
        m_linWidgetInstancesByPackage.value(linWidgetPackageKey(packagePath)));

    auto& bridge = LinWidgetRuntimeBridge::instance();
    QStringList stoppedIds;
    if (bridge.isLoaded()) {
        QJsonObject req;
        req.insert(QStringLiteral("action"), QStringLiteral("list"));
        const QJsonObject resp = bridge.queryWidgetManager(req);
        const QJsonArray widgets = resp.value(QStringLiteral("widgets")).toArray();
        for (const QJsonValue& val : widgets) {
            const QJsonObject obj = val.toObject();
            if (obj.value(QStringLiteral("id")).toString().compare(registryId, Qt::CaseInsensitive) == 0 &&
                obj.value(QStringLiteral("isDevelopment")).toBool() == isDevelopment) {
                const QJsonArray runningIds = obj.value(QStringLiteral("runningInstanceIds")).toArray();
                for (const QJsonValue& idVal : runningIds) {
                    const QString id = idVal.toString();
                    bridge.stopWidget(id);
                    stoppedIds.append(id);
                }
            }
        }
        if (stoppedIds.isEmpty() && !primaryInstanceId.isEmpty()) {
            bridge.stopWidget(primaryInstanceId);
            stoppedIds.append(primaryInstanceId);
        }
        if (stoppedIds.isEmpty() && !registryKey.isEmpty()) {
            bridge.stopWidget(registryKey);
            stoppedIds.append(registryKey);
        }
    }

    auto& registry = LinWidget::WidgetRegistryManager::instance();
    registry.load();
    registry.setLastRunState(registryId, isDevelopment, QStringLiteral("stopped"));
    registry.save();

    m_linWidgetInstancesById.remove(registryKey);
    m_linWidgetInstancesByPackage.remove(linWidgetPackageKey(packagePath));
    if (m_linWidgetsPageStatusLabel) {
        m_linWidgetsPageStatusLabel->setText(
            stoppedIds.size() > 1
                ? QStringLiteral("Unloaded %1 instance(s) of widget: %2").arg(stoppedIds.size()).arg(registryId)
                : QStringLiteral("Unloaded widget: %1").arg(!stoppedIds.isEmpty() ? stoppedIds.value(0) : registryId));
    }
    updateSelectedLinWidgetItemText();
    updateLinWidgetsButtons();
    updateLinWidgetDetailsPanel();
    return true;
}

void ListViewDebugPanel::exportLinWidgetDiagnostics()
{
    const QString packagePath = selectedLinWidgetPackagePath();
    const QString registryId = selectedLinWidgetRegistryId();
    if (packagePath.isEmpty() && registryId.isEmpty()) {
        if (m_linWidgetsPageStatusLabel) {
            m_linWidgetsPageStatusLabel->setText(QStringLiteral("Please select a widget from the list to export diagnostics."));
        }
        return;
    }

    const QString instanceId = selectedLinWidgetRunningInstanceId();
    const bool isRunning = selectedLinWidgetIsRunning();
    const bool isDev = selectedLinWidgetIsDevelopment();

    // 1. Load Manifest
    QString manifestContent;
    const QString manifestPath = QDir(packagePath).filePath(QStringLiteral("manifest.json"));
    QFile manifestFile(manifestPath);
    if (manifestFile.open(QIODevice::ReadOnly)) {
        manifestContent = QString::fromUtf8(manifestFile.readAll());
        manifestFile.close();
    } else {
        manifestContent = QStringLiteral("{\n  \"error\": \"manifest.json not found\"\n}");
    }

    // 2. Capture runtime diagnostic state
    QString logs;
    const QString logFile = linWidgetLogFilePathForInstance(instanceId.isEmpty() ? registryId : instanceId);
    if (!logFile.isEmpty() && QFile::exists(logFile)) {
        QFile f(logFile);
        if (f.open(QIODevice::ReadOnly)) {
            logs = QString::fromUtf8(f.readAll());
            f.close();
        }
    }
    if (logs.isEmpty()) {
        logs = QStringLiteral("[No recent log output captured]");
    } else {
        const QStringList lines = logs.split(QLatin1Char('\n'));
        if (lines.size() > 200) {
            logs = lines.mid(lines.size() - 200).join(QLatin1Char('\n'));
        }
    }

    // 3. Mask sensitive paths
    auto sanitize = [](QString text) {
        const QString userHome = QDir::homePath();
        const QString appData = AppPaths::userConfigDir();
        const QString localData = AppPaths::userLocalConfigDir();
        if (!userHome.isEmpty()) {
            text.replace(userHome, QStringLiteral("<USER_HOME>"), Qt::CaseInsensitive);
        }
        if (!appData.isEmpty()) {
            text.replace(appData, QStringLiteral("<APP_DATA>"), Qt::CaseInsensitive);
        }
        if (!localData.isEmpty()) {
            text.replace(localData, QStringLiteral("<LOCAL_APP_DATA>"), Qt::CaseInsensitive);
        }
        return text;
    };

    manifestContent = sanitize(manifestContent);
    logs = sanitize(logs);

    // 4. Format Markdown diagnostic report
    QString report;
    QTextStream ts(&report);
    ts << QStringLiteral("# LinWidget Diagnostic Package\n\n");
    ts << QStringLiteral("- Exported At: %1 UTC\n").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    ts << QStringLiteral("- LinDesk Host Version: %1\n").arg(QString::fromLatin1(LINDESK_VERSION_STRING));
    ts << QStringLiteral("- Target Widget ID: `%1`\n").arg(registryId);
    ts << QStringLiteral("- Project Mode: %1\n").arg(isDev ? QStringLiteral("Development Project (Dev)") : QStringLiteral("Official / Installed Widget"));
    ts << QStringLiteral("- Runtime Status: %1\n").arg(isRunning ? QStringLiteral("Running") : QStringLiteral("Stopped"));
    if (!instanceId.isEmpty()) {
        ts << QStringLiteral("- Active Instance ID: `%1`\n").arg(instanceId);
    }
    ts << QStringLiteral("- Requested Permissions: %1\n").arg(selectedLinWidgetRequestedPermissions().join(QStringLiteral(", ")));
    ts << QStringLiteral("- Granted Permissions: %1\n\n").arg(selectedLinWidgetGrantedPermissions().join(QStringLiteral(", ")));

    ts << QStringLiteral("## 1. Widget Manifest\n```json\n");
    ts << manifestContent.trimmed() << QStringLiteral("\n```\n\n");

    ts << QStringLiteral("## 2. Recent Runtime & Diagnostic Logs\n```log\n");
    ts << logs.trimmed() << QStringLiteral("\n```\n\n");

    // 5. Copy to system clipboard
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard) {
        clipboard->setText(report);
    }

    if (m_linWidgetsPageStatusLabel) {
        m_linWidgetsPageStatusLabel->setText(QStringLiteral("诊断报告已成功复制到剪贴板！"));
    }
}

QWidget* ListViewDebugPanel::createLinWidgetItemWidget(QListWidgetItem* item)
{
    auto* row = new QWidget();
    row->setToolTip(item ? item->toolTip() : QString());
    auto* layout = new QVBoxLayout(row);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(6);

    const QString id = item ? item->data(LinWidgetRegistryIdRole).toString() : QString();
    const bool isDevelopment = item && item->data(LinWidgetDevelopmentRole).toBool();
    const QString registryKey = linWidgetRegistryKey(id, isDevelopment);
    const QString packagePath = item ? item->data(LinWidgetPackagePathRole).toString() : QString();
    const bool running = m_linWidgetInstancesById.contains(registryKey) ||
        m_linWidgetInstancesByPackage.contains(linWidgetPackageKey(packagePath));
    const QString name = item ? item->data(LinWidgetNameRole).toString() : QString();
    const QString version = item ? item->data(LinWidgetVersionRole).toString() : QString();
    const QString runtimeType = item ? item->data(LinWidgetRuntimeTypeRole).toString() : QString();
    const QSize defaultSize = item ? item->data(LinWidgetDefaultSizeRole).toSize() : QSize();
    const bool isValid = item && item->data(LinWidgetManifestValidRole).toBool();
    const QString manifestError = item ? item->data(LinWidgetManifestErrorRole).toString() : QString();

    // Top row: Name + Version chip + Type chip + Status pill + Action buttons
    auto* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(8);

    auto* nameLabel = new QLabel(name.isEmpty() ? id : name, row);
    nameLabel->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 13px; font-weight: 700; color: #0f172a; background: transparent;"
    ).arg(AppFonts::uiCssFamily()));
    topRow->addWidget(nameLabel);

    if (!version.isEmpty()) {
        auto* verLabel = new QLabel(QStringLiteral("v%1").arg(version), row);
        verLabel->setStyleSheet(QStringLiteral(
            "font-family: %1; font-size: 11px; font-weight: 600; color: #475569;"
            "background: #f1f5f9; border-radius: 4px; padding: 1px 6px;"
        ).arg(AppFonts::uiCssFamily()));
        topRow->addWidget(verLabel);
    }

    if (isDevelopment) {
        auto* devBadge = new QLabel(QStringLiteral("开发"), row);
        devBadge->setStyleSheet(QStringLiteral(
            "font-family: %1; font-size: 10px; font-weight: 700; color: #7c3aed;"
            "background: #ede9fe; border-radius: 4px; padding: 1px 6px;"
        ).arg(AppFonts::uiCssFamily()));
        topRow->addWidget(devBadge);
    } else {
        auto* offBadge = new QLabel(QStringLiteral("官方"), row);
        offBadge->setStyleSheet(QStringLiteral(
            "font-family: %1; font-size: 10px; font-weight: 700; color: #0284c7;"
            "background: #e0f2fe; border-radius: 4px; padding: 1px 6px;"
        ).arg(AppFonts::uiCssFamily()));
        topRow->addWidget(offBadge);
    }

    auto* statusBadge = new QLabel(row);
    if (!isValid) {
        statusBadge->setText(QStringLiteral("⚠️ 无效配置"));
        statusBadge->setStyleSheet(QStringLiteral(
            "font-family: %1; font-size: 11px; font-weight: 600; color: #dc2626;"
            "background: #fee2e2; border-radius: 4px; padding: 1px 6px;"
        ).arg(AppFonts::uiCssFamily()));
    } else if (running) {
        statusBadge->setText(QStringLiteral("● 运行中"));
        statusBadge->setStyleSheet(QStringLiteral(
            "font-family: %1; font-size: 11px; font-weight: 600; color: #15803d;"
            "background: #dcfce7; border-radius: 4px; padding: 1px 6px;"
        ).arg(AppFonts::uiCssFamily()));
    } else {
        statusBadge->setText(QStringLiteral("○ 已停止"));
        statusBadge->setStyleSheet(QStringLiteral(
            "font-family: %1; font-size: 11px; font-weight: 500; color: #64748b;"
            "background: #f1f5f9; border-radius: 4px; padding: 1px 6px;"
        ).arg(AppFonts::uiCssFamily()));
    }
    topRow->addWidget(statusBadge);

    topRow->addStretch(1);

    // Trailing buttons: [Load/Unload], [Reload], [Settings]
    auto* actionButton = new QToolButton(row);
    actionButton->setObjectName(QStringLiteral("linWidgetItemAction"));
    actionButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    actionButton->setCursor(Qt::PointingHandCursor);
    actionButton->setText(running ? QStringLiteral("停止") : QStringLiteral("运行"));
    actionButton->setStyleSheet(QStringLiteral(
        "QToolButton {"
        "  min-width: 50px; height: 26px; padding: 0 8px; border-radius: 6px;"
        "  font-family: %1; font-size: 11px; font-weight: 700;"
        "  background: %2; color: %3; border: 1px solid %4;"
        "}"
        "QToolButton:hover { background: %5; }"
        "QToolButton:disabled { background: #f1f5f9; color: #94a3b8; border-color: rgba(15, 23, 42, 0.08); }")
        .arg(AppFonts::uiCssFamily(),
             running ? QStringLiteral("#f8fafc") : QStringLiteral("#0067c0"),
             running ? QStringLiteral("#475569") : QStringLiteral("#ffffff"),
             running ? QStringLiteral("rgba(15, 23, 42, 0.14)") : QStringLiteral("#005fb8"),
             running ? QStringLiteral("#f1f5f9") : QStringLiteral("#1975c5")));
    topRow->addWidget(actionButton);

    auto* reloadButton = new QToolButton(row);
    reloadButton->setObjectName(QStringLiteral("linWidgetItemReload"));
    reloadButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    reloadButton->setCursor(Qt::PointingHandCursor);
    reloadButton->setText(QStringLiteral("重启"));
    reloadButton->setEnabled(running);
    reloadButton->setStyleSheet(QStringLiteral(
        "QToolButton {"
        "  min-width: 48px; height: 26px; padding: 0 6px; border-radius: 6px;"
        "  font-family: %1; font-size: 11px; font-weight: 600;"
        "  background: #ffffff; color: #1e293b; border: 1px solid rgba(15, 23, 42, 0.14);"
        "}"
        "QToolButton:hover { background: #f1f5f9; }"
        "QToolButton:disabled { background: #f8fafc; color: #cbd5e1; border-color: rgba(15, 23, 42, 0.06); }")
        .arg(AppFonts::uiCssFamily()));
    topRow->addWidget(reloadButton);

    auto* settingsButton = new QToolButton(row);
    settingsButton->setObjectName(QStringLiteral("linWidgetItemSettings"));
    settingsButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    settingsButton->setCursor(Qt::PointingHandCursor);
    settingsButton->setText(QStringLiteral("设置"));
    const QString settingsPath = item ? item->data(LinWidgetSettingsRole).toString().trimmed() : QString();
    const bool hasSettings = item && !settingsPath.isEmpty() && QFileInfo::exists(QDir(packagePath).filePath(settingsPath));
    settingsButton->setVisible(!settingsPath.isEmpty());
    settingsButton->setEnabled(hasSettings);
    settingsButton->setToolTip(hasSettings ? QStringLiteral("打开组件专属设置") : QStringLiteral("未配置设置页面"));
    settingsButton->setStyleSheet(reloadButton->styleSheet());
    topRow->addWidget(settingsButton);

    layout->addLayout(topRow);

    // Bottom row: ID · Runtime · Grid Size
    auto* bottomRow = new QHBoxLayout();
    bottomRow->setContentsMargins(0, 0, 0, 0);
    bottomRow->setSpacing(6);

    auto* metaLabel = new QLabel(row);
    if (!isValid) {
        metaLabel->setText(QStringLiteral("配置异常: %1").arg(manifestError));
        metaLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #dc2626; font-weight: 500;"));
    } else {
        QString sizeStr = (defaultSize.width() > 0 && defaultSize.height() > 0)
            ? QStringLiteral("%1×%2 单元格").arg(defaultSize.width()).arg(defaultSize.height())
            : QStringLiteral("自适应尺寸");
        metaLabel->setText(QStringLiteral("%1  ·  %2  ·  %3")
            .arg(id)
            .arg(runtimeType.isEmpty() ? QStringLiteral("CEF OSR") : runtimeType.toUpper())
            .arg(sizeStr));
        metaLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #64748b; font-weight: 500;"));
    }
    bottomRow->addWidget(metaLabel);
    bottomRow->addStretch(1);
    layout->addLayout(bottomRow);

    auto selectItem = [this, item]() {
        if (!item || !m_linWidgetsList) {
            return false;
        }
        m_linWidgetsList->setCurrentItem(item);
        return true;
    };

    auto runAction = [this, item, selectItem]() {
        if (!selectItem()) {
            return;
        }
        if (item && !item->data(LinWidgetManifestValidRole).toBool()) {
            if (m_linWidgetsPageStatusLabel) {
                m_linWidgetsPageStatusLabel->setText(QStringLiteral("禁止加载: %1")
                    .arg(item->data(LinWidgetManifestErrorRole).toString()));
            }
            updateLinWidgetsButtons();
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
        auto& bridge = LinWidgetRuntimeBridge::instance();
        if (!bridge.isLoaded() || !bridge.restartWidget(instanceId)) {
            if (m_linWidgetsPageStatusLabel) {
                m_linWidgetsPageStatusLabel->setText("重启组件失败: " + instanceId);
            }
            return;
        }
        if (m_linWidgetsPageStatusLabel) {
            m_linWidgetsPageStatusLabel->setText("已重启小组件: " + instanceId);
        }
        updateLinWidgetDetailsPanel();
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
                m_linWidgetsPageStatusLabel->setText("打开设置失败: " + errorMessage);
            }
            return;
        }
        if (!LinWidgetRuntimeBridge::instance().openWidgetSettings(packagePath, selectedLinWidgetInstanceIdForLog())) {
            if (m_linWidgetsPageStatusLabel) {
                m_linWidgetsPageStatusLabel->setText("打开设置失败 (运行环境未就绪)");
            }
        }
    });

    const bool runtimeEnabled = LinWidgetRuntimeBridge::instance().isEnabled();
    actionButton->setEnabled(runtimeEnabled);
    reloadButton->setEnabled(runtimeEnabled && running);

    return row;
}

QListWidget* ListViewDebugPanel::activeLinWidgetList() const
{
    return m_linWidgetsList;
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

QWidget* ListViewDebugPanel::createLinWidgetsPage()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 28);
    layout->setSpacing(16);

    layout->addWidget(createPageTitleLabel(QStringLiteral("壁纸与组件底层诊断")));
    layout->addWidget(createPageSubtitleLabel(QStringLiteral("多进程沙箱隔离底座、CEF OSR 离屏渲染管道与桌面组件实例调测。")));

    m_linWidgetsPageStatusLabel = new QLabel(page);
    m_linWidgetsPageStatusLabel->setWordWrap(true);
    m_linWidgetsPageStatusLabel->setText(QStringLiteral("壁纸与小组件多进程隔离运行时就绪 · 硬件图形加速正常"));
    m_linWidgetsPageStatusLabel->setStyleSheet(QStringLiteral(
        "background: #eff6ff;"
        "border: 1px solid #bfdbfe;"
        "border-radius: 8px;"
        "padding: 10px 14px;"
        "font-family: %1;"
        "font-size: 12px;"
        "font-weight: 500;"
        "color: #1e40af;"
    ).arg(AppFonts::uiCssFamily()));
    layout->addWidget(m_linWidgetsPageStatusLabel);

    QSettings settings(debugConfigPath(), QSettings::IniFormat);

    // Top Control Toolbar Card
    auto* controlBar = new QWidget(page);
    controlBar->setObjectName(QStringLiteral("linWidgetControlBar"));
    controlBar->setStyleSheet(QStringLiteral(
        "QWidget#linWidgetControlBar {"
        "  background: #ffffff;"
        "  border: 1px solid rgba(15, 23, 42, 0.08);"
        "  border-radius: 10px;"
        "}"));
    auto* controlBarLayout = new QHBoxLayout(controlBar);
    controlBarLayout->setContentsMargins(14, 10, 14, 10);
    controlBarLayout->setSpacing(14);

    auto addToggleRow = [controlBar](const QString& name, const QString& tooltip, FluentToggleSwitch* toggle) -> QWidget* {
        auto* item = new QWidget(controlBar);
        item->setToolTip(tooltip);
        auto* iLayout = new QHBoxLayout(item);
        iLayout->setContentsMargins(0, 0, 0, 0);
        iLayout->setSpacing(6);
        auto* label = new QLabel(name, item);
        label->setToolTip(tooltip);
        label->setStyleSheet(QStringLiteral(
            "font-family: %1; font-size: 12px; font-weight: 600; color: #334155; background: transparent;"
        ).arg(AppFonts::uiCssFamily()));
        toggle->setParent(item);
        toggle->setToolTip(tooltip);
        iLayout->addWidget(label, 0, Qt::AlignVCenter);
        iLayout->addWidget(toggle, 0, Qt::AlignVCenter);
        return item;
    };

    // 1. 组件系统底座开关 (必须保留)
    m_linWidgetEnabledToggle = new FluentToggleSwitch(page);
    auto& bridge = LinWidgetRuntimeBridge::instance();
    bool cefOsrInit = bridge.isLoaded() ? bridge.isCefOsrEnabled() : settings.value("LinWidgets/CefOsrEnabled", true).toBool();
    m_linWidgetEnabledToggle->setChecked(cefOsrInit, false);
    connect(m_linWidgetEnabledToggle, &FluentToggleSwitch::toggled, this, [this](bool checked) {
        QSettings s(debugConfigPath(), QSettings::IniFormat);
        s.setValue("LinWidgets/CefOsrEnabled", checked);
        if (checked) {
            s.setValue("LinWidgets/Enabled", true);
        }
        s.sync();

        auto& b = LinWidgetRuntimeBridge::instance();
        if (b.isLoaded()) {
            if (checked && !b.isEnabled()) {
                b.setEnabled(true);
            }
            b.setCefOsrEnabled(checked);
            if (m_linWidgetsPageStatusLabel) {
                m_linWidgetsPageStatusLabel->setText(checked
                    ? QStringLiteral("壁纸与小组件运行支持已启用 · 沙箱多进程已就绪。")
                    : QStringLiteral("壁纸与小组件运行支持已停用，已退回原生桌面图标模式。"));
            }
        }
        refreshLinWidgetRunningMaps();
        updateSelectedLinWidgetItemText();
        updateLinWidgetsButtons();
        updateLinWidgetDetailsPanel();
        if (m_listView) {
            m_listView->update();
        }
    });

    // 2. 显示组件边框 (必须保留)
    m_linWidgetBoundsToggle = new FluentToggleSwitch(page);
    m_linWidgetBoundsToggle->setChecked(settings.value("LinWidgets/ShowWidgetBounds", false).toBool(), false);
    connect(m_linWidgetBoundsToggle, &FluentToggleSwitch::toggled, this, [this](bool checked) {
        QSettings s(debugConfigPath(), QSettings::IniFormat);
        s.setValue("LinWidgets/ShowWidgetBounds", checked);
        s.remove("LinWidgets/ShowWebViewBounds");
        s.sync();
        auto& b = LinWidgetRuntimeBridge::instance();
        if (b.isLoaded()) {
            b.setShowWidgetBounds(checked);
        }
        if (m_linWidgetsPageStatusLabel) {
            m_linWidgetsPageStatusLabel->setText(checked
                ? QStringLiteral("小组件视口矩形辅助边界已开启（青色线框）。")
                : QStringLiteral("小组件视口边界已关闭。"));
        }
    });

    // 3. 桌面网格占用高亮
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
                ? QStringLiteral("桌面被小组件占用的单元格已高亮标出。")
                : QStringLiteral("桌面单元格占用高亮已关闭。"));
        }
    });

    auto* hubButton = createActionButton(QStringLiteral("管理中心 (Hub)"), true, false);
    connect(hubButton, &QPushButton::clicked, []() { LinHub::WallpaperStudioWindow::showStudio(); });

    m_linWidgetsRuntimeToggleButton = createActionButton(QStringLiteral("安装组件包"), true, false);
    connect(m_linWidgetsRuntimeToggleButton, &QPushButton::clicked, this, [this]() {
        auto& installer = RuntimeInstaller::instance();
        if (installer.isRuntimeInstalled()) {
            QString err;
            if (installer.uninstallRuntime(false, &err)) {
                if (m_linWidgetsPageStatusLabel) {
                    m_linWidgetsPageStatusLabel->setText(
                        QStringLiteral("组件运行环境已卸载，当前处于未安装状态，可前往设置中心测试远程下载与安装。"));
                }
            } else {
                if (m_linWidgetsPageStatusLabel) {
                    m_linWidgetsPageStatusLabel->setText(QStringLiteral("卸载失败: %1").arg(err));
                }
            }
        } else {
            QString err;
            if (installer.installRuntimeFromLocalBuild(&err)) {
                if (m_linWidgetsPageStatusLabel) {
                    m_linWidgetsPageStatusLabel->setText(
                        QStringLiteral("已从本地构建目录一键安装组件运行底座，多进程沙箱隔离已就绪！"));
                }
            } else {
                if (m_linWidgetsPageStatusLabel) {
                    m_linWidgetsPageStatusLabel->setText(QStringLiteral("本地安装失败: %1").arg(err));
                }
            }
        }
        updateLinWidgetsButtons();
        reloadLinWidgetsList();
    });

    connect(&RuntimeInstaller::instance(), &RuntimeInstaller::statusChanged, this, [this](RuntimeInstaller::Status) {
        updateLinWidgetsButtons();
        updateLinWidgetRuntimeInfoCard();
        reloadLinWidgetsList();
    });
    connect(&RuntimeInstaller::instance(), &RuntimeInstaller::runtimeInstalledAndReady, this, [this]() {
        updateLinWidgetsButtons();
        updateLinWidgetRuntimeInfoCard();
        reloadLinWidgetsList();
    });

    auto* refreshButton = createActionButton(QStringLiteral("刷新"), false, false);
    m_linWidgetsAddDevButton = createActionButton(QStringLiteral("+ 导入工程"), false, false);
    m_linWidgetsResetBundledButton = createActionButton(QStringLiteral("重置默认组件"), false, false);
    m_linWidgetsOpenLogsButton = createActionButton(QStringLiteral("查看日志"), false, false);

    controlBarLayout->addWidget(hubButton);
    controlBarLayout->addWidget(addToggleRow(QStringLiteral("运行底座"), QStringLiteral("开关壁纸与小组件底层运行时 (CEF OSR)"), m_linWidgetEnabledToggle));
    controlBarLayout->addWidget(addToggleRow(QStringLiteral("组件边框"), QStringLiteral("绘制组件外框与像素视口矩形辅助边界"), m_linWidgetBoundsToggle));
    controlBarLayout->addWidget(addToggleRow(QStringLiteral("网格高亮"), QStringLiteral("高亮桌面被组件占用的排布单元格"), m_linWidgetBlockedCellsToggle));
    controlBarLayout->addStretch(1);
    controlBarLayout->addWidget(m_linWidgetsRuntimeToggleButton);
    controlBarLayout->addWidget(m_linWidgetsAddDevButton);
    controlBarLayout->addWidget(refreshButton);
    controlBarLayout->addWidget(m_linWidgetsResetBundledButton);
    controlBarLayout->addWidget(m_linWidgetsOpenLogsButton);

    layout->addWidget(controlBar);

    // Two-Column Split Content
    m_linWidgetsContentGrid = new QGridLayout();
    m_linWidgetsContentGrid->setContentsMargins(0, 0, 0, 0);
    m_linWidgetsContentGrid->setHorizontalSpacing(16);
    m_linWidgetsContentGrid->setVerticalSpacing(16);

    // Left Column: Unified Widgets List
    m_linWidgetsListsPanel = new QWidget(page);
    auto* listsLayout = new QVBoxLayout(m_linWidgetsListsPanel);
    listsLayout->setContentsMargins(0, 0, 0, 0);
    listsLayout->setSpacing(8);

    m_linWidgetFilterTabBar = new QTabBar(m_linWidgetsListsPanel);
    m_linWidgetFilterTabBar->setObjectName(QStringLiteral("linWidgetFilterTabBar"));
    m_linWidgetFilterTabBar->addTab(QStringLiteral("全部组件"));
    m_linWidgetFilterTabBar->addTab(QStringLiteral("官方随包"));
    m_linWidgetFilterTabBar->addTab(QStringLiteral("开发工程"));
    m_linWidgetFilterTabBar->setDrawBase(false);
    m_linWidgetFilterTabBar->setExpanding(false);
    listsLayout->addWidget(m_linWidgetFilterTabBar, 0, Qt::AlignLeft);

    const QString listStyle = QStringLiteral(
        "QListWidget {"
        "  background: #ffffff;"
        "  border: 1px solid rgba(15, 23, 42, 0.08);"
        "  border-radius: 10px;"
        "  padding: 6px;"
        "  font-family: %1;"
        "  color: #0f172a;"
        "}"
        "QListWidget::item {"
        "  min-height: 64px;"
        "  margin: 2px 0px;"
        "  border-radius: 8px;"
        "  background: #f8fafc;"
        "  border: 1px solid rgba(15, 23, 42, 0.05);"
        "}"
        "QListWidget::item:hover { background: #f1f5f9; border-color: rgba(15, 23, 42, 0.12); }"
        "QListWidget::item:selected { background: #eff6ff; border: 1px solid #bfdbfe; }")
        .arg(AppFonts::uiCssFamily());

    m_linWidgetsList = new QListWidget(m_linWidgetsListsPanel);
    m_linWidgetsList->setMinimumHeight(440);
    m_linWidgetsList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_linWidgetsList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_linWidgetsList->setMouseTracking(true);
    m_linWidgetsList->setStyleSheet(listStyle);
    listsLayout->addWidget(m_linWidgetsList, 1);

    connect(m_linWidgetFilterTabBar, &QTabBar::currentChanged, this, [this](int index) {
        if (!m_linWidgetsList) return;
        for (int i = 0; i < m_linWidgetsList->count(); ++i) {
            QListWidgetItem* item = m_linWidgetsList->item(i);
            const bool isDev = item->data(LinWidgetDevelopmentRole).toBool();
            if (index == 1) item->setHidden(isDev);
            else if (index == 2) item->setHidden(!isDev);
            else item->setHidden(false);
        }
        updateLinWidgetsButtons();
        updateLinWidgetDetailsPanel();
    });

    // Right Column: Diagnostic Workstation & Controls
    m_linWidgetsLogPanel = new QWidget(page);
    auto* rightColLayout = new QVBoxLayout(m_linWidgetsLogPanel);
    rightColLayout->setContentsMargins(0, 0, 0, 0);
    rightColLayout->setSpacing(14);

    const QString cardStyle = QStringLiteral(
        "QWidget {"
        "  background: #ffffff;"
        "  border: 1px solid rgba(15, 23, 42, 0.08);"
        "  border-radius: 10px;"
        "}");

    // Card 1: Runtime Base Diagnostics & Health (全新升级)
    auto* runtimeCard = new QWidget(m_linWidgetsLogPanel);
    runtimeCard->setStyleSheet(cardStyle);
    auto* runtimeLayout = new QVBoxLayout(runtimeCard);
    runtimeLayout->setContentsMargins(16, 14, 16, 14);
    runtimeLayout->setSpacing(8);

    auto* runtimeHeader = createSectionHeaderWidget(QString::fromUtf8(u8"\uE7F4"), QStringLiteral("运行时健康诊断"), QStringLiteral("多进程隔离底座状态、物理挂载路径与硬件加速"));
    runtimeLayout->addWidget(runtimeHeader);

    m_linWidgetRuntimeInfoLabel = new QLabel(runtimeCard);
    m_linWidgetRuntimeInfoLabel->setTextFormat(Qt::RichText);
    m_linWidgetRuntimeInfoLabel->setWordWrap(true);
    m_linWidgetRuntimeInfoLabel->setStyleSheet(QStringLiteral("font-family: %1; background: transparent; border: none;").arg(AppFonts::uiCssFamily()));
    runtimeLayout->addWidget(m_linWidgetRuntimeInfoLabel);
    rightColLayout->addWidget(runtimeCard);

    // Card 2: Widget Inspector
    auto* detailsCard = new QWidget(m_linWidgetsLogPanel);
    detailsCard->setStyleSheet(cardStyle);
    auto* detailsLayout = new QVBoxLayout(detailsCard);
    detailsLayout->setContentsMargins(16, 14, 16, 14);
    detailsLayout->setSpacing(8);

    auto* detailsHeader = createSectionHeaderWidget(QString::fromUtf8(u8"\uE7B5"), QStringLiteral("组件属性与沙箱检查"), QStringLiteral("实时窗口坐标、Manifest 配置与已声明权限"));
    detailsLayout->addWidget(detailsHeader);

    m_linWidgetDetailsLabel = new QLabel(detailsCard);
    m_linWidgetDetailsLabel->setTextFormat(Qt::RichText);
    m_linWidgetDetailsLabel->setWordWrap(true);
    m_linWidgetDetailsLabel->setStyleSheet(QStringLiteral("font-family: %1; background: transparent; border: none;").arg(AppFonts::uiCssFamily()));
    detailsLayout->addWidget(m_linWidgetDetailsLabel);
    rightColLayout->addWidget(detailsCard);

    // Card 3: Developer Tools & Actions
    auto* actionsCard = new QWidget(m_linWidgetsLogPanel);
    actionsCard->setStyleSheet(cardStyle);
    auto* actionsLayout = new QVBoxLayout(actionsCard);
    actionsLayout->setContentsMargins(16, 14, 16, 14);
    actionsLayout->setSpacing(10);

    auto* actionsHeader = createSectionHeaderWidget(QString::fromUtf8(u8"\uE7BE"), QStringLiteral("调试与维护工具"), QStringLiteral("沙箱热调测、配置窗口与诊断报告"));
    actionsLayout->addWidget(actionsHeader);

    auto* row1 = new QHBoxLayout();
    row1->setContentsMargins(0, 0, 0, 0);
    row1->setSpacing(10);

    m_linWidgetsDevToolsButton = createActionButton(QStringLiteral("打开 DevTools (F12)"), true, false);
    m_linWidgetsDevToolsButton->setMinimumHeight(32);
    m_linWidgetsDevToolsButton->setToolTip(QStringLiteral("打开选中组件的独立 Chromium 开发者控制台 (Console / Elements / Network)"));
    connect(m_linWidgetsDevToolsButton, &QPushButton::clicked, this, [this]() {
        const QString instanceId = selectedLinWidgetRunningInstanceId();
        if (!instanceId.isEmpty()) {
            LinWidgetRuntimeBridge::instance().showDevTools(instanceId);
        }
    });
    row1->addWidget(m_linWidgetsDevToolsButton);

    m_linWidgetsSettingsButton = createActionButton(QStringLiteral("组件设置"), false, false);
    m_linWidgetsSettingsButton->setMinimumHeight(32);
    connect(m_linWidgetsSettingsButton, &QPushButton::clicked, this, [this]() {
        const QString packagePath = selectedLinWidgetPackagePath();
        const QString instanceId = selectedLinWidgetRunningInstanceId();
        if (!packagePath.isEmpty()) {
            LinWidgetRuntimeBridge::instance().openWidgetSettings(packagePath, instanceId);
        }
    });
    row1->addWidget(m_linWidgetsSettingsButton);

    m_linWidgetsRestartButton = createActionButton(QStringLiteral("重启组件"), false, false);
    m_linWidgetsRestartButton->setMinimumHeight(32);
    connect(m_linWidgetsRestartButton, &QPushButton::clicked, this, [this]() {
        const QString instanceId = selectedLinWidgetRunningInstanceId();
        if (!instanceId.isEmpty()) {
            LinWidgetRuntimeBridge::instance().restartWidget(instanceId);
            updateLinWidgetDetailsPanel();
        }
    });
    row1->addWidget(m_linWidgetsRestartButton);
    actionsLayout->addLayout(row1);

    auto* row2 = new QHBoxLayout();
    row2->setContentsMargins(0, 0, 0, 0);
    row2->setSpacing(10);

    m_linWidgetsOpenFolderButton = createActionButton(QStringLiteral("打开源码目录"), false, false);
    m_linWidgetsOpenFolderButton->setMinimumHeight(32);
    connect(m_linWidgetsOpenFolderButton, &QPushButton::clicked, this, [this]() {
        const QString packagePath = selectedLinWidgetPackagePath();
        if (!packagePath.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(packagePath));
        }
    });
    row2->addWidget(m_linWidgetsOpenFolderButton);

    auto* exportDiagBtn = createActionButton(QStringLiteral("复制诊断报告"), false, false);
    exportDiagBtn->setMinimumHeight(32);
    exportDiagBtn->setToolTip(QStringLiteral("将组件 Manifest、坐标状态与诊断日志打包复制到剪贴板"));
    connect(exportDiagBtn, &QPushButton::clicked, this, &ListViewDebugPanel::exportLinWidgetDiagnostics);
    row2->addWidget(exportDiagBtn);

    m_linWidgetsDeleteButton = createActionButton(QStringLiteral("删除组件"), false, true);
    m_linWidgetsDeleteButton->setMinimumHeight(32);
    row2->addWidget(m_linWidgetsDeleteButton);
    actionsLayout->addLayout(row2);

    rightColLayout->addWidget(actionsCard);
    rightColLayout->addStretch(1);

    layout->addLayout(m_linWidgetsContentGrid, 1);

    connect(refreshButton, &QPushButton::clicked, this, &ListViewDebugPanel::reloadLinWidgetsList);
    connect(m_linWidgetsList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem*) {
        updateLinWidgetsButtons();
        updateLinWidgetDetailsPanel();
    });

    connect(m_linWidgetsAddDevButton, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("选择 LinWidget 开发工程目录"));
        if (dir.isEmpty()) return;
        auto& registry = LinWidget::WidgetRegistryManager::instance();
        QString errorMessage;
        registry.load();
        if (!registry.addDevelopmentProject(dir, &errorMessage)) {
            m_linWidgetsPageStatusLabel->setText(tr("添加工程失败: %1").arg(errorMessage));
            return;
        }
        registry.save();
        m_linWidgetsPageStatusLabel->setText(tr("已成功导入开发工程: %1").arg(dir));
        reloadLinWidgetsList();
    });

    connect(m_linWidgetsOpenLogsButton, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::linWidgetLogsDir()));
    });

    connect(m_linWidgetsResetBundledButton, &QPushButton::clicked, this, [this]() {
        QString errorMessage;
        const QStringList resetFolders = resetAllBundledLinWidgets(&errorMessage);
        if (resetFolders.isEmpty() && !errorMessage.isEmpty()) {
            m_linWidgetsPageStatusLabel->setText(tr("恢复默认组件失败: %1").arg(errorMessage));
            return;
        }
        auto& registry = LinWidget::WidgetRegistryManager::instance();
        registry.load();
        for (const QString& folderName : resetFolders) {
            registry.upsertFromPackage(QDir(userLinWidgetsDir()).filePath(folderName), &errorMessage);
        }
        registry.save();
        m_linWidgetsPageStatusLabel->setText(QStringLiteral("已从安装包资源恢复 %1 个默认官方小组件。").arg(resetFolders.size()));
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

        const QString modeText = isDevelopment ? QStringLiteral("开发工程") : QStringLiteral("已安装组件");
        if (!FluentDialog::confirm(
                this,
                QStringLiteral("删除组件"),
                QStringLiteral("确认要从系统中移除该 %1 吗？\n\n%2\n\n这将会清理注册表记录及磁盘文件。")
                    .arg(modeText, packagePath),
                QStringLiteral("删除"),
                QStringLiteral("取消"),
                true)) {
            return;
        }

        const QString runningInstanceId = m_linWidgetInstancesById.value(
            registryKey,
            m_linWidgetInstancesByPackage.value(linWidgetPackageKey(packagePath)));
        if (!runningInstanceId.isEmpty()) {
            m_linWidgetsPageStatusLabel->setText(QStringLiteral("请先停止 (Unload) 正在运行的组件，然后再执行删除。"));
            updateLinWidgetsButtons();
            return;
        }
        removeLinWidgetLayoutState(registryKey, packagePath);

        auto& registry = LinWidget::WidgetRegistryManager::instance();
        QString errorMessage;
        registry.load();
        if (isDevelopment) {
            registry.removeDevelopmentProject(registryId);
        } else {
            registry.removeById(registryId);
        }
        registry.save();

        if (!removeLinWidgetDirectory(packagePath, &errorMessage)) {
            m_linWidgetsPageStatusLabel->setText(QStringLiteral("注册表已移除，但磁盘目录删除失败: %1").arg(errorMessage));
            reloadLinWidgetsList();
            return;
        }

        m_linWidgetsPageStatusLabel->setText(QStringLiteral("已成功删除组件: %1").arg(registryId));
        reloadLinWidgetsList();
    });

    updateAdaptiveLayout();
    reloadLinWidgetsList();
    updateLinWidgetDetailsPanel();
    return page;
}

QWidget* ListViewDebugPanel::createAIPage()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(18);

    layout->addWidget(createPageTitleLabel("AI"));
    layout->addWidget(createPageSubtitleLabel("Configure an AI provider and send a quick API test request from the debug console."));
    layout->addWidget(createPageSubtitleLabel(
        QStringLiteral("Choose a provider, enter an API key, then select or type a model. Base URL is generated from the provider and only needs changes for proxy or self hosted endpoints.")));

    const QString fieldStyle = QStringLiteral(
        "QLineEdit, QComboBox, QPlainTextEdit {"
        "  background: #ffffff;"
        "  border: 1px solid rgba(15, 23, 42, 0.14);"
        "  border-radius: 8px;"
        "  padding: 8px 10px;"
        "  font-family: %1;"
        "  font-size: 13px;"
        "  color: #111827;"
        "}"
        "QLineEdit:focus, QComboBox:focus, QPlainTextEdit:focus {"
        "  border: 1px solid #0f6cbd;"
        "}"
        "QComboBox::drop-down {"
        "  width: 26px;"
        "  border: none;"
        "}").arg(AppFonts::uiCssFamily());

    auto* configPanel = new QWidget(page);
    configPanel->setObjectName(QStringLiteral("aiConfigPanel"));
    configPanel->setAttribute(Qt::WA_StyledBackground, true);
    configPanel->setStyleSheet(QStringLiteral(
        "QWidget#aiConfigPanel {"
        "  background: #ffffff;"
        "  border: 1px solid rgba(15, 23, 42, 0.10);"
        "  border-radius: 10px;"
        "}"));
    auto* configLayout = new QGridLayout(configPanel);
    configLayout->setContentsMargins(16, 16, 16, 16);
    configLayout->setHorizontalSpacing(14);
    configLayout->setVerticalSpacing(12);

    auto createFieldLabel = [](const QString& text, QWidget* parent) {
        auto* label = new QLabel(text, parent);
        label->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: 600; color: #374151;"));
        return label;
    };

    m_aiProviderCombo = new QComboBox(configPanel);
    m_aiProviderCombo->addItem(QStringLiteral("OpenAI"), static_cast<int>(AI::AIClient::ProviderKind::OpenAI));
    m_aiProviderCombo->addItem(QStringLiteral("Claude"), static_cast<int>(AI::AIClient::ProviderKind::Claude));
    m_aiProviderCombo->addItem(QStringLiteral("Ollama"), static_cast<int>(AI::AIClient::ProviderKind::Ollama));
    m_aiProviderCombo->addItem(QStringLiteral("Google"), static_cast<int>(AI::AIClient::ProviderKind::Google));
    m_aiProviderCombo->addItem(QStringLiteral("DeepSeek"), static_cast<int>(AI::AIClient::ProviderKind::DeepSeek));
    m_aiProviderCombo->addItem(QStringLiteral("Qwen"), static_cast<int>(AI::AIClient::ProviderKind::Qwen));
    m_aiProviderCombo->setStyleSheet(fieldStyle);

    m_aiApiKeyEdit = new QLineEdit(configPanel);
    m_aiApiKeyEdit->setEchoMode(QLineEdit::Password);
    m_aiApiKeyEdit->setPlaceholderText(QStringLiteral("API key, Ollama can stay empty"));
    m_aiApiKeyEdit->setStyleSheet(fieldStyle);

    m_aiModelCombo = new QComboBox(configPanel);
    m_aiModelCombo->setEditable(true);
    m_aiModelCombo->setInsertPolicy(QComboBox::NoInsert);
    m_aiModelCombo->setStyleSheet(fieldStyle);

    configLayout->addWidget(createFieldLabel(QStringLiteral("Provider"), configPanel), 0, 0);
    configLayout->addWidget(m_aiProviderCombo, 1, 0);
    configLayout->addWidget(createFieldLabel(QStringLiteral("Model"), configPanel), 0, 1);
    configLayout->addWidget(m_aiModelCombo, 1, 1);
    configLayout->addWidget(createFieldLabel(QStringLiteral("API Key"), configPanel), 2, 0, 1, 2);
    configLayout->addWidget(m_aiApiKeyEdit, 3, 0, 1, 2);
    configLayout->setColumnStretch(0, 1);
    configLayout->setColumnStretch(1, 1);
    layout->addWidget(configPanel);

    auto* advancedPanel = new QWidget(page);
    auto* advancedLayout = new QVBoxLayout(advancedPanel);
    advancedLayout->setContentsMargins(0, 0, 0, 0);
    advancedLayout->setSpacing(8);
    auto* advancedToggle = createActionButton(QStringLiteral("Advanced Base URL"), false, false);
    advancedToggle->setCheckable(true);
    advancedToggle->setChecked(false);
    advancedToggle->setFixedWidth(170);
    m_aiBaseUrlEdit = new QLineEdit(advancedPanel);
    m_aiBaseUrlEdit->setPlaceholderText(QStringLiteral("Provider default Base URL"));
    m_aiBaseUrlEdit->setStyleSheet(fieldStyle);
    m_aiBaseUrlEdit->setVisible(false);
    advancedLayout->addWidget(advancedToggle, 0, Qt::AlignLeft);
    advancedLayout->addWidget(m_aiBaseUrlEdit);
    connect(advancedToggle, &QPushButton::toggled, m_aiBaseUrlEdit, &QLineEdit::setVisible);
    layout->addWidget(advancedPanel);

    m_aiStatusLabel = createPageSubtitleLabel(QStringLiteral("Ready."));
    layout->addWidget(m_aiStatusLabel);

    m_aiOutputView = new QPlainTextEdit(page);
    m_aiOutputView->setReadOnly(true);
    m_aiOutputView->setMinimumHeight(340);
    m_aiOutputView->setStyleSheet(QStringLiteral(
        "QPlainTextEdit {"
        "  background: #0f172a;"
        "  color: #dbeafe;"
        "  border: 1px solid rgba(15, 23, 42, 0.16);"
        "  border-radius: 10px;"
        "  padding: 10px;"
        "  font-family: Consolas, 'Cascadia Mono', monospace;"
        "  font-size: 12px;"
        "}"));
    layout->addWidget(m_aiOutputView, 1);
    m_aiUserPromptEdit = new QPlainTextEdit(page);
    m_aiUserPromptEdit->setMinimumHeight(120);
    m_aiUserPromptEdit->setPlaceholderText(QStringLiteral("Type a message here"));
    m_aiUserPromptEdit->setStyleSheet(fieldStyle);
    layout->addWidget(m_aiUserPromptEdit);

    auto* actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(10);
    m_aiSaveButton = createActionButton(QStringLiteral("Save Config"), false, false);
    m_aiSendButton = createActionButton(QStringLiteral("Send"), true, false);
    m_aiStreamButton = createActionButton(QStringLiteral("Stream"), false, false);
    m_aiStopButton = createActionButton(QStringLiteral("Stop"), false, true);
    m_aiStopButton->setEnabled(false);
    m_aiClearButton = createActionButton(QStringLiteral("Clear"), false, false);
    actionRow->addWidget(m_aiSaveButton);
    actionRow->addWidget(m_aiSendButton);
    actionRow->addWidget(m_aiStreamButton);
    actionRow->addWidget(m_aiStopButton);
    actionRow->addWidget(m_aiClearButton);
    actionRow->addStretch();
    layout->addLayout(actionRow);

    m_aiCoreService = new AI::AICoreService(page);
    connect(m_aiCoreService, &AI::AICoreService::replyFinished, this, [this](const AI::ChatResponse& response) {
        if (response.requestId != m_aiActiveRequestId) {
            return;
        }
        appendAIChatLine(QStringLiteral("assistant"), response.text);
        if (m_aiStatusLabel) {
            m_aiStatusLabel->setText(QStringLiteral("Request finished. Provider: %1, status: %2.")
                .arg(response.provider)
                .arg(response.statusCode));
        }
        setAIRequestRunning(false);
    });
    connect(m_aiCoreService, &AI::AICoreService::streamDelta, this, [this](const QString& requestId, const QString& delta) {
        if (requestId != m_aiActiveRequestId || !m_aiOutputView) {
            return;
        }
        m_aiStreamingAssistantText += delta;
        m_aiOutputView->moveCursor(QTextCursor::End);
        m_aiOutputView->insertPlainText(delta);
        m_aiOutputView->moveCursor(QTextCursor::End);
        m_aiOutputView->ensureCursorVisible();
        m_aiOutputView->repaint();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    });
    connect(m_aiCoreService, &AI::AICoreService::streamFinished, this, [this](const AI::ChatResponse& response) {
        if (response.requestId != m_aiActiveRequestId) {
            return;
        }
        if (!m_aiStreamingAssistantText.trimmed().isEmpty()) {
            AI::ChatMessage assistantMessage;
            assistantMessage.role = QStringLiteral("assistant");
            assistantMessage.content = m_aiStreamingAssistantText;
            m_aiConversationMessages.append(assistantMessage);
        }
        if (m_aiOutputView) {
            m_aiOutputView->appendPlainText(QStringLiteral("\n\n[stream finished]"));
        }
        if (m_aiStatusLabel) {
            m_aiStatusLabel->setText(QStringLiteral("Stream finished. Provider: %1, status: %2.")
                .arg(response.provider)
                .arg(response.statusCode));
        }
        setAIRequestRunning(false);
    });
    connect(m_aiCoreService, &AI::AICoreService::requestFailed, this, [this](const AI::ChatResponse& response) {
        if (!m_aiActiveRequestId.isEmpty() && response.requestId != m_aiActiveRequestId) {
            return;
        }
        if (m_aiOutputView) {
            m_aiOutputView->appendPlainText(QStringLiteral("\n[error]\n%1").arg(response.error));
        }
        if (m_aiStatusLabel) {
            m_aiStatusLabel->setText(QStringLiteral("Request failed: %1").arg(response.error));
        }
        setAIRequestRunning(false);
        m_aiStreamingAssistantText.clear();
    });
    connect(m_aiCoreService, &AI::AICoreService::requestCanceled, this, [this](const QString& requestId) {
        if (requestId != m_aiActiveRequestId) {
            return;
        }
        if (m_aiOutputView) {
            m_aiOutputView->appendPlainText(QStringLiteral("\n[canceled]"));
        }
        if (m_aiStatusLabel) {
            m_aiStatusLabel->setText(QStringLiteral("Request canceled."));
        }
        setAIRequestRunning(false);
    });

    connect(m_aiProviderCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        loadAIProviderConfig(index);
    });
    connect(m_aiSaveButton, &QPushButton::clicked, this, [this]() {
        saveAIConfig();
        if (m_aiStatusLabel) {
            m_aiStatusLabel->setText(QStringLiteral("AI configuration saved."));
        }
    });
    connect(m_aiSendButton, &QPushButton::clicked, this, [this]() {
        AI::ChatRequest request = buildAIChatRequest();
        if (request.model.trimmed().isEmpty() || request.messages.isEmpty() || request.messages.last().content.trimmed().isEmpty()) {
            if (m_aiStatusLabel) {
                m_aiStatusLabel->setText(QStringLiteral("Model and message are required."));
            }
            return;
        }
        m_aiActiveRequestId = request.requestId;
        appendAIChatLine(QStringLiteral("user"), request.messages.last().content);
        if (m_aiUserPromptEdit) {
            m_aiUserPromptEdit->clear();
        }
        setAIRequestRunning(true);
        m_aiCoreService->sendMessage(request);
    });
    connect(m_aiStreamButton, &QPushButton::clicked, this, [this]() {
        AI::ChatRequest request = buildAIChatRequest();
        if (request.model.trimmed().isEmpty() || request.messages.isEmpty() || request.messages.last().content.trimmed().isEmpty()) {
            if (m_aiStatusLabel) {
                m_aiStatusLabel->setText(QStringLiteral("Model and message are required."));
            }
            return;
        }
        m_aiActiveRequestId = request.requestId;
        m_aiStreamingAssistantText.clear();
        appendAIChatLine(QStringLiteral("user"), request.messages.last().content);
        if (m_aiUserPromptEdit) {
            m_aiUserPromptEdit->clear();
        }
        if (m_aiOutputView) {
            m_aiOutputView->appendPlainText(QStringLiteral("\nassistant:\n"));
        }
        setAIRequestRunning(true);
        m_aiCoreService->streamReply(request);
    });
    connect(m_aiStopButton, &QPushButton::clicked, this, [this]() {
        if (!m_aiActiveRequestId.isEmpty() && m_aiCoreService) {
            m_aiCoreService->cancelRequest(m_aiActiveRequestId);
        }
    });
    connect(m_aiClearButton, &QPushButton::clicked, this, [this]() {
        clearAIConversation();
    });

    loadAIConfig();
    return page;
}

void ListViewDebugPanel::applyAIProviderDefaults(bool forceBaseUrl)
{
    if (!m_aiProviderCombo || !m_aiBaseUrlEdit || !m_aiModelCombo) {
        return;
    }

    const int providerIndex = m_aiProviderCombo->currentIndex();
    const QString currentModel = m_aiModelCombo->currentText().trimmed();
    const QStringList models = defaultAIModels(providerIndex);

    m_aiModelCombo->clear();
    m_aiModelCombo->addItems(models);
    if (!currentModel.isEmpty() && !models.contains(currentModel)) {
        m_aiModelCombo->insertItem(0, currentModel);
        m_aiModelCombo->setCurrentText(currentModel);
    } else {
        m_aiModelCombo->setCurrentText(models.isEmpty() ? QString() : models.first());
    }

    if (forceBaseUrl || m_aiBaseUrlEdit->text().trimmed().isEmpty()) {
        m_aiBaseUrlEdit->setText(defaultAIBaseUrl(providerIndex));
    }
}

void ListViewDebugPanel::loadAIProviderConfig(int providerIndex)
{
    if (!m_aiProviderCombo || !m_aiBaseUrlEdit || !m_aiApiKeyEdit || !m_aiModelCombo) {
        return;
    }

    applyAIProviderDefaults(true);
    const QJsonObject root = loadAIConfigRoot();
    const QJsonObject providers = root.value(QStringLiteral("providers")).toObject();
    const QJsonObject provider = providers.value(aiProviderSettingsPrefix(providerIndex)).toObject();

    const QString savedBaseUrl = provider.value(QStringLiteral("baseUrl")).toString().trimmed();
    if (!savedBaseUrl.isEmpty()) {
        m_aiBaseUrlEdit->setText(savedBaseUrl);
    }
    m_aiApiKeyEdit->setText(provider.value(QStringLiteral("apiKey")).toString());
    const QString savedModel = provider.value(QStringLiteral("model")).toString(defaultAIModel(providerIndex));
    if (!savedModel.trimmed().isEmpty()) {
        if (m_aiModelCombo->findText(savedModel) < 0) {
            m_aiModelCombo->insertItem(0, savedModel);
        }
        m_aiModelCombo->setCurrentText(savedModel);
        if (m_aiModelCombo->lineEdit()) {
            m_aiModelCombo->lineEdit()->setText(savedModel);
        }
    }
    if (m_aiSystemPromptEdit) {
        m_aiSystemPromptEdit->setPlainText(provider.value(QStringLiteral("systemPrompt")).toString());
    }
    if (m_aiUserPromptEdit) {
        m_aiUserPromptEdit->setPlainText(provider.value(QStringLiteral("userPrompt")).toString(QStringLiteral("Say hello from LinDesk AI Core.")));
    }

    QJsonObject nextRoot = root;
    nextRoot.insert(QStringLiteral("currentProvider"), aiProviderSettingsPrefix(providerIndex));
    saveAIConfigRoot(nextRoot);
    m_aiCurrentProviderIndex = providerIndex;

    if (m_aiCoreService && m_aiProviderCombo) {
        const auto providerKind = static_cast<AI::AIClient::ProviderKind>(m_aiProviderCombo->itemData(providerIndex).toInt());
        m_aiCoreService->setProvider(providerKind);
    }
    if (m_aiStatusLabel && m_aiProviderCombo) {
        m_aiStatusLabel->setText(QStringLiteral("Loaded AI config: %1, model %2. File: %3")
            .arg(m_aiProviderCombo->itemText(providerIndex), m_aiModelCombo->currentText(), aiConfigPath()));
    }
}

void ListViewDebugPanel::saveAIProviderConfig(int providerIndex)
{
    if (!m_aiBaseUrlEdit || !m_aiApiKeyEdit || !m_aiModelCombo) {
        return;
    }

    QJsonObject root = loadAIConfigRoot();
    QJsonObject providers = root.value(QStringLiteral("providers")).toObject();
    const QString providerName = aiProviderSettingsPrefix(providerIndex);
    QJsonObject provider;
    provider.insert(QStringLiteral("baseUrl"), m_aiBaseUrlEdit->text().trimmed());
    provider.insert(QStringLiteral("apiKey"), m_aiApiKeyEdit->text());
    provider.insert(QStringLiteral("model"), m_aiModelCombo->currentText().trimmed());
    provider.insert(QStringLiteral("systemPrompt"), m_aiSystemPromptEdit ? m_aiSystemPromptEdit->toPlainText() : QString());
    provider.insert(QStringLiteral("userPrompt"), m_aiUserPromptEdit ? m_aiUserPromptEdit->toPlainText() : QString());
    providers.insert(providerName, provider);
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("providers"), providers);
    saveAIConfigRoot(root);
}

void ListViewDebugPanel::loadAIConfig()
{
    if (!m_aiProviderCombo) {
        return;
    }

    const QJsonObject root = loadAIConfigRoot();
    const QString providerName = root.value(QStringLiteral("currentProvider")).toString(QStringLiteral("OpenAI"));
    const int providerIndex = aiProviderIndexFromName(providerName);
    {
        const QSignalBlocker providerBlocker(m_aiProviderCombo);
        m_aiProviderCombo->setCurrentIndex(providerIndex);
    }
    loadAIProviderConfig(providerIndex);
    m_aiCurrentProviderIndex = providerIndex;
}

void ListViewDebugPanel::saveAIConfig()
{
    if (!m_aiProviderCombo || !m_aiBaseUrlEdit || !m_aiApiKeyEdit || !m_aiModelCombo) {
        return;
    }

    const int providerIndex = m_aiProviderCombo->currentIndex();
    QJsonObject root = loadAIConfigRoot();
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("currentProvider"), aiProviderSettingsPrefix(providerIndex));
    saveAIConfigRoot(root);
    saveAIProviderConfig(providerIndex);
    m_aiCurrentProviderIndex = providerIndex;
}

AI::ChatRequest ListViewDebugPanel::buildAIChatRequest() const
{
    AI::ChatRequest request;
    request.requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    request.model = m_aiModelCombo ? m_aiModelCombo->currentText().trimmed() : QString();
    request.apiKey = m_aiApiKeyEdit ? m_aiApiKeyEdit->text() : QString();
    request.baseUrl = QUrl(m_aiBaseUrlEdit ? m_aiBaseUrlEdit->text().trimmed() : QString());
    request.systemPrompt = m_aiSystemPromptEdit ? m_aiSystemPromptEdit->toPlainText() : QString();
    request.temperature = 0.2;

    request.messages = m_aiConversationMessages;

    const QString userText = m_aiUserPromptEdit ? m_aiUserPromptEdit->toPlainText() : QString();
    AI::ChatMessage userMessage;
    userMessage.role = QStringLiteral("user");
    userMessage.content = userText;
    request.messages.append(userMessage);

    if (m_aiProviderCombo && m_aiCoreService) {
        const auto provider = static_cast<AI::AIClient::ProviderKind>(m_aiProviderCombo->currentData().toInt());
        m_aiCoreService->setProvider(provider);
    }
    return request;
}

void ListViewDebugPanel::appendAIChatLine(const QString& role, const QString& content)
{
    if (content.trimmed().isEmpty()) {
        return;
    }

    AI::ChatMessage message;
    message.role = role;
    message.content = content;
    m_aiConversationMessages.append(message);

    if (m_aiOutputView) {
        const QString label = role == QStringLiteral("assistant") ? QStringLiteral("assistant") : QStringLiteral("user");
        m_aiOutputView->appendPlainText(QStringLiteral("\n%1:\n%2").arg(label, content));
        m_aiOutputView->moveCursor(QTextCursor::End);
    }
}

void ListViewDebugPanel::clearAIConversation()
{
    m_aiConversationMessages.clear();
    m_aiStreamingAssistantText.clear();
    if (m_aiOutputView) {
        m_aiOutputView->clear();
    }
    if (m_aiStatusLabel) {
        m_aiStatusLabel->setText(QStringLiteral("Conversation cleared."));
    }
}

void ListViewDebugPanel::setAIRequestRunning(bool running)
{
    if (m_aiSendButton) {
        m_aiSendButton->setEnabled(!running);
    }
    if (m_aiStreamButton) {
        m_aiStreamButton->setEnabled(!running);
    }
    if (m_aiSaveButton) {
        m_aiSaveButton->setEnabled(!running);
    }
    if (m_aiStopButton) {
        m_aiStopButton->setEnabled(running);
    }
    if (m_aiStatusLabel && running) {
        m_aiStatusLabel->setText(QStringLiteral("Request is running: %1 / %2")
            .arg(m_aiProviderCombo ? m_aiProviderCombo->currentText() : QString(),
                 m_aiModelCombo ? m_aiModelCombo->currentText() : QString()));
    }
}

QWidget* ListViewDebugPanel::createLayoutPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(24);

    layout->addWidget(createPageTitleLabel("Layout & Behavior"));
    layout->addWidget(createPageSubtitleLabel("Adjust desktop spacing and desktop embedding behavior for visual debugging."));

    LinDesktop* desk = nullptr;
    if (m_listView) {
        QWidget* p = m_listView;
        while (p) {
            if (LinDesktop* d = qobject_cast<LinDesktop*>(p)) {
                desk = d;
                break;
            }
            p = p->parentWidget();
        }
    }
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

void ListViewDebugPanel::updateLabels()
{
    updateOpenGLStatus();

    if (m_rowValueLabel) {
        m_rowValueLabel->setText(QString::number(m_rowSlider ? m_rowSlider->value() : 0));
    }
    if (m_colValueLabel) {
        m_colValueLabel->setText(QString::number(m_colSlider ? m_colSlider->value() : 0));
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



