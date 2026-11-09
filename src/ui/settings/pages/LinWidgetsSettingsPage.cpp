#include "ui/settings/pages/LinWidgetsSettingsPage.h"
#include "ui/settings/SettingsUICommon.h"
#include "ui/hub/WallpaperStudioWindow.h"
#include "desktop/runtime/RuntimeInstaller.h"
#include "desktop/widgets/runtime_export.h"
#include "services/AppPaths.h"
#include "app/AppFonts.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QStackedWidget>
#include "ui/Fluent/FluentDialog.h"
#include <QFileDialog>
#include <QUrl>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSettings>

using namespace SettingsUICommon;

namespace {

qint64 calculateDirectorySize(const QString& dirPath) {
    if (dirPath.isEmpty() || !QFileInfo::exists(dirPath)) return 0;
    qint64 total = 0;
    QDirIterator it(dirPath, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

// Authentic Windows 11 WinUI 3 SettingsCard container
QWidget* createFluentSettingsCard(QWidget* parent) {
    auto* card = new QWidget(parent);
    card->setObjectName(QStringLiteral("fluentSettingsCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    card->setStyleSheet(QStringLiteral(
        "QWidget#fluentSettingsCard {"
        "  background-color: #ffffff;"
        "  border: 1px solid #e5e5e5;"
        "  border-radius: 8px;"
        "}"));
    return card;
}

// Section header matching Win11 Settings group title (14px Semibold #1b1b1b)
QLabel* createFluentSectionHeader(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  background: transparent;"
        "  color: #1b1b1b;"
        "  font-size: 14px;"
        "  font-weight: 600;"
        "  padding: 0px;"
        "}"));
    return label;
}

// Standard 20px monochrome glyph for Win11 SettingsCard rows
QLabel* createFluentRowIcon(ushort codepoint, QWidget* parent, bool accentTile = false) {
    auto* label = new QLabel(glyph(codepoint), parent);
    label->setAlignment(Qt::AlignCenter);
    if (accentTile) {
        label->setFixedSize(36, 36);
        label->setStyleSheet(QStringLiteral(
            "QLabel {"
            "  background-color: #eff6fc;"
            "  color: #0067c0;"
            "  border-radius: 8px;"
            "  font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets';"
            "  font-size: 18px;"
            "}"));
    } else {
        label->setFixedSize(24, 24);
        label->setStyleSheet(QStringLiteral(
            "QLabel {"
            "  background: transparent;"
            "  color: #1b1b1b;"
            "  font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets';"
            "  font-size: 18px;"
            "}"));
    }
    return label;
}

// Single-row Win11 SettingsCard with icon, title, subtitle, and right-side control
QWidget* createFluentToggleCard(ushort iconCode,
                                const QString& title,
                                const QString& subtitle,
                                FluentToggleSwitch* toggle,
                                QWidget* parent) {
    QWidget* card = createFluentSettingsCard(parent);
    auto* rowLayout = new QHBoxLayout(card);
    rowLayout->setContentsMargins(20, 15, 20, 15);
    rowLayout->setSpacing(16);

    rowLayout->addWidget(createFluentRowIcon(iconCode, card, false), 0, Qt::AlignVCenter);

    auto* textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    auto* titleLabel = new QLabel(title, card);
    titleLabel->setWordWrap(true);
    titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    titleLabel->setStyleSheet(QStringLiteral(
        "QLabel { background: transparent; color: #1b1b1b; font-size: 14px; font-weight: 500; }"));

    auto* subLabel = new QLabel(subtitle, card);
    subLabel->setWordWrap(true);
    subLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    subLabel->setStyleSheet(QStringLiteral(
        "QLabel { background: transparent; color: #5c5c5c; font-size: 12px; font-weight: 400; }"));

    textLayout->addWidget(titleLabel);
    textLayout->addWidget(subLabel);
    rowLayout->addLayout(textLayout, 1);

    if (toggle) {
        rowLayout->addWidget(toggle, 0, Qt::AlignVCenter);
    }
    return card;
}

} // namespace

LinWidgetsSettingsPage::LinWidgetsSettingsPage(QWidget* parent)
    : QWidget(parent)
{
    initUI();

    auto& installer = RuntimeInstaller::instance();
    connect(&installer, &RuntimeInstaller::statusChanged, this, [this](RuntimeInstaller::Status status) {
        if (status == RuntimeInstaller::Status::NotInstalled) {
            LinHub::WallpaperStudioWindow::hideIfOpen();
        }
        updateRuntimeStatusView();
    });
    connect(&installer, &RuntimeInstaller::installProgress, this, [this](qint64 received, qint64 total, const QString& stage) {
        const QString text = stage.isEmpty() ? tr("正在配置动态渲染支持组件...") : stage;
        auto updateBar = [&](QProgressBar* bar, QLabel* label) {
            if (bar) {
                bar->setVisible(true);
                if (total > 0) {
                    bar->setRange(0, 100);
                    bar->setValue(static_cast<int>((received * 100) / total));
                } else {
                    bar->setRange(0, 0);
                }
            }
            if (label) {
                label->setVisible(true);
                label->setText(text);
            }
        };
        updateBar(m_installProgressBar, m_progressStageLabel);
        updateBar(m_onboardingProgressBar, m_onboardingStageLabel);
    });
    connect(&installer, &RuntimeInstaller::installFinished, this, [this](bool success, const QString& msg) {
        if (m_installProgressBar) m_installProgressBar->setVisible(false);
        if (m_progressStageLabel) m_progressStageLabel->setVisible(false);
        if (m_onboardingProgressBar) m_onboardingProgressBar->setVisible(false);
        if (m_onboardingStageLabel) m_onboardingStageLabel->setVisible(false);
        updateRuntimeStatusView();
        if (success) {
            FluentDialog::showInfo(this, tr("壁纸与小组件"), tr("组件运行库已成功配置就绪！\n\n您现在可以畅享高帧率动态壁纸与轻量小组件。"));
        } else {
            FluentDialog::showWarning(this, tr("配置失败"), tr("组件运行库安装遇到问题：%1").arg(msg));
        }
    });

    updateRuntimeStatusView();

    LinWidgetRuntimeBridge::instance().addEnabledListener(this, [this](bool enabled) {
        if (m_masterToggle) {
            m_masterToggle->blockSignals(true);
            m_masterToggle->setChecked(enabled);
            m_masterToggle->blockSignals(false);
        }
        updateRuntimeStatusView();
    });
}

LinWidgetsSettingsPage::~LinWidgetsSettingsPage()
{
    LinWidgetRuntimeBridge::instance().removeEnabledListener(this);
}

void LinWidgetsSettingsPage::reloadPage()
{
    updateRuntimeStatusView();
}

void LinWidgetsSettingsPage::updateTranslations()
{
    if (m_pageTitle) m_pageTitle->setText(tr("壁纸与小组件"));
    if (m_pageSubtitle) m_pageSubtitle->setText(tr("管理动态壁纸与桌面小组件的开关、省电策略及缓存清理。"));
    updateRuntimeStatusView();
}

void LinWidgetsSettingsPage::initUI()
{
    auto* page = this;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(36, 12, 36, 32);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignTop);

    // Page Title & Subtitle (Win11 Fluent standard header)
    m_pageTitle = createPageTitle(tr("壁纸与小组件"), page);
    m_pageTitle->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(m_pageTitle, 0, Qt::AlignLeft);
    layout->addSpacing(4);

    m_pageSubtitle = createPageSubtitle(tr("管理动态壁纸与桌面小组件的开关、省电策略及缓存清理。"), page);
    layout->addWidget(m_pageSubtitle);
    layout->addSpacing(20);

    m_viewStack = new QStackedWidget(page);
    m_viewStack->setStyleSheet(QStringLiteral("QStackedWidget { background: transparent; border: none; }"));

    m_onboardingView = createOnboardingView(m_viewStack);
    m_installedView = createInstalledView(m_viewStack);

    m_viewStack->addWidget(m_onboardingView); // Index 0: Uninstalled Single-Row Empty State Card
    m_viewStack->addWidget(m_installedView);  // Index 1: Win11 Fluent SettingsCard Installed View

    layout->addWidget(m_viewStack);
}

QWidget* LinWidgetsSettingsPage::createOnboardingView(QWidget* parent)
{
    auto* container = new QWidget(parent);
    auto* outerLayout = new QVBoxLayout(container);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);
    outerLayout->setAlignment(Qt::AlignTop);

    QWidget* emptyCard = createFluentSettingsCard(container);
    auto* cardLayout = new QVBoxLayout(emptyCard);
    cardLayout->setContentsMargins(20, 18, 20, 18);
    cardLayout->setSpacing(12);

    auto* rowLayout = new QHBoxLayout();
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(16);

    rowLayout->addWidget(createFluentRowIcon(0xE896, emptyCard, true), 0, Qt::AlignVCenter);

    auto* textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(3);

    auto* titleLabel = new QLabel(tr("尚未安装壁纸与组件支持包"), emptyCard);
    titleLabel->setStyleSheet(QStringLiteral(
        "QLabel { background: transparent; color: #1b1b1b; font-size: 14px; font-weight: 600; }"));

    auto* subLabel = new QLabel(tr("安装后即可使用动态壁纸与桌面小组件，您的个人设置和数据会自动保留。"), emptyCard);
    subLabel->setWordWrap(true);
    subLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    subLabel->setStyleSheet(QStringLiteral(
        "QLabel { background: transparent; color: #5c5c5c; font-size: 12px; font-weight: 400; }"));

    textLayout->addWidget(titleLabel);
    textLayout->addWidget(subLabel);
    rowLayout->addLayout(textLayout, 1);

    auto* btnGroup = new QHBoxLayout();
    btnGroup->setContentsMargins(0, 0, 0, 0);
    btnGroup->setSpacing(8);

    m_onboardingInstallBtn = createSettingsActionButton(tr("立即安装"), emptyCard, true);
    connect(m_onboardingInstallBtn, &QPushButton::clicked, this, &LinWidgetsSettingsPage::onInstallRuntimeClicked);
    btnGroup->addWidget(m_onboardingInstallBtn);

    m_onboardingZipBtn = createSettingsActionButton(tr("离线包安装 (*.zip)..."), emptyCard);
    connect(m_onboardingZipBtn, &QPushButton::clicked, this, &LinWidgetsSettingsPage::onInstallFromZipClicked);
    btnGroup->addWidget(m_onboardingZipBtn);

    rowLayout->addLayout(btnGroup, 0);
    cardLayout->addLayout(rowLayout);

    m_onboardingProgressBar = new QProgressBar(emptyCard);
    m_onboardingProgressBar->setObjectName("updateProgressBar");
    m_onboardingProgressBar->setFixedHeight(4);
    m_onboardingProgressBar->setTextVisible(false);
    m_onboardingProgressBar->setVisible(false);
    cardLayout->addWidget(m_onboardingProgressBar);

    m_onboardingStageLabel = new QLabel(emptyCard);
    m_onboardingStageLabel->setStyleSheet(QStringLiteral(
        "QLabel { background: transparent; color: #5c5c5c; font-size: 12px; }"));
    m_onboardingStageLabel->setVisible(false);
    cardLayout->addWidget(m_onboardingStageLabel);

    outerLayout->addWidget(emptyCard);
    outerLayout->addStretch(1);
    return container;
}

QWidget* LinWidgetsSettingsPage::createInstalledView(QWidget* parent)
{
    auto* container = new QWidget(parent);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignTop);

    // =========================================================
    // 1. Primary Action & Master Switch (Win11 SettingsCards)
    // =========================================================
    // Card 1: LinDesk Hub Launcher Card
    QWidget* hubCard = createFluentSettingsCard(container);
    auto* hubLayout = new QHBoxLayout(hubCard);
    hubLayout->setContentsMargins(20, 16, 20, 16);
    hubLayout->setSpacing(16);

    hubLayout->addWidget(createFluentRowIcon(0xE771, hubCard, true), 0, Qt::AlignVCenter);

    auto* hubTextLayout = new QVBoxLayout();
    hubTextLayout->setContentsMargins(0, 0, 0, 0);
    hubTextLayout->setSpacing(2);

    auto* hubTitle = new QLabel(tr("壁纸与小组件中心"), hubCard);
    hubTitle->setStyleSheet(QStringLiteral(
        "QLabel { background: transparent; color: #1b1b1b; font-size: 14px; font-weight: 600; }"));

    m_hubStatusSubtitle = new QLabel(tr("浏览、导入和设置桌面动态壁纸与小组件"), hubCard);
    m_hubStatusSubtitle->setWordWrap(true);
    m_hubStatusSubtitle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_hubStatusSubtitle->setStyleSheet(QStringLiteral(
        "QLabel { background: transparent; color: #5c5c5c; font-size: 12px; font-weight: 400; }"));

    hubTextLayout->addWidget(hubTitle);
    hubTextLayout->addWidget(m_hubStatusSubtitle);
    hubLayout->addLayout(hubTextLayout, 1);

    m_openHubBtn = createSettingsActionButton(tr("打开中心"), hubCard, true);
    connect(m_openHubBtn, &QPushButton::clicked, this, &LinWidgetsSettingsPage::onOpenHubClicked);
    hubLayout->addWidget(m_openHubBtn, 0, Qt::AlignVCenter);

    layout->addWidget(hubCard);
    layout->addSpacing(4);

    // Card 2: Master Toggle Switch Card
    QSettings masterSettings(settingsConfigPath(), QSettings::IniFormat);
    const bool isMasterEnabled = masterSettings.value(QStringLiteral("LinWidgets/Enabled"), true).toBool();

    m_masterToggle = new FluentToggleSwitch(container);
    m_masterToggle->setChecked(isMasterEnabled);
    connect(m_masterToggle, &FluentToggleSwitch::toggled, this, [this](bool checked) {
        QSettings s(settingsConfigPath(), QSettings::IniFormat);
        s.setValue(QStringLiteral("LinWidgets/Enabled"), checked);
        s.sync();

        auto& bridge = LinWidgetRuntimeBridge::instance();
        if (bridge.isLoaded()) {
            bridge.setEnabled(checked);
            if (checked) {
                bridge.restoreFromConfig(QString());
            } else {
                bridge.stopWallpaper(false);
            }
        }

        updateRuntimeStatusView();
    });

    layout->addWidget(createFluentToggleCard(
        0xE7E8,
        tr("动态壁纸与桌面小组件总开关"),
        tr("开启后可在桌面显示动态壁纸和小组件；关闭后全部暂停并释放电脑内存"),
        m_masterToggle,
        container
    ));

    // =========================================================
    // 2. Performance & Power Strategy Section
    // =========================================================
    layout->addSpacing(24);
    layout->addWidget(createFluentSectionHeader(tr("省电与流畅度设置"), container));
    layout->addSpacing(8);

    QSettings settings(settingsConfigPath(), QSettings::IniFormat);

    // Row 1: GPU Acceleration
    m_gpuAccelToggle = new FluentToggleSwitch(container);
    m_gpuAccelToggle->setChecked(settings.value(QStringLiteral("LinWidgets/CefOsrEnabled"), true).toBool());
    connect(m_gpuAccelToggle, &FluentToggleSwitch::toggled, this, [](bool checked) {
        QSettings s(settingsConfigPath(), QSettings::IniFormat);
        s.setValue(QStringLiteral("LinWidgets/CefOsrEnabled"), checked);
        s.sync();
        auto& bridge = LinWidgetRuntimeBridge::instance();
        if (bridge.isLoaded()) {
            bridge.setCefOsrEnabled(checked);
        }
    });
    layout->addWidget(createFluentToggleCard(
        0xE7F8,
        tr("显卡加速"),
        tr("让动态壁纸播放更流畅；若老旧电脑出现画面闪烁可尝试关闭"),
        m_gpuAccelToggle,
        container
    ));
    layout->addSpacing(4);

    // Row 2: Fullscreen Game/App Pause
    m_fullscreenPauseToggle = new FluentToggleSwitch(container);
    m_fullscreenPauseToggle->setChecked(settings.value(QStringLiteral("LinWidgets/PauseOnFullscreen"), true).toBool());
    connect(m_fullscreenPauseToggle, &FluentToggleSwitch::toggled, this, [](bool checked) {
        QSettings s(settingsConfigPath(), QSettings::IniFormat);
        s.setValue(QStringLiteral("LinWidgets/PauseOnFullscreen"), checked);
        s.sync();
    });
    layout->addWidget(createFluentToggleCard(
        0xE7FC,
        tr("全屏游戏或看视频时自动暂停"),
        tr("全屏玩游戏或全屏使用软件时自动暂停桌面壁纸，不占用电脑性能"),
        m_fullscreenPauseToggle,
        container
    ));
    layout->addSpacing(4);

    // Row 3: Battery Saver Mode
    m_batterySaverToggle = new FluentToggleSwitch(container);
    m_batterySaverToggle->setChecked(settings.value(QStringLiteral("LinWidgets/BatterySaverEnabled"), true).toBool());
    connect(m_batterySaverToggle, &FluentToggleSwitch::toggled, this, [](bool checked) {
        QSettings s(settingsConfigPath(), QSettings::IniFormat);
        s.setValue(QStringLiteral("LinWidgets/BatterySaverEnabled"), checked);
        s.sync();
    });
    layout->addWidget(createFluentToggleCard(
        0xE945,
        tr("使用电池时自动省电"),
        tr("笔记本未插电源时自动降低动态壁纸耗电，延长续航时间"),
        m_batterySaverToggle,
        container
    ));

    // =========================================================
    // 3. Storage & Runtime Maintenance Section
    // =========================================================
    layout->addSpacing(24);
    layout->addWidget(createFluentSectionHeader(tr("存储空间管理"), container));
    layout->addSpacing(8);

    // Card A: Cache Management
    QWidget* cacheCard = createFluentSettingsCard(container);
    auto* cacheLayout = new QHBoxLayout(cacheCard);
    cacheLayout->setContentsMargins(20, 15, 20, 15);
    cacheLayout->setSpacing(16);

    cacheLayout->addWidget(createFluentRowIcon(0xE8B7, cacheCard, false), 0, Qt::AlignVCenter);

    auto* cacheTextLayout = new QVBoxLayout();
    cacheTextLayout->setContentsMargins(0, 0, 0, 0);
    cacheTextLayout->setSpacing(2);

    auto* cacheTitle = new QLabel(tr("临时缓存文件"), cacheCard);
    cacheTitle->setStyleSheet(QStringLiteral(
        "QLabel { background: transparent; color: #1b1b1b; font-size: 14px; font-weight: 500; }"));

    m_cacheStatusSubtitle = new QLabel(tr("正在计算缓存占用..."), cacheCard);
    m_cacheStatusSubtitle->setWordWrap(true);
    m_cacheStatusSubtitle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_cacheStatusSubtitle->setStyleSheet(QStringLiteral(
        "QLabel { background: transparent; color: #5c5c5c; font-size: 12px; font-weight: 400; }"));

    cacheTextLayout->addWidget(cacheTitle);
    cacheTextLayout->addWidget(m_cacheStatusSubtitle);
    cacheLayout->addLayout(cacheTextLayout, 1);

    m_clearCacheBtn = createSettingsActionButton(tr("清理缓存"), cacheCard);
    connect(m_clearCacheBtn, &QPushButton::clicked, this, &LinWidgetsSettingsPage::onClearCacheClicked);
    cacheLayout->addWidget(m_clearCacheBtn, 0, Qt::AlignVCenter);

    layout->addWidget(cacheCard);
    layout->addSpacing(4);

    // Card B: Component Runtime Management
    QWidget* runtimeCard = createFluentSettingsCard(container);
    auto* runtimeCardLayout = new QVBoxLayout(runtimeCard);
    runtimeCardLayout->setContentsMargins(20, 15, 20, 15);
    runtimeCardLayout->setSpacing(10);

    auto* runtimeRowLayout = new QHBoxLayout();
    runtimeRowLayout->setContentsMargins(0, 0, 0, 0);
    runtimeRowLayout->setSpacing(16);

    runtimeRowLayout->addWidget(createFluentRowIcon(0xE7B8, runtimeCard, false), 0, Qt::AlignVCenter);

    auto* runtimeTextLayout = new QVBoxLayout();
    runtimeTextLayout->setContentsMargins(0, 0, 0, 0);
    runtimeTextLayout->setSpacing(2);

    auto* runtimeTitle = new QLabel(tr("壁纸与组件支持包"), runtimeCard);
    runtimeTitle->setStyleSheet(QStringLiteral(
        "QLabel { background: transparent; color: #1b1b1b; font-size: 14px; font-weight: 500; }"));

    m_runtimeStatusSubtitle = new QLabel(tr("管理本地支持包，可导入安装包更新或卸载以释放磁盘空间"), runtimeCard);
    m_runtimeStatusSubtitle->setWordWrap(true);
    m_runtimeStatusSubtitle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_runtimeStatusSubtitle->setStyleSheet(QStringLiteral(
        "QLabel { background: transparent; color: #5c5c5c; font-size: 12px; font-weight: 400; }"));

    runtimeTextLayout->addWidget(runtimeTitle);
    runtimeTextLayout->addWidget(m_runtimeStatusSubtitle);
    runtimeRowLayout->addLayout(runtimeTextLayout, 1);

    auto* runtimeBtnGroup = new QHBoxLayout();
    runtimeBtnGroup->setContentsMargins(0, 0, 0, 0);
    runtimeBtnGroup->setSpacing(8);

    m_installZipBtn = createSettingsActionButton(tr("离线包更新..."), runtimeCard);
    connect(m_installZipBtn, &QPushButton::clicked, this, &LinWidgetsSettingsPage::onInstallFromZipClicked);
    runtimeBtnGroup->addWidget(m_installZipBtn);

    m_uninstallBtn = createSettingsActionButton(tr("卸载"), runtimeCard, false, true);
    connect(m_uninstallBtn, &QPushButton::clicked, this, &LinWidgetsSettingsPage::onUninstallRuntimeClicked);
    runtimeBtnGroup->addWidget(m_uninstallBtn);

    runtimeRowLayout->addLayout(runtimeBtnGroup, 0);
    runtimeCardLayout->addLayout(runtimeRowLayout);

    m_installProgressBar = new QProgressBar(runtimeCard);
    m_installProgressBar->setObjectName("updateProgressBar");
    m_installProgressBar->setFixedHeight(4);
    m_installProgressBar->setTextVisible(false);
    m_installProgressBar->setVisible(false);
    runtimeCardLayout->addWidget(m_installProgressBar);

    m_progressStageLabel = new QLabel(runtimeCard);
    m_progressStageLabel->setStyleSheet(QStringLiteral(
        "QLabel { background: transparent; color: #5c5c5c; font-size: 12px; }"));
    m_progressStageLabel->setVisible(false);
    runtimeCardLayout->addWidget(m_progressStageLabel);

    layout->addWidget(runtimeCard);
    layout->addStretch(1);
    return container;
}

void LinWidgetsSettingsPage::updateRuntimeStatusView()
{
    auto& installer = RuntimeInstaller::instance();
    const bool installed = installer.isRuntimeInstalled();
    const QString version = installer.installedVersion().isEmpty() ? QStringLiteral("1.0.0") : installer.installedVersion();

    if (m_viewStack) {
        m_viewStack->setCurrentIndex(installed ? 1 : 0);
    }

    QSettings s(settingsConfigPath(), QSettings::IniFormat);
    const bool masterEnabled = s.value(QStringLiteral("LinWidgets/Enabled"), true).toBool();
    if (m_masterToggle) {
        m_masterToggle->blockSignals(true);
        m_masterToggle->setChecked(masterEnabled);
        m_masterToggle->blockSignals(false);
    }

    if (m_openHubBtn) {
        m_openHubBtn->setEnabled(installed && masterEnabled);
    }

    if (m_hubStatusSubtitle) {
        if (masterEnabled) {
            m_hubStatusSubtitle->setText(tr("状态：运行中 · 浏览、导入与管理桌面动态壁纸和小组件"));
        } else {
            m_hubStatusSubtitle->setText(tr("状态：已暂停 · 开启下方总开关后即可打开壁纸与小组件中心"));
        }
    }

    if (m_cacheStatusSubtitle) {
        const QString dataPath = AppPaths::userLinWidgetsDir();
        const qint64 totalBytes = calculateDirectorySize(dataPath);
        const double mb = static_cast<double>(totalBytes) / (1024.0 * 1024.0);
        m_cacheStatusSubtitle->setText(
            tr("当前共占用 %1 MB · 清理临时缓存不会丢失您的壁纸和小组件设置")
                .arg(QString::number(mb, 'f', 2)));
    }

    if (m_runtimeStatusSubtitle) {
        m_runtimeStatusSubtitle->setText(
            tr("当前版本 v%1 · 支持导入离线包更新或卸载释放约 85 MB 磁盘空间").arg(version));
    }
}

void LinWidgetsSettingsPage::onOpenHubClicked()
{
    LinHub::WallpaperStudioWindow::showStudio();
}

void LinWidgetsSettingsPage::onInstallRuntimeClicked()
{
    auto& installer = RuntimeInstaller::instance();
    if (installer.status() == RuntimeInstaller::Status::Installing) {
        FluentDialog::showInfo(this, tr("正在配置"), tr("组件运行库正在安装或下载中，请稍候..."));
        return;
    }

    if (installer.isRuntimeInstalled()) {
        if (!FluentDialog::confirm(
                this,
                tr("校验组件运行库"),
                tr("当前组件运行库已安装，是否重新下载并修复以确保完整性？"),
                tr("重新安装"),
                tr("取消"))) {
            return;
        }
    }

    installer.downloadAndInstallLatestRuntime();
}

void LinWidgetsSettingsPage::onInstallFromZipClicked()
{
    const QString zipFile = QFileDialog::getOpenFileName(
        this,
        tr("选择组件运行库离线安装包"),
        QString(),
        tr("离线组件包 (*.zip);;所有文件 (*.*)")
    );
    if (zipFile.isEmpty()) return;

    QString error;
    if (RuntimeInstaller::instance().installFromLocalZip(zipFile, &error)) {
        updateRuntimeStatusView();
        FluentDialog::showInfo(this, tr("安装成功"), tr("已成功从本地压缩包解压并安装组件运行库！"));
    } else {
        FluentDialog::showWarning(this, tr("安装失败"), tr("解压离线包失败：%1").arg(error));
    }
}

void LinWidgetsSettingsPage::onUninstallRuntimeClicked()
{
    if (!FluentDialog::confirm(
            this,
            tr("卸载组件运行库"),
            tr("卸载组件运行库将释放约 85 MB 磁盘空间，正在运行的动态壁纸与小组件将立即停止。\n\n"
               "您的小组件个性化数据与配置将被安全保留，随时可重新安装激活。"),
            tr("确认卸载"),
            tr("取消"),
            true)) {
        return;
    }

    LinHub::WallpaperStudioWindow::hideIfOpen();

    QString error;
    if (RuntimeInstaller::instance().uninstallRuntime(&error)) {
        updateRuntimeStatusView();
        FluentDialog::showInfo(this, tr("卸载完成"), tr("组件运行库已成功卸载，已切换至未安装状态。"));
    } else {
        FluentDialog::showWarning(this, tr("卸载遇到问题"), tr("未能完全清理文件：%1").arg(error));
    }
}

void LinWidgetsSettingsPage::onClearCacheClicked()
{
    const QString dataPath = AppPaths::userLinWidgetsDir();
    QDir dataDir(dataPath);
    if (dataDir.exists()) {
        const QStringList cacheDirs = { QStringLiteral("cache"), QStringLiteral("GPUCache"), QStringLiteral("Code Cache") };
        for (const QString& cName : cacheDirs) {
            QDir sub(dataDir.filePath(cName));
            if (sub.exists()) {
                sub.removeRecursively();
            }
        }
    }
    updateRuntimeStatusView();
    FluentDialog::showInfo(this, tr("清理完成"), tr("运行缓存已清理完毕！"));
}
