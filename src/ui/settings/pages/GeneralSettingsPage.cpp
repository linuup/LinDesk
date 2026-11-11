#include "ui/settings/pages/GeneralSettingsPage.h"
#include "ui/settings/SettingsUICommon.h"
#include "app/LanguageManager.h"
#include "desktop/LinDesktop.h"
#include "ui/Fluent/FluentDialog.h"
#include "ui/Fluent/FluentComboBox.h"
#include "ui/Fluent/FluentToggleSwitch.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QProcess>
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QSettings>
#include <QMouseEvent>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

using namespace SettingsUICommon;

namespace {
class TileClickFilter : public QObject {
public:
    TileClickFilter(QObject* parent, std::function<void()> onClick)
        : QObject(parent), m_onClick(std::move(onClick)) {}
protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::MouseButtonRelease) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton && m_onClick) {
                m_onClick();
                return true;
            }
        }
        return QObject::eventFilter(obj, event);
    }
private:
    std::function<void()> m_onClick;
};
} // namespace

GeneralSettingsPage::GeneralSettingsPage(QWidget* parent)
    : QWidget(parent)
{
    initUI();
}

void GeneralSettingsPage::updateTranslations()
{
    if (m_pageTitle) m_pageTitle->setText(tr("General"));
    if (m_pageSubtitle) m_pageSubtitle->setText(tr("Manage LinDesk preferences, desktop behavior, and plugins from one place."));
}

void GeneralSettingsPage::initUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(36, 12, 36, 32);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignTop);

    const QString configPath = settingsConfigPath();
    QSettings settings(configPath, QSettings::IniFormat);

    m_pageTitle = createPageTitle(tr("General"), this);
    m_pageTitle->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(m_pageTitle, 0, Qt::AlignLeft);
    layout->addSpacing(4);
    m_pageSubtitle = createPageSubtitle(tr("Manage LinDesk preferences, desktop behavior, and plugins from one place."), this);
    layout->addWidget(m_pageSubtitle);
    layout->addSpacing(20);

    // 1. Home Quick Navigation Tiles (2x2 Win11 Horizontal Header Cards)
    QWidget* tileGrid = new QWidget(this);
    QGridLayout* tileLayout = new QGridLayout(tileGrid);
    tileLayout->setContentsMargins(0, 0, 0, 0);
    tileLayout->setHorizontalSpacing(12);
    tileLayout->setVerticalSpacing(12);

    auto addTile = [this, tileGrid, tileLayout](int row, int col, char16_t iconCode, const QString& title, const QString& desc, int targetIndex, const QString& customIcon = QString()) {
        const QString iconKey = customIcon.isEmpty() ? glyph(iconCode) : customIcon;
        QWidget* tile = createHomeTile(iconKey, title, desc, tileGrid);
        tile->setCursor(Qt::PointingHandCursor);
        for (QWidget* child : tile->findChildren<QWidget*>()) {
            child->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        }
        tile->installEventFilter(new TileClickFilter(tile, [this, targetIndex]() {
            emit requestNavigate(targetIndex);
        }));
        tileLayout->addWidget(tile, row, col);
    };

    addTile(0, 0, 0xE7F8, tr("Desktop"), tr("Customize desktop experience and settings."), 1);
    addTile(0, 1, 0xECAA, tr("Context Menu"), tr("Manage right-click menu items and filtering rules."), 2);
    addTile(1, 0, 0xF133, tr("Box"), tr("Adjust Box interface and interaction."), 3);
    addTile(1, 1, 0, tr("壁纸与小组件"), tr("Configure live wallpapers, desktop widgets, and runtime."), 4, QStringLiteral(":/Assets/hub_icon.png"));

    tileLayout->setColumnStretch(0, 1);
    tileLayout->setColumnStretch(1, 1);
    layout->addWidget(tileGrid);

    // 2. System Status Panel
    QLabel* statusTitle = createCardTitle(tr("System Status"), this);
    layout->addSpacing(24);
    layout->addWidget(statusTitle);
    layout->addSpacing(8);

    QWidget* statusPanel = createPanel(this);
    QVBoxLayout* statusLayout = new QVBoxLayout(statusPanel);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(0);

    // Toggle: Launch at Login
    m_autoStartToggle = new FluentToggleSwitch(this);
    const bool autoStartEnabled = isLaunchAtLoginEnabled();
    m_autoStartToggle->setChecked(autoStartEnabled);
    settings.setValue("General/AutoStart", autoStartEnabled);
    connect(m_autoStartToggle, &FluentToggleSwitch::toggled, this, [this, configPath](bool checked){
        if (!setLaunchAtLoginEnabled(checked)) {
            if (m_autoStartToggle) {
                const QSignalBlocker blocker(m_autoStartToggle);
                m_autoStartToggle->setChecked(!checked);
            }
            FluentDialog::showInfo(this, tr("Launch at Login"), tr("LinDesk could not update the startup shortcut."));
            return;
        }

        QSettings s(configPath, QSettings::IniFormat);
        s.setValue("General/AutoStart", checked);
    });
    statusLayout->addWidget(createSwitchRow(glyph(0xE7E6), tr("Launch at Login"), tr("Automatically start LinDesk when you sign in to Windows."), m_autoStartToggle, statusPanel));
    statusLayout->addWidget(createDivider(statusPanel));

    // Toggle: Minimize to Tray
    m_trayOnlyToggle = new FluentToggleSwitch(this);
    m_trayOnlyToggle->setChecked(settings.value("General/MinimizeToTray", true).toBool());
    connect(m_trayOnlyToggle, &FluentToggleSwitch::toggled, this, [configPath](bool checked){
        QSettings s(configPath, QSettings::IniFormat);
        s.setValue("General/MinimizeToTray", checked);
        s.sync();
    });
    statusLayout->addWidget(createSwitchRow(glyph(0xE921), tr("Minimize to Tray"), tr("Keep LinDesk running in the system tray when the window closes."), m_trayOnlyToggle, statusPanel));
    statusLayout->addWidget(createDivider(statusPanel));

    // Toggle: OpenGL Rendering
    m_openGLToggle = new FluentToggleSwitch(this);
    m_openGLToggle->setChecked(settings.value("General/UseOpenGL", false).toBool());
    connect(m_openGLToggle, &FluentToggleSwitch::toggled, this, [this, configPath](bool checked) {
        QSettings s(configPath, QSettings::IniFormat);
        const bool previousValue = s.value("General/UseOpenGL", false).toBool();
        if (previousValue == checked) {
            return;
        }

        s.setValue("General/UseOpenGL", checked);
        s.sync();
        FluentDialog::showInfo(
            this,
            tr("Restart Required"),
            tr("Restart LinDesk to apply the OpenGL rendering change."));
    });
    statusLayout->addWidget(createSwitchRow(glyph(0xE7F8), tr("OpenGL Rendering"), tr("Use OpenGL to accelerate rendering."), m_openGLToggle, statusPanel));
    statusLayout->addWidget(createDivider(statusPanel));

    // Combo: Display Language
    QWidget* languageRow = new QWidget(statusPanel);
    languageRow->setObjectName("settingsRow");
    QHBoxLayout* languageRowLayout = new QHBoxLayout(languageRow);
    languageRowLayout->setContentsMargins(16, 13, 16, 13);
    languageRowLayout->setSpacing(14);
    languageRowLayout->addWidget(createGlyphLabel(glyph(0xE774), languageRow, 18, QColor("#1b1b1b")), 0, Qt::AlignVCenter);

    QVBoxLayout* languageText = new QVBoxLayout();
    languageText->setContentsMargins(0, 0, 0, 0);
    languageText->setSpacing(2);
    QLabel* languageTitle = new QLabel(tr("Display Language"), languageRow);
    languageTitle->setObjectName("rowTitle");
    QLabel* languageSub = new QLabel(tr("Choose which language is used in the interface. Restart LinDesk to apply changes."), languageRow);
    languageSub->setObjectName("rowSubtitle");
    languageSub->setWordWrap(true);
    languageText->addWidget(languageTitle);
    languageText->addWidget(languageSub);
    languageRowLayout->addLayout(languageText, 1);

    m_languageCombo = new FluentComboBox(this);
    m_languageCombo->addItem(tr("English (United States)"), QStringLiteral("en_US"));
    m_languageCombo->addItem(tr("Chinese (Simplified)"), QStringLiteral("zh_CN"));
    const QString configuredLanguage = LanguageManager::configuredLanguageCode();
    int languageIndex = m_languageCombo->findData(configuredLanguage);
    if (languageIndex < 0) {
        languageIndex = 0;
    }
    m_languageCombo->setCurrentIndex(languageIndex);
    m_languageCombo->setMinimumWidth(180);
    connect(m_languageCombo, QOverload<int>::of(&FluentComboBox::currentIndexChanged), this, [this](int index){
        if (index < 0) {
            return;
        }

        const QString languageCode = LanguageManager::normalizeLanguageCode(m_languageCombo->itemData(index));
        if (languageCode == LanguageManager::configuredLanguageCode()) {
            return;
        }

        if (!LanguageManager::instance().setConfiguredLanguageCode(languageCode)) {
            FluentDialog::showInfo(this, tr("Display Language"), tr("LinDesk could not save the language setting."));
            return;
        }

        if (languageCode == LanguageManager::instance().currentLanguageCode()) {
            return;
        }

        FluentDialog dlg(this);
        dlg.setDialogTitle(tr("Restart Required"));
        dlg.setMessage(tr("Restart LinDesk to apply the display language change."));
        dlg.addActionButton(tr("Restart Now"), FluentDialog::ActionType::Primary, [this]() {
            if (!LinDesktop::relaunchApplication()) {
                FluentDialog::showInfo(this, tr("Restart Required"), tr("LinDesk could not restart automatically. Please restart it manually."));
            }
        });
        dlg.addActionButton(tr("Later"), FluentDialog::ActionType::Close);
        dlg.exec();
    });

    languageRowLayout->addWidget(m_languageCombo, 0, Qt::AlignVCenter);
    statusLayout->addWidget(languageRow);
    layout->addWidget(statusPanel);
    layout->addSpacing(12);

    // 3. Tip panel
    QWidget* tipPanel = createPanel(this);
    QHBoxLayout* tipLayout = new QHBoxLayout(tipPanel);
    tipLayout->setContentsMargins(16, 13, 16, 13);
    tipLayout->setSpacing(14);
    tipLayout->addWidget(createGlyphLabel(glyph(0xE946), tipPanel, 18, QColor("#0067c0")), 0, Qt::AlignVCenter);
    QLabel* tipText = new QLabel(tr("Tip: You can find more settings in each section. Explore the categories above to personalize your LinDesk experience."), tipPanel);
    tipText->setObjectName("rowSubtitle");
    tipText->setWordWrap(true);
    tipLayout->addWidget(tipText, 1);
    layout->addWidget(tipPanel);

    layout->addStretch();
}

QString GeneralSettingsPage::startupShortcutPath()
{
#ifdef Q_OS_WIN
    wchar_t startupDir[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_STARTUP, nullptr, SHGFP_TYPE_CURRENT, startupDir))) {
        return QString();
    }
    return QDir::fromNativeSeparators(QString::fromWCharArray(startupDir)) + "/LinDesk.lnk";
#else
    return QString();
#endif
}

bool GeneralSettingsPage::isLaunchAtLoginEnabled()
{
    const QString shortcutPath = startupShortcutPath();
    return !shortcutPath.isEmpty() && QFile::exists(shortcutPath);
}

bool GeneralSettingsPage::setLaunchAtLoginEnabled(bool enabled)
{
#ifdef Q_OS_WIN
    const QString shortcutPath = startupShortcutPath();
    if (shortcutPath.isEmpty()) {
        return false;
    }

    if (!enabled) {
        return !QFile::exists(shortcutPath) || QFile::remove(shortcutPath);
    }

    const QString exePath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString workingDir = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    const std::wstring shortcutPathW = QDir::toNativeSeparators(shortcutPath).toStdWString();
    const std::wstring exePathW = exePath.toStdWString();
    const std::wstring workingDirW = workingDir.toStdWString();

    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(initHr);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) {
        return false;
    }

    IShellLinkW* shellLink = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&shellLink));
    if (FAILED(hr) || !shellLink) {
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return false;
    }

    shellLink->SetPath(exePathW.c_str());
    shellLink->SetWorkingDirectory(workingDirW.c_str());
    shellLink->SetIconLocation(exePathW.c_str(), 0);
    shellLink->SetDescription(L"Launch LinDesk at sign in");

    IPersistFile* persistFile = nullptr;
    hr = shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persistFile));
    bool ok = false;
    if (SUCCEEDED(hr) && persistFile) {
        ok = SUCCEEDED(persistFile->Save(shortcutPathW.c_str(), TRUE));
        persistFile->Release();
    }

    shellLink->Release();
    if (shouldUninitialize) {
        CoUninitialize();
    }
    return ok;
#else
    return false;
#endif
}
