#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDataStream>
#include <QFile>
#include <QSet>
#include <QCoreApplication>
#include <QSettings>
#include "desktop/ListView.h"
#include "app/AppFonts.h"
#include "desktop/ListViewMappedBoxHelper.h"
#include "models/DesktopQuadTree.h"
#include "ui/SettingUI.h"
#include <QPushButton>
#include <QtMath>
#include <QProcess>
#include <QStandardPaths>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QClipboard>
#include "SystemApi/SystemApi.h"
#include <QDrag>
#include <QMimeData>
#include <QUrl>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QFileInfo>
#include <QDir>
#include <QFileDialog>
#include <QEventLoop>
#include <QFont>
#include <QFontMetrics>
#include <climits>
#include <shobjidl.h>
#include "../OS/SystemApi/resources.h"
#include "ui/Fluent/FluentDialog.h"
#include "ui/Fluent/FluentInfoTip.h"
#include "desktop/ListViewFileOpsHelper.h"
#include "desktop/ListViewSelectionSnapshot.h"
#include "box/LinBox.h"
#include "plugins/PluginManager.h"
#include "services/HotkeyManager.h"
#include "services/IconCacheManager.h"
#include "services/DpiScaleHelper.h"
#include "box/BoxManager.h"
#include "desktop/ListViewDebugPanel.h"

namespace {
QString normalizeDesktopPath(const QString& path)
{
    QString normalized = QDir::toNativeSeparators(path);
    while (normalized.endsWith('\\') || normalized.endsWith('/')) {
        normalized.chop(1);
    }
    return normalized;
}

QString normalizeMappedFolderPathForLookup(const QString& path)
{
    QString normalized = QDir::toNativeSeparators(QDir(path).absolutePath());
    if (normalized.length() == 2 && normalized[1] == ':') {
        normalized += '\\';
    } else if (normalized.length() > 3) {
        while (normalized.endsWith('\\') || normalized.endsWith('/')) {
            normalized.chop(1);
        }
    }
    return normalized;
}

int iconCellHeightForTwoTextLines(int iconSide)
{
    QFont labelFont = AppFonts::uiFont(9);

    const QFontMetrics metrics(labelFont);
    const int twoLineTextHeight = metrics.lineSpacing();
    return iconSide + twoLineTextHeight;
}

float normalizedSavedBoxOpacity(const QVariant& value)
{
    const double rawOpacity = value.toDouble();
    if (rawOpacity <= 0.0) {
        return 0.45f;
    }

    if (rawOpacity > 1.0) {
        return static_cast<float>(qBound(0.0, rawOpacity / 100.0, 1.0));
    }

    return static_cast<float>(qBound(0.0, rawOpacity, 1.0));
}

int specialFolderNameSortPriority(const ListItem* item)
{
    if (!item || item->type() != ObjectType::SpecialFolder) {
        return 100;
    }

    const QString clsid = item->getItemData().getCLSID();
    if (clsid == APP_USER_FOLDER_CLS) return 1;
    if (clsid == APP_MY_COMPUTER_CLS) return 2;
    if (clsid == APP_NETWORK_CLS) return 3;
    if (clsid == APP_RECYCLE_BIN_CLS) return 4;
    if (clsid == APP_CONTROL_PANEL_CLS) return 5;
    return 6;
}

int categorizedPriority(const ListItem* item)
{
    if (!item) {
        return 999;
    }
    if (item->type() == ObjectType::SpecialFolder) return 0;
    if (item->type() == ObjectType::DiskLetter || item->fileInfo().isDir()) return 1;

    const QString ext = item->fileInfo().suffix().toLower();
    if (ext == "exe" || ext == "lnk" || ext == "url") return 2;

    static const QStringList docExts = {"txt", "pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "md", "zip", "rar", "7z"};
    if (docExts.contains(ext)) return 3;
    return 4;
}

quint64 packGridCell(int gridX, int gridY)
{
    return ((quint64)(uint)gridX << 32) | (quint64)(uint)gridY;
}

QPoint pointToGridCell(const QPoint& pos, int marginLeft, int marginTop, int strideX, int strideY)
{
    const int gridX = qMax(0, qRound((double)(pos.x() - marginLeft) / qMax(1, strideX)));
    const int gridY = qMax(0, qRound((double)(pos.y() - marginTop) / qMax(1, strideY)));
    return QPoint(gridX, gridY);
}

QPoint gridCellToPoint(int gridX, int gridY, int marginLeft, int marginTop, int strideX, int strideY)
{
    return QPoint(marginLeft + gridX * strideX, marginTop + gridY * strideY);
}

int maxGridRowsForHeight(int viewHeight, int marginTop, int bottomPadding, int iconHeight, int strideY)
{
    int rows = 0;
    int gridY = 0;
    while (marginTop + gridY * strideY <= viewHeight - bottomPadding - iconHeight) {
        ++rows;
        ++gridY;
    }
    return qMax(1, rows);
}

int maxGridColsForWidth(int viewWidth, int marginLeft, int iconWidth, int strideX)
{
    int cols = 0;
    int gridX = 0;
    while (marginLeft + gridX * strideX <= viewWidth - iconWidth) {
        ++cols;
        ++gridX;
    }
    return qMax(1, cols);
}

QPoint gridIndexToCell(int linearIndex, int maxRows)
{
    const int safeRows = qMax(1, maxRows);
    return QPoint(linearIndex / safeRows, linearIndex % safeRows);
}

int pointToGridLinearIndex(const QPoint& pos, int marginLeft, int marginTop, int strideX, int strideY, int maxRows)
{
    const int safeStrideX = qMax(1, strideX);
    const int safeStrideY = qMax(1, strideY);
    const int safeRows = qMax(1, maxRows);

    int gridX = qMax(0, qFloor(static_cast<double>(pos.x() - marginLeft) / safeStrideX));
    int gridY = qMax(0, qFloor(static_cast<double>(pos.y() - marginTop) / safeStrideY));

    gridX += gridY / safeRows;
    gridY %= safeRows;
    return gridX * safeRows + gridY;
}

bool isItemFromMappedBox(ListItem* item)
{
    if (!item || item->boxId().isEmpty()) {
        return false;
    }

    if (LinBox* sourceBox = BoxManager::instance()->findBoxById(item->boxId())) {
        return sourceBox->isFolderMapped();
    }
    return false;
}

bool isPathFromMappedBox(const QString& filePath)
{
    if (filePath.isEmpty()) {
        return false;
    }

    if (LinBox* sourceBox = BoxManager::instance()->findBoxContainingItem(filePath)) {
        return sourceBox->isFolderMapped();
    }
    return false;
}

bool draggedPathsContainMappedSource(const QStringList& paths)
{
    for (const QString& path : paths) {
        if (isPathFromMappedBox(path)) {
            return true;
        }
    }
    return false;
}

void logDesktopLayoutMetrics(const char* tag,
                             const ListView* view,
                             int layoutLeft,
                             int layoutTop,
                             int usableRight,
                             int usableBottom,
                             int itemWidth,
                             int itemHeight,
                             int strideX,
                             int strideY)
{
    qDebug().nospace()
        << "[DesktopLayout][" << tag << "] "
        << "viewMode=" << static_cast<int>(view->viewMode())
        << " layoutLeft=" << layoutLeft
        << " layoutTop=" << layoutTop
        << " usableRight=" << usableRight
        << " usableBottom=" << usableBottom
        << " itemWidth=" << itemWidth
        << " itemHeight=" << itemHeight
        << " strideX=" << strideX
        << " strideY=" << strideY;
}

}

QRect ListView::desktopLayoutBounds() const
{
    QScreen* activeScreen = screen();
    if (!activeScreen && windowHandle()) {
        activeScreen = windowHandle()->screen();
    }
    if (!activeScreen) {
        activeScreen = QApplication::primaryScreen();
    }
    if (!activeScreen) {
        return rect();
    }

    const QRect globalViewRect(mapToGlobal(QPoint(0, 0)), size());
    const QRect usableGlobalRect = activeScreen->availableGeometry().intersected(globalViewRect);
    if (!usableGlobalRect.isValid() || usableGlobalRect.isEmpty()) {
        return rect();
    }

    const QRect usableLocalRect(usableGlobalRect.topLeft() - globalViewRect.topLeft(), usableGlobalRect.size());
    return usableLocalRect.intersected(rect());
}

int ListView::desktopLayoutLeft() const
{
    return qMax(m_marginLeft, desktopLayoutBounds().left());
}

int ListView::desktopLayoutTop() const
{
    return qMax(m_marginTop, desktopLayoutBounds().top());
}

int ListView::desktopUsableRight() const
{
    const QRect bounds = desktopLayoutBounds();
    return qMax(desktopLayoutLeft(), qMin(width() - 1, bounds.right()));
}

int ListView::desktopUsableBottom() const
{
    const QRect bounds = desktopLayoutBounds();
    return qMax(desktopLayoutTop(), qMin(height() - 1, bounds.bottom() - m_bottomPadding));
}

int ListView::desktopItemFootprintHeight() const
{
    if (m_iconShowRule.viewMode == ViewMode::SmallIcon) {
        const int iconSide = scalePx(32);
        const int lineHeight = QFontMetrics(AppFonts::uiFont(9)).lineSpacing();
        return scalePx(1) + iconSide + scalePx(ListItem::kIconTextGap) + qMax(lineHeight, lineHeight * 2) + scalePx(2);
    }

    if (m_iconShowRule.viewMode == ViewMode::LargeIcon) {
        const int iconSide = scalePx(96);
        const int lineHeight = QFontMetrics(AppFonts::uiFont(9)).lineSpacing();
        return scalePx(1) + iconSide + scalePx(ListItem::kIconTextGap) + qMax(lineHeight, lineHeight * 2) + scalePx(2);
    }

    const int iconSide = scalePx(48);
    const int lineHeight = QFontMetrics(AppFonts::uiFont(9)).lineSpacing();
    return scalePx(1) + iconSide + scalePx(ListItem::kIconTextGap) + qMax(lineHeight, lineHeight * 2) + scalePx(2);
}

// 闂傚牊鐟﹂埀顑跨劍閸ㄦ岸宕ㄥΟ鍝勭仴濠殿喖顑呯€?
ListView* ListView::m_instance = nullptr;
int ListItemData::s_targetIconSize = ListView::kMediumIconW;

ListView::ListView(QWidget* parent) : QWidget(parent),
    m_lastRightClickPos(-1, -1)  // 闁告帗绻傞～鎰板礌閺嵮冪闂佹鍠氶崑锝夊礄鐠佸磭绉寸紓鍐惧枙鐠愮喖寮悩铏珡闁?
{
    setAcceptDrops(true);
    // 閻犱礁澧介悿鍡楊浖瀹€鍕〃闁煎啿鏈▍娆撳炊閸撗冾暬
    //SystemParametersInfo(SPI_SETDESKWALLPAPER, 0, (PVOID)imagePath.toStdString().c_str(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);


    // 閻犱礁澧介悿鍡涘籍閻樿櫣鐝舵俊妤€妫涢悰銉╁矗?
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);

    // 閻犱礁澧介悿鍡涙焻韫囨梹顫栭柤鍐叉湰濞?
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    // 閻犱礁澧介悿鍡涙焻韫囨梹顫栨鐐舵硾閸樻垹鎷嬮幖浣插亾韫囨氨绠栫紒鎰殔瑜版盯鎯囩€ｎ亜鐓傞柛姘叄濞间即鎯冮崟顐㈡暥閻?
    //setAttribute(Qt::WA_TransparentForMouseEvents); // 闁稿繋娴囬蹇涙倷閻熸澘姣婇梺顐㈢箺缁诲啰绮ｅΔ鈧ぐ?
    //setAttribute(Qt::WA_OpaquePaintEvent);  // 闁告凹鍨抽弫銈夋嚄鐏炵偓鐝梺顐㈢箲濡叉垿宕?

    setGeometry(QApplication::primaryScreen()->geometry());
    //setAutoFillBackground(true);

    monitor = new DesktopInfoMonitor(this);
    startListeningSpecialIcon();
    m_windowsConfigMonitor = new WindowsConfigMonitor(this);
    connect(m_windowsConfigMonitor, &WindowsConfigMonitor::showHiddenFilesChanged,
        this, &ListView::onWindowsShowHiddenFilesChanged);
    m_windowsConfigMonitor->startListening();
    m_showHiddenFiles = m_windowsConfigMonitor->showHiddenFiles();

    // Initialize Plugin System & Hotkeys ASYNCHRONOUSLY to avoid recursion in constructor
    QTimer::singleShot(0, this, [this]() {
        PluginManager::instance()->loadPlugins();
        
        // Register a test hotkey for the Baidu OCR plugin
        // Alt + S = Sample OCR Trigger
        if (!PluginManager::instance()->allPlugins().isEmpty()) {
     // Register system-wide Hotkeys
    // Note: OCR and other plugins now register their own hotkeys in ocr_config.ini
    // and PluginManager handles the registration dynamically.
        }
    });

    //m_deskMonitor = new DirectoryMonitor(reinterpret_cast<HWND>(winId()),false, this);
    //m_deskMonitor->startListening();
    
    // 闁兼儳鍢茶ぐ鍥儑閻旈鏉介柣銊ュ椤㈡垿妫冮姀锝囩唴鐎?
    QString desktopPath;
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &path))) {
        desktopPath = QString::fromWCharArray(path);
        CoTaskMemFree(path);
    } else {
        // 闂傚嫬绉舵鍥棘鐟欏嫷鏀抽柨娑欑煯婵炲洭鎮介埉顨箃andardPaths
        desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    }
    m_desktopDir = desktopPath; // 濞ｅ洦绻傞悺銊╁礆閻楀牆鐏囬柛娑櫭ぐ澶愭煂?
    
    qDebug() << "闁烩晜鍨剁敮璺侯浖瀹€鍕〃閻犱警鍨扮欢?" << desktopPath;
    
    // 婵炲鍔岄崬浠嬫嚊椤忓嫮鏆板☉鏂款槺鐞氼偊宕圭€ｅ墎绀夐柛蹇庢祰椤斿繒鎹勯妸褍娈犵紒瀣儔濡诧箓宕氬鍙ョ箚闁告瑨娓圭槐鍫曟焻?
    qRegisterMetaType<FileSystemEventInfo>("FileSystemEventInfo");
    
    // 濞达綀娉曢弫顥瞚n闁烩晩鍠栫紞宥夋儎閹寸偛浠橀柛?
    m_linMonitor = new LinDirectoryMonitor(desktopPath, false, this);
    connect(m_linMonitor, &LinDirectoryMonitor::fileSystemChanged, this, &ListView::onFileSystemChanged);
    m_linMonitor->startListening();

    rightMenu = RightMenu::getInstance();
    rightMenu->initializeCustomMenu();
    rightMenu->setHWND((HWND)this->winId());

    // Set persistence path first so we can load viewMode before creating items
    setFocusPolicy(Qt::StrongFocus);
    m_pendingShellNewTargetTimer = new QTimer(this);
    m_pendingShellNewTargetTimer->setSingleShot(true);
    m_pendingShellNewTargetTimer->setInterval(15000);
    connect(m_pendingShellNewTargetTimer, &QTimer::timeout, this, &ListView::clearPendingShellNewTargetBox);
#ifdef QT_DEBUG
    m_persistencePath = QCoreApplication::applicationDirPath() + "/data.json";
#else
    m_persistencePath = QCoreApplication::applicationDirPath() + "/data.dat";
#endif
    
    // Load viewMode from config BEFORE creating any ListItem objects
    // This ensures ListItemData::s_targetIconSize is set correctly

    this->initialize();
    this->initRightMenuConnections();
    this->syncViewModeSizes(); // Call syncViewModeSizes after initialization
    
    // Initialize BoxManager
    BoxManager::instance(this);
    BoxManager::instance()->setPersistencePath(m_persistencePath);
    connect(BoxManager::instance(), &BoxManager::saveRequested, this, &ListView::saveBoxes);
    
    // Defer loading boxes to ensure the event loop is running and windows can be properly shown/mapped
    QTimer::singleShot(100, [this](){
        this->loadBoxes(true);
        const QString configPath = QCoreApplication::applicationDirPath() + "/config.ini";
        QSettings settings(configPath, QSettings::IniFormat);
        const bool isFirstLaunch = settings.value("Firslanuch", true).toBool();
        if (isFirstLaunch) {
            this->setPersistenceEnabled(true); 
            settings.setValue("Firslanuch", false);
            settings.sync();
        }
        this->onMenuRefresh();
        this->raise();

        // Open debug panel on startup
        auto* debugPanel = new ListViewDebugPanel(this);
        debugPanel->show();
    });
}

void ListView::updateSystemIconVisibility(const QString& clsid, bool hidden)
{
    qDebug() << "updateSystemIconVisibility:" << clsid << "->" << (hidden ? "hiding" : "showing");
    
    // 閻熸瑥瀚€垫牠宕犻張鍢桽ID闁哄秶鍘х槐锟犳晬閸懣鈺呮⒔閵堝懎璁查柤瀹犳閻°劑宕烽妸褎鐣?:闁告挸绉剁槐鎴︽晬?
    QString normalizedClsid = clsid;
    normalizedClsid.replace("::", "");
    
    if (hidden) {
        // 闂傚洠鍋撻悷鏇氱窔濞堬綁鎸婅箛鎾寸闁哄秴娴勭槐鐗堢鎼淬垺鈻旂紒鈧崫鍕仚閻炴稈鏆昣items濞戞搩鍘句簺闂?
        for (auto it = m_items.begin(); it != m_items.end(); ) {
            ListItem* item = *it;
            if (item) {
                QString itemClsid = item->getItemData().getCLSID();
                if (itemClsid.replace("::", "") == normalizedClsid) {
                    qDebug() << "Hiding item:" << item->baseName();
                    item->hide(); // 闂傚懏鍔樺Λ宀勫箳瑜屽▎?
                    it = m_items.erase(it); // 濞寸姴瀛╁Ο澶岀矆閸濆嫬鐏欓悶娑栧妺閼垫垹绮旀繝姘彑
                    // 婵炲鍔嶉崜浼存晬濮橆偆鐟濆ù鐘茬獓_allItems濞戞搩鍘句簺闂傚嫨鍊х槐婵嬪炊閻樿精绀嬮悗鐟板暞濡叉悂骞€鐠囨彃鐏欓悶?
                    continue; // 缂備綀鍛暰濞戞挸顑勭粩瀛樼▔椤忓嫭鍎曢柣婊庡灡閸ㄣ劑鎳撻崨顖涚函闁规亽鍎寸换鎴﹀炊閻戝洨绀夐弶鈺傜懇閸ｇ兘鎮介埡鐒tinue
                }
            }
            ++it;
        }
    }
    else {
        // 闂傚洠鍋撻悷鏇氱劍濡绮堥崫鍕闁哄秴娴勭槐鏉课涢埀顒勫蓟閵夛附笑闁告熬绠戦崙锟犲捶閵婏附鈻旂紒鈧崫鍕仚閻炴稏鍔嬮懙?
        bool alreadyVisible = false;
        for (const auto& item : m_items) {
             if (item) {
                QString itemClsid = item->getItemData().getCLSID();
                if (itemClsid.replace("::", "") == normalizedClsid) {
                    alreadyVisible = true;
                    break;
                }
             }
        }
        
        if (!alreadyVisible) {
            // 濞寸姴閰ｅ▓锝夋寠韫囨挸鐏欓悶娑栧妽閸ㄣ劑骞€鐠囨彃鐏欓悶娑栧妺閼垫垿寮婚妷锕€顥濇鐐跺煐閸у﹪宕濋悩鎻掔厒闁哄嫬澧介妵姘跺礆濡ゅ嫨鈧?
            // 濡絾鐗曢崢娑樜涢埀顒勫蓟椤х牎hiddenIcons闁挎稑鐗呯粭鎾绘⒒閵娿儳鎽犻柡鈧崜褔鍏囩紓浣哄枎濞存﹢寮介崶鈺傜暠濠㈣泛娲ㄩ弫銈夊礆濡ゅ嫨鈧啴鏁?
            bool found = false;
            for (auto* item : m_hiddenIcons) {
                if (item) {
                     QString itemClsid = item->getItemData().getCLSID();
                     if (itemClsid.replace("::", "") == normalizedClsid) {
                        qDebug() << "Restoring item from hidden icons:" << item->baseName();
                        m_items.push_back(item);
                        item->show();
                        found = true;
                        break;
                     }
                }
            }
            
            // 濠碘€冲€归悘澶愬捶閳侯槒ddenIcons婵炲备鍓濇竟姗€宕氱敮顔剧閻忓繑绻嗛惁顖炲捶閳烘姬lItems濞戞搩鍘芥竟姗€鏁嶉崼婊呯▕濞戞挸鎼崢瑙勬償閺囶亞绀?
            if (!found) {
                for (auto* item : m_allItems) {
                    if (item) {
                         QString itemClsid = item->getItemData().getCLSID();
                         if (itemClsid.replace("::", "") == normalizedClsid) {
                            // 缁绢収鍠曠换姘▔瀹ュ娅㈠璺虹У閸у﹪宕?
                            if (!m_items.contains(item)) {
                                qDebug() << "Restoring item from all items:" << item->baseName();
                                m_items.push_back(item);
                                item->show();
                                found = true;
                            }
                            break;
                         }
                    }
                }
            }
        }
    }
    
    // 闂佹彃绉甸弻濠囨焻濮樺磭绠栭柟鐑樺笒缁厼顕ｉ崫鍕厬闁告帡鏀遍弻濠冦亜閸濆嫮纰嶉柨娑樼墢闁绱掗悢閿嬬闁哄秴娲埀顒佽壘閻栧爼寮垫径濠冪ゼ閻庤宀搁妴搴㈡償韫囥儳绀夐弶鈺傜懇閸ｉ绮婚埀顒勫础閺囩喎鐦荤憸鐗堟尭婢х姷鎲撮崟顐㈢仧闂佹彃绉电敮鎾绘晬?
    // 濠碘€冲€归悘澶愭閳ь剛鎲版担椋庣闁归晲鑳舵竟鎺斺偓瑙勫哺閵嗗孩鎯旇箛銉х闁告瑯鍨甸崗姗€妫侀埀顒傛啺娴ｈ绾璺虹У濞煎懘鎯冮崟顒€绲婚柛蹇嬪劦閳ь剚妲掔欢?
    //sortItems(m_sortType);
    
    // Refresh our local snapshot of all system icon states before re-rendering
    if (monitor) {
        sysIconRegisterInfo = monitor->getCurrentIcons();
    }
    
    // 闂佹彃绉甸弻濠勬暜閸愩劎婀伴柛婊冪灱缁垶宕?
    loadIconLayer();
    update();
    repaint();
}

int maxGridRowsForBounds(int layoutTop, int usableBottom, int itemHeight, int strideY)
{
    int rows = 0;
    int gridY = 0;
    while (layoutTop + gridY * strideY + itemHeight - 1 <= usableBottom) {
        ++rows;
        ++gridY;
    }
    return qMax(1, rows);
}

int maxGridColsForBounds(int layoutLeft, int usableRight, int itemWidth, int strideX)
{
    int cols = 0;
    int gridX = 0;
    while (layoutLeft + gridX * strideX + itemWidth - 1 <= usableRight) {
        ++cols;
        ++gridX;
    }
    return qMax(1, cols);
}

void ListView::onWindowsShowHiddenFilesChanged(bool showHiddenFiles)
{
    if (m_showHiddenFiles == showHiddenFiles) {
        return;
    }

    m_showHiddenFiles = showHiddenFiles;
    refreshVisibilityForWindowsConfig();
}

void ListView::startListeningSpecialIcon()
{
    // 鐎殿喒鍋撳┑顔碱儑濞插啴宕?
    if (monitor->startListening()) {
        sysIconRegisterInfo = monitor->getCurrentIcons();

        /*for (const auto& icon : monitor->getCurrentIcons()) {
            qDebug() << icon.name << ":" << (icon.isHidden ? "hiddening" : "showing");
        }*/

        // 閻犱礁澧介悿鍡涘炊閻愬瓨鏆紒鏃€鐟╁▓锝夋寠?
        //monitor->setIconVisibility("{645FF040-5081-101B-9F08-00AA002F954E}", true);
    }
    // 閺夆晝鍋炵敮瀛樼┍閳ュ啿濞?
    connect(monitor, &DesktopInfoMonitor::iconVisibilityChanged,
        this, &ListView::updateSystemIconVisibility);
}

//bool ListView::nativeEvent(const QByteArray& eventType, void* message, long* result) {
//    MSG* msg = static_cast<MSG*>(message);
//    if (msg->message == WM_HOTKEY) {
//        handleHotkey(static_cast<int>(msg->wParam));
//        return true; // We handled it
//    }
//    if (msg->wParam == VK_SHIFT &&
//        (GetKeyState(VK_DELETE) & 0x8000) != 0) {
//        this->onMenuDelete(true);
//        if (result) {
//            *result = 0;
//        }
//        return true;
//    }
//    if (msg->message == WM_LINDESK_START_INLINE_RENAME) {
//        if (m_selectedItems.size() == 1 && m_selectedItems.first()) {
//            RenameManager::instance()->startRename(m_selectedItems.first());
//            if (result) {
//                *result = 0;
//            }
//            return true;
//        }
//    }
//    return QWidget::nativeEvent(eventType, message, result);
//}

void ListView::handleHotkey(int id) {
    QString actionId = HotkeyManager::instance()->getActionId(id);
    if (!actionId.isEmpty()) {
        qDebug() << "[ListView] Dispatching Hotkey ID:" << id << "to Action:" << actionId;
        PluginManager::instance()->triggerPlugin(actionId);
    }
}
ListView::~ListView() {
    saveBoxes();
    
    // Clean up drag tip
    if (m_dragTip) {
        delete m_dragTip;
        m_dragTip = nullptr;
    }

    if (m_infoTip) {
        delete m_infoTip;
        m_infoTip = nullptr;
    }
    
    // Clean up ghost widgets
    for (auto& ghost : m_ghosts) {
        if (ghost.widget) {
            ghost.widget->deleteLater();
        }
    }
    m_ghosts.clear();
    
    // Delete all ListItems in m_allItems
    qDeleteAll(m_allItems);
    m_allItems.clear();
    m_items.clear();
    m_selectedItems.clear();
}
DesktopRootItemState* ListView::findAnyItemStateByPath(const QString& filePath)
{
    if (DesktopRootItemState* state = findDesktopStateByPath(filePath)) {
        return state;
    }

    if (LinBox* box = BoxManager::instance()->findBoxContainingItem(filePath)) {
        return box->findItemStateByPath(filePath);
    }

    return nullptr;
}

const DesktopRootItemState* ListView::findAnyItemStateByPath(const QString& filePath) const
{
    if (const DesktopRootItemState* state = findDesktopStateByPath(filePath)) {
        return state;
    }

    if (LinBox* box = BoxManager::instance()->findBoxContainingItem(filePath)) {
        return box->findItemStateByPath(filePath);
    }

    return nullptr;
}
void ListView::clearCheckedItem()
{
    clearDesktopSelection();
    m_focusedDesktopPath.clear();
    m_anchorDesktopPath.clear();
    m_focusedBoxPath.clear();
    m_anchorBoxPath.clear();
    m_selectedBoxPaths.clear();

    for (auto* item : m_selectedItems) {
        if (item) {
            clearItemExpandedState(item);
            item->unCheckSelf();
        }
    }
    m_selectedItems.clear();
    
    // Also clear selection state for items inside boxes
    BoxManager* bm = BoxManager::instance();
    if (bm) {
        QSet<LinBox*> dirtyBoxes;
        for (const auto& box : bm->allBoxes()) {
            if (box) {
                for (DesktopRootItemState& state : const_cast<QVector<DesktopRootItemState>&>(box->itemStates())) {
                    if (state.model.isSelected) {
                        clearItemExpandedState(state.model.filePath);
                        state.model.isSelected = false;
                        state.model.renderData.selected = false;
                        dirtyBoxes.insert(box);
                    }
                    if (state.model.focused) {
                        state.model.focused = false;
                        dirtyBoxes.insert(box);
                    }
                }
            }
        }
        for (LinBox* dirtyBox : dirtyBoxes) {
            if (!dirtyBox) {
                continue;
            }
            dirtyBox->refreshLayout();
            dirtyBox->update();
        }
    }
}

void ListView::clearSelectionState()
{
    clearCheckedItem();
    setFocusedItem(nullptr);
    m_anchorItem = nullptr;
    clearCutMarks();
    resetRenameClickState();
}

void ListView::setSelectedItemPaths(const QStringList& filePaths, const QString& focusedPath, bool expandSingle)
{
    const QString normalizedFocusedPath = normalizeDesktopItemPath(focusedPath);
    QStringList desktopPaths;
    QStringList boxPaths;

    for (const QString& path : filePaths) {
        const QString normalizedPath = normalizeDesktopItemPath(path);
        if (normalizedPath.isEmpty()) {
            continue;
        }

        if (const DesktopRootItemState* state = findDesktopStateByPath(normalizedPath)) {
            if (state->model.boxId.isEmpty()) {
                if (!desktopPaths.contains(normalizedPath)) {
                    desktopPaths.append(normalizedPath);
                }
                continue;
            }
        }

        if (findAnyItemStateByPath(normalizedPath) && !boxPaths.contains(normalizedPath)) {
            boxPaths.append(normalizedPath);
        }
    }

    if (!desktopPaths.isEmpty()) {
        BoxManager* bm = BoxManager::instance();
        if (bm) {
            QSet<LinBox*> dirtyBoxes;
            for (const QString& path : m_selectedBoxPaths) {
                clearItemExpandedState(path);
            }
            for (const auto& box : bm->allBoxes()) {
                if (!box) {
                    continue;
                }
                for (DesktopRootItemState& state : const_cast<QVector<DesktopRootItemState>&>(box->itemStates())) {
                    if (state.model.isSelected || state.model.focused) {
                        state.model.isSelected = false;
                        state.model.renderData.selected = false;
                        state.model.focused = false;
                        dirtyBoxes.insert(box);
                    }
                }
            }
            for (LinBox* dirtyBox : dirtyBoxes) {
                if (!dirtyBox) {
                    continue;
                }
                dirtyBox->refreshLayout();
                dirtyBox->update();
            }
        }
        m_selectedBoxPaths.clear();
        m_focusedBoxPath.clear();
        m_anchorBoxPath.clear();
        m_selectedItems.clear();
        m_anchorItem = nullptr;
        setFocusedItem(nullptr);
        applyDesktopSelection(desktopPaths,
            normalizedFocusedPath.isEmpty() ? desktopPaths.last() : normalizedFocusedPath,
            expandSingle && desktopPaths.size() == 1);
        if (m_anchorDesktopPath.isEmpty()) {
            m_anchorDesktopPath = desktopPaths.first();
        }
        return;
    }

    if (!boxPaths.isEmpty()) {
        clearDesktopSelection();
        m_focusedDesktopPath.clear();
        m_anchorDesktopPath.clear();
        for (const QString& path : m_selectedBoxPaths) {
            clearItemExpandedState(path);
        }

        BoxManager* bm = BoxManager::instance();
        QSet<LinBox*> dirtyBoxes;
        const QString resolvedFocusedPath = normalizedFocusedPath.isEmpty() ? boxPaths.last() : normalizedFocusedPath;
        if (bm) {
            for (const auto& box : bm->allBoxes()) {
                if (!box) {
                    continue;
                }
                bool boxDirty = false;
                for (DesktopRootItemState& state : const_cast<QVector<DesktopRootItemState>&>(box->itemStates())) {
                    const QString statePath = normalizeDesktopItemPath(state.model.filePath);
                    const bool selected = boxPaths.contains(statePath);
                    const bool focused = statePath == resolvedFocusedPath;
                    if (state.model.isSelected != selected) {
                        state.model.isSelected = selected;
                        state.model.renderData.selected = selected;
                        boxDirty = true;
                    }
                    if (state.model.focused != focused) {
                        state.model.focused = focused;
                        boxDirty = true;
                    }
                }
                if (boxDirty) {
                    dirtyBoxes.insert(box);
                }
            }
        }

        for (const QString& path : boxPaths) {
            setItemFullNameExpanded(path, false);
        }
        if (expandSingle && boxPaths.size() == 1) {
            setItemFullNameExpanded(boxPaths.first(), true);
        }

        for (LinBox* dirtyBox : dirtyBoxes) {
            if (!dirtyBox) {
                continue;
            }
            dirtyBox->refreshLayout();
            dirtyBox->update();
        }

        m_selectedItems.clear();
        m_selectedBoxPaths = boxPaths.toVector();
        m_focusedBoxPath = resolvedFocusedPath;
        if (m_anchorBoxPath.isEmpty()) {
            m_anchorBoxPath = boxPaths.first();
        }
        m_anchorItem = nullptr;
        setFocusedItem(nullptr);
        return;
    }

    clearSelectionState();
}

void ListView::setSelectedItems(const QVector<ListItem*>& items, bool expandSingle)
{
    applySelectionState(items, items.isEmpty() ? nullptr : items.last(), expandSingle);
}

QVector<ListItem*> ListView::selectableItemsForHost(LinBox* box) const
{
    QVector<ListItem*> items;
    if (box) {
        return items;
    }

    for (ListItem* item : m_items) {
        if (item && item->isVisible() && item->boxId().isEmpty()) {
            items.append(item);
        }
    }
    return items;
}

void ListView::applySelectionState(const QVector<ListItem*>& items, ListItem* focusedItem, bool expandSingle)
{
    for (auto* item : m_selectedItems) {
        if (item) {
            clearItemExpandedState(item);
            item->unCheckSelf();
        }
    }

    QVector<ListItem*> filteredItems;
    filteredItems.reserve(items.size());
    for (ListItem* item : items) {
        if (!item || filteredItems.contains(item)) {
            continue;
        }
        filteredItems.append(item);
    }

    for (ListItem* item : filteredItems) {
        setItemFullNameExpanded(item, false);
        item->checkSelf();
    }

    if (expandSingle && filteredItems.size() == 1 && filteredItems.first()) {
        setItemFullNameExpanded(filteredItems.first(), true);
    }

    m_selectedItems = filteredItems;
    m_selectedBoxPaths.clear();
    m_selectedBoxPaths.reserve(filteredItems.size());
    for (ListItem* item : filteredItems) {
        const QString normalizedPath = item ? item->normalizedPath() : QString();
        if (!normalizedPath.isEmpty() && !m_selectedBoxPaths.contains(normalizedPath)) {
            m_selectedBoxPaths.append(normalizedPath);
        }
    }

    setFocusedItem(focusedItem);
    if (!m_anchorItem && focusedItem) {
        m_anchorItem = focusedItem;
    }
    m_focusedBoxPath = focusedItem ? focusedItem->normalizedPath() : QString();
    if (m_anchorBoxPath.isEmpty() && focusedItem) {
        m_anchorBoxPath = focusedItem->normalizedPath();
    }
}

void ListView::setItemFullNameExpanded(ListItem* item, bool expanded)
{
    if (!item) {
        return;
    }

    ensureModelStateForLegacyItem(item);
    DesktopRootItemState* state = findModelStateForLegacyItem(item);
    if (!state) {
        return;
    }

    state->model.isFullNameExpanded = expanded;
    if (!expanded) {
        state->model.expandedPreviewText.clear();
    }

    if (item->isFullNameExpanded() != expanded) {
        item->setFullNameExpanded(expanded);
    } else if (!expanded && !item->expandedPreviewText().isEmpty()) {
        item->setExpandedPreviewText(QString());
    }
}

void ListView::setItemExpandedPreviewText(ListItem* item, const QString& text)
{
    if (!item) {
        return;
    }

    ensureModelStateForLegacyItem(item);
    DesktopRootItemState* state = findModelStateForLegacyItem(item);
    if (!state) {
        return;
    }

    state->model.expandedPreviewText = text;
    if (item->expandedPreviewText() != text) {
        item->setExpandedPreviewText(text);
    }
}

void ListView::clearItemExpandedState(ListItem* item)
{
    if (!item) {
        return;
    }

    setItemExpandedPreviewText(item, QString());
    setItemFullNameExpanded(item, false);
}

void ListView::setItemFullNameExpanded(const QString& filePath, bool expanded)
{
    if (DesktopRootItemState* state = findDesktopStateByPath(filePath)) {
        state->model.isFullNameExpanded = expanded;
        if (!expanded) {
            state->model.expandedPreviewText.clear();
        }
        updateAllItemRenderData();
        update();
        return;
    }

    if (LinBox* box = BoxManager::instance()->findBoxContainingItem(filePath)) {
        if (DesktopRootItemState* state = box->findItemStateByPath(filePath)) {
            state->model.isFullNameExpanded = expanded;
            if (!expanded) {
                state->model.expandedPreviewText.clear();
            }
            box->refreshLayout();
            box->update();
            return;
        }
    }
}

void ListView::setItemExpandedPreviewText(const QString& filePath, const QString& text)
{
    if (DesktopRootItemState* state = findDesktopStateByPath(filePath)) {
        state->model.expandedPreviewText = text;
        updateAllItemRenderData();
        update();
        return;
    }

    if (LinBox* box = BoxManager::instance()->findBoxContainingItem(filePath)) {
        if (DesktopRootItemState* state = box->findItemStateByPath(filePath)) {
            state->model.expandedPreviewText = text;
            box->refreshLayout();
            box->update();
            return;
        }
    }
}

void ListView::clearItemExpandedState(const QString& filePath)
{
    setItemExpandedPreviewText(filePath, QString());
    setItemFullNameExpanded(filePath, false);
}

void ListView::setItemRenaming(const QString& filePath, bool renaming)
{
    if (DesktopRootItemState* state = findDesktopStateByPath(filePath)) {
        state->model.isRenaming = renaming;
        updateAllItemRenderData();
        update();
        return;
    }

    if (LinBox* box = BoxManager::instance()->findBoxContainingItem(filePath)) {
        if (DesktopRootItemState* state = box->findItemStateByPath(filePath)) {
            state->model.isRenaming = renaming;
            box->refreshLayout();
            box->update();
            return;
        }
    }
}

void ListView::setFocusedItemPath(const QString& filePath)
{
    const QString normalizedPath = normalizeDesktopItemPath(filePath);
    BoxManager* bm = BoxManager::instance();
    QSet<LinBox*> dirtyBoxes;
    auto updateBoxFocusState = [&](const QString& targetPath) {
        if (!bm) {
            return;
        }
        for (const auto& box : bm->allBoxes()) {
            if (!box) {
                continue;
            }
            bool boxDirty = false;
            for (DesktopRootItemState& state : const_cast<QVector<DesktopRootItemState>&>(box->itemStates())) {
                const bool focused = !targetPath.isEmpty()
                    && normalizeDesktopItemPath(state.model.filePath) == targetPath;
                if (state.model.focused != focused) {
                    state.model.focused = focused;
                    boxDirty = true;
                }
            }
            if (boxDirty) {
                dirtyBoxes.insert(box);
            }
        }
    };

    if (normalizedPath.isEmpty()) {
        setFocusedItem(nullptr);
        m_focusedDesktopPath.clear();
        m_focusedBoxPath.clear();
        updateBoxFocusState(QString());
        for (LinBox* dirtyBox : dirtyBoxes) {
            if (!dirtyBox) {
                continue;
            }
            dirtyBox->refreshLayout();
            dirtyBox->update();
        }
        return;
    }

    if (const DesktopRootItemState* state = findDesktopStateByPath(normalizedPath)) {
        if (state->model.boxId.isEmpty()) {
            setFocusedItem(nullptr);
            m_focusedDesktopPath = normalizedPath;
            m_focusedBoxPath.clear();
            updateBoxFocusState(QString());
            for (LinBox* dirtyBox : dirtyBoxes) {
                if (!dirtyBox) {
                    continue;
                }
                dirtyBox->refreshLayout();
                dirtyBox->update();
            }
            return;
        }
    }

    if (findAnyItemStateByPath(normalizedPath)) {
        setFocusedItem(nullptr);
        m_focusedDesktopPath.clear();
        m_focusedBoxPath = normalizedPath;
        updateBoxFocusState(normalizedPath);
        for (LinBox* dirtyBox : dirtyBoxes) {
            if (!dirtyBox) {
                continue;
            }
            dirtyBox->refreshLayout();
            dirtyBox->update();
        }
        return;
    }

    setFocusedItem(nullptr);
    m_focusedBoxPath.clear();
    updateBoxFocusState(QString());
    for (LinBox* dirtyBox : dirtyBoxes) {
        if (!dirtyBox) {
            continue;
        }
        dirtyBox->refreshLayout();
        dirtyBox->update();
    }
}

void ListView::setFocusedItem(ListItem* item)
{
    m_focusedDesktopPath.clear();
    if (m_focusedItem) {
        m_focusedItem->setFocused(false);
    }
    m_focusedItem = item;
    if (m_focusedItem) {
        m_focusedItem->setFocused(true);
        m_focusedItem->raise();
    }
}

void ListView::toggleItemSelection(const QString& filePath)
{
    const QString normalizedPath = normalizeDesktopItemPath(filePath);
    if (normalizedPath.isEmpty()) {
        return;
    }

    if (const DesktopRootItemState* state = findDesktopStateByPath(normalizedPath)) {
        if (state->model.boxId.isEmpty()) {
            QStringList updatedPaths = selectedDesktopPaths();
            if (updatedPaths.contains(normalizedPath)) {
                updatedPaths.removeAll(normalizedPath);
                clearItemExpandedState(normalizedPath);
            } else {
                updatedPaths.append(normalizedPath);
                setItemFullNameExpanded(normalizedPath, false);
            }
            applyDesktopSelection(updatedPaths, normalizedPath, false);
            if (m_anchorDesktopPath.isEmpty() && !updatedPaths.isEmpty()) {
                m_anchorDesktopPath = normalizedPath;
            }
            return;
        }
    }

    if (findAnyItemStateByPath(normalizedPath)) {
        QStringList updatedPaths = selectedBoxPaths();
        if (m_selectedBoxPaths.contains(normalizedPath)) {
            updatedPaths.removeAll(normalizedPath);
            clearItemExpandedState(normalizedPath);
        } else {
            updatedPaths.append(normalizedPath);
            setItemFullNameExpanded(normalizedPath, false);
        }
        setSelectedItemPaths(updatedPaths, normalizedPath, false);
        if (m_anchorBoxPath.isEmpty() && !updatedPaths.isEmpty()) {
            m_anchorBoxPath = normalizedPath;
        }
    }
}

void ListView::toggleItemSelection(ListItem* item)
{
    if (!item) {
        return;
    }

    if (m_selectedItems.contains(item)) {
        m_selectedItems.removeOne(item);
        m_selectedBoxPaths.removeOne(item->normalizedPath());
        clearItemExpandedState(item);
        item->unCheckSelf();
    } else {
        m_selectedItems.append(item);
        if (!m_selectedBoxPaths.contains(item->normalizedPath())) {
            m_selectedBoxPaths.append(item->normalizedPath());
        }
        setItemFullNameExpanded(item, false);
        item->checkSelf();
    }

    setFocusedItem(item);
    m_focusedBoxPath = item->normalizedPath();
}

void ListView::selectRangeToPath(const QString& filePath, LinBox* box)
{
    const QString normalizedPath = normalizeDesktopItemPath(filePath);
    if (normalizedPath.isEmpty()) {
        return;
    }

    if (!box) {
        const QStringList orderedPaths = selectableDesktopPaths();
        if (orderedPaths.isEmpty()) {
            return;
        }

        const QString anchorPath = m_anchorDesktopPath.isEmpty() ? normalizedPath : m_anchorDesktopPath;
        const int anchorIndex = orderedPaths.indexOf(normalizeDesktopItemPath(anchorPath));
        const int itemIndex = orderedPaths.indexOf(normalizedPath);
        if (anchorIndex < 0 || itemIndex < 0) {
            applyDesktopSelection({ normalizedPath }, normalizedPath, true);
            m_anchorDesktopPath = normalizedPath;
            return;
        }

        QStringList rangePaths;
        const int start = qMin(anchorIndex, itemIndex);
        const int end = qMax(anchorIndex, itemIndex);
        for (int i = start; i <= end; ++i) {
            rangePaths.append(orderedPaths.at(i));
        }
        applyDesktopSelection(rangePaths, normalizedPath, false);
        return;
    }

    const QStringList orderedPaths = selectableBoxPaths(box);
    if (orderedPaths.isEmpty()) {
        return;
    }

    const QString anchorPath = m_anchorBoxPath.isEmpty() ? normalizedPath : m_anchorBoxPath;
    const int anchorIndex = orderedPaths.indexOf(normalizeDesktopItemPath(anchorPath));
    const int itemIndex = orderedPaths.indexOf(normalizedPath);
    if (anchorIndex < 0 || itemIndex < 0) {
        setSelectedItemPaths({ normalizedPath }, normalizedPath, true);
        m_anchorBoxPath = normalizedPath;
        return;
    }

    QStringList rangePaths;
    const int start = qMin(anchorIndex, itemIndex);
    const int end = qMax(anchorIndex, itemIndex);
    for (int i = start; i <= end; ++i) {
        rangePaths.append(orderedPaths.at(i));
    }
    setSelectedItemPaths(rangePaths, normalizedPath, false);
}

void ListView::selectRangeToItem(ListItem* item, LinBox* box)
{
    if (!item) {
        return;
    }

    QVector<ListItem*> orderedItems = selectableItemsForHost(box);
    if (orderedItems.isEmpty()) {
        return;
    }

    ListItem* anchor = m_anchorItem ? m_anchorItem.data() : item;
    const int anchorIndex = orderedItems.indexOf(anchor);
    const int itemIndex = orderedItems.indexOf(item);
    if (anchorIndex < 0 || itemIndex < 0) {
        applySelectionState({ item }, item, true);
        m_anchorItem = item;
        return;
    }

    QVector<ListItem*> rangeItems;
    const int from = qMin(anchorIndex, itemIndex);
    const int to = qMax(anchorIndex, itemIndex);
    rangeItems.reserve(to - from + 1);
    for (int i = from; i <= to; ++i) {
        rangeItems.append(orderedItems[i]);
    }
    applySelectionState(rangeItems, item, false);
}

void ListView::moveFocusInHost(LinBox* box, FocusMoveDirection direction, bool extendSelection, bool focusOnly)
{
    if (!box) {
        const QStringList orderedPaths = selectableDesktopPaths();
        if (orderedPaths.isEmpty()) {
            return;
        }

        const QString currentPath = effectiveFocusedDesktopPath();
        QString targetPath = findDirectionalDesktopFocusCandidate(currentPath, direction);
        if (targetPath.isEmpty() && !currentPath.isEmpty()) {
            const int currentIndex = orderedPaths.indexOf(currentPath);
            if (currentIndex >= 0) {
                const int delta = (direction == FocusMoveDirection::Left || direction == FocusMoveDirection::Up) ? -1 : 1;
                const int fallbackIndex = qBound(0, currentIndex + delta, orderedPaths.size() - 1);
                targetPath = orderedPaths.at(fallbackIndex);
            }
        }

        if (targetPath.isEmpty()) {
            return;
        }

        if (focusOnly) {
            m_focusedDesktopPath = targetPath;
            return;
        }

        if (extendSelection) {
            QString anchorPath = m_anchorDesktopPath.isEmpty() ? targetPath : m_anchorDesktopPath;
            const int anchorIndex = orderedPaths.indexOf(normalizeDesktopItemPath(anchorPath));
            const int itemIndex = orderedPaths.indexOf(targetPath);
            if (anchorIndex >= 0 && itemIndex >= 0) {
                QStringList rangePaths;
                const int start = qMin(anchorIndex, itemIndex);
                const int end = qMax(anchorIndex, itemIndex);
                for (int i = start; i <= end; ++i) {
                    rangePaths.append(orderedPaths.at(i));
                }
                applyDesktopSelection(rangePaths, targetPath, false);
            } else {
                applyDesktopSelection({ targetPath }, targetPath, true);
            }
            if (m_anchorDesktopPath.isEmpty()) {
                m_anchorDesktopPath = anchorPath;
            }
            return;
        }

        applyDesktopSelection({ targetPath }, targetPath, true);
        m_anchorDesktopPath = targetPath;
        return;
    }

    const QStringList orderedPaths = selectableBoxPaths(box);
    if (orderedPaths.isEmpty()) {
        return;
    }

    const QString currentPath = effectiveFocusedBoxPath();
    QString targetPath = findDirectionalBoxFocusCandidate(box, currentPath, direction);

    if (targetPath.isEmpty() && !currentPath.isEmpty()) {
        const int currentIndex = orderedPaths.indexOf(currentPath);
        if (currentIndex >= 0) {
            const int delta = (direction == FocusMoveDirection::Left || direction == FocusMoveDirection::Up) ? -1 : 1;
            const int fallbackIndex = qBound(0, currentIndex + delta, orderedPaths.size() - 1);
            targetPath = orderedPaths.at(fallbackIndex);
        }
    }

    if (targetPath.isEmpty()) {
        return;
    }

    if (focusOnly) {
        setFocusedItemPath(targetPath);
        return;
    }

    if (extendSelection) {
        const QString anchorPath = m_anchorBoxPath.isEmpty() ? targetPath : m_anchorBoxPath;
        const int anchorIndex = orderedPaths.indexOf(normalizeDesktopItemPath(anchorPath));
        const int itemIndex = orderedPaths.indexOf(targetPath);
        if (anchorIndex >= 0 && itemIndex >= 0) {
            QStringList rangePaths;
            const int start = qMin(anchorIndex, itemIndex);
            const int end = qMax(anchorIndex, itemIndex);
            for (int i = start; i <= end; ++i) {
                rangePaths.append(orderedPaths.at(i));
            }
            setSelectedItemPaths(rangePaths, targetPath, false);
        } else {
            setSelectedItemPaths({ targetPath }, targetPath, true);
        }
        if (m_anchorBoxPath.isEmpty()) {
            m_anchorBoxPath = anchorPath;
        }
        return;
    }

    setSelectedItemPaths({ targetPath }, targetPath, true);
    m_anchorBoxPath = targetPath;
}

void ListView::clearCutMarks()
{
    for (auto& state : m_itemStates) {
        state.model.isCutMarked = false;
        state.model.renderData.cutMarked = false;
    }

    BoxManager* bm = BoxManager::instance();
    if (bm) {
        QSet<LinBox*> dirtyBoxes;
        for (const auto& box : bm->allBoxes()) {
            if (!box) {
                continue;
            }
            bool boxDirty = false;
            for (DesktopRootItemState& state : const_cast<QVector<DesktopRootItemState>&>(box->itemStates())) {
                if (!state.model.isCutMarked && !state.model.renderData.cutMarked) {
                    continue;
                }
                state.model.isCutMarked = false;
                state.model.renderData.cutMarked = false;
                boxDirty = true;
            }
            if (boxDirty) {
                dirtyBoxes.insert(box);
            }
        }
        for (LinBox* dirtyBox : dirtyBoxes) {
            if (!dirtyBox) {
                continue;
            }
            dirtyBox->refreshLayout();
            dirtyBox->update();
        }
    }

    for (ListItem* item : m_allItems) {
        if (!item) {
            continue;
        }
        item->setProperty("cutMarked", false);
        item->update();
    }
}

void ListView::applyCutMarks(const QVector<ListItem*>& items)
{
    clearCutMarks();
    for (ListItem* item : items) {
        if (!item) {
            continue;
        }
        item->setProperty("cutMarked", true);
        item->update();
    }
}

QStringList ListView::selectedItemPaths() const
{
    QStringList paths;
    paths.append(selectedDesktopPaths());
    for (const QString& path : m_selectedBoxPaths) {
        if (!path.isEmpty() && !paths.contains(path)) {
            paths.append(path);
        }
    }
    return paths;
}

QString ListView::focusedItemPath() const
{
    if (!m_focusedDesktopPath.isEmpty()) {
        return effectiveFocusedDesktopPath();
    }
    if (!m_focusedBoxPath.isEmpty()) {
        return effectiveFocusedBoxPath();
    }
    return m_focusedItem ? m_focusedItem->normalizedPath() : QString();
}

QString ListView::anchorItemPath() const
{
    if (!m_anchorDesktopPath.isEmpty()) {
        return effectiveAnchorDesktopPath();
    }
    if (!m_anchorBoxPath.isEmpty()) {
        return effectiveAnchorBoxPath();
    }
    return m_anchorItem ? m_anchorItem->normalizedPath() : QString();
}

QString ListView::draggedItemPath() const
{
    if (!m_draggedDesktopPath.isEmpty()) {
        return normalizeDesktopItemPath(m_draggedDesktopPath);
    }
    return m_draggedItem ? m_draggedItem->normalizedPath() : QString();
}

void ListView::restoreSelectionStateByPaths(const QStringList& selectedPaths, const QString& focusedPath, const QString& anchorPath, bool expandSingle)
{
    setSelectedItemPaths(selectedPaths, focusedPath, expandSingle);
    m_anchorDesktopPath = anchorPath.isEmpty() ? QString() : normalizeDesktopItemPath(anchorPath);
    m_anchorBoxPath = anchorPath.isEmpty() ? QString() : normalizeDesktopItemPath(anchorPath);
    m_anchorItem = nullptr;
}

void ListView::writeSelectionToClipboard(bool cutOperation)
{
    if (m_selectedBoxPaths.isEmpty() && m_selectedDesktopPaths.isEmpty()) {
        return;
    }

    QList<QUrl> urls;
    for (const QString& path : m_selectedDesktopPaths) {
        const DesktopRootItemState* state = findDesktopStateByPath(path);
        if (!state) {
            continue;
        }
        if (state->model.objectType == ObjectType::SpecialFolder) {
            continue;
        }
        const QString filePath = state->model.filePath;
        if (filePath.isEmpty() || filePath.startsWith("::")) {
            continue;
        }
        urls.append(QUrl::fromLocalFile(filePath));
    }
    for (const QString& path : selectedBoxPaths()) {
        const DesktopRootItemState* state = findAnyItemStateByPath(path);
        if (!state) {
            continue;
        }
        if (state->model.objectType == ObjectType::SpecialFolder) {
            continue;
        }
        const QString filePath = state->model.filePath;
        if (filePath.isEmpty() || filePath.startsWith("::")) {
            continue;
        }
        urls.append(QUrl::fromLocalFile(filePath));
    }

    if (urls.isEmpty()) {
        return;
    }

    ListViewFileOpsHelper::writeUrlsToClipboard(urls, cutOperation);
    if (cutOperation) {
        for (const QString& path : m_selectedDesktopPaths) {
            if (DesktopRootItemState* state = findDesktopStateByPath(path)) {
                state->model.isCutMarked = true;
                state->model.renderData.cutMarked = true;
            }
        }

        BoxManager* bm = BoxManager::instance();
        if (bm) {
            QSet<LinBox*> dirtyBoxes;
            for (const QString& path : selectedBoxPaths()) {
                if (LinBox* box = bm->findBoxContainingItem(path)) {
                    if (DesktopRootItemState* state = box->findItemStateByPath(path)) {
                        state->model.isCutMarked = true;
                        state->model.renderData.cutMarked = true;
                        dirtyBoxes.insert(box);
                    }
                }
            }
            for (LinBox* dirtyBox : dirtyBoxes) {
                if (!dirtyBox) {
                    continue;
                }
                dirtyBox->refreshLayout();
                dirtyBox->update();
            }
        }
    } else {
        clearCutMarks();
    }
}

void ListView::armPendingShellNewTargetBox(LinBox* box)
{
    m_pendingShellNewTargetBox = box;
    if (m_pendingShellNewTargetTimer) {
        m_pendingShellNewTargetTimer->start();
    }
}

void ListView::clearPendingShellNewTargetBox()
{
    m_pendingShellNewTargetBox.clear();
    if (m_pendingShellNewTargetTimer) {
        m_pendingShellNewTargetTimer->stop();
    }
}

void ListView::requestSystemRightMenu(ObjectType objType, QPoint point, const QVector<ListItem*> checkedItems)
{
    RightMenu* r = RightMenu::getInstance();
    QVector<DesktopRootItemModel> models;
    models.reserve(qMax<int>(checkedItems.size(), selectedItemPaths().size()));
    for (ListItem* item : checkedItems) {
        if (!item) {
            continue;
        }
        if (const DesktopRootItemState* state = findAnyItemStateByPath(item->filePath())) {
            models.push_back(state->model);
        }
    }

    if (models.isEmpty() && objType != ObjectType::BackGround) {
        for (const QString& path : selectedItemPaths()) {
            if (const DesktopRootItemState* state = findAnyItemStateByPath(path)) {
                bool exists = false;
                for (const DesktopRootItemModel& model : models) {
                    if (normalizeDesktopItemPath(model.filePath) == normalizeDesktopItemPath(state->model.filePath)) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    models.push_back(state->model);
                }
            }
        }
    }

    if (checkedItems.size() == 1 || models.size() == 1) {
        r->setShowType(objType);
        if (!models.isEmpty()) {
            r->showMenu(point, models.first());
        }
        return;
    }
    else if (objType == ObjectType::BackGround) {
        r->setShowType(ObjectType::BackGround);
        r->showMenu(point, DesktopRootItemModel{});
        return;
    }
    else {
        r->setShowType(ObjectType::MultObject);
        if (!models.isEmpty()) {
            r->showMultObjectMenu(point, models);
        }
    }
}

void ListView::refreshCheckedItem(QRect rect)
{
    QStringList nextSelection;
    nextSelection.reserve(m_itemStates.size());
    for (const auto& state : m_itemStates) {
        if (!shouldPaintDesktopModelState(state)) {
            continue;
        }
        const QString normalizedPath = normalizeDesktopItemPath(state.model.filePath);
        if (normalizedPath.isEmpty()) {
            continue;
        }
        if (state.model.renderData.selectionContentRect.intersects(rect)) {
            nextSelection.append(normalizedPath);
        }
    }

    const QString focusPath = nextSelection.isEmpty()
        ? QString()
        : (m_focusedDesktopPath.isEmpty() ? nextSelection.last() : m_focusedDesktopPath);
    applyDesktopSelection(nextSelection, focusPath, false);
    if (!nextSelection.isEmpty() && m_anchorDesktopPath.isEmpty()) {
        m_anchorDesktopPath = nextSelection.first();
    }
}

void ListView::paintEvent(QPaintEvent* event)
{
    updateAllItemRenderData();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::TextAntialiasing);

    // 缂佹ê鍩楀宀勬桨妞?(Model-Driven Stateless Rendering)
    for (const auto& state : m_itemStates) {
        if (!shouldPaintDesktopModelState(state)) continue;
        
        // 閹嗗厴娴兼ê瀵查敍姘涧缂佹ê鍩楅崷銊ョ秼閸撳秷鍓伴崠鍝勭厵閸愬懐娈戞い?
        if (!event->rect().intersects(state.model.renderData.itemRect)) {
            continue;
        }

        paintDesktopRootItem(painter, state.model.renderData);
    }

    const QRect indicatorRect = desktopAutoArrangeInsertionIndicatorRect();
    if (!indicatorRect.isNull()) {
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 220, 220, 80));
        painter.drawRoundedRect(indicatorRect.adjusted(-4, -3, 4, 3), 5, 5);
        painter.setBrush(QColor(0, 220, 220, 230));
        painter.drawRoundedRect(indicatorRect, 2, 2);
        painter.restore();
    }
}

void ListView::contextMenuEvent(QContextMenuEvent* event)
{
    if (event->reason() == QContextMenuEvent::Mouse && this->rect().contains(event->pos())) {
        if (DesktopRootItemState* hitState = findDesktopStateAt(event->pos())) {
            const QString itemPath = normalizeDesktopItemPath(hitState->model.filePath);
            if (!isDesktopPathSelected(itemPath)) {
                applyDesktopSelection({ itemPath }, itemPath, true);
                m_anchorDesktopPath = itemPath;
            } else {
                m_focusedDesktopPath = itemPath;
            }

            QVector<DesktopRootItemModel> desktopModels = selectedDesktopModels();
            RightMenu* menu = RightMenu::getInstance();
            if (desktopModels.size() == 1) {
                menu->setShowType(hitState->model.objectType);
                menu->showMenu(event->globalPos(), desktopModels.first());
            } else if (!desktopModels.isEmpty()) {
                menu->setShowType(ObjectType::MultObject);
                menu->showMultObjectMenu(event->globalPos(), desktopModels);
            }
            event->accept();
            return;
        }

        auto rightClickPoint = mapToGlobal(event->pos());
        this->m_lastRightClickPos = event->pos();

        // Since ListItems now intercept their own contextMenuEvent and emit signals,
        // this handler is only reached if the background (desktop) is clicked.
        
        // 濠碘€冲€归悘澶愬矗閹惰姤鏆涢柣鎰嚀閸ゎ喗鎷呭鍥╂瀭濞戞挸绉村﹢顏呯鐠佸磭绉块柛銉у亾閻栵綁宕犻崫鍕幍闁告劕鎷戠槐婵嗐€掗崨瀛樼彑鐎圭寮跺﹢渚€鏌呮径澶庡幀
        clearCheckedItem();
        // 闁哄嫬澧介妵姘浖瀹€鍕〃闁汇劌瀚ぐ鎼佹煥椤斿灝缍呴柛?
        requestSystemRightMenu(ObjectType::BackGround, rightClickPoint, {});
    }
}

void ListView::mousePressEvent(QMouseEvent* event)
{
    RenameManager::instance()->cancelRename();
    if (event->button() == Qt::LeftButton) {
        if (DesktopRootItemState* hitState = findDesktopStateAt(event->pos())) {
            this->activateWindow();
            this->setFocus(Qt::MouseFocusReason);

            const QString itemPath = normalizeDesktopItemPath(hitState->model.filePath);
            const Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();

            if (modifiers & Qt::ControlModifier) {
                const bool selected = isDesktopPathSelected(itemPath);
                setDesktopItemSelected(itemPath, !selected);
                m_focusedDesktopPath = itemPath;
                if (m_anchorDesktopPath.isEmpty()) {
                    m_anchorDesktopPath = itemPath;
                }
                m_wasSelectedBeforeClick = false;
            } else if (modifiers & Qt::ShiftModifier) {
                QStringList orderedPaths;
                for (const auto& state : m_itemStates) {
                    if (shouldPaintDesktopModelState(state)) {
                        orderedPaths.append(normalizeDesktopItemPath(state.model.filePath));
                    }
                }

                const QString anchorPath = m_anchorDesktopPath.isEmpty() ? itemPath : m_anchorDesktopPath;
                const int anchorIndex = orderedPaths.indexOf(anchorPath);
                const int itemIndex = orderedPaths.indexOf(itemPath);
                if (anchorIndex >= 0 && itemIndex >= 0) {
                    QStringList rangePaths;
                    const int start = qMin(anchorIndex, itemIndex);
                    const int end = qMax(anchorIndex, itemIndex);
                    for (int i = start; i <= end; ++i) {
                        rangePaths.append(orderedPaths.at(i));
                    }
                    applyDesktopSelection(rangePaths, itemPath, false);
                } else {
                    applyDesktopSelection({ itemPath }, itemPath, true);
                    m_anchorDesktopPath = itemPath;
                }
                m_wasSelectedBeforeClick = false;
            } else {
                if (!isDesktopPathSelected(itemPath)) {
                    applyDesktopSelection({ itemPath }, itemPath, true);
                    m_anchorDesktopPath = itemPath;
                    m_wasSelectedBeforeClick = false;
                } else {
                    m_focusedDesktopPath = itemPath;
                    hitState->model.isFullNameExpanded = true;
                    m_wasSelectedBeforeClick = (m_selectedDesktopPaths.size() == 1);
                }
            }

            m_lastClickedDesktopPath = itemPath;
            m_clickTimer.restart();
            if (!(modifiers & Qt::ControlModifier) && !(modifiers & Qt::ShiftModifier)) {
                m_anchorDesktopPath = itemPath;
            }

            m_draggedDesktopPath = itemPath;
            m_draggedItem = nullptr;
            m_dragOffset = event->pos() - hitState->model.renderData.itemRect.topLeft();
            m_isDragging = true;
            m_startPos = event->pos();
            event->accept();
            return;
        }

        clearHoveredDesktopItem();
        beginBackgroundSelection(event->pos());
        event->accept();
        return;
    }

    event->ignore();
}

void ListView::mouseMoveEvent(QMouseEvent* event)
{
    // handle multi-dragging
    if (m_isDragging && !m_ghosts.isEmpty()) {
        QPoint mousePos = event->globalPos();
        for (auto& ghost : m_ghosts) {
            if (ghost.widget) {
                ghost.widget->move(mousePos + ghost.offset);
            }
        }
        return;
    }

    if (m_isDragging && m_draggedItem == nullptr && !m_draggedDesktopPath.isEmpty()) {
        if ((event->pos() - m_startPos).manhattanLength() > QApplication::startDragDistance()) {
            if (m_ghosts.isEmpty()) {
                createDragGhosts(m_startPos);
            } else {
                QPoint mousePos = event->globalPos();
                for (auto& ghost : m_ghosts) {
                    if (ghost.widget) {
                        ghost.widget->move(mousePos + ghost.offset);
                    }
                }
            }
        }
        event->accept();
        return;
    }

    if (m_isSelecting) {
        updateBackgroundSelection(event->pos());
        event->ignore();
        return;
    }

    updateHoveredDesktopItem(event->pos());


    event->ignore();

}

void ListView::mouseReleaseEvent(QMouseEvent* event)
{
    // No-op for Item-based drags, as QDropEvent now handles them
    if (m_isDragging) {
        if (m_draggedItem == nullptr && !m_draggedDesktopPath.isEmpty()) {
            onItemRelayReleased(nullptr, nullptr, event);
            m_draggedDesktopPath.clear();
            event->accept();
            return;
        }
        m_isDragging = false;
        m_draggedItem = nullptr;
    }
    clearDesktopAutoArrangeReorderPreview();
    
    // Clear selection state if we were selecting background
    if (m_isSelecting) {
        endBackgroundSelection();
    }
    event->ignore();
}

void ListView::beginBackgroundSelection(const QPoint& startPos)
{
    clearCheckedItem();
    setFocus();
    rebuildSpatialIndex();

    m_isSelecting = true;
    m_startPos = startPos;
    m_selectionStart = startPos;
    m_selectionRect = QRect(startPos, QSize(0, 0));
}

bool ListView::updateBackgroundSelection(const QPoint& currentPos, QRect* selectionRect)
{
    if (!m_isSelecting) {
        return false;
    }

    const QRect rect = QRect(m_selectionStart, currentPos).normalized();
    const bool shouldShowSelection = rect.width() > 3 || rect.height() > 3;
    m_selectionRect = rect;

    if (shouldShowSelection) {
        refreshCheckedItem(rect);
    } else {
        clearCheckedItem();
    }

    if (selectionRect) {
        *selectionRect = rect;
    }

    return shouldShowSelection;
}

void ListView::endBackgroundSelection()
{
    m_isSelecting = false;
    m_startPos = QPoint();
    m_selectionStart = QPoint();
    m_selectionRect = QRect();
}

void ListView::rebuildSpatialIndex()
{
    if (!m_spatialIndex) {
        m_spatialIndex = std::make_unique<DesktopQuadTree>(rect());
    }
    m_spatialIndex->setBounds(rect());
    m_spatialIndex->rebuild(collectAutoArrangeStates());
}

QVector<ListItem*> ListView::queryItemsInRect(const QRect& rect) const
{
    Q_UNUSED(rect);
    QVector<ListItem*> results;
    return results;
}

void ListView::dragEnterEvent(QDragEnterEvent* event)
{
    RenameManager::instance()->cancelRename();
    if (event->mimeData()->hasUrls()) {
        const QStringList draggedPaths = ListViewFileOpsHelper::draggedLocalFilePaths(event->mimeData());
        if (event->source() != this) {
            if (shouldPreferShortcutForDrop(draggedPaths, event->keyboardModifiers(), true)) {
                event->setDropAction(Qt::LinkAction);
            } else if (event->keyboardModifiers() & Qt::ControlModifier) {
                event->setDropAction(Qt::CopyAction);
            } else {
                event->setDropAction(Qt::MoveAction);
            }
            ListViewFileOpsHelper::updateInternalDragDescription(
                event->mimeData(),
                event->dropAction(),
                QCoreApplication::translate("ListView", "Desktop"));
            event->accept();
            return;
        }
        if (shouldPreferShortcutForDrop(draggedPaths, event->keyboardModifiers(), false)) {
            event->setDropAction(Qt::LinkAction);
        } else if (event->keyboardModifiers() & Qt::ControlModifier) {
            event->setDropAction(Qt::CopyAction);
        } else {
            event->setDropAction(Qt::MoveAction);
        }
        ListViewFileOpsHelper::updateInternalDragDescription(
            event->mimeData(),
            event->dropAction(),
            QCoreApplication::translate("ListView", "Desktop"));
        event->accept();
    }
}

void ListView::dragLeaveEvent(QDragLeaveEvent* event)
{
    if (m_dragTip) m_dragTip->hide();
    clearDesktopAutoArrangeReorderPreview();
    event->accept();
}

void ListView::dragMoveEvent(QDragMoveEvent* event)
{
    if (!event->mimeData()->hasUrls()) {
        if (m_dragTip) m_dragTip->hide();
        clearDesktopAutoArrangeReorderPreview();
        return;
    }
    const QStringList draggedPaths = ListViewFileOpsHelper::draggedLocalFilePaths(event->mimeData());
    const bool preferShortcutForDrop = shouldPreferShortcutForDrop(
        draggedPaths, event->keyboardModifiers(), event->source() != this);
    if (preferShortcutForDrop) {
        event->setDropAction(Qt::LinkAction);
    } else if (event->keyboardModifiers() & Qt::ControlModifier) {
        event->setDropAction(Qt::CopyAction);
    } else {
        event->setDropAction(Qt::MoveAction);
    }
    event->accept();

    const bool isInternalDrag = (event->source() == this);

    // 1. Find if we are hovering over an item
    ListItem* hitItem = findDesktopItemAt(event->pos());
    if (hitItem && isDesktopPathSelected(hitItem->filePath())) {
        hitItem = nullptr;
    }

    bool isCtrl = (event->keyboardModifiers() & Qt::ControlModifier);
    const bool canPreviewDesktopReorder = isInternalDrag &&
        m_iconShowRule.autoArrange &&
        m_iconShowRule.sortType == SortOrderType::SortByCustom &&
        !preferShortcutForDrop &&
        !isCtrl;
    const QStringList reorderDraggedPaths = canPreviewDesktopReorder
        ? orderedDraggedPaths(draggedPaths)
        : QStringList();
    if (canPreviewDesktopReorder &&
        !reorderDraggedPaths.isEmpty() &&
        !draggedPathsContainMappedSource(reorderDraggedPaths)) {
        updateDesktopAutoArrangeReorderPreview(event->pos(), draggedPaths);
        ListViewFileOpsHelper::updateInternalDragDescription(
            event->mimeData(),
            Qt::MoveAction,
            QCoreApplication::translate("ListView", "Desktop"));
        if (m_dragTip) {
            m_dragTip->hide();
        }
        event->setDropAction(Qt::MoveAction);
        event->accept();
        return;
    }

    clearDesktopAutoArrangeReorderPreview();

    if (hitItem) {
        if (isInternalDrag && (!m_draggedDesktopPath.isEmpty() || m_draggedItem)) {
            const DesktopRootItemState* draggedState = m_draggedDesktopPath.isEmpty() ? nullptr : findDesktopStateByPath(m_draggedDesktopPath);
            const QString draggedClsid = draggedState ? draggedState->model.itemData.getCLSID() : m_draggedItem->getItemData().getCLSID();
            const QString draggedFilePath = draggedState ? draggedState->model.filePath : m_draggedItem->filePath();
            bool isDraggedPC = (draggedClsid == APP_MY_COMPUTER_CLS);
            bool isDraggedNetwork = (draggedClsid == APP_NETWORK_CLS);
            bool isDraggedControlPanel = (draggedClsid == APP_CONTROL_PANEL_CLS);
            bool isDraggedControlPanel1 = (draggedClsid == APP_CONTROL_PANEL1_CLS);
            bool isDraggedRecycleBin = (draggedClsid == APP_RECYCLE_BIN_CLS && draggedFilePath == "::{645FF040-5081-101B-9F08-00AA002F954E}");
            bool isLinux = (draggedClsid == APP_LINUX_CLS);
            bool isDraggedSystemIcon = isDraggedPC || isDraggedNetwork || isDraggedControlPanel || isDraggedControlPanel1 || isDraggedRecycleBin || isLinux;

            if (isDraggedSystemIcon) {
                ListViewFileOpsHelper::updateInternalDragDescription(
                    event->mimeData(),
                    Qt::IgnoreAction,
                    hitItem->baseName(),
                    true);
                ensureDragTip();
                m_dragTip->setText("<b style='color:#ff4d4d'>Operation Forbidden</b>");
                m_dragTip->adjustSize();
                m_dragTip->move(event->pos() + QPoint(24, 24));
                m_dragTip->show();
                m_dragTip->raise();
                event->setDropAction(Qt::IgnoreAction);
                event->accept();
                return;
            }
        }

        ensureDragTip();

        QString clsid = hitItem->getItemData().getCLSID();
        bool isRecycleBin = (clsid == APP_RECYCLE_BIN_CLS);
        bool isPC = (clsid == APP_MY_COMPUTER_CLS);
        bool isNetwork = (clsid == APP_NETWORK_CLS);
        bool isControlPanel = (clsid == APP_CONTROL_PANEL_CLS);
        bool isFolder = (hitItem->type() == ObjectType::Folder_ObjectType ||
                         hitItem->type() == ObjectType::SpecialFolder ||
                         hitItem->type() == ObjectType::DiskLetter);

        if (isPC || isNetwork || isControlPanel) {
            ListViewFileOpsHelper::updateInternalDragDescription(
                event->mimeData(),
                Qt::IgnoreAction,
                hitItem->baseName(),
                true);
            m_dragTip->setText("<b style='color:#ff4d4d'>Operation Forbidden</b>");
            m_dragTip->adjustSize();
            m_dragTip->move(event->pos() + QPoint(24, 24));
            m_dragTip->show();
            m_dragTip->raise();
            event->setDropAction(Qt::IgnoreAction);
            event->accept();
            return;
        }

        QString actionKeyword;
        QString highlightColor = "#70a1ff";
        const DesktopRootItemState* draggedState = m_draggedDesktopPath.isEmpty() ? nullptr : findDesktopStateByPath(m_draggedDesktopPath);
        const bool isDraggedDiskLetter = draggedState
            ? (draggedState->model.objectType == ObjectType::DiskLetter)
            : (m_draggedItem && m_draggedItem->type() == ObjectType::DiskLetter);
        QString htmlText;

        if (isRecycleBin) {
            actionKeyword = "Move to";
            highlightColor = "#ff4d4d";
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, hitItem->baseName());
        } else if (isFolder && preferShortcutForDrop) {
            actionKeyword = "Create shortcut in";
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, hitItem->baseName());
        } else if (isFolder && isDraggedDiskLetter) {
            htmlText = QString("<b style='color:%1'>闁?/b> <span>%2</span> <b style='color:%1'>闂佹彃鑻崹鍗烆嚈濞差亝鎳犻柟?/b>")
                .arg(highlightColor, hitItem->baseName());
        } else if (isFolder) {
            actionKeyword = isCtrl ? "Copy to" : "Move to";
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, hitItem->baseName());
        } else {
            actionKeyword = "Open with";
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, hitItem->baseName());
        }
        if (isRecycleBin) {
            ListViewFileOpsHelper::updateInternalDragDescriptionCustom(
                event->mimeData(),
                DROPIMAGE_MOVE,
                QCoreApplication::translate("ListView", "Move to %1"),
                hitItem->baseName());
        } else if (isFolder && preferShortcutForDrop) {
            ListViewFileOpsHelper::updateInternalDragDescriptionCustom(
                event->mimeData(),
                DROPIMAGE_LINK,
                QCoreApplication::translate("ListView", "Create shortcut in %1"),
                hitItem->baseName());
        } else if (!isFolder) {
            ListViewFileOpsHelper::updateInternalDragDescriptionCustom(
                event->mimeData(),
                DROPIMAGE_LABEL,
                QCoreApplication::translate("ListView", "Open with %1"),
                hitItem->baseName());
        } else {
            ListViewFileOpsHelper::updateInternalDragDescription(
                event->mimeData(),
                event->dropAction(),
                hitItem->baseName());
        }

        m_dragTip->setText(htmlText);
        m_dragTip->adjustSize();
        m_dragTip->move(event->pos() + QPoint(24, 24));
        m_dragTip->show();
        m_dragTip->raise();
        event->accept();
    } else {
        ListViewFileOpsHelper::updateInternalDragDescription(
            event->mimeData(),
            event->dropAction(),
            QCoreApplication::translate("ListView", "Desktop"));
        if (preferShortcutForDrop) {
            ensureDragTip();
            const QString htmlText = QString("<b style='color:#70a1ff'>Create shortcut in</b> <span>Desktop</span>");
            m_dragTip->setText(htmlText);
            m_dragTip->adjustSize();
            m_dragTip->move(event->pos() + QPoint(24, 24));
            m_dragTip->show();
            m_dragTip->raise();
        } else if (isCtrl) {
            ensureDragTip();
            const QString htmlText = QString("<b style='color:#70a1ff'>Copy to</b> <span>Desktop</span>");
            m_dragTip->setText(htmlText);
            m_dragTip->adjustSize();
            m_dragTip->move(event->pos() + QPoint(24, 24));
            m_dragTip->show();
            m_dragTip->raise();
        } else if (m_dragTip) {
            m_dragTip->hide();
        }
    }
}

void ListView::dropEvent(QDropEvent* event)
{
    RenameManager::instance()->cancelRename();
    if (m_dragTip) m_dragTip->hide();

    ListItem* hitItem = findDesktopItemAt(event->pos());
    if (hitItem && isDesktopPathSelected(hitItem->filePath())) {
        hitItem = nullptr;
    }

    const QMimeData* mime = event->mimeData();
    if (!mime->hasUrls()) {
        clearDesktopAutoArrangeReorderPreview();
        return;
    }
    const QStringList draggedPaths = ListViewFileOpsHelper::draggedLocalFilePaths(mime);
    const bool desktopReorderCopyMode = (event->keyboardModifiers() & Qt::ControlModifier);
    const bool desktopReorderShortcutMode = shouldPreferShortcutForDrop(
        draggedPaths, event->keyboardModifiers(), event->source() != this);
    const QStringList desktopReorderDraggedPaths = (event->source() == this)
        ? orderedDraggedPaths(draggedPaths)
        : QStringList();
    const bool forceDesktopReorder = event->source() == this &&
        m_iconShowRule.autoArrange &&
        m_iconShowRule.sortType == SortOrderType::SortByCustom &&
        !desktopReorderCopyMode &&
        !desktopReorderShortcutMode &&
        !desktopReorderDraggedPaths.isEmpty() &&
        !draggedPathsContainMappedSource(draggedPaths);

    // Drop onto an APP/File: Open with it; Drop onto Folder: Copy to it
    if (hitItem && !isDesktopPathSelected(hitItem->filePath()) && !forceDesktopReorder) {
        QString targetPath = hitItem->filePath();
        QString clsid = hitItem->getItemData().getCLSID();

        // Forbidden targets
        if (clsid == APP_MY_COMPUTER_CLS || clsid == APP_NETWORK_CLS || clsid == APP_CONTROL_PANEL_CLS) {
            event->ignore();
            return;
        }

        QStringList args;
        std::vector<std::wstring> srcPaths;

        for (const QString& localPath : draggedPaths) {
            if (!localPath.isEmpty()) {
                args << "\"" + localPath + "\"";
                srcPaths.push_back(localPath.toStdWString());
            }
        }
        
        // 闁哄秷顫夊畵渚€鏌ㄩ鐐８濞ｅ浂鍣ｉ妶鎵箔閿曗偓閸ㄤ粙寮鐔感﹀璺虹Т閸╂娼诲Ο缁樞︾紒澶庮嚙婵晠鏁嶉崷鏅卹l=濠㈣泛绉撮崺? 闁哄啰濮抽幈銊︻殰閹殿噮鍎?缂佸顕ф慨鈺呮晬?
        bool isCopyAction = (event->keyboardModifiers() & Qt::ControlModifier);
        const bool preferShortcutForDrop = shouldPreferShortcutForDrop(
            draggedPaths, event->keyboardModifiers(), event->source() != this);

        if (srcPaths.empty()) return;

        const bool shouldPreHideDraggedItems = (event->source() == this);
        const QStringList preHideDraggedPaths = shouldPreHideDraggedItems
            ? orderedDraggedPaths(draggedPaths)
            : QStringList();
        QVector<ListItem*> draggedItems;
        bool isRecycleBin = (hitItem->getItemData().getCLSID() == APP_RECYCLE_BIN_CLS);

        if (isRecycleBin) {
            if (shouldPreHideDraggedItems && !draggedItems.isEmpty()) {
                setItemsTransientHidden(draggedItems, true);
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            }

            const bool deleteOk = CSystemApi::DeleteFiles(srcPaths, true, true);
            if (!deleteOk) {
                if (shouldPreHideDraggedItems && !draggedItems.isEmpty()) {
                    setItemsTransientHidden(draggedItems, false);
                    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                }
                event->ignore();
                return;
            }

            if (shouldPreHideDraggedItems && !draggedItems.isEmpty()) {
                trackPendingExternalRemovals(draggedItems);
                removeItemsBatch(draggedItems);
                requestSaveBoxes();
            }

            event->accept();
            return;
        } else if (hitItem->type() == ObjectType::Folder_ObjectType ||
                   hitItem->type() == ObjectType::SpecialFolder ||
                   hitItem->type() == ObjectType::DiskLetter) {
            // 闁哄秷顫夊畵渚€鏌ㄩ鐐８濞ｅ浂鍣ｉ妶鎵箔閿曞倵鍋撴径瀣仴濠㈣泛绉撮崺妤呭箣閺嶎倣鈺呭礉閵婏附鐎ù鐘烘硾閸╁矂鎯勯鐣屽灱闁哄倸娲ｅ▎銏″緞?
            if (preferShortcutForDrop) {
                if (createShortcutsInFolder(draggedPaths, targetPath)) {
                    event->setDropAction(Qt::LinkAction);
                    event->accept();
                } else {
                    event->ignore();
                }
                return;
            }
            if (!isCopyAction && shouldPreHideDraggedItems && !draggedItems.isEmpty()) {
                setItemsTransientHidden(draggedItems, true);
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            }

            bool moveOk = false;
            if (isCopyAction) {
                moveOk = CSystemApi::CopyFiles(srcPaths, targetPath.toStdWString());
            } else {
                moveOk = CSystemApi::MoveFiles(srcPaths, targetPath.toStdWString());
            }

            if (!moveOk) {
                if (!isCopyAction && shouldPreHideDraggedItems && !draggedItems.isEmpty()) {
                    setItemsTransientHidden(draggedItems, false);
                    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                }
                event->ignore();
                return;
            }

            if (!isCopyAction && shouldPreHideDraggedItems && !draggedItems.isEmpty()) {
                trackPendingExternalRemovals(draggedItems);
                removeItemsBatch(draggedItems);
                requestSaveBoxes();
            }

            event->accept();
            return;
        } else {
            // Open with target app
            qDebug() << "Dropping files onto app:" << targetPath << "with args:" << args;
            std::wstring wApp = QDir::toNativeSeparators(targetPath).toStdWString();
            std::wstring wArgs = args.join(" ").toStdWString();
            
            HINSTANCE result = ShellExecuteW(NULL, L"open", wApp.c_str(), wArgs.c_str(), NULL, SW_SHOWNORMAL);
            if ((INT_PTR)result > 32) {
                event->acceptProposedAction();
                return;
            }
        }
    }

    if (draggedPaths.isEmpty()) {
        clearDesktopAutoArrangeReorderPreview();
        return;
    }

    // Handle dropping onto the desktop background (no hitItem)
    if (!hitItem) {
        // 闁哄秷顫夊畵渚€鏌ㄩ鐐８濞ｅ浂鍣ｉ妶鎵箔閿曗偓閸ㄤ粙寮鐔感﹀璺虹Т閸╂娼诲Ο缁樞︾紒澶庮嚙婵晠鏁嶉崷鏅卹l=濠㈣泛绉撮崺? 闁哄啰濮抽幈銊︻殰閹殿噮鍎?缂佸顕ф慨鈺呮晬?
        bool isCopyAction = (event->keyboardModifiers() & Qt::ControlModifier);
        
        // 濠㈣埖鐗犻崕鎾箯閺嶃劍鏉归柟瀛樼墪瀹搁亶宕氱捄渚Щ闁?
        const bool preferShortcutForDrop = shouldPreferShortcutForDrop(
            draggedPaths, event->keyboardModifiers(), event->source() != this);
        if (event->source() != this || isCopyAction || preferShortcutForDrop) {
            if (!m_iconShowRule.autoArrange) {
                for (const QString& localPath : draggedPaths) {
                    if (localPath.isEmpty()) {
                        continue;
                    }
                    const QString reservedPath = preferShortcutForDrop
                        ? ListViewFileOpsHelper::uniqueShortcutPathForTarget(localPath, m_desktopDir)
                        : ListViewFileOpsHelper::uniquePasteDestinationPath(localPath, m_desktopDir);
                    reserveCreatedItemPosition(reservedPath, event->pos());
                }
            }

            std::vector<std::wstring> srcPaths;
            for (const QString& localPath : draggedPaths) {
                if (!localPath.isEmpty()) srcPaths.push_back(localPath.toStdWString());
            }

            if (!srcPaths.empty()) {
                // 闁哄秷顫夊畵渚€鏌ㄩ鐐８濞ｅ浂鍣ｉ妶鎵箔閿曞倵鍋撴径瀣仴濠㈣泛绉撮崺妤呭箣閺嶎倣鈺呭礉?
                bool result;
                if (preferShortcutForDrop) {
                    result = createShortcutsInFolder(draggedPaths, m_desktopDir);
                } else if (isCopyAction) {
                    result = ListViewFileOpsHelper::copyFilesToDirectoryWithRule(draggedPaths, m_desktopDir);
                } else {
                    result = CSystemApi::MoveFiles(srcPaths, QDir::toNativeSeparators(m_desktopDir).toStdWString());
                }
                if (result) {
                    applyAutoArrangeAfterDesktopMutation(false, false);
                }
                event->accept();
                return;
            }
        }
    }

    if (event->source() == this) {
        const bool copyMode = (event->keyboardModifiers() & Qt::ControlModifier);
        const QVector<ListItem*> activeDesktopDragItems = selectedDesktopLegacyItems();
        const bool hasMappedSourceItems = draggedPathsContainMappedSource(draggedPaths);
        if (hasMappedSourceItems) {
            bool ok = false;
            if (copyMode) {
                ok = ListViewFileOpsHelper::copyFilesToDirectoryWithRule(draggedPaths, m_desktopDir);
            } else {
                std::vector<std::wstring> srcPaths;
                for (const QString& localPath : draggedPaths) {
                    if (!localPath.isEmpty()) {
                        srcPaths.push_back(QDir::toNativeSeparators(localPath).toStdWString());
                    }
                }
                if (!srcPaths.empty()) {
                    ok = CSystemApi::MoveFiles(srcPaths, QDir::toNativeSeparators(m_desktopDir).toStdWString());
                }
            }

            if (ok) {
                clearCheckedItem();
                applyAutoArrangeAfterDesktopMutation(false, false);
                requestSaveBoxes();
                event->accept();
            } else {
                event->ignore();
            }
            return;
        }

        // --- ???????? ---
        QPoint dropPos = event->pos(); // ????????? dragHotSpot ??????
        
        if (!activeDesktopDragItems.isEmpty()) {
            if (m_iconShowRule.autoArrange) {
                const bool customSortDrag = (m_iconShowRule.sortType == SortOrderType::SortByCustom);
                const QStringList draggedDesktopPaths = draggedPaths;
                const int customInsertIndex = customSortDrag
                    ? (m_desktopReorderInsertIndex >= 0
                        ? m_desktopReorderInsertIndex
                        : desktopCustomInsertionIndexForPos(dropPos, draggedDesktopPaths))
                    : -1;

                for (auto* item : activeDesktopDragItems) {
                    if (!item) {
                        continue;
                    }

                    if (item->parentWidget() != this || !item->boxId().isEmpty()) {
                        QWidget* parent = item->parentWidget();
                        LinBox* sourceBox = nullptr;
                        while (parent) {
                            sourceBox = qobject_cast<LinBox*>(parent);
                            if (sourceBox) {
                                break;
                            }
                            parent = parent->parentWidget();
                        }
                        if (sourceBox) {
                            sourceBox->removeItemState(item->filePath(), false);
                            sourceBox->refreshLayout();
                            sourceBox->update();
                        }

                        item->setBoxId("");
                        item->setParent(this);
                        item->setLayoutMode(ListItem::LayoutMode::IconMode);
                        item->setItemSize(m_iconWidth, m_iconHeight);
                        setupItemConnections(item);
                        const bool shouldShowOnDesktop = shouldShowFileSystemItem(item);
                        if (shouldShowOnDesktop && !m_items.contains(item)) {
                            m_items.push_back(item);
                        } else if (!shouldShowOnDesktop && !m_hiddenIcons.contains(item)) {
                            m_hiddenIcons.push_back(item);
                        }
                        if (shouldShowOnDesktop && m_iconShowRule.showDesktopIcons) {
                            item->show();
                        } else if (!shouldShowOnDesktop) {
                            item->hide();
                        }
                    }
                    if (shouldShowFileSystemItem(item)) {
                        item->raise();
                    }
                }

                if (customSortDrag) {
                    QVector<DesktopRootItemState*> orderedStates = collectAutoArrangeStates();
                    sortAutoArrangeStates(orderedStates, SortOrderType::SortByCustom);
                    QSet<QString> draggedPathSet;
                    QVector<DesktopRootItemState*> orderedDraggedStates;
                    for (const QString& path : draggedDesktopPaths) {
                        draggedPathSet.insert(normalizeDesktopItemPath(path));
                    }
                    for (DesktopRootItemState* state : orderedStates) {
                        if (state && draggedPathSet.contains(normalizeDesktopItemPath(state->model.filePath))) {
                            orderedDraggedStates.append(state);
                        }
                    }
                    orderedStates.erase(std::remove_if(orderedStates.begin(), orderedStates.end(), [&](DesktopRootItemState* state) {
                        return state && draggedPathSet.contains(normalizeDesktopItemPath(state->model.filePath));
                    }), orderedStates.end());
                    const int clampedIndex = qBound(0, customInsertIndex, orderedStates.size());
                    for (int i = 0; i < orderedDraggedStates.size(); ++i) {
                        orderedStates.insert(clampedIndex + i, orderedDraggedStates[i]);
                    }
                    assignCustomSortOrders(orderedStates);
                }

                applyAutoArrangeLayout(true, false);
                clearDesktopAutoArrangeReorderPreview();
                clearCheckedItem();
                event->acceptProposedAction();
                return;
            }

            const int itemHeight = desktopItemFootprintHeight();
            const int strideX = m_iconWidth + m_colSpacing;
            const int strideY = itemHeight + m_rowSpacing;
            const int layoutLeft = desktopLayoutLeft();
            const int layoutTop = desktopLayoutTop();

            // 1. 閻犱緤绱曢悾缁樜楅悩宕囧灱濞达絽绉朵簺闂?(releasePos - grabPos)
            QPoint displacement = dropPos - m_startPos;

            // 濡澘瀚崢娑氭媼閿涘嫮鏆柟纰樺亾闁哄牆顦甸妴宥夋儍閸曨厽绐楅柡宥呮矗缂嶅懐绱旈鍡欑妤犵偠鍩栭ˉ鍛村蓟閵夛附笑闁告熬绠戣ぐ鏌ュ绩閸撗呮瀭
            QMap<ListItem*, QPoint> targetPositions;
            bool canPlaceAll = true;

            for (auto* item : activeDesktopDragItems) {
                if (!item) continue;

                QPoint itemIntendedTopLeft;

                // 2. 闁哄秷顫夊畵浣规媴瀹ュ泦鈺冩媼閿涘嫮鏆柟纰樺亾闁哄牆顦甸妴宥夋儍閸曨剚鐓€闁炽儲绮嶉崜浼村炊闁款垪鍋撳┑鍡樼稄闁?(闂傚洠鍋撻悷鏇氳兌鐎氼厾绮╃€ｎ亶妲遍柣鐐叉濞夋洟宕洪悢绋跨彌闁圭柉妫勫妤呭冀?
                if (item->parentWidget() != this && item->parentWidget() != nullptr) {
                    // 閻庣懓鍟板ú浼村礈瀹ュ懐娼ｅù?Box闁挎稑鏈晶宥嗙?mapToGlobal 闁兼儳鍢茶ぐ鍥礂鐠鸿櫣鏉介梻鍕噹閸欏繒浠﹂埀顒勫锤閹邦厾鍨?
                    QPoint globalInitialTopLeft = item->mapToGlobal(QPoint(0,0));
                    QPoint initialTopLeftOnDesktop = this->mapFromGlobal(globalInitialTopLeft);
                    itemIntendedTopLeft = initialTopLeftOnDesktop + displacement;
                } else {
                    // 闁告鍠撻弫鎾愁浖瀹€鍕〃闁搞儳鍋撻悥?
                    itemIntendedTopLeft = item->pos() + displacement;
                }

                QPoint targetPos;

                if (m_iconShowRule.gridAlignment) {
                    // Grid Mode: Calculate nearest grid cell
                    int gridX = qRound((double)(itemIntendedTopLeft.x() - layoutLeft) / strideX);
                    int gridY = qRound((double)(itemIntendedTopLeft.y() - layoutTop) / strideY);

                    targetPos.setX(layoutLeft + gridX * strideX);
                    targetPos.setY(layoutTop + gridY * strideY);

                    // 缁炬壆澧楅幐鎺懳涢埀顒€霉鐎ｅ墎绐楁俊顐熷亾闁哄被鍎撮姘辩磾閹寸偟澹愰柡鍕靛灠閹礁顔忛懠棰濇蕉闁炽儲绮撳顏囥亹閹惧啿顤呴梺顐㈩槷閼垫垿鍨惧┑瀣ㄢ偓宥夊础閻樺灚鏆?(濞寸姴鎳庡﹢顏嗙磾閹寸偟澹愭俊顖椻偓宕囩濞戞挸顑呭閬嶅礆?
                    for (auto* other : m_items) {
                        if (other && other->isVisible() && !activeDesktopDragItems.contains(other)) {
                            if (other->pos() == targetPos) {
                                canPlaceAll = false;
                                break;
                            }
                        }
                    }
                } else {
                    // Free Mode: Use exact floating position
                    targetPos = itemIntendedTopLeft;
                }

                // 閺夊牆婀遍弲顐⑽涢埀顒勫蓟?(闁哄啰濮鹃鎴﹀及椤栨碍鎲跨紒澶婄У鑶╃€殿喖绻橀崗妯荤┍濠靛牊娈岄柨娑樼焸濡茶顫㈤姀鐘崇闁哄秴娲ˉ锝夊礄閸濆嫮娼屾?
                targetPos.setX(qMax(layoutLeft, qMin(targetPos.x(), desktopUsableRight() - m_iconWidth + 1)));
                targetPos.setY(qMax(layoutTop, qMin(targetPos.y(), desktopUsableBottom() - itemHeight + 1)));

                if (!canPlaceAll) break;
                targetPositions.insert(item, targetPos);
            }

            // 濠碘€冲€归悘澶愬礂閵娾晛鍔ユ俊顐熷亾闁哄被鍎甸埀顒佷亢缁诲啴鏁嶇仦鎯р挃閻炴稑鐬间簺闁?
            if (canPlaceAll) {
                for (auto it = targetPositions.begin(); it != targetPositions.end(); ++it) {
                    ListItem* item = it.key();
                    
                    // Moving from BOX to DESKTOP?
                    if (item && item->parentWidget() != this && item->parentWidget() != nullptr) {
                        qDebug() << "Moving item out of box:" << item->baseName();
                        
                        // 1. Clear item from Box (trace up the tree to find LinBox due to scroll areas)
                        QWidget* p = item->parentWidget();
                        LinBox* box = nullptr;
                        while (p) {
                            box = qobject_cast<LinBox*>(p);
                            if (box) break;
                            p = p->parentWidget();
                        }
                        if (box) {
                            box->removeItemState(item->filePath());
                        }

                        // 2. Reparent to Desktop
                        item->setBoxId("");
                        item->setParent(this);
                        
                        // 3. Restore icon layout properties for Desktop and apply GRID/FREE target position
                        item->setLayoutMode(ListItem::LayoutMode::IconMode);
                        item->setItemSize(m_iconWidth, m_iconHeight);
                        item->move(it.value()); // <--- Key fix: uses proper grid location instead of outPos
                        item->updateLocationInfo(it.value());
                        
                        // 4. Ensure it's in the correct visible pools
                        const bool shouldShowOnDesktop = shouldShowFileSystemItem(item);
                        if (shouldShowOnDesktop && !m_items.contains(item)) {
                            m_items.push_back(item);
                        } else if (!shouldShowOnDesktop && !m_hiddenIcons.contains(item)) {
                            m_hiddenIcons.push_back(item);
                        }
                        if (shouldShowOnDesktop && m_iconShowRule.showDesktopIcons) {
                            item->show();
                        } else if (!shouldShowOnDesktop) {
                            item->hide();
                        }
                        if (shouldShowOnDesktop) {
                            item->raise();
                        }
                        
                        // IMPORTANT: Do NOT delete the item. 
                        // We use a single unified instance across Box and Desktop.
                    } else {
                        item->move(it.value());
                        item->updateLocationInfo(it.value());
                        item->raise();
                    }
                }
            }
            // 濞ｅ洦绻傞悺銊╁炊閻愵剛鍨煎ù锝呯Ф閻ゅ棝宕氭导鏉戝赋缂傚喚鍠楅弸鍐╃?
            requestSaveBoxes();
        }
        clearCheckedItem();
    } else {
        // --- 濠㈣埖鐗犻崕鎾箯閺嵮冨汲闂侇偅妲掔欢?---
        QString desktopDir = m_desktopDir;
        std::vector<std::wstring> srcPaths;
        for (const QString& local : draggedPaths) {
            if (!local.isEmpty() && QDir::toNativeSeparators(local) != QDir::toNativeSeparators(desktopDir)) {
                srcPaths.push_back(QDir::toNativeSeparators(local).toStdWString());
            }
        }
        
        if (!srcPaths.empty()) {
            qDebug() << "External drop into Desktop:" << desktopDir;
            // 閻犲鍟伴弫?Shell API 閺夆晜绋栭、鎴﹀箯閻ゎ垳顦柕?濞ｅ洦绻冪€?confirm 濞?true闁?
            CSystemApi::CopyFiles(srcPaths, desktopDir.toStdWString());
        }
    }
    applyAutoArrangeAfterDesktopMutation(true, false);
    if (m_dragTip) m_dragTip->hide();
    event->acceptProposedAction();
}

void ListView::mouseDoubleClickEvent(QMouseEvent* event)
{
    RenameManager::instance()->cancelRename();
    if (event->button() == Qt::LeftButton) {
        if (DesktopRootItemState* hitState = findDesktopStateAt(event->pos())) {
            clearCutMarks();
            openDesktopRootItem(hitState->model);
            event->accept();
            return;
        }

        if (m_iconShowRule.doubleClickHideIcons) {
            onMenuShowDesktopIcons();
            event->accept();
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

void ListView::keyPressEvent(QKeyEvent* event)
{
    // 閼惧嘲褰囩€圭偞妞傞惃鍕暛閻╂濮搁幀?
    const bool shiftPressed = GetAsyncKeyState(VK_SHIFT) & 0x8000;
    const bool ctrlPressed = GetAsyncKeyState(VK_CONTROL) & 0x8000;

    const bool extendSelection = (event->modifiers() & Qt::ShiftModifier) || shiftPressed;
    const bool shortcutPaste = ctrlPressed && (event->key() == Qt::Key_V);
    const bool shortcutNewFolder = ctrlPressed && extendSelection && (event->key() == Qt::Key_N);
    const bool permanentDelete = shiftPressed && (event->key() == Qt::Key_Delete);

    qDebug() << "Use GetAsyncKeyState check: Shift =" << shiftPressed
        << ", Delete =" << (event->key() == Qt::Key_Delete)
        << ", permanentDelete =" << permanentDelete;
    
    if (event->matches(QKeySequence::SelectAll)) {
        QStringList desktopPaths;
        for (const auto& state : m_itemStates) {
            if (shouldPaintDesktopModelState(state)) {
                desktopPaths.append(normalizeDesktopItemPath(state.model.filePath));
            }
        }
        applyDesktopSelection(desktopPaths, m_focusedDesktopPath, false);
        applySelectionState(QVector<ListItem*>(), nullptr, false);
        event->accept();
        qDebug() << "Select All triggered";
        return;
    }
    if (event->matches(QKeySequence::Copy)) {
        writeSelectionToClipboard(false);
        event->accept();
        qDebug() << "Copy triggered";
        return;
    }
    //first judge if it's a Shift+Delete for permanent delete, if not, then handle regular Delete key
    if (permanentDelete) {
        onMenuDelete(true);
        event->accept();
        qDebug() << "Permanent Delete triggered";
        return;
    }
    if (event->matches(QKeySequence::Cut)) {
        writeSelectionToClipboard(true);
        event->accept();
        qDebug() << "Cut triggered";
        return;
    }
    if (event->matches(QKeySequence::Paste)) {
        clearCutMarks();
        onMenuPaste();
        event->accept();
        qDebug() << "Paste triggered";
        return;
    }
    
    if (shortcutPaste) {
        onMenuPasteShortcut();
        event->accept();
        qDebug() << "Paste Shortcut triggered";
        return;
    }
    if (shortcutNewFolder) {
        createNewFolderInHost();
        event->accept();
        qDebug() << "New Folder triggered";
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        clearSelectionState();
        RenameManager::instance()->cancelRename();
        event->accept();
        qDebug() << "Escape triggered: Cleared selection and canceled rename";
        return;
    }
    if ((event->modifiers() & Qt::AltModifier) && event->key() == Qt::Key_Return) {
        const DesktopRootItemState* desktopTarget = !m_focusedDesktopPath.isEmpty()
            ? findDesktopStateByPath(m_focusedDesktopPath)
            : (m_selectedDesktopPaths.isEmpty() ? nullptr : findDesktopStateByPath(m_selectedDesktopPaths.first()));
        const QString targetPath = desktopTarget
            ? QString()
            : (focusedItemPath().isEmpty()
                ? (selectedBoxPaths().isEmpty() ? QString() : selectedBoxPaths().first())
                : focusedItemPath());
        if (desktopTarget && !desktopTarget->model.filePath.isEmpty()) {
            CSystemApi::File_RightMenuOperations(reinterpret_cast<const TCHAR*>(QDir::toNativeSeparators(desktopTarget->model.filePath).utf16()), Z_PROPERTIES);
            event->accept();
            qDebug() << "Alt+Enter triggered: Opened properties for" << desktopTarget->model.displayName;
            return;
        }
        if (!targetPath.isEmpty() && showPropertiesForPath(targetPath)) {
            event->accept();
            qDebug() << "Alt+Enter triggered: Opened properties for" << QFileInfo(targetPath).completeBaseName();
            return;
        }
    }
    if (ctrlPressed && event->key() == Qt::Key_Space) {
        if (!m_focusedDesktopPath.isEmpty()) {
            const bool selected = isDesktopPathSelected(m_focusedDesktopPath);
            setDesktopItemSelected(m_focusedDesktopPath, !selected);
            if (m_anchorDesktopPath.isEmpty()) {
                m_anchorDesktopPath = m_focusedDesktopPath;
            }
        } else if (!m_focusedBoxPath.isEmpty()) {
            toggleItemSelection(m_focusedBoxPath);
            if (m_anchorBoxPath.isEmpty()) {
                m_anchorBoxPath = m_focusedBoxPath;
            }
        }
        event->accept();
        qDebug() << "Ctrl+Space triggered: Toggled selection for focused item";
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        const DesktopRootItemState* desktopTarget = !m_focusedDesktopPath.isEmpty()
            ? findDesktopStateByPath(m_focusedDesktopPath)
            : (m_selectedDesktopPaths.isEmpty() ? nullptr : findDesktopStateByPath(m_selectedDesktopPaths.first()));
        const QString targetPath = desktopTarget
            ? QString()
            : (focusedItemPath().isEmpty()
                ? (selectedBoxPaths().isEmpty() ? QString() : selectedBoxPaths().first())
                : focusedItemPath());
        if (desktopTarget) {
            clearCutMarks();
            openDesktopRootItem(desktopTarget->model);
            event->accept();
            qDebug() << "Enter triggered: Opened" << desktopTarget->model.displayName;
            return;
        }
        if (!targetPath.isEmpty() && openItemByPath(targetPath)) {
            event->accept();
            qDebug() << "Enter triggered: Opened" << QFileInfo(targetPath).completeBaseName();
            return;
        }
    }
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Up) {
        moveFocusInHost(nullptr,
            event->key() == Qt::Key_Left ? FocusMoveDirection::Left : FocusMoveDirection::Up,
            extendSelection,
            ctrlPressed);
        event->accept();
        qDebug() << (event->key() == Qt::Key_Left ? "Left" : "Up") << "arrow triggered: Moved focus";
        return;
    }
    if (event->key() == Qt::Key_Right || event->key() == Qt::Key_Down) {
        moveFocusInHost(nullptr,
            event->key() == Qt::Key_Right ? FocusMoveDirection::Right : FocusMoveDirection::Down,
            extendSelection,
            ctrlPressed);
        event->accept();
        qDebug() << (event->key() == Qt::Key_Right ? "Right" : "Down") << "arrow triggered: Moved focus";
        return;
    }
    if (event->key() == Qt::Key_F2) {
        if (m_selectedDesktopPaths.size() == 1) {
            if (const DesktopRootItemState* state = findDesktopStateByPath(m_selectedDesktopPaths.first())) {
                RenameManager::instance()->startRename(state->model.filePath);
                qDebug() << "F2 triggered: Started rename for" << state->model.displayName;
                return;
            }
        } else if (selectedBoxPaths().size() == 1) {
            RenameManager::instance()->startRename(selectedBoxPaths().first());
            qDebug() << "F2 triggered: Started rename for" << selectedBoxPaths().first();
            return;
        }
    }
    if (event->key() == Qt::Key_Delete) {
        /*const Qt::KeyboardModifiers modifiers = event->modifiers() | QGuiApplication::keyboardModifiers();
        bool permanently = (modifiers & Qt::ShiftModifier);*/
        onMenuDelete(false);
        event->accept();
        qDebug() << "Delete triggered: Deleted selected items";
        return;
    }
    
    QWidget::keyPressEvent(event);
}

void ListView::enterEvent(QEvent* event)
{
    QWidget::enterEvent(event);
}

void ListView::leaveEvent(QEvent* event)
{
    clearHoveredDesktopItem();
    QWidget::leaveEvent(event);
}

ListView* ListView::instance(QWidget* parent) {
    if (!m_instance) {
        m_instance = new ListView(parent);
    }
    return m_instance;
}

void ListView::initialize()
{
    // === 闁告艾鏈?Windows 婵℃鐭傚浼村箳閹烘垵鐏欓悹浣稿⒔閻?===
    bool osAutoArrange = false;
    bool osSnapToGrid = false;
    CSystemApi::GetDesktopConfigFlags(osAutoArrange, osSnapToGrid);
    m_iconShowRule.autoArrange = osAutoArrange;
    m_iconShowRule.gridAlignment = osSnapToGrid;
    if (m_iconShowRule.autoArrange) {
        m_iconShowRule.sortTypeBeforeAutoArrange = m_iconShowRule.sortType;
        m_iconShowRule.sortType = SortOrderType::SortByCustom;
        m_iconShowRule.gridAlignment = true;
        m_iconShowRule.iconPosMode = IconPosMode::AlignIconToGrid;
    }
    syncViewModeSizes(); // Sync initial sizes to member variables
    qDebug() << "Synced Windows Desktop Layout Rules: AutoArrange=" << osAutoArrange << ", SnapToGrid=" << osSnapToGrid;
    // ===================================

    const vector<ListItemData> allDatas = DesktopItemLoader().loadAllItems();

    m_allItems.clear();
    m_items.clear();
    m_hiddenIcons.clear();
    m_itemStates.clear();
    if (monitor) {
        sysIconRegisterInfo = monitor->getCurrentIcons();
    }

    auto normalizeClsid = [](QString clsid) {
        return clsid.replace("::", "").replace("{", "").replace("}", "").toLower();
    };

    DesktopItemInitContext context;
    context.sysIconRegisterInfo = sysIconRegisterInfo;
    context.systemFolderCLSIDs  = SystemFolderCLSIDs;
    context.showHiddenFiles     = m_showHiddenFiles;
    context.getBoxId = [this](const QString& path) { return getBoxIdForItem(path); };

    for (const auto& data : allDatas) {
        DesktopRootItemState state;
        initDesktopRootItemModel(state.model, data, context);

        const QPoint nativePos(
            state.model.fileInfo.absoluteFilePath().isEmpty() ? 0 : data.itemData().position.x,
            state.model.fileInfo.absoluteFilePath().isEmpty() ? 0 : data.itemData().position.y);

        state.model.position = (nativePos.x() != 0 || nativePos.y() != 0)
            ? nativePos
            : QPoint(0, 0);
        m_itemStates.push_back(state);
    }


    // --- Compact Native Layout Algorithm (Fill the gaps) ---
    // 1. Gather all unique physical coordinates from the entire system desktop
    QVector<QPoint> nativeSlots;
    for (const auto& data : allDatas) {
        QPoint p(data.itemData().position.x, data.itemData().position.y);
        if (p.x() != 0 || p.y() != 0) {
            if (!nativeSlots.contains(p)) {
                nativeSlots.append(p);
            }
        }
    }

    // 2. Sort the physical slots (Column-major: Left->Right, Top->Bottom)
    std::sort(nativeSlots.begin(), nativeSlots.end(), [](const QPoint& a, const QPoint& b){
        if (qAbs(a.x() - b.x()) > 10) return a.x() < b.x();
        return a.y() < b.y();
    });


    QVector<DesktopRootItemState*> startupItemsWithNativePos;
    QVector<DesktopRootItemState*> startupItemsWithoutNativePos;
    startupItemsWithNativePos.reserve(m_itemStates.size());
    startupItemsWithoutNativePos.reserve(m_itemStates.size());
    for (auto& state : m_itemStates) {
        if (!state.model.visible || !state.model.boxId.isEmpty()) {
            continue;
        }
        if (state.model.position == QPoint(0, 0)) {
            startupItemsWithoutNativePos.push_back(&state);
        } else {
            startupItemsWithNativePos.push_back(&state);
        }
    }

    std::sort(startupItemsWithNativePos.begin(), startupItemsWithNativePos.end(), [](const DesktopRootItemState* a, const DesktopRootItemState* b) {
        const QPoint pa = a->model.position;
        const QPoint pb = b->model.position;
        if (qAbs(pa.x() - pb.x()) > 10) {
            return pa.x() < pb.x();
        }
        return pa.y() < pb.y();
    });

    qDebug() << "=== Compacted Native Layout (Model) ===";
    for (int i = 0; i < startupItemsWithNativePos.size() && i < nativeSlots.size(); ++i) {
        startupItemsWithNativePos[i]->model.position = nativeSlots[i];
        qDebug() << QString("Model item(%1) compacted to Position:(%2, %3) - Name: %4")
                    .arg(i)
                    .arg(nativeSlots[i].x())
                    .arg(nativeSlots[i].y())
                    .arg(startupItemsWithNativePos[i]->model.itemData.fullName());
    }

    for (DesktopRootItemState* state : startupItemsWithoutNativePos) {
        if (!state) continue;
        const QPoint generatedPos = generateItemPosition();
        state->model.position = generatedPos;
        qDebug() << QString("Deferred model item placed at Position:(%1, %2) - Name: %3")
                    .arg(generatedPos.x())
                    .arg(generatedPos.y())
                    .arg(state->model.itemData.fullName());
    }
    qDebug() << "======================================";
    updateAllItemRenderData();
}

void ListView::loadIconLayer()
{
    qDebug() << "[ListView] Starting reload. Master list size:" << m_allItems.size();
    const bool shouldRestorePersistedLayout = m_isLoadingPersistence || m_forceConfigLayout;

    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [this](ListItem* item) {
        if (!item || shouldShowFileSystemItem(item)) {
            return false;
        }

        item->hide();
        if (!m_hiddenIcons.contains(item)) {
            m_hiddenIcons.push_back(item);
        }
        return true;
    }), m_items.end());

    qDebug() << "[ListView] After render filtering, item count in m_items:" << m_items.size();
    if (m_iconShowRule.autoArrange && !shouldRestorePersistedLayout) {
        qDebug() << "[ListView] Applying auto-layout (Stable Categorized).";
        applyAutoArrangeLayout(false, false);
    } else if (shouldRestorePersistedLayout) {
        qDebug() << "[ListView] Applying persistent layout.";
        loadPersistentLayout();
    } else if (m_iconShowRule.gridAlignment) {
        qDebug() << "[ListView] Applying grid layout.";
        loadGridLayout();
    } else {
        qDebug() << "[ListView] No layout rule active? Defaulting to grid.";
        loadGridLayout();
    }
    
    
    // Ensure visibility and stacking
    for (auto* item : m_items) {
        if (!item) continue;
        if (!shouldShowFileSystemItem(item)) {
            item->hide();
            if (!m_hiddenIcons.contains(item)) {
                m_hiddenIcons.push_back(item);
            }
            continue;
        }
        if (m_iconShowRule.showDesktopIcons) {
            item->show();
            item->raise();
        } else {
            item->hide();
        }
    }

    // Refresh all boxes to ensured their icons are positioned and visible
    for (auto& box : BoxManager::instance()->allBoxes()) {
        if (box) {
            box->refreshLayout();
            box->raise();
        }
    }

    BoxManager::instance()->raiseAllBoxes();
}


QVector<DesktopRootItemState*> ListView::collectAutoArrangeStates() const
{
    QVector<DesktopRootItemState*> states;
    states.reserve(m_itemStates.size());
    for (const auto& state : m_itemStates) {
        if (!state.model.boxId.isEmpty() || !state.model.visible) {
            continue;
        }
        states.push_back(const_cast<DesktopRootItemState*>(&state));
    }
    return states;
}

void ListView::sortAutoArrangeStates(QVector<DesktopRootItemState*>& states, SortOrderType order) const
{
    const SortOrderType effectiveOrder = (order == SortOrderType::Default)
        ? SortOrderType::SortByCategorized
        : order;

    auto specialPriority = [](const DesktopRootItemState* state) {
        if (!state || state->model.objectType != ObjectType::SpecialFolder) {
            return 100;
        }
        const QString clsid = state->model.clsid;
        if (clsid == APP_USER_FOLDER_CLS) return 1;
        if (clsid == APP_MY_COMPUTER_CLS) return 2;
        if (clsid == APP_NETWORK_CLS) return 3;
        if (clsid == APP_RECYCLE_BIN_CLS) return 4;
        if (clsid == APP_CONTROL_PANEL_CLS) return 5;
        return 6;
    };

    auto categorizedPriorityForState = [](const DesktopRootItemState* state) {
        if (!state) return 999;
        if (state->model.objectType == ObjectType::SpecialFolder) return 0;
        if (state->model.objectType == ObjectType::DiskLetter || state->model.fileInfo.isDir()) return 1;
        const QString ext = state->model.fileInfo.suffix().toLower();
        if (ext == "exe" || ext == "lnk" || ext == "url") return 2;
        static const QStringList docExts = {"txt", "pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "md", "zip", "rar", "7z"};
        if (docExts.contains(ext)) return 3;
        return 4;
    };

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setIgnorePunctuation(true);
    collator.setLocale(QLocale::system());

    switch (effectiveOrder) {
    case SortOrderType::SortByCustom:
        std::stable_sort(states.begin(), states.end(), [](DesktopRootItemState* a, DesktopRootItemState* b) {
            const int orderA = a ? a->model.customSortOrder : -1;
            const int orderB = b ? b->model.customSortOrder : -1;
            const int safeOrderA = orderA < 0 ? INT_MAX : orderA;
            const int safeOrderB = orderB < 0 ? INT_MAX : orderB;
            if (safeOrderA != safeOrderB) {
                return safeOrderA < safeOrderB;
            }
            return a->model.displayName.toLower() < b->model.displayName.toLower();
        });
        break;
    case SortOrderType::SortByName_ASC:
    case SortOrderType::SortByName_DESC:
    {
        const bool ascending = (effectiveOrder == SortOrderType::SortByName_ASC);
        std::sort(states.begin(), states.end(), [&](DesktopRootItemState* a, DesktopRootItemState* b) {
            const int prioA = specialPriority(a);
            const int prioB = specialPriority(b);
            if (prioA != prioB) {
                return ascending ? (prioA < prioB) : (prioA > prioB);
            }
            const int result = collator.compare(a->model.sortName, b->model.sortName);
            return ascending ? (result < 0) : (result > 0);
        });
        break;
    }
    case SortOrderType::SortBySize_ASC:
    case SortOrderType::SortBySize_DESC:
    {
        const bool ascending = (effectiveOrder == SortOrderType::SortBySize_ASC);
        std::sort(states.begin(), states.end(), [ascending](DesktopRootItemState* a, DesktopRootItemState* b) {
            const bool aIsSpecial = (a->model.objectType == ObjectType::SpecialFolder);
            const bool bIsSpecial = (b->model.objectType == ObjectType::SpecialFolder);
            if (aIsSpecial != bIsSpecial) {
                return aIsSpecial;
            }
            const bool aIsDir = a->model.fileInfo.isDir();
            const bool bIsDir = b->model.fileInfo.isDir();
            if (aIsDir != bIsDir) {
                return ascending ? aIsDir : !aIsDir;
            }
            const qint64 sizeA = a->model.fileInfo.size();
            const qint64 sizeB = b->model.fileInfo.size();
            if (sizeA != sizeB) {
                return ascending ? (sizeA < sizeB) : (sizeA > sizeB);
            }
            return a->model.displayName.toLower() < b->model.displayName.toLower();
        });
        break;
    }
    case SortOrderType::SortByType_ASC:
    case SortOrderType::SortByType_DESC:
    {
        const bool ascending = (effectiveOrder == SortOrderType::SortByType_ASC);
        std::sort(states.begin(), states.end(), [ascending](DesktopRootItemState* a, DesktopRootItemState* b) {
            const bool aIsSpecial = (a->model.objectType == ObjectType::SpecialFolder);
            const bool bIsSpecial = (b->model.objectType == ObjectType::SpecialFolder);
            if (aIsSpecial != bIsSpecial) {
                return aIsSpecial;
            }
            const bool aIsDirLike = (a->model.objectType == ObjectType::DiskLetter) || a->model.fileInfo.isDir();
            const bool bIsDirLike = (b->model.objectType == ObjectType::DiskLetter) || b->model.fileInfo.isDir();
            const QString extA = aIsDirLike ? QString() : a->model.fileInfo.suffix().toLower();
            const QString extB = bIsDirLike ? QString() : b->model.fileInfo.suffix().toLower();
            if (extA != extB) {
                return ascending ? (extA < extB) : (extA > extB);
            }
            return a->model.displayName.toLower() < b->model.displayName.toLower();
        });
        break;
    }
    case SortOrderType::SortByDateModified_ASC:
    case SortOrderType::SortByDateModified_DESC:
    {
        const bool ascending = (effectiveOrder == SortOrderType::SortByDateModified_ASC);
        std::sort(states.begin(), states.end(), [ascending](DesktopRootItemState* a, DesktopRootItemState* b) {
            const bool aIsSpecial = (a->model.objectType == ObjectType::SpecialFolder);
            const bool bIsSpecial = (b->model.objectType == ObjectType::SpecialFolder);
            if (aIsSpecial != bIsSpecial) {
                return aIsSpecial;
            }
            const QDateTime dateA = a->model.fileInfo.lastModified();
            const QDateTime dateB = b->model.fileInfo.lastModified();
            if (dateA != dateB) {
                return ascending ? (dateA < dateB) : (dateA > dateB);
            }
            return a->model.displayName.toLower() < b->model.displayName.toLower();
        });
        break;
    }
    case SortOrderType::SortByCategorized:
    default:
        std::sort(states.begin(), states.end(), [&](DesktopRootItemState* a, DesktopRootItemState* b) {
            const int pA = categorizedPriorityForState(a);
            const int pB = categorizedPriorityForState(b);
            if (pA != pB) {
                return pA < pB;
            }
            return collator.compare(a->model.sortName, b->model.sortName) < 0;
        });
        break;
    }
}

void ListView::assignCustomSortOrders(const QVector<DesktopRootItemState*>& orderedStates)
{
    int order = 0;
    for (DesktopRootItemState* state : orderedStates) {
        if (!state || !state->model.boxId.isEmpty()) {
            continue;
        }
        state->model.customSortOrder = order++;
    }
}

void ListView::clearDesktopCustomSortOrders()
{
    const QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
    for (DesktopRootItemState* state : states) {
        if (!state || !state->model.boxId.isEmpty()) {
            continue;
        }
        state->model.customSortOrder = -1;
    }
}

int ListView::desktopCustomInsertionIndexForPos(const QPoint& pos, const QStringList& draggedPaths) const
{
    QVector<DesktopRootItemState*> orderedStates = collectAutoArrangeStates();
    sortAutoArrangeStates(orderedStates, SortOrderType::SortByCustom);
    QSet<QString> draggedPathSet;
    for (const QString& path : draggedPaths) {
        draggedPathSet.insert(normalizeDesktopItemPath(path));
    }
    orderedStates.erase(std::remove_if(orderedStates.begin(), orderedStates.end(), [&](DesktopRootItemState* state) {
        return state && draggedPathSet.contains(normalizeDesktopItemPath(state->model.filePath));
    }), orderedStates.end());

    for (int i = 0; i < orderedStates.size(); ++i) {
        const QRect itemRect = orderedStates[i]->model.renderData.itemRect;
        if (itemRect.contains(pos)) {
            return pos.y() <= itemRect.center().y() ? i : i + 1;
        }
    }

    int bestIndex = 0;
    qint64 bestScore = LLONG_MAX;
    for (int i = 0; i <= orderedStates.size(); ++i) {
        const QRect rect = desktopAutoArrangeInsertionIndicatorRectForIndex(i, orderedStates);
        if (rect.isNull()) {
            continue;
        }

        const int dx = pos.x() < rect.left()
            ? rect.left() - pos.x()
            : (pos.x() > rect.right() ? pos.x() - rect.right() : 0);
        const int dy = pos.y() < rect.top()
            ? rect.top() - pos.y()
            : (pos.y() > rect.bottom() ? pos.y() - rect.bottom() : 0);
        const qint64 score = static_cast<qint64>(dx) * dx + static_cast<qint64>(dy) * dy;
        if (score < bestScore) {
            bestScore = score;
            bestIndex = i;
        }
    }

    return qBound(0, bestIndex, orderedStates.size());
}

void ListView::updateDesktopAutoArrangeReorderPreview(const QPoint& pos, const QStringList& draggedPaths)
{
    const int nextIndex = desktopCustomInsertionIndexForPos(pos, draggedPaths);
    QVector<DesktopRootItemState*> allOrderedStates = collectAutoArrangeStates();
    sortAutoArrangeStates(allOrderedStates, SortOrderType::SortByCustom);
    QVector<DesktopRootItemState*> orderedStates = collectAutoArrangeStates();
    sortAutoArrangeStates(orderedStates, SortOrderType::SortByCustom);
    QSet<QString> draggedPathSet;
    for (const QString& path : draggedPaths) {
        draggedPathSet.insert(normalizeDesktopItemPath(path));
    }
    orderedStates.erase(std::remove_if(orderedStates.begin(), orderedStates.end(), [&](DesktopRootItemState* state) {
        return state && draggedPathSet.contains(normalizeDesktopItemPath(state->model.filePath));
    }), orderedStates.end());

    QRect nextIndicatorRect;
    const int lineHeight = 2;
    const int horizontalInset = 8;
    const int indicatorWidth = qMax(20, m_iconWidth - horizontalInset * 2);
    for (DesktopRootItemState* draggedState : allOrderedStates) {
        if (!draggedState || !draggedPathSet.contains(normalizeDesktopItemPath(draggedState->model.filePath))) {
            continue;
        }

        const QRect draggedRect = draggedState->model.renderData.itemRect;
        const QRect draggedSlotRect = draggedRect.adjusted(0, -qMax(2, m_rowSpacing / 2), 0, qMax(2, m_rowSpacing / 2));
        if (!draggedSlotRect.contains(pos)) {
            continue;
        }

        const int draggedIndex = allOrderedStates.indexOf(draggedState);
        if (draggedIndex < 0) {
            continue;
        }

        const bool insertBeforeItem = pos.y() <= draggedRect.center().y();
        int y = insertBeforeItem ? draggedRect.top() : draggedRect.bottom();
        int left = draggedRect.left() + horizontalInset;
        int width = indicatorWidth;
        if (insertBeforeItem && draggedIndex > 0 && allOrderedStates[draggedIndex - 1]) {
            const QRect prevRect = allOrderedStates[draggedIndex - 1]->model.renderData.itemRect;
            y = (prevRect.bottom() + draggedRect.top()) / 2;
            y = qMin(y, draggedRect.top() - lineHeight);
            left = qMin(prevRect.left(), draggedRect.left()) + horizontalInset;
            const int right = qMax(prevRect.right(), draggedRect.right()) - horizontalInset;
            width = qMax(20, right - left);
        } else if (!insertBeforeItem && draggedIndex + 1 < allOrderedStates.size() && allOrderedStates[draggedIndex + 1]) {
            const QRect nextRect = allOrderedStates[draggedIndex + 1]->model.renderData.itemRect;
            y = (draggedRect.bottom() + nextRect.top()) / 2;
            y = qMax(y, draggedRect.bottom() + lineHeight);
            left = qMin(draggedRect.left(), nextRect.left()) + horizontalInset;
            const int right = qMax(draggedRect.right(), nextRect.right()) - horizontalInset;
            width = qMax(20, right - left);
        }

        nextIndicatorRect = QRect(left, y - lineHeight / 2, width, lineHeight);
        break;
    }

    for (int i = 0; i < allOrderedStates.size(); ++i) {
        if (!nextIndicatorRect.isNull()) {
            break;
        }
        DesktopRootItemState* hoverState = allOrderedStates[i];
        if (!hoverState || !hoverState->model.renderData.itemRect.contains(pos)) {
            continue;
        }

        const QRect hoverRect = hoverState->model.renderData.itemRect;
        const bool insertBeforeItem = pos.y() <= hoverRect.center().y();
        if (insertBeforeItem) {
            if (i > 0 && allOrderedStates[i - 1]) {
                const QRect prevRect = allOrderedStates[i - 1]->model.renderData.itemRect;
                const int y = (prevRect.bottom() + hoverRect.top()) / 2;
                const int left = qMin(prevRect.left(), hoverRect.left()) + horizontalInset;
                const int right = qMax(prevRect.right(), hoverRect.right()) - horizontalInset;
                nextIndicatorRect = QRect(left,
                    y - lineHeight / 2,
                    qMax(20, right - left),
                    lineHeight);
            } else {
                nextIndicatorRect = QRect(hoverRect.left() + horizontalInset,
                    hoverRect.top() - lineHeight / 2,
                    indicatorWidth,
                    lineHeight);
            }
        } else {
            if (i + 1 < allOrderedStates.size() && allOrderedStates[i + 1]) {
                const QRect nextRect = allOrderedStates[i + 1]->model.renderData.itemRect;
                const int y = (hoverRect.bottom() + nextRect.top()) / 2;
                const int left = qMin(hoverRect.left(), nextRect.left()) + horizontalInset;
                const int right = qMax(hoverRect.right(), nextRect.right()) - horizontalInset;
                nextIndicatorRect = QRect(left,
                    y - lineHeight / 2,
                    qMax(20, right - left),
                    lineHeight);
            } else {
                nextIndicatorRect = QRect(hoverRect.left() + horizontalInset,
                    hoverRect.bottom() - lineHeight / 2,
                    indicatorWidth,
                    lineHeight);
            }
        }
        break;
    }
    if (nextIndicatorRect.isNull()) {
        nextIndicatorRect = desktopAutoArrangeInsertionIndicatorRectForIndex(nextIndex, orderedStates);
    }
    for (DesktopRootItemState* draggedState : allOrderedStates) {
        if (!draggedState || !draggedPathSet.contains(normalizeDesktopItemPath(draggedState->model.filePath)) || nextIndicatorRect.isNull()) {
            continue;
        }

        const QRect draggedRect = draggedState->model.renderData.itemRect;
        if (!draggedRect.intersects(nextIndicatorRect) && !draggedRect.contains(nextIndicatorRect.center())) {
            continue;
        }

        const int y = pos.y() <= draggedRect.center().y()
            ? draggedRect.top() - lineHeight
            : draggedRect.bottom() + lineHeight;
        nextIndicatorRect.moveTop(y - lineHeight / 2);
        break;
    }

    const bool sameDraggedItems = (m_desktopReorderDraggedPaths == draggedPaths);
    if (m_desktopReorderInsertIndex == nextIndex &&
        sameDraggedItems &&
        m_desktopReorderIndicatorRect == nextIndicatorRect) {
        return;
    }

    m_desktopReorderInsertIndex = nextIndex;
    m_desktopReorderDraggedPaths = draggedPaths;
    m_desktopReorderIndicatorRect = nextIndicatorRect;
    update();
}

void ListView::clearDesktopAutoArrangeReorderPreview()
{
    if (m_desktopReorderInsertIndex < 0 &&
        m_desktopReorderDraggedPaths.isEmpty() &&
        m_desktopReorderIndicatorRect.isNull()) {
        return;
    }

    m_desktopReorderInsertIndex = -1;
    m_desktopReorderDraggedPaths.clear();
    m_desktopReorderIndicatorRect = QRect();
    update();
}

QRect ListView::desktopAutoArrangeInsertionIndicatorRect() const
{
    if (!m_iconShowRule.autoArrange ||
        m_iconShowRule.sortType != SortOrderType::SortByCustom ||
        m_desktopReorderInsertIndex < 0) {
        return QRect();
    }

    if (!m_desktopReorderIndicatorRect.isNull()) {
        return m_desktopReorderIndicatorRect;
    }

    QVector<DesktopRootItemState*> orderedStates = collectAutoArrangeStates();
    sortAutoArrangeStates(orderedStates, SortOrderType::SortByCustom);
    QSet<QString> draggedPathSet;
    for (const QString& path : m_desktopReorderDraggedPaths) {
        draggedPathSet.insert(normalizeDesktopItemPath(path));
    }
    orderedStates.erase(std::remove_if(orderedStates.begin(), orderedStates.end(), [&](DesktopRootItemState* state) {
        return state && draggedPathSet.contains(normalizeDesktopItemPath(state->model.filePath));
    }), orderedStates.end());

    return desktopAutoArrangeInsertionIndicatorRectForIndex(m_desktopReorderInsertIndex, orderedStates);
}

QRect ListView::desktopAutoArrangeInsertionIndicatorRectForIndex(int insertIndex, const QVector<DesktopRootItemState*>& orderedStates) const
{
    insertIndex = qBound(0, insertIndex, orderedStates.size());
    const int lineHeight = 2;
    const int horizontalInset = 8;
    const int indicatorWidth = qMax(20, m_iconWidth - horizontalInset * 2);

    if (orderedStates.isEmpty()) {
        return QRect(m_marginLeft + horizontalInset, m_marginTop - lineHeight / 2, indicatorWidth, lineHeight);
    }

    if (insertIndex <= 0) {
        const QRect nextRect = orderedStates.first()->model.renderData.itemRect;
        const int y = qMax(0, nextRect.top());
        return QRect(nextRect.left() + horizontalInset, y - lineHeight / 2, indicatorWidth, lineHeight);
    }

    if (insertIndex >= orderedStates.size()) {
        const QRect prevRect = orderedStates.last()->model.renderData.itemRect;
        const int y = qMin(height() - lineHeight, prevRect.bottom());
        return QRect(prevRect.left() + horizontalInset, y - lineHeight / 2, indicatorWidth, lineHeight);
    }

    const QRect prevRect = orderedStates[insertIndex - 1]->model.renderData.itemRect;
    const QRect nextRect = orderedStates[insertIndex]->model.renderData.itemRect;
    const int strideX = m_iconWidth + m_colSpacing;
    const bool sameColumn = qAbs(prevRect.center().x() - nextRect.center().x()) <= qMax(1, strideX / 2);

    if (sameColumn) {
        const int y = (prevRect.bottom() + nextRect.top()) / 2;
        const int left = qMin(prevRect.left(), nextRect.left()) + horizontalInset;
        const int right = qMax(prevRect.right(), nextRect.right()) - horizontalInset;
        return QRect(left, y - lineHeight / 2, qMax(20, right - left), lineHeight);
    }

    const int y = qMax(0, nextRect.top());
    return QRect(nextRect.left() + horizontalInset, y - lineHeight / 2, indicatorWidth, lineHeight);
}
QVector<QPoint> ListView::layoutAutoArrangeItems(const QVector<DesktopRootItemState*>& states) const
{
    QVector<QPoint> positions;
    positions.reserve(states.size());

    const int itemWidth = m_iconWidth;
    const int itemHeight = desktopItemFootprintHeight();
    const int strideX = itemWidth + m_colSpacing;
    const int strideY = itemHeight + m_rowSpacing;
    int iconX = desktopLayoutLeft();
    int iconY = desktopLayoutTop();
    const int usableBottom = desktopUsableBottom();
    logDesktopLayoutMetrics("AutoArrangeModel",
                            this,
                            desktopLayoutLeft(),
                            desktopLayoutTop(),
                            desktopUsableRight(),
                            usableBottom,
                            itemWidth,
                            itemHeight,
                            strideX,
                            strideY);
    for (DesktopRootItemState* state : states) {
        if (!state) {
            positions.push_back(QPoint());
            continue;
        }

        positions.push_back(QPoint(iconX, iconY));

        iconY += strideY;
        if (iconY > desktopLayoutTop() && iconY + itemHeight - 1 > usableBottom) {
            iconX += strideX;
            iconY = desktopLayoutTop();
        }
    }

    return positions;
}

void ListView::finalizeAutoArrangeLayout(const QVector<DesktopRootItemState*>& states,
                                         const QVector<QPoint>& positions,
                                         bool persist,
                                         bool refreshSelection)
{
    if (states.size() != positions.size()) {
        return;
    }

    QScopedPointer<ListViewSelectionSnapshot> selectionSnapshot;
    if (refreshSelection) {
        selectionSnapshot.reset(new ListViewSelectionSnapshot(ListViewSelectionSnapshot::capture(*this)));
    }

    for (int i = 0; i < states.size(); ++i) {
        DesktopRootItemState* state = states[i];
        if (!state) {
            continue;
        }

        const QPoint& pos = positions[i];
        state->model.position = pos;
        state->model.baseSize = QSize(m_iconWidth, m_iconHeight);
    }

    updateAllItemRenderData();
    rebuildSpatialIndex();
    update();
    if (persist) {
        requestSaveBoxes();
    }
    if (selectionSnapshot) {
        selectionSnapshot->restore(*this, false);
    }
}

void ListView::applyAutoArrangeLayout(bool persist, bool refreshSelection)
{
    QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
    sortAutoArrangeStates(states, m_iconShowRule.sortType);
    if (m_iconShowRule.sortType == SortOrderType::SortByCustom) {
        assignCustomSortOrders(states);
    }
    const QVector<QPoint> positions = layoutAutoArrangeItems(states);
    finalizeAutoArrangeLayout(states, positions, persist, refreshSelection);
}

void ListView::applyAutoArrangeAfterDesktopMutation(bool persist, bool refreshSelection)
{
    if (!m_iconShowRule.autoArrange) {
        return;
    }

    applyAutoArrangeLayout(persist, refreshSelection);
}

bool ListView::fileHasHiddenAttribute(const QString& filePath) const
{
    if (filePath.isEmpty() || filePath.startsWith("::")) {
        return false;
    }

    const std::wstring nativePath = QDir::toNativeSeparators(filePath).toStdWString();
    const DWORD attributes = GetFileAttributesW(nativePath.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_HIDDEN);
}

void ListView::applyWindowsHiddenVisualState(ListItem* item)
{
    if (!item) {
        return;
    }

    const bool isSpecial = item->getItemData().type() == ObjectType::SpecialFolder ||
        SystemFolderCLSIDs.contains(item->getItemData().getCLSID()) ||
        item->filePath().startsWith("::");
    const bool shouldFade = !isSpecial && m_showHiddenFiles && fileHasHiddenAttribute(item->filePath());
    item->setHiddenFileVisual(shouldFade);
}

bool ListView::shouldShowFileSystemItem(ListItem* item) const
{
    if (!item) {
        return false;
    }

    if (item->type() == ObjectType::DiskLetter) {
        return false;
    }

    const bool isSpecial = item->getItemData().type() == ObjectType::SpecialFolder ||
        SystemFolderCLSIDs.contains(item->getItemData().getCLSID()) ||
        item->filePath().startsWith("::");
    if (isSpecial) {
        return true;
    }

    return m_showHiddenFiles || !fileHasHiddenAttribute(item->filePath());
}

void ListView::refreshVisibilityForWindowsConfig()
{
    const QVector<QPointer<LinBox>> boxes = BoxManager::instance()->allBoxes();
    for (const QPointer<LinBox>& box : boxes) {
        if (box && box->isFolderMapped()) {
            reloadMappedFolderBox(box);
        }
    }

    onMenuRefresh();
}

void ListView::loadAutoLayout()
{
    applyAutoArrangeLayout(false, false);
}

void ListView::loadGridLayout()
{
    const int strideX = m_iconWidth + m_colSpacing;
    const int itemHeight = desktopItemFootprintHeight();
    const int strideY = itemHeight + m_rowSpacing;
    const int layoutLeft = desktopLayoutLeft();
    const int layoutTop = desktopLayoutTop();
    logDesktopLayoutMetrics("GenerateItemPosition",
                            this,
                            layoutLeft,
                            layoutTop,
                            desktopUsableRight(),
                            desktopUsableBottom(),
                            m_iconWidth,
                            itemHeight,
                            strideX,
                            strideY);
    const int maxRows = maxGridRowsForBounds(layoutTop, desktopUsableBottom(), itemHeight, strideY);
    const int maxCols = maxGridColsForBounds(layoutLeft, desktopUsableRight(), m_iconWidth, strideX);
    logDesktopLayoutMetrics("GridLayout",
                            this,
                            layoutLeft,
                            layoutTop,
                            desktopUsableRight(),
                            desktopUsableBottom(),
                            m_iconWidth,
                            itemHeight,
                            strideX,
                            strideY);
    const int visibleCapacity = qMax(1, maxCols * maxRows);
    QSet<quint64> occupiedSlots;
    int overflowIndex = 0;

    for (auto* item : m_items) {
        item->setItemSize(m_iconWidth, m_iconHeight);
        item->initData();

        QPoint p = item->getLocationInfo(); // Rely strictly on explicitly calculated memory coordinates
        if (p == QPoint(0, 0)) {
            p = generateItemPosition();
            item->move(p);
        }

        if (m_iconShowRule.gridAlignment) {
            const int requestedIndex = pointToGridLinearIndex(p, layoutLeft, layoutTop, strideX, strideY, maxRows);
            int gridX = 0;
            int gridY = 0;

            if (occupiedSlots.size() < visibleCapacity) {
                for (int step = 0; step < visibleCapacity; ++step) {
                    const int linearIndex = (requestedIndex + step) % visibleCapacity;
                    const QPoint candidateCell = gridIndexToCell(linearIndex, maxRows);
                    const quint64 slotKey = packGridCell(candidateCell.x(), candidateCell.y());
                    if (!occupiedSlots.contains(slotKey)) {
                        gridX = candidateCell.x();
                        gridY = candidateCell.y();
                        occupiedSlots.insert(slotKey);
                        break;
                    }
                }
            } else {
                const QPoint overflowCell = gridIndexToCell(overflowIndex % visibleCapacity, maxRows);
                gridX = overflowCell.x();
                gridY = overflowCell.y();
                ++overflowIndex;
            }

            const QPoint snappedPoint = gridCellToPoint(gridX, gridY, layoutLeft, layoutTop, strideX, strideY);
            const int snappedX = snappedPoint.x();
            const int snappedY = snappedPoint.y();

            if (item->pos() != QPoint(snappedX, snappedY)) {
                item->updateLocationInfo(QPoint(snappedX, snappedY));
                item->move(snappedX, snappedY);
            }
        } else {
            if (item->pos() != p) {
                item->updateLocationInfo(p);
                item->move(p);
            }
        }
    }

    rebuildSpatialIndex();
}
ListView::SortOrderType ListView::getOppositeDirection() {

    switch (m_iconShowRule.sortType) {
    case SortOrderType::SortByName_ASC:
        return SortOrderType::SortByName_DESC;
    case SortOrderType::SortByName_DESC:
        return SortOrderType::SortByName_ASC;
    case SortOrderType::SortBySize_ASC:
        return SortOrderType::SortBySize_DESC;
    case SortOrderType::SortBySize_DESC:
        return SortOrderType::SortBySize_ASC;
    case SortOrderType::SortByType_ASC:
        return SortOrderType::SortByType_DESC;
    case SortOrderType::SortByType_DESC:
        return SortOrderType::SortByType_ASC;
    case SortOrderType::SortByDateModified_ASC:
        return SortOrderType::SortByDateModified_DESC;
    case SortOrderType::SortByDateModified_DESC:
        return SortOrderType::SortByDateModified_ASC;
    case SortOrderType::SortByCategorized:
        return SortOrderType::SortByCategorized;
    case SortOrderType::SortByCustom:
        return SortOrderType::SortByCustom;
    case SortOrderType::Default:
        return SortOrderType::SortByName_ASC; // Or stay at Default
    default:
        return SortOrderType::SortByName_ASC;
    }
}
void ListView::sortItems(SortOrderType order)
{
    // Filter out items that are inside a Box (Mutual Exclusion)
    auto filterIt = std::remove_if(m_items.begin(), m_items.end(), [](ListItem* item) {
        return !item || !item->boxId().isEmpty();
    });
    m_items.erase(filterIt, m_items.end());

    if (m_iconShowRule.sortType == order) {
        order = getOppositeDirection();
    }
    
    performSort(order);
}

void ListView::performSort(SortOrderType order)
{
    switch (order) {
    case SortOrderType::Default:
        // Use Categorized sort if auto-arrange is on, otherwise do nothing
        if (m_iconShowRule.autoArrange) {
            sortByCategorized();
        }
        break;
    case SortOrderType::SortByName_ASC:
        sortByName(true);
        break;
    case SortOrderType::SortByName_DESC:
        sortByName(false);
        break;
    case SortOrderType::SortBySize_ASC:
        sortBySize(true);
        break;
    case SortOrderType::SortBySize_DESC:
        sortBySize(false);
        break;
    case SortOrderType::SortByType_ASC:
        sortByType(true);
        break;
    case SortOrderType::SortByType_DESC:
        sortByType(false);
        break;
    case SortOrderType::SortByDateModified_ASC:
        sortByDate(true);
        break;
    case SortOrderType::SortByDateModified_DESC:
        sortByDate(false);
        break;
    case SortOrderType::SortByCategorized:
        sortByCategorized();
        break;
    case SortOrderType::SortByCustom:
        sortByCustom();
        break;
    default:
        // Do nothing for unknown states
        break;
    }
    m_iconShowRule.sortType = order;
    applyAutoArrangeAfterDesktopMutation(false, false);
}

void ListView::sortByCategorized()
{
    // Ensure box-contained items are excluded
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [](ListItem* item) {
        return !item || !item->boxId().isEmpty();
    }), m_items.end());

    std::sort(m_items.begin(), m_items.end(), [](ListItem* a, ListItem* b) {
        // 1. Definition of priority groups
        auto getPriority = [](ListItem* item) -> int {
            if (item->type() == ObjectType::SpecialFolder) return 0; // System Folders
            if (item->type() == ObjectType::DiskLetter || item->fileInfo().isDir()) return 1; // Directory-like items
            
            QString ext = item->fileInfo().suffix().toLower();
            if (ext == "exe" || ext == "lnk" || ext == "url") return 2; // Apps
            
            static const QStringList docExts = {"txt", "pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "md", "zip", "rar", "7z"};
            if (docExts.contains(ext)) return 3;                     // Common Docs
            
            return 4;                                               // Miscellaneous
        };

        int pA = getPriority(a);
        int pB = getPriority(b);
        if (pA != pB) return pA < pB;

        // 2. Secondary sort: Alphabetical (Natural sort order)
        return a->baseName().toLower() < b->baseName().toLower();
    });
}

void ListView::sortByCustom()
{
    QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
    sortAutoArrangeStates(states, SortOrderType::SortByCustom);
    assignCustomSortOrders(states);
}

void ListView::sortByName(bool ascending)
{
    // Ensure box-contained items are excluded
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [](ListItem* item) {
        return !item || !item->boxId().isEmpty();
    }), m_items.end());

    QCollator collator;
    collator.setNumericMode(true);          // 闁告凹鍨抽弫銈夊极閺夎法鎽熼柟鐑樺笒缁?
    collator.setCaseSensitivity(Qt::CaseInsensitive); // 濞戞挸绉寸亸顖炲礆閸℃ぜ浜ｉ悘蹇撶箰閸?
    collator.setIgnorePunctuation(true);    // 闊洨鏅弳鎰板冀閸モ晛浠紒妤嬬畱瑜?
    // 閻庣敻鈧稓鑹惧☉鎿冨幗閺嬪啴鎮抽姘兼殧闁挎稑鐭侀鏇犵磾椤旇姤鎷遍柛锔芥緲鐎?
    collator.setLocale(QLocale::system());

    std::sort(m_items.begin(), m_items.end(), [&](ListItem* a, ListItem* b) {
        auto getPriority = [](ListItem* item) {
            if (item->type() == ObjectType::SpecialFolder) {
                QString clsid = item->getItemData().getCLSID();
                if (clsid == APP_USER_FOLDER_CLS) return 1;
                if (clsid == APP_MY_COMPUTER_CLS) return 2;
                if (clsid == APP_NETWORK_CLS) return 3;
                if (clsid == APP_RECYCLE_BIN_CLS) return 4;
                if (clsid == APP_CONTROL_PANEL_CLS) return 5;

                return 6;
            }
            return 100;
        };

        int prioA = getPriority(a);
        int prioB = getPriority(b);

        if (prioA != prioB) {
            // 闁告娲ょ花顓㈠籍鐠佸湱绀夊ù鍏济崢娑氱棯瑜嶉惃顒勬儍閸曨剙绗撻柛锔哄妼婢х娀妫冮～顔剧1, 2, 3, 100闁?
            // 闂傚嫬绉寸花顓㈠籍鐠佸湱绀夊ù鍏济崢娑氱棯瑜嶉妵鍥儍閸曨剙绗撻柛锔哄妼婢х娀妫冮～顔剧100, 3, 2, 1闁?
            return ascending ? (prioA < prioB) : (prioA > prioB);
        }

        int result = collator.compare(a->getSortName(), b->getSortName());
        return ascending ? (result < 0) : (result > 0);
    });
}

void ListView::sortBySize(bool ascending)
{
    // Ensure box-contained items are excluded
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [](ListItem* item) {
        return !item || !item->boxId().isEmpty();
    }), m_items.end());

    std::sort(m_items.begin(), m_items.end(), [ascending](ListItem* a, ListItem* b) {
        // 闁绘顫夐悾鈺呭棘閸ワ附顐藉鍓佹缁辨瑧鍖栭懡銈囧煚闁搞儳鍋撻悥锝夋晬婢跺娼楃紓浣哥墛鐢捇宕烽妸锔戒粯闁告挸绋勭槐婵嬪礃閸涙潙鍔ュ銈呮惈缁厽绌卞┑鍥х槷濞戞挸绉磋ぐ?
        bool aIsSpecial = (a->type() == ObjectType::SpecialFolder);
        bool bIsSpecial = (b->type() == ObjectType::SpecialFolder);
        if (aIsSpecial != bIsSpecial)
            return aIsSpecial;

        // 闁哄倸娲ｅ▎銏″緞鐟欏嫮姊鹃柡?濠㈠爢鍐瘓"闁挎稑鐬肩划鐑樼▔閳ь剛鎲撮崱鏇＄ -1闁挎稑鏈敮鎾诲捶閵婏附鐝梺顐ｇ閺嬪啯绂掔捄鍝勵枀闂傚牜婢佺槐娆撳础閸パ呯闁?
        bool aIsDir = a->fileInfo().isDir();
        bool bIsDir = b->fileInfo().isDir();
        if (aIsDir != bIsDir)
            return ascending ? aIsDir : !aIsDir;

        qint64 sizeA = a->fileInfo().size();
        qint64 sizeB = b->fileInfo().size();
        if (sizeA != sizeB)
            return ascending ? (sizeA < sizeB) : (sizeA > sizeB);

        // 濠㈠爢鍐瘓闁烩晝顭堥幃鎾诲籍閼搁潧鐦婚柛姘Ф琚ㄩ柛妤€娲ょ花顓㈠礂濠婂啰淇?
        return a->baseName().toLower() < b->baseName().toLower();
    });
}

void ListView::sortByType(bool ascending)
{
    // Ensure box-contained items are excluded
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [](ListItem* item) {
        return !item || !item->boxId().isEmpty();
    }), m_items.end());

    std::sort(m_items.begin(), m_items.end(), [ascending](ListItem* a, ListItem* b) {
        bool aIsSpecial = (a->type() == ObjectType::SpecialFolder);
        bool bIsSpecial = (b->type() == ObjectType::SpecialFolder);
        if (aIsSpecial != bIsSpecial)
            return aIsSpecial;

        // 闁圭顦伴弸鍐╃閼搁潧鈷栭悘鐐存礀閹洟骞掗幒鎴犵闁挎稒绋掗弸鍐╃鐠烘亽浠氶柟纰樻櫅閻秹宕ュ鍫綊濞戞捁娅ｉ埞鏍偓娑欘殘椤戜焦绋夌拠褏绀勯柟鐑樺笚濞撳爼宕滃蹇曠
        const bool aIsDirLike = (a->type() == ObjectType::DiskLetter) || a->fileInfo().isDir();
        const bool bIsDirLike = (b->type() == ObjectType::DiskLetter) || b->fileInfo().isDir();
        QString extA = aIsDirLike ? QString() : a->fileInfo().suffix().toLower();
        QString extB = bIsDirLike ? QString() : b->fileInfo().suffix().toLower();
        if (extA != extB)
            return ascending ? (extA < extB) : (extA > extB);

        // 闁告艾鐬肩悮顐﹀垂鐎ｎ偅顦ч柟绋款槸閹洜绮旈弶鍨３閹兼潙绻愰崢瑙勬償?
        return a->baseName().toLower() < b->baseName().toLower();
    });
}

void ListView::sortByDate(bool ascending)
{
    // Ensure box-contained items are excluded
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [](ListItem* item) {
        return !item || !item->boxId().isEmpty();
    }), m_items.end());

    std::sort(m_items.begin(), m_items.end(), [ascending](ListItem* a, ListItem* b) {
        bool aIsSpecial = (a->type() == ObjectType::SpecialFolder);
        bool bIsSpecial = (b->type() == ObjectType::SpecialFolder);
        if (aIsSpecial != bIsSpecial)
            return aIsSpecial;

        QDateTime dateA = a->fileInfo().lastModified();
        QDateTime dateB = b->fileInfo().lastModified();
        if (dateA != dateB)
            return ascending ? (dateA < dateB) : (dateA > dateB);

        // 闁哄啫鐖煎Λ鍧楁儎缁嬫寧鍊遍柡鍐煐鐎垫粓宕ュ鍥嗙偤宕￠崶褏纰嶉柛蹇旂矊缁?
        return a->baseName().toLower() < b->baseName().toLower();
    });
}

void ListView::addItem(ListItem* item, bool refresh)
{
    if (!item) return;

    applyWindowsHiddenVisualState(item);
    const bool shouldShowOnDesktop = shouldShowFileSystemItem(item);
    QPoint pos;
    if (shouldShowOnDesktop && !m_iconShowRule.autoArrange) {
        if (!takeReservedCreatedItemPosition(item->filePath(), pos)) {
            pos = generateItemPosition();
        }
        item->updateLocationInfo(pos);
    }
    item->setItemSize(m_iconWidth, m_iconHeight);
    
    setupItemConnections(item);
    ensureModelStateForLegacyItem(item);
    
    // 闁圭鍋撻柡鍫濐槹閺嬪啯绂掗崼鏇炲幋闁告梻濮撮崣?m_items 闁?m_allItems
    if (shouldShowOnDesktop) {
        m_items.push_back(item);
    } else if (!m_hiddenIcons.contains(item)) {
        m_hiddenIcons.push_back(item);
    }
    m_allItems.push_back(item);
    
    if (shouldShowOnDesktop && m_iconShowRule.showDesktopIcons) {
        item->show();
    } else {
        item->hide();
    }
    if (shouldShowOnDesktop && m_iconShowRule.autoArrange) {
        applyAutoArrangeLayout(false, false);
    } else {
        rebuildSpatialIndex();
    }
}

void ListView::addItemNoDelete(ListItem* item)
{
    if (!item) return;
    
    // Clear box id - item is returning to desktop
    item->setBoxId(QString());
    
    // Ensure parent is ListView for proper positioning
    item->setParent(this);
    
    // Check if already in m_allItems
    if (!m_allItems.contains(item)) {
        m_allItems.push_back(item);
    }
    ensureModelStateForLegacyItem(item);

    m_hiddenIcons.removeOne(item);
    
    // Check if already in m_items
    if (!m_items.contains(item)) {
        applyWindowsHiddenVisualState(item);
        if (!shouldShowFileSystemItem(item)) {
            if (!m_hiddenIcons.contains(item)) {
                m_hiddenIcons.push_back(item);
            }
            item->hide();
            return;
        }

        item->setItemSize(m_iconWidth, m_iconHeight);
        if (!m_iconShowRule.autoArrange) {
            QPoint pos = generateItemPosition();
            item->updateLocationInfo(pos);
        }
        setupItemConnections(item);
        m_items.push_back(item);
    }
    
    // Ensure item is visible on desktop
    if (m_iconShowRule.showDesktopIcons) {
        item->show();
    }

    if (m_iconShowRule.autoArrange) {
        applyAutoArrangeLayout(false, false);
    } else {
        rebuildSpatialIndex();
        update();
    }
}

void ListView::removeItem(ListItem* item)
{
    if (item && item->isReady) {
        qDebug() << item->filePath();
        // 濞寸姴瀛╂晶宥夊嫉婢跺本绁查柛蹇氭珪閺嗙喓绱掗崟顏囧幀缂佸顭峰▍宸宼em
        m_items.removeOne(item);
        m_allItems.removeOne(item);
        m_selectedItems.removeOne(item);
        m_selectedBoxPaths.removeOne(normalizeDesktopItemPath(item->filePath()));
        removeModelStateForLegacyItem(item);
        
        // 缁绢収鍠曠换姘鎼淬垹顣查悘鐐靛仧濞?Box 濞戞搩鍘虹弧鍐矓婵犳碍鐝熼悹?item (闂傚啫寮堕娑㈠箖椤掑倵鏁勯柟绋挎喘閹?
        if (!item->boxId().isEmpty()) {
            LinBox* box = BoxManager::instance()->findBoxById(item->boxId());
            if (box) {
                box->removeItemState(item->filePath());
            }
        }

        // 缁绢収鍠曠换姝﹖em閻炴凹鍋勯悾顒勫礂閵娾晜顓洪柦?
        item->setVisible(false);
        item->hide();
        
        // 閻犱礁澧介悿鍡涙偉閼搁潧浠樺ù鐘虫构鐠愮劆ullptr闁挎稑鐬奸垾妯荤┍濠曨柡em濞戞挸绉撮崯鈧柛娆愵殘閸╂骞掕濞嗐垻绮婚敍鍕€?
        item->setParent(nullptr);
        
        item->isReady = false;
        item->deleteLater();
    }
    
    // 鐎殿喖鎼崺妤呭即鐎涙ɑ鐓€闁伙絽鐭傚?
    if (m_iconShowRule.autoArrange) {
        applyAutoArrangeLayout(false, false);
    } else {
        rebuildSpatialIndex();
        update();
        repaint();
    }
    
}

void ListView::removeItemNoDelete(ListItem* item)
{
    if (item && item->isReady) {
        qDebug() << "[removeItemNoDelete]" << item->filePath();
        m_selectedItems.removeOne(item);
        m_selectedBoxPaths.removeOne(normalizeDesktopItemPath(item->filePath()));
        m_items.removeOne(item);
        if (!m_hiddenIcons.contains(item)) {
            m_hiddenIcons.push_back(item);
        }
          
        // Keep the item in global render collections. Box only stores a reference.
        item->setVisible(false);
        item->hide();
    }
    
    // 鐎殿喖鎼崺妤呭即鐎涙ɑ鐓€闁伙絽鐭傚?
    if (m_iconShowRule.autoArrange) {
        applyAutoArrangeLayout(false, false);
    } else {
        rebuildSpatialIndex();
        update();
        repaint();
    }
}

void ListView::removeItemsBatch(const QVector<ListItem*>& items, bool deleteWidgets)
{
    if (items.isEmpty()) {
        return;
    }

    QSet<ListItem*> seenItems;
    QSet<LinBox*> sourceBoxes;
    bool removedAny = false;

    for (ListItem* item : items) {
        if (!item || !item->isReady || seenItems.contains(item)) {
            continue;
        }
        seenItems.insert(item);
        removedAny = true;

        m_items.removeOne(item);
        m_allItems.removeOne(item);
        m_selectedItems.removeOne(item);
        m_selectedBoxPaths.removeOne(normalizeDesktopItemPath(item->filePath()));
        m_hiddenIcons.removeOne(item);
        removeModelStateForLegacyItem(item);

        if (m_focusedItem == item) {
            m_focusedItem = nullptr;
            m_focusedBoxPath.clear();
        }
        if (m_anchorItem == item) {
            m_anchorItem = nullptr;
            m_anchorBoxPath.clear();
        }

        if (!item->boxId().isEmpty()) {
            if (LinBox* box = BoxManager::instance()->findBoxById(item->boxId())) {
                box->removeItemState(item->filePath(), false);
                sourceBoxes.insert(box);
            }
        }

        item->setVisible(false);
        item->hide();

        if (deleteWidgets) {
            item->setParent(nullptr);
            item->isReady = false;
            item->deleteLater();
        }
    }

    if (!removedAny) {
        return;
    }

    for (LinBox* box : sourceBoxes) {
        if (!box) {
            continue;
        }
        box->refreshLayout();
        box->update();
    }

    if (m_iconShowRule.autoArrange) {
        applyAutoArrangeLayout(false, false);
    } else {
        rebuildSpatialIndex();
        update();
        repaint();
    }
}

void ListView::setItemsTransientHidden(const QVector<ListItem*>& items, bool hidden)
{
    if (items.isEmpty()) {
        return;
    }

    QSet<LinBox*> sourceBoxes;
    for (ListItem* item : items) {
        if (!item || !item->isReady) {
            continue;
        }

        if (!item->boxId().isEmpty()) {
            if (LinBox* sourceBox = BoxManager::instance()->findBoxById(item->boxId())) {
                sourceBoxes.insert(sourceBox);
            }
        }

        if (hidden) {
            item->setVisible(false);
            item->hide();
        } else {
            item->setVisible(true);
            item->show();
        }
    }

    for (LinBox* sourceBox : sourceBoxes) {
        if (!sourceBox) {
            continue;
        }
        sourceBox->update();
        sourceBox->repaint();
    }

    update();
    repaint();
}

void ListView::trackPendingExternalRemovals(const QVector<ListItem*>& items)
{
    for (ListItem* item : items) {
        if (!item) {
            continue;
        }

        const QString normalizedPath = normalizeDesktopPath(item->filePath());
        if (!normalizedPath.isEmpty()) {
            m_pendingExternalRemovalPaths.insert(normalizedPath);
        }
    }
}

bool ListView::consumePendingExternalRemoval(const QString& path)
{
    const QString normalizedPath = normalizeDesktopPath(path);
    if (normalizedPath.isEmpty()) {
        return false;
    }
    return m_pendingExternalRemovalPaths.remove(normalizedPath);
}

ListItem* ListView::findItemByPath(const QString& filePath) const {
    if (filePath.isEmpty()) return nullptr;
    
    QString target = QDir::toNativeSeparators(filePath);
    while (target.endsWith('\\')) target.chop(1);
    
    for (auto* item : m_allItems) {
        if (!item || !item->isReady) continue;
        QString itemPath = QDir::toNativeSeparators(item->filePath());
        while (itemPath.endsWith('\\')) itemPath.chop(1);
        if (itemPath == target) {
            return item;
        }
    }
    return nullptr;
}

QPoint ListView::resolveCreatePosition(const QPoint& requestedPos) const
{
    QPoint pos = requestedPos;
    if (pos == QPoint(-1, -1)) {
        pos = const_cast<ListView*>(this)->generateItemPosition();
    }

    const int itemHeight = desktopItemFootprintHeight();
    const int layoutLeft = desktopLayoutLeft();
    const int layoutTop = desktopLayoutTop();
    const int usableRight = desktopUsableRight();
    const int usableBottom = desktopUsableBottom();

    if (m_iconShowRule.gridAlignment) {
        const int strideX = m_iconWidth + m_colSpacing;
        const int strideY = itemHeight + m_rowSpacing;
        const int maxRows = maxGridRowsForBounds(layoutTop, usableBottom, itemHeight, strideY);
        const int maxCols = maxGridColsForBounds(layoutLeft, usableRight, m_iconWidth, strideX);
        const int visibleCapacity = qMax(1, maxCols * maxRows);
        const int requestedIndex = pointToGridLinearIndex(pos, layoutLeft, layoutTop, strideX, strideY, maxRows);
        int gridX = 0;
        int gridY = 0;

        QSet<quint64> occupiedSlots;
        int desktopItemCount = 0;
        for (const auto& state : m_itemStates) {
            if (!state.model.boxId.isEmpty() || !state.model.visible) continue;
            ++desktopItemCount;
            const QPoint gridCell = pointToGridCell(state.model.position, layoutLeft, layoutTop, strideX, strideY);
            occupiedSlots.insert(packGridCell(qBound(0, gridCell.x(), maxCols - 1), qBound(0, gridCell.y(), maxRows - 1)));
        }
        for (auto it = m_pendingCreatedItemPositions.constBegin(); it != m_pendingCreatedItemPositions.constEnd(); ++it) {
            ++desktopItemCount;
            const QPoint gridCell = pointToGridCell(it.value(), layoutLeft, layoutTop, strideX, strideY);
            occupiedSlots.insert(packGridCell(qBound(0, gridCell.x(), maxCols - 1), qBound(0, gridCell.y(), maxRows - 1)));
        }

        if (occupiedSlots.size() < visibleCapacity) {
            for (int offset = 0; offset < visibleCapacity; ++offset) {
                const int cellIndex = (requestedIndex + offset) % visibleCapacity;
                const QPoint visibleCell = gridIndexToCell(cellIndex, maxRows);
                const quint64 slotKey = packGridCell(visibleCell.x(), visibleCell.y());
                if (occupiedSlots.contains(slotKey)) {
                    continue;
                }
                gridX = visibleCell.x();
                gridY = visibleCell.y();
                return gridCellToPoint(gridX, gridY, layoutLeft, layoutTop, strideX, strideY);
            }
        }

        const QPoint overflowCell = gridIndexToCell(desktopItemCount % visibleCapacity, maxRows);
        return gridCellToPoint(overflowCell.x(), overflowCell.y(), layoutLeft, layoutTop, strideX, strideY);
    }

    pos.setX(qMax(layoutLeft, qMin(pos.x(), usableRight - m_iconWidth + 1)));
    pos.setY(qMax(layoutTop, qMin(pos.y(), usableBottom - itemHeight + 1)));

    const int step = 16;
    const int minX = layoutLeft;
    const int minY = layoutTop;
    const int maxX = qMax(minX, usableRight - m_iconWidth + 1);
    const int maxY = qMax(minY, usableBottom - itemHeight + 1);
    QSet<quint64> visitedPositions;

    while (true) {
        QRect candidateRect(pos, QSize(m_iconWidth, itemHeight));
        bool occupied = false;

        for (const auto& state : m_itemStates) {
            if (!state.model.boxId.isEmpty() || !state.model.visible) continue;
            const QRect stateRect(state.model.position, QSize(m_iconWidth, itemHeight));
            if (candidateRect.intersects(stateRect)) {
                occupied = true;
                break;
            }
        }

        if (!occupied) {
            for (auto it = m_pendingCreatedItemPositions.constBegin(); it != m_pendingCreatedItemPositions.constEnd(); ++it) {
                QRect pendingRect(it.value(), QSize(m_iconWidth, itemHeight));
                if (candidateRect.intersects(pendingRect)) {
                    occupied = true;
                    break;
                }
            }
        }

        if (!occupied) {
            return pos;
        }

        const quint64 visitKey = packGridCell(pos.x(), pos.y());
        if (visitedPositions.contains(visitKey)) {
            return pos;
        }
        visitedPositions.insert(visitKey);

        int nextX = pos.x();
        int nextY = pos.y() + step;
        if (nextY > maxY) {
            nextY = minY;
            nextX += step;
        }
        if (nextX > maxX) {
            nextX = minX;
            nextY = minY;
        }
        pos = QPoint(nextX, nextY);
    }
}

void ListView::reserveCreatedItemPosition(const QString& filePath, const QPoint& requestedPos)
{
    if (m_iconShowRule.autoArrange) {
        return;
    }

    const QString normalizedPath = normalizeDesktopPath(filePath);
    if (normalizedPath.isEmpty()) return;
    m_pendingCreatedItemPositions[normalizedPath] = resolveCreatePosition(requestedPos);
}

bool ListView::takeReservedCreatedItemPosition(const QString& filePath, QPoint& posOut)
{
    const QString normalizedPath = normalizeDesktopPath(filePath);
    auto it = m_pendingCreatedItemPositions.find(normalizedPath);
    if (it == m_pendingCreatedItemPositions.end()) {
        return false;
    }

    posOut = it.value();
    m_pendingCreatedItemPositions.erase(it);
    return true;
}

bool ListView::handlePendingShellCreatedItem(const QString& newPath)
{
    if (!m_pendingShellNewTargetBox) {
        return false;
    }

    QPointer<LinBox> targetBox = m_pendingShellNewTargetBox;
    clearPendingShellNewTargetBox();
    if (!targetBox) {
        return false;
    }

    if (targetBox->isFolderMapped()) {
        const QString mappedFolderPath = targetBox->mappedFolderPath();
        if (mappedFolderPath.isEmpty() || targetBox->mappedFolderState() != LinBox::MappedFolderState::Normal) {
            return false;
        }

        const QString destinationPath = ListViewFileOpsHelper::uniquePasteDestinationPath(newPath, mappedFolderPath);
        if (!ListViewFileOpsHelper::movePathToExactDestination(newPath, destinationPath)) {
            return false;
        }

        removeDesktopStateByPath(newPath);

        loadMappedFolderItemsForBox(targetBox);
        targetBox->refreshLayout();
        targetBox->raise();
        update();
        return true;
    }

    moveItemToBox(newPath, targetBox, false, -1);
    targetBox->refreshLayout();
    targetBox->raise();
    update();
    return true;
}

/// <summary>
/// 闁汇垻鍠愰崹姘辨喆閸曨偄鐏?
/// 1.unGrid婵☆垪鈧磭纭€
/// UI闁告帗绋戠紓鎻憈em,闁烩晛鐡ㄧ敮鎾箵閹邦亞杩斿褏濮甸悥锝夊矗閹惰姤鏆涢柛褎鍔栭悥?
/// 闂傚牊鎷稩闁告帗绋戠紓鎾绘晬鐏炵厧鈻忛柣鈶╁墵rid闁告帗绋戠紓鎾舵喆閸曨偄鐏?
/// 2.Grid婵☆垪鈧磭纭€
/// 闁哄被鍎撮妤呭冀閻撳海鎽嶉悽顖氬暙閻剙袙韫囧酣鍤嬮柛鏍ф惈閻撴瑩寮伴姘剨闁哄牆顦伴弸鍐╃鐠鸿櫣鎽犻柛锔荤厜缁辨繃淇婇崒娑氫函濞戞挸绉撮悺銊╁捶閵婏富姊剧紒渚垮灮閳规牠寮介悡搴ｆ憤闁挎稑濂旀繛鍥偨閵娧屽剳濞戞挴鍋撳☉鎿冧簼閻楀摜鈧稒鍔曞妤呭冀?
/// </summary>
/// <returns></returns>
QPoint ListView::generateItemPosition()
{
    qDebug() << "[generateItemPosition] gridAlignment=" << m_iconShowRule.gridAlignment 
             << "m_lastRightClickPos=" << m_lastRightClickPos;
    
    // 闂傚牊鍑痳id閻㈩垰鍟惇顒佺▔閺冨倹鏆燯I闁瑰灝绉崇紞鏃傛喆閿曗偓瑜板倿鎯冮崟鍓佺闁告瑦濞婇弫顓㈡嚕濠婂啫绀嬮柡鍌涙緲缂傛捇鏁嶆径娑氱闁烩晛鐡ㄧ敮瀛樻媴鐠恒劍鏆忓褏濮甸悥锝夊矗閹惰姤鏆涘ù锝呯Ф閻?
    if (!m_iconShowRule.gridAlignment && m_lastRightClickPos != QPoint(-1, -1)) {
        QPoint pos = m_lastRightClickPos;
        qDebug() << "[generateItemPosition] Using right-click position:" << pos;
        // 闂佹彃绉堕悿?m_lastRightClickPos闁挎稑鐭傚Σ璇差潰閵忋倖濮淯I闁告帗绋戠紓鎾绘儍閸曨剚鐎ù鐘虫构缁″啯鎷呯捄銊︽殢閻犲洢鍎卞妤呭冀?
        m_lastRightClickPos = QPoint(-1, -1);
        return pos;
    }
    
    qDebug() << "[generateItemPosition] Using grid layout position";

    const int strideX = m_iconWidth + m_colSpacing;
    const int itemHeight = desktopItemFootprintHeight();
    const int strideY = itemHeight + m_rowSpacing;
    const int layoutLeft = desktopLayoutLeft();
    const int layoutTop = desktopLayoutTop();
    const int maxRows = maxGridRowsForBounds(layoutTop, desktopUsableBottom(), itemHeight, strideY);
    const int maxCols = maxGridColsForBounds(layoutLeft, desktopUsableRight(), m_iconWidth, strideX);
    const int visibleCapacity = qMax(1, maxCols * maxRows);
    QSet<quint64> occupiedSlots;
    int desktopItemCount = 0;

    for (const auto& state : m_itemStates) {
        if (!state.model.boxId.isEmpty() || !state.model.visible) {
            continue;
        }
        if (state.model.position.x() > 0 || state.model.position.y() > 0) {
            ++desktopItemCount;
            const QPoint gridCell = pointToGridCell(state.model.position, layoutLeft, layoutTop, strideX, strideY);
            occupiedSlots.insert(packGridCell(qBound(0, gridCell.x(), maxCols - 1), qBound(0, gridCell.y(), maxRows - 1)));
        }
    }
    for (auto it = m_pendingCreatedItemPositions.constBegin(); it != m_pendingCreatedItemPositions.constEnd(); ++it) {
        ++desktopItemCount;
        const QPoint gridCell = pointToGridCell(it.value(), layoutLeft, layoutTop, strideX, strideY);
        occupiedSlots.insert(packGridCell(qBound(0, gridCell.x(), maxCols - 1), qBound(0, gridCell.y(), maxRows - 1)));
    }

    if (occupiedSlots.size() < visibleCapacity) {
        for (int gridX = 0; gridX < maxCols; ++gridX) {
            for (int gridY = 0; gridY < maxRows; ++gridY) {
                if (occupiedSlots.contains(packGridCell(gridX, gridY))) {
                    continue;
                }

                return gridCellToPoint(gridX, gridY, layoutLeft, layoutTop, strideX, strideY);
            }
        }
    }

    const QPoint overflowCell = gridIndexToCell(desktopItemCount % visibleCapacity, maxRows);
    return gridCellToPoint(overflowCell.x(), overflowCell.y(), layoutLeft, layoutTop, strideX, strideY);

    return QPoint(0, 0); // 闁稿繑绮岀花铏交閺傛寧绀€
}


void ListView::initRightMenuConnections() {
    RightMenu* menu = RightMenu::getInstance();

    // View sub-menu connections
    connect(menu, &RightMenu::LargeIconsTriggered, this, &ListView::onMenuLargeIcons);
    connect(menu, &RightMenu::MediumIconsTriggered, this, &ListView::onMenuMediumIcons);
    connect(menu, &RightMenu::SmallIconsTriggered, this, &ListView::onMenuSmallIcons);
    connect(menu, &RightMenu::AutoArrangeIconsTriggered, this, &ListView::onMenuAutoArrangeIcons);
    connect(menu, &RightMenu::AlignIconsToGridTriggered, this, &ListView::onMenuAlignIconsToGrid);
    connect(menu, &RightMenu::ShowDesktopIconsTriggered, this, &ListView::onMenuShowDesktopIcons);
    connect(menu, &RightMenu::DoubleClickedHideIconsTriggered, this, &ListView::onMenuDoubleClickHideIcons);

    // Sort sub-menu connections
    connect(menu, &RightMenu::sortByNameTriggered, this, &ListView::onMenuSortByName);
    connect(menu, &RightMenu::sortBySizeTriggered, this, &ListView::onMenuSortBySize);
    connect(menu, &RightMenu::sortByTypeTriggered, this, &ListView::onMenuSortByType);
    connect(menu, &RightMenu::sortByDateTriggered, this, &ListView::onMenuSortByDate);

    // Other actions
    connect(menu, &RightMenu::refreshTriggered, this, &ListView::onMenuRefresh);
    connect(menu, &RightMenu::pasteTriggered, this, &ListView::onMenuPaste);
    connect(menu, &RightMenu::pasteShortcutTriggered, this, &ListView::onMenuPasteShortcut);
    connect(menu, &RightMenu::createBoxTriggered, this, &ListView::onMenuCreateBox);
    connect(menu, &RightMenu::mapFolderTriggered, this, &ListView::onMenuMapFolder);
    connect(menu, &RightMenu::organizeDesktopTriggered, this, &ListView::onMenuOrganizeDesktopByNameSort);
    connect(menu, &RightMenu::linDeskTriggered, this, &ListView::onMenuLinDesk);
    connect(menu, &RightMenu::quitAppTriggered, this, &ListView::onMenuQuitApp);
    
    //init view sub-menu check states
    switch (m_iconShowRule.viewMode) {
    case ViewMode::LargeIcon:
        menu->setLargeIconsChecked(true);
        break;
    case ViewMode::MediumIcon:
        menu->setMediumIconsChecked(true);
        break;
    case ViewMode::SmallIcon:
        menu->setSmallIconsChecked(true);
        break;
    default:
        menu->setMediumIconsChecked(true);
    }
    
    if(m_iconShowRule.autoArrange) {
        menu->setAutoArrangeIconsChecked(true);
    } else {
        menu->setAutoArrangeIconsChecked(false);
    }
    if(m_iconShowRule.gridAlignment) {
        menu->setAlignIconsToGridChecked(true);
    } else {
        menu->setAlignIconsToGridChecked(false);
    }
    if(m_iconShowRule.showDesktopIcons) {
        menu->setShowDesktopIconsChecked(true);
    } else {
        menu->setShowDesktopIconsChecked(false);
    }
    if(m_iconShowRule.doubleClickHideIcons) {
        menu->setDoubleClickedHideIconsChecked(true);
    } else {
        menu->setDoubleClickedHideIconsChecked(false);
    }

    // init sort-by sub-menu check state
    const SortOrderType visibleSortType =
        (m_iconShowRule.autoArrange && m_iconShowRule.sortType == SortOrderType::SortByCustom)
            ? m_iconShowRule.sortTypeBeforeAutoArrange
            : m_iconShowRule.sortType;
    switch (visibleSortType) {
    case SortOrderType::SortByName_ASC:
    case SortOrderType::SortByName_DESC:
        menu->setSortByNameChecked(true);
        break;
    case SortOrderType::SortBySize_ASC:
    case SortOrderType::SortBySize_DESC:
        menu->setSortBySizeChecked(true);
        break;
    case SortOrderType::SortByType_ASC:
    case SortOrderType::SortByType_DESC:
        menu->setSortByTypeChecked(true);
        break;
    case SortOrderType::SortByDateModified_ASC:
    case SortOrderType::SortByDateModified_DESC:
        menu->setSortByDateChecked(true);
        break;
    default:
        menu->setSortByNameChecked(true); // 濮掓稒顭堥濠氭焻婢跺鍘柛姘Ф琚?
        break;
    }
}

void ListView::syncViewModeSizes() {
    m_marginTop = scalePx(kBaseMarginTop);
    m_marginLeft = scalePx(kBaseMarginLeft);
    m_bottomPadding = scalePx(kBaseBottomPadding);

    switch (m_iconShowRule.viewMode) {
        case ViewMode::LargeIcon:
            m_iconWidth  = scalePx(kLargeIconW);
            m_rowSpacing = scalePx(kLargeRowSpacing);
            m_colSpacing = scalePx(kLargeColSpacing);
            ListItemData::s_targetIconSize = m_iconWidth;
            m_iconHeight = iconCellHeightForTwoTextLines(m_iconWidth - scalePx(4));
            if (rightMenu) rightMenu->setLargeIconsChecked(true);
            break;
        case ViewMode::MediumIcon:
            m_iconWidth  = scalePx(kMediumIconW);
            m_iconHeight = iconCellHeightForTwoTextLines(m_iconWidth - scalePx(4));
            m_rowSpacing = scalePx(kMediumRowSpacing);
            m_colSpacing = scalePx(kMediumColSpacing);
            ListItemData::s_targetIconSize = m_iconWidth;
            if (rightMenu) rightMenu->setMediumIconsChecked(true);
            break;
        case ViewMode::SmallIcon:
            m_iconWidth = scalePx(kSmallIconW);
            m_iconHeight = iconCellHeightForTwoTextLines(m_iconWidth - scalePx(4));
            m_rowSpacing = scalePx(kSmallRowSpacing);
            m_colSpacing = scalePx(kSmallColSpacing);
            ListItemData::s_targetIconSize = m_iconWidth;
            if (rightMenu) rightMenu->setSmallIconsChecked(true);
            break;
    }
}

qreal ListView::currentDpiScale() const
{
    return DpiScaleHelper::uiScaleForWidget(this);
}

DesktopRenderParams ListView::currentRenderParams() const
{
    DesktopRenderParams params;
    params.iconWidth = m_iconWidth;
    params.iconHeight = m_iconHeight;
    params.largeIconW = scalePx(kLargeIconW);
    params.smallIconW = scalePx(kSmallIconW);
    params.iconTextGap = ListItem::kIconTextGap;
    params.dpiScale = currentDpiScale();
    params.labelFont = font();
    params.showShortcutArrow = ShortcutArrowManager::isShortcutArrowEnabled();
    return params;
}

DesktopItemInitContext ListView::currentDesktopItemInitContext() const
{
    DesktopItemInitContext context;
    context.sysIconRegisterInfo = sysIconRegisterInfo;
    context.systemFolderCLSIDs = SystemFolderCLSIDs;
    context.showHiddenFiles = m_showHiddenFiles;
    context.getBoxId = [this](const QString& path) {
        return getBoxIdForItem(path);
    };
    return context;
}

bool ListView::addDesktopModelItemFromPath(const QString& filePath)
{
    if (filePath.isEmpty() || findDesktopStateByPath(filePath)) {
        return false;
    }

    const ListItemData data = DesktopItemLoader().loadItem(filePath);
    DesktopRootItemState state;
    initDesktopRootItemModel(state.model, data, currentDesktopItemInitContext());
    state.model.layoutMode = DesktopLayoutMode::IconMode;
    state.model.baseSize = QSize(m_iconWidth, m_iconHeight);
    state.model.effectiveSize = state.model.baseSize;

    QPoint reservedPos;
    if (takeReservedCreatedItemPosition(filePath, reservedPos)) {
        state.model.position = reservedPos;
    } else if (state.model.visible && state.model.boxId.isEmpty() && !m_iconShowRule.autoArrange) {
        state.model.position = generateItemPosition();
    }

    m_itemStates.push_back(state);
    updateAllItemRenderData();

    if (m_iconShowRule.autoArrange) {
        applyAutoArrangeAfterDesktopMutation(false, false);
    } else {
        rebuildSpatialIndex();
        update();
    }
    return true;
}

bool ListView::refreshDesktopModelItemPath(const QString& oldPath, const QString& newPath)
{
    DesktopRootItemState* state = findDesktopStateByPath(oldPath);
    if (!state) {
        return false;
    }

    const QString oldNormalizedPath = normalizeDesktopItemPath(state->model.filePath);
    const ListItemData data = DesktopItemLoader().loadItem(newPath);

    DesktopRootItemModel updatedModel;
    initDesktopRootItemModel(updatedModel, data, currentDesktopItemInitContext());
    updatedModel.layoutMode = state->model.layoutMode;
    updatedModel.position = state->model.position;
    updatedModel.baseSize = QSize(m_iconWidth, m_iconHeight);
    updatedModel.effectiveSize = state->model.effectiveSize;
    updatedModel.visibleTextHeight = state->model.visibleTextHeight;
    updatedModel.isSelected = state->model.isSelected;
    updatedModel.isHovered = state->model.isHovered;
    updatedModel.focused = state->model.focused;
    updatedModel.isCutMarked = state->model.isCutMarked;
    updatedModel.isFullNameExpanded = state->model.isFullNameExpanded;
    updatedModel.customSortOrder = state->model.customSortOrder;
    updatedModel.expandedPreviewText = state->model.expandedPreviewText;
    updatedModel.isRenaming = state->model.isRenaming;
    updatedModel.isReady = true;

    state->model = updatedModel;

    const QString newNormalizedPath = normalizeDesktopItemPath(state->model.filePath);
    for (QString& selectedPath : m_selectedDesktopPaths) {
        if (selectedPath == oldNormalizedPath) {
            selectedPath = newNormalizedPath;
        }
    }
    if (m_focusedDesktopPath == oldNormalizedPath) {
        m_focusedDesktopPath = newNormalizedPath;
    }
    if (m_anchorDesktopPath == oldNormalizedPath) {
        m_anchorDesktopPath = newNormalizedPath;
    }
    if (m_lastClickedDesktopPath == oldNormalizedPath) {
        m_lastClickedDesktopPath = newNormalizedPath;
    }
    if (m_draggedDesktopPath == oldNormalizedPath) {
        m_draggedDesktopPath = newNormalizedPath;
    }
    if (m_hoveredDesktopPath == oldNormalizedPath) {
        m_hoveredDesktopPath = newNormalizedPath;
    }

    updateAllItemRenderData();
    if (m_iconShowRule.autoArrange) {
        applyAutoArrangeAfterDesktopMutation(false, false);
    } else {
        rebuildSpatialIndex();
        update();
    }
    return true;
}

DesktopRootItemState* ListView::findModelStateForLegacyItem(ListItem* item)
{
    Q_UNUSED(item);
    return nullptr;
}

const DesktopRootItemState* ListView::findModelStateForLegacyItem(ListItem* item) const
{
    Q_UNUSED(item);
    return nullptr;
}

DesktopRootItemState* ListView::findDesktopStateAt(const QPoint& pos)
{
    for (auto it = m_itemStates.rbegin(); it != m_itemStates.rend(); ++it) {
        if (shouldPaintDesktopModelState(*it) && it->model.renderData.itemRect.contains(pos)) {
            return &(*it);
        }
    }
    return nullptr;
}

const DesktopRootItemState* ListView::findDesktopStateAt(const QPoint& pos) const
{
    for (auto it = m_itemStates.crbegin(); it != m_itemStates.crend(); ++it) {
        if (shouldPaintDesktopModelState(*it) && it->model.renderData.itemRect.contains(pos)) {
            return &(*it);
        }
    }
    return nullptr;
}

QString ListView::normalizeDesktopItemPath(const QString& filePath) const
{
    if (filePath.isEmpty()) {
        return QString();
    }

    QString normalized = QDir::toNativeSeparators(filePath);
    while (normalized.endsWith('\\')) {
        normalized.chop(1);
    }
    return normalized;
}

DesktopRootItemState* ListView::findDesktopStateByPath(const QString& filePath)
{
    const QString normalizedPath = normalizeDesktopItemPath(filePath);
    if (normalizedPath.isEmpty()) {
        return nullptr;
    }

    for (auto& state : m_itemStates) {
        if (normalizeDesktopItemPath(state.model.filePath) == normalizedPath) {
            return &state;
        }
    }
    return nullptr;
}

const DesktopRootItemState* ListView::findDesktopStateByPath(const QString& filePath) const
{
    const QString normalizedPath = normalizeDesktopItemPath(filePath);
    if (normalizedPath.isEmpty()) {
        return nullptr;
    }

    for (const auto& state : m_itemStates) {
        if (normalizeDesktopItemPath(state.model.filePath) == normalizedPath) {
            return &state;
        }
    }
    return nullptr;
}

bool ListView::isDesktopPathSelected(const QString& filePath) const
{
    const QString normalizedPath = normalizeDesktopItemPath(filePath);
    return !normalizedPath.isEmpty() && m_selectedDesktopPaths.contains(normalizedPath);
}

QVector<ListItem*> ListView::selectedDesktopLegacyItems() const
{
    return {};
}

QVector<DesktopRootItemModel> ListView::selectedDesktopModels() const
{
    QVector<DesktopRootItemModel> models;
    models.reserve(m_selectedDesktopPaths.size());
    for (const QString& path : m_selectedDesktopPaths) {
        const DesktopRootItemState* state = findDesktopStateByPath(path);
        if (state) {
            models.push_back(state->model);
        }
    }
    return models;
}

QStringList ListView::selectedDesktopPaths() const
{
    QStringList paths;
    for (const QString& path : m_selectedDesktopPaths) {
        if (!path.isEmpty()) {
            paths.append(path);
        }
    }
    return paths;
}

QStringList ListView::selectedBoxPaths() const
{
    QStringList paths;
    for (const QString& path : m_selectedBoxPaths) {
        if (!path.isEmpty()) {
            paths.append(path);
        }
    }
    return paths;
}

QString ListView::effectiveFocusedDesktopPath() const
{
    return normalizeDesktopItemPath(m_focusedDesktopPath);
}

QString ListView::effectiveAnchorDesktopPath() const
{
    return normalizeDesktopItemPath(m_anchorDesktopPath);
}

QString ListView::effectiveFocusedBoxPath() const
{
    return normalizeDesktopItemPath(m_focusedBoxPath);
}

QString ListView::effectiveAnchorBoxPath() const
{
    return normalizeDesktopItemPath(m_anchorBoxPath);
}

QStringList ListView::selectableDesktopPaths() const
{
    QStringList paths;
    paths.reserve(m_itemStates.size());
    for (const auto& state : m_itemStates) {
        if (!shouldPaintDesktopModelState(state)) {
            continue;
        }
        const QString normalizedPath = normalizeDesktopItemPath(state.model.filePath);
        if (!normalizedPath.isEmpty()) {
            paths.append(normalizedPath);
        }
    }
    return paths;
}

QStringList ListView::selectableBoxPaths(LinBox* box) const
{
    QStringList paths;
    if (!box) {
        return paths;
    }
    const auto& states = box->itemStates();
    paths.reserve(states.size());
    for (const auto& state : states) {
        if (!state.model.visible) {
            continue;
        }
        const QString normalizedPath = normalizeDesktopItemPath(state.model.filePath);
        if (!normalizedPath.isEmpty()) {
            paths.append(normalizedPath);
        }
    }
    return paths;
}

QString ListView::findDirectionalDesktopFocusCandidate(const QString& focusedPath, FocusMoveDirection direction) const
{
    if (m_itemStates.isEmpty()) {
        return QString();
    }

    const DesktopRootItemState* focusedState = focusedPath.isEmpty()
        ? nullptr
        : findDesktopStateByPath(focusedPath);
    if (!focusedState) {
        const QStringList orderedPaths = selectableDesktopPaths();
        return orderedPaths.isEmpty() ? QString() : orderedPaths.first();
    }

    const QPoint currentCenter = focusedState->model.renderData.selectionContentRect.center();
    qint64 bestDistanceSquared = LLONG_MAX;
    int bestPrimaryDistance = INT_MAX;
    QString candidatePath;

    for (const auto& state : m_itemStates) {
        if (!shouldPaintDesktopModelState(state)) {
            continue;
        }

        const QString normalizedPath = normalizeDesktopItemPath(state.model.filePath);
        if (normalizedPath.isEmpty() || normalizedPath == normalizeDesktopItemPath(focusedPath)) {
            continue;
        }

        const QPoint candidateCenter = state.model.renderData.selectionContentRect.center();
        const int dx = candidateCenter.x() - currentCenter.x();
        const int dy = candidateCenter.y() - currentCenter.y();

        int primaryDistance = INT_MAX;
        switch (direction) {
        case FocusMoveDirection::Left:
            if (dx >= 0) continue;
            primaryDistance = -dx;
            break;
        case FocusMoveDirection::Right:
            if (dx <= 0) continue;
            primaryDistance = dx;
            break;
        case FocusMoveDirection::Up:
            if (dy >= 0) continue;
            primaryDistance = -dy;
            break;
        case FocusMoveDirection::Down:
            if (dy <= 0) continue;
            primaryDistance = dy;
            break;
        }

        const qint64 distanceSquared = static_cast<qint64>(dx) * dx + static_cast<qint64>(dy) * dy;
        if (distanceSquared < bestDistanceSquared ||
            (distanceSquared == bestDistanceSquared && primaryDistance < bestPrimaryDistance)) {
            bestDistanceSquared = distanceSquared;
            bestPrimaryDistance = primaryDistance;
            candidatePath = normalizedPath;
        }
    }

    return candidatePath;
}

QString ListView::findDirectionalBoxFocusCandidate(LinBox* box, const QString& focusedPath, FocusMoveDirection direction) const
{
    if (!box) {
        return QString();
    }

    const DesktopRootItemState* focusedState = focusedPath.isEmpty()
        ? nullptr
        : box->findItemStateByPath(focusedPath);
    if (!focusedState) {
        const QStringList orderedPaths = selectableBoxPaths(box);
        return orderedPaths.isEmpty() ? QString() : orderedPaths.first();
    }

    const QPoint currentCenter = focusedState->model.renderData.selectionContentRect.center();
    qint64 bestDistanceSquared = LLONG_MAX;
    int bestPrimaryDistance = INT_MAX;
    QString candidatePath;

    for (const auto& state : box->itemStates()) {
        if (!state.model.visible) {
            continue;
        }

        const QString normalizedPath = normalizeDesktopItemPath(state.model.filePath);
        if (normalizedPath.isEmpty() || normalizedPath == normalizeDesktopItemPath(focusedPath)) {
            continue;
        }

        const QPoint candidateCenter = state.model.renderData.selectionContentRect.center();
        const int dx = candidateCenter.x() - currentCenter.x();
        const int dy = candidateCenter.y() - currentCenter.y();

        int primaryDistance = INT_MAX;
        switch (direction) {
        case FocusMoveDirection::Left:
            if (dx >= 0) continue;
            primaryDistance = -dx;
            break;
        case FocusMoveDirection::Right:
            if (dx <= 0) continue;
            primaryDistance = dx;
            break;
        case FocusMoveDirection::Up:
            if (dy >= 0) continue;
            primaryDistance = -dy;
            break;
        case FocusMoveDirection::Down:
            if (dy <= 0) continue;
            primaryDistance = dy;
            break;
        }

        const qint64 distanceSquared = static_cast<qint64>(dx) * dx + static_cast<qint64>(dy) * dy;
        if (distanceSquared < bestDistanceSquared ||
            (distanceSquared == bestDistanceSquared && primaryDistance < bestPrimaryDistance)) {
            bestDistanceSquared = distanceSquared;
            bestPrimaryDistance = primaryDistance;
            candidatePath = normalizedPath;
        }
    }

    return candidatePath;
}

void ListView::setDesktopItemSelected(const QString& filePath, bool selected)
{
    DesktopRootItemState* state = findDesktopStateByPath(filePath);
    if (!state) {
        return;
    }

    const QString normalizedPath = normalizeDesktopItemPath(state->model.filePath);
    if (normalizedPath.isEmpty()) {
        return;
    }

    state->model.isSelected = selected;
    state->model.renderData.selected = selected;
    if (selected) {
        if (!m_selectedDesktopPaths.contains(normalizedPath)) {
            m_selectedDesktopPaths.push_back(normalizedPath);
        }
    } else {
        m_selectedDesktopPaths.removeOne(normalizedPath);
    }

    update(desktopRootItemHighlightDirtyRect(state->model.renderData));
}

void ListView::clearDesktopSelection()
{
    const QStringList previousSelection = selectedDesktopPaths();
    for (const QString& path : previousSelection) {
        setDesktopItemSelected(path, false);
    }
    m_selectedDesktopPaths.clear();
}

void ListView::applyDesktopSelection(const QStringList& filePaths, const QString& focusedPath, bool expandSingle)
{
    clearDesktopSelection();

    QStringList filteredPaths;
    for (const QString& path : filePaths) {
        const QString normalizedPath = normalizeDesktopItemPath(path);
        if (normalizedPath.isEmpty() || filteredPaths.contains(normalizedPath)) {
            continue;
        }
        if (!findDesktopStateByPath(normalizedPath)) {
            continue;
        }
        filteredPaths.append(normalizedPath);
        setDesktopItemSelected(normalizedPath, true);
    }

    m_focusedDesktopPath = filteredPaths.isEmpty()
        ? QString()
        : normalizeDesktopItemPath(focusedPath.isEmpty() ? filteredPaths.last() : focusedPath);
    if (m_focusedDesktopPath.isEmpty() && !filteredPaths.isEmpty()) {
        m_focusedDesktopPath = filteredPaths.last();
    }

    if (expandSingle && filteredPaths.size() == 1) {
        if (DesktopRootItemState* state = findDesktopStateByPath(filteredPaths.first())) {
            state->model.isFullNameExpanded = true;
        }
    }
}

void ListView::ensureModelStateForLegacyItem(ListItem* item)
{
    Q_UNUSED(item);
}

void ListView::removeModelStateForLegacyItem(ListItem* item)
{
    Q_UNUSED(item);
}

void ListView::removeDesktopStateByPath(const QString& filePath)
{
    const QString normalizedPath = normalizeDesktopItemPath(filePath);
    if (normalizedPath.isEmpty()) {
        return;
    }

    for (auto it = m_itemStates.begin(); it != m_itemStates.end(); ) {
        if (normalizeDesktopItemPath(it->model.filePath) == normalizedPath) {
            const QRect dirty = desktopRootItemHighlightDirtyRect(it->model.renderData);
            it = m_itemStates.erase(it);
            if (m_hoveredDesktopPath == normalizedPath) {
                m_hoveredDesktopPath.clear();
            }
            m_selectedDesktopPaths.removeOne(normalizedPath);
            if (m_focusedDesktopPath == normalizedPath) {
                m_focusedDesktopPath.clear();
            }
            if (m_anchorDesktopPath == normalizedPath) {
                m_anchorDesktopPath.clear();
            }
            if (m_lastClickedDesktopPath == normalizedPath) {
                m_lastClickedDesktopPath.clear();
            }
            if (m_draggedDesktopPath == normalizedPath) {
                m_draggedDesktopPath.clear();
            }
            update(dirty);
        } else {
            ++it;
        }
    }
}

void ListView::syncModelStateFromLegacyItem(DesktopRootItemState& state, const QSet<ListItem*>& visibleDesktopItems)
{
    Q_UNUSED(visibleDesktopItems);
    const QString normalizedPath = normalizeDesktopItemPath(state.model.filePath);
    state.model.focused =
        (!m_focusedDesktopPath.isEmpty() && normalizedPath == effectiveFocusedDesktopPath()) ||
        (!m_focusedBoxPath.isEmpty() && normalizedPath == effectiveFocusedBoxPath());
    state.model.isRenaming = RenameManager::instance()->isActivePath(state.model.filePath);
}

bool ListView::shouldPaintDesktopModelState(const DesktopRootItemState& state) const
{
    if (!m_iconShowRule.showDesktopIcons) {
        return false;
    }
    if (!state.model.isReady) {
        return false;
    }
    if (!state.model.visible) {
        return false;
    }
    if (!state.model.boxId.isEmpty()) {
        return false;
    }
    return true;
}

ListItem* ListView::findDesktopItemAt(const QPoint& pos) const
{
    Q_UNUSED(pos);
    return nullptr;
}

QRect ListView::desktopSelectionRectForItem(ListItem* item) const
{
    if (!item) {
        return QRect();
    }
    return item->getChooseItemRect();
}

void ListView::updateHoveredDesktopItem(const QPoint& pos)
{
    DesktopRootItemState* nextHoveredState = findDesktopStateAt(pos);
    const QString nextHoveredPath = nextHoveredState
        ? normalizeDesktopItemPath(nextHoveredState->model.filePath)
        : QString();
    if (m_hoveredDesktopPath == nextHoveredPath) {
        return;
    }

    auto updateHoverState = [this](const QString& filePath, bool hovered) {
        DesktopRootItemState* state = findDesktopStateByPath(filePath);
        if (!state) {
            return;
        }

        const QRect dirty = desktopRootItemHighlightDirtyRect(state->model.renderData);
        state->model.isHovered = hovered;
        state->model.renderData.hovered = hovered && !state->model.isSelected;
        update(dirty);
    };

    updateHoverState(m_hoveredDesktopPath, false);
    m_hoveredDesktopPath = nextHoveredPath;
    m_hoveredItem = nullptr;
    updateHoverState(m_hoveredDesktopPath, true);
}

void ListView::clearHoveredDesktopItem()
{
    if (m_hoveredDesktopPath.isEmpty() && !m_hoveredItem) {
        return;
    }

    updateHoveredDesktopItem(QPoint(-1, -1));
    m_hoveredDesktopPath.clear();
    m_hoveredItem = nullptr;
}

void ListView::updateAllItemRenderData()
{
    DesktopRenderParams params = currentRenderParams();
    for (auto& state : m_itemStates) {
        syncModelStateFromLegacyItem(state, {});
        state.model.renderData = buildRenderDataFromModel(state.model, params);
    }
}

int ListView::scalePx(int logicalPx) const
{
    return DpiScaleHelper::scalePx(this, logicalPx);
}

QPoint ListView::remapPositionForViewMetrics(const QPoint& pos,
                                             int oldIconWidth,
                                             int oldIconHeight,
                                             int oldRowSpacing,
                                             int oldColSpacing,
                                             int oldMarginTop,
                                             int oldMarginLeft) const
{
    if (pos == QPoint(0, 0)) {
        return pos;
    }

    const int oldStrideX = qMax(1, oldIconWidth + oldColSpacing);
    const int oldStrideY = qMax(1, oldIconHeight + oldRowSpacing);
    const int newStrideX = qMax(1, m_iconWidth + m_colSpacing);
    const int newStrideY = qMax(1, desktopItemFootprintHeight() + m_rowSpacing);

    QPoint mappedPos = pos;
    if (m_iconShowRule.gridAlignment) {
        const QPoint gridCell = pointToGridCell(pos, oldMarginLeft, oldMarginTop, oldStrideX, oldStrideY);
        mappedPos = gridCellToPoint(gridCell.x(), gridCell.y(), desktopLayoutLeft(), desktopLayoutTop(), newStrideX, newStrideY);
    } else {
        const double logicalX = static_cast<double>(pos.x() - oldMarginLeft) / oldStrideX;
        const double logicalY = static_cast<double>(pos.y() - oldMarginTop) / oldStrideY;
        mappedPos.setX(desktopLayoutLeft() + qRound(logicalX * newStrideX));
        mappedPos.setY(desktopLayoutTop() + qRound(logicalY * newStrideY));
    }

    mappedPos.setX(qMax(desktopLayoutLeft(), qMin(mappedPos.x(), desktopUsableRight() - m_iconWidth + 1)));
    mappedPos.setY(qMax(desktopLayoutTop(), qMin(mappedPos.y(), desktopUsableBottom() - desktopItemFootprintHeight() + 1)));
    return mappedPos;
}

void ListView::applyViewModeMetricsChange(int oldIconWidth,
                                          int oldIconHeight,
                                          int oldRowSpacing,
                                          int oldColSpacing,
                                          int oldMarginTop,
                                          int oldMarginLeft)
{
    if (oldIconWidth == m_iconWidth &&
        oldIconHeight == m_iconHeight &&
        oldRowSpacing == m_rowSpacing &&
        oldColSpacing == m_colSpacing &&
        oldMarginTop == m_marginTop &&
        oldMarginLeft == m_marginLeft) {
        return;
    }

    for (ListItem* item : m_allItems) {
        if (!item || !item->boxId().isEmpty()) {
            continue;
        }

        const QPoint currentPos = item->getLocationInfo();
        if (currentPos == QPoint(0, 0)) {
            continue;
        }

        item->updateLocationInfo(remapPositionForViewMetrics(
            currentPos,
            oldIconWidth,
            oldIconHeight,
            oldRowSpacing,
            oldColSpacing,
            oldMarginTop,
            oldMarginLeft));
    }

    for (auto it = m_savedIconPositions.begin(); it != m_savedIconPositions.end(); ++it) {
        if (!it.value().boxId.isEmpty()) {
            continue;
        }

        const QPoint currentPos(it.value().x, it.value().y);
        const QPoint mappedPos = remapPositionForViewMetrics(
            currentPos,
            oldIconWidth,
            oldIconHeight,
            oldRowSpacing,
            oldColSpacing,
            oldMarginTop,
            oldMarginLeft);
        it.value().x = mappedPos.x();
        it.value().y = mappedPos.y();
    }

    for (auto it = m_pendingCreatedItemPositions.begin(); it != m_pendingCreatedItemPositions.end(); ++it) {
        it.value() = remapPositionForViewMetrics(
            it.value(),
            oldIconWidth,
            oldIconHeight,
            oldRowSpacing,
            oldColSpacing,
            oldMarginTop,
            oldMarginLeft);
    }
    updateAllItemRenderData();
}

void ListView::refreshForDpiChange()
{
    const int oldIconWidth = m_iconWidth;
    const int oldIconHeight = m_iconHeight;
    const int oldRowSpacing = m_rowSpacing;
    const int oldColSpacing = m_colSpacing;
    const int oldMarginTop = m_marginTop;
    const int oldMarginLeft = m_marginLeft;

    syncViewModeSizes();
    IconCacheManager::instance().clear();
    updateAllItemRenderData();


    if (m_iconShowRule.autoArrange) {
        applyAutoArrangeLayout(false, false);
    } else {
        applyViewModeMetricsChange(
            oldIconWidth,
            oldIconHeight,
            oldRowSpacing,
            oldColSpacing,
            oldMarginTop,
            oldMarginLeft);
        loadIconLayer();
    }

    for (const QPointer<LinBox>& box : BoxManager::instance()->allBoxes()) {
        if (box) {
            box->refreshLayout();
            box->update();
        }
    }

    update();
}

void ListView::setDebugGridSpacing(int rowSpacing, int colSpacing)
{
    m_rowSpacing = rowSpacing;
    m_colSpacing = colSpacing;
    loadIconLayer();
    update();
}

void ListView::onMenuLargeIcons() {
    qDebug() << "ListView: Large Icons triggered";
    if (m_iconShowRule.viewMode == ViewMode::LargeIcon) return;
    const int oldIconWidth = m_iconWidth;
    const int oldIconHeight = m_iconHeight;
    const int oldRowSpacing = m_rowSpacing;
    const int oldColSpacing = m_colSpacing;
    const int oldMarginTop = m_marginTop;
    const int oldMarginLeft = m_marginLeft;
    m_iconShowRule.viewMode = ViewMode::LargeIcon;
    syncViewModeSizes();
    if (m_iconShowRule.autoArrange) {
        applyAutoArrangeLayout(false, false);
    } else {
        applyViewModeMetricsChange(oldIconWidth, oldIconHeight, oldRowSpacing, oldColSpacing, oldMarginTop, oldMarginLeft);
        loadIconLayer();
    }
    update();
    requestSaveBoxes();
}

void ListView::onMenuMediumIcons() {
    qDebug() << "ListView: Medium Icons triggered";
    if (m_iconShowRule.viewMode == ViewMode::MediumIcon) return;
    const int oldIconWidth = m_iconWidth;
    const int oldIconHeight = m_iconHeight;
    const int oldRowSpacing = m_rowSpacing;
    const int oldColSpacing = m_colSpacing;
    const int oldMarginTop = m_marginTop;
    const int oldMarginLeft = m_marginLeft;
    m_iconShowRule.viewMode = ViewMode::MediumIcon;
    syncViewModeSizes();
    if (m_iconShowRule.autoArrange) {
        applyAutoArrangeLayout(false, false);
    } else {
        applyViewModeMetricsChange(oldIconWidth, oldIconHeight, oldRowSpacing, oldColSpacing, oldMarginTop, oldMarginLeft);
        loadIconLayer();
    }
    update();
    requestSaveBoxes();
}

void ListView::onMenuSmallIcons() {
    qDebug() << "ListView: Small Icons triggered";
    if (m_iconShowRule.viewMode == ViewMode::SmallIcon) return;
    const int oldIconWidth = m_iconWidth;
    const int oldIconHeight = m_iconHeight;
    const int oldRowSpacing = m_rowSpacing;
    const int oldColSpacing = m_colSpacing;
    const int oldMarginTop = m_marginTop;
    const int oldMarginLeft = m_marginLeft;
    m_iconShowRule.viewMode = ViewMode::SmallIcon;
    syncViewModeSizes();
    if (m_iconShowRule.autoArrange) {
        applyAutoArrangeLayout(false, false);
    } else {
        applyViewModeMetricsChange(oldIconWidth, oldIconHeight, oldRowSpacing, oldColSpacing, oldMarginTop, oldMarginLeft);
        loadIconLayer();
    }
    update();
    requestSaveBoxes();
}

void ListView::onMenuAutoArrangeIcons() {
    qDebug() << "ListView: Auto Arrange Icons triggered";
    const bool enableAutoArrange = rightMenu->isAutoArrangeIconsChecked();
    m_iconShowRule.autoArrange = enableAutoArrange;

    if (enableAutoArrange) {
        if (m_iconShowRule.sortType != SortOrderType::SortByCustom) {
            m_iconShowRule.sortTypeBeforeAutoArrange = m_iconShowRule.sortType;
            QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
            sortAutoArrangeStates(states, m_iconShowRule.sortType);
            assignCustomSortOrders(states);
        }
        m_iconShowRule.sortType = SortOrderType::SortByCustom;
        m_iconShowRule.gridAlignment = true;
        m_iconShowRule.iconPosMode = IconPosMode::AlignIconToGrid;
        updateRightMenuState();
        applyAutoArrangeLayout(true, true);
    } else {
        m_iconShowRule.sortType = m_iconShowRule.sortTypeBeforeAutoArrange;
        m_iconShowRule.gridAlignment = rightMenu->isAlignIconsToGridChecked();
        m_iconShowRule.iconPosMode = m_iconShowRule.gridAlignment
            ? IconPosMode::AlignIconToGrid
            : IconPosMode::UnAlignIconToGrid;
        clearDesktopCustomSortOrders();
        updateRightMenuState();
        update();
        requestSaveBoxes();
    }
}

void ListView::onMenuAlignIconsToGrid() {
    qDebug() << "ListView: Align Icons to Grid triggered";
    if (m_iconShowRule.autoArrange) {
        m_iconShowRule.gridAlignment = true;
        m_iconShowRule.iconPosMode = IconPosMode::AlignIconToGrid;
        updateRightMenuState();
        applyAutoArrangeLayout(true, true);
        return;
    }

    m_iconShowRule.gridAlignment = rightMenu->isAlignIconsToGridChecked();
    m_iconShowRule.iconPosMode = m_iconShowRule.gridAlignment
        ? IconPosMode::AlignIconToGrid
        : IconPosMode::UnAlignIconToGrid;
    
    if (m_iconShowRule.gridAlignment) {
        loadGridLayout();
    }
    update();
    requestSaveBoxes();
}

void ListView::onMenuShowDesktopIcons() {
    qDebug() << "ListView: Show Desktop Icons triggered";
    if(m_iconShowRule.showDesktopIcons) {
        m_iconShowRule.showDesktopIcons = false;
        this->rightMenu->setShowDesktopIconsChecked(false);
        for (auto item : m_items) {
            item->hide();
        }
        for (const QPointer<LinBox>& box : BoxManager::instance()->allBoxes()) {
            if (box) {
                box->hide();
            }
        }
    } else {
        m_iconShowRule.showDesktopIcons = true;
        this->rightMenu->setShowDesktopIconsChecked(true);
        for (auto* item : m_items) {
            if (item) {
                item->hide(); // Force hidden to suppress legacy rendering
            }
        }
        for (const QPointer<LinBox>& box : BoxManager::instance()->allBoxes()) {
            if (box) {
                box->show();
            }
        }
        BoxManager::instance()->raiseAllBoxes();
    }
    requestSaveBoxes();
}

void ListView::onMenuDoubleClickHideIcons() {
    qDebug() << "ListView: Double Click To Hide Icons triggered";
    m_iconShowRule.doubleClickHideIcons = !m_iconShowRule.doubleClickHideIcons;
    this->rightMenu->setDoubleClickedHideIconsChecked(m_iconShowRule.doubleClickHideIcons);
    requestSaveBoxes();
}

void ListView::setDoubleClickHideIconsEnabled(bool enabled) {
    m_iconShowRule.doubleClickHideIcons = enabled;
    requestSaveBoxes();
}

bool ListView::isDoubleClickHideIconsEnabled() const {
    return m_iconShowRule.doubleClickHideIcons;
}

void ListView::onMenuLinDesk() {
    showSettings(nullptr);
}

void ListView::showSettings(LinBox* target) {
    if (m_settingsWindow) {
        m_settingsWindow->setTargetBox(target);
        m_settingsWindow->show();
        m_settingsWindow->raise();
        m_settingsWindow->activateWindow();
        return;
    }
    QVector<LinBox*> all = boxes();
    m_settingsWindow = new SettingUI(target, all);
    m_settingsWindow->show();
}

void ListView::onMenuSortByName() {
    qDebug() << "ListView: Sort By Name triggered";
    if (m_iconShowRule.autoArrange) {
        m_iconShowRule.sortTypeBeforeAutoArrange = SortOrderType::SortByName_ASC;
        QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
        sortAutoArrangeStates(states, SortOrderType::SortByName_ASC);
        assignCustomSortOrders(states);
        m_iconShowRule.sortType = SortOrderType::SortByCustom;
        updateRightMenuState();
        applyAutoArrangeLayout(false, false);
        update();
        requestSaveBoxes();
        return;
    }

    this->sortItems(SortOrderType::SortByName_ASC);
    updateRightMenuState();
    if (!m_iconShowRule.autoArrange) {
        applyAutoArrangeLayout(false, false);
    }
    update();
    requestSaveBoxes();
}

void ListView::onMenuSortBySize() {
    qDebug() << "ListView: Sort By Size triggered";
    if (m_iconShowRule.autoArrange) {
        m_iconShowRule.sortTypeBeforeAutoArrange = SortOrderType::SortBySize_ASC;
        QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
        sortAutoArrangeStates(states, SortOrderType::SortBySize_ASC);
        assignCustomSortOrders(states);
        m_iconShowRule.sortType = SortOrderType::SortByCustom;
        updateRightMenuState();
        applyAutoArrangeLayout(false, false);
        update();
        requestSaveBoxes();
        return;
    }

    this->sortItems(SortOrderType::SortBySize_ASC);
    updateRightMenuState();
    if (!m_iconShowRule.autoArrange) {
        applyAutoArrangeLayout(false, false);
    }
    update();
    requestSaveBoxes();
}

void ListView::onMenuSortByType() {
    qDebug() << "ListView: Sort By Type triggered";
    if (m_iconShowRule.autoArrange) {
        m_iconShowRule.sortTypeBeforeAutoArrange = SortOrderType::SortByType_ASC;
        QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
        sortAutoArrangeStates(states, SortOrderType::SortByType_ASC);
        assignCustomSortOrders(states);
        m_iconShowRule.sortType = SortOrderType::SortByCustom;
        updateRightMenuState();
        applyAutoArrangeLayout(false, false);
        update();
        requestSaveBoxes();
        return;
    }

    this->sortItems(SortOrderType::SortByType_ASC);
    updateRightMenuState();
    if (!m_iconShowRule.autoArrange) {
        applyAutoArrangeLayout(false, false);
    }
    update();
    requestSaveBoxes();
}

void ListView::onMenuSortByDate() {
    qDebug() << "ListView: Sort By Date triggered";
    if (m_iconShowRule.autoArrange) {
        m_iconShowRule.sortTypeBeforeAutoArrange = SortOrderType::SortByDateModified_ASC;
        QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
        sortAutoArrangeStates(states, SortOrderType::SortByDateModified_ASC);
        assignCustomSortOrders(states);
        m_iconShowRule.sortType = SortOrderType::SortByCustom;
        updateRightMenuState();
        applyAutoArrangeLayout(false, false);
        update();
        requestSaveBoxes();
        return;
    }

    this->sortItems(SortOrderType::SortByDateModified_ASC);
    updateRightMenuState();
    if (!m_iconShowRule.autoArrange) {
        applyAutoArrangeLayout(false, false);
    }
    update();
    requestSaveBoxes();
}

void ListView::onMenuRefresh() {
    // 1. Start Fake Animation: Hide the entire desktop layer
    this->hide();

    // 2. Delay the actual logical refresh
    QTimer::singleShot(80, this, [this]() {
        this->show();
        this->raise();
        this->update();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

        QTimer::singleShot(0, this, [this]() {
            const QVector<QPointer<LinBox>> allBoxes = BoxManager::instance()->allBoxes();
            this->setUpdatesEnabled(false);
            for (const auto& box : allBoxes) {
                if (box) {
                    box->setUpdatesEnabled(false);
                }
            }

            // --- Original Refresh Logic ---
            if (m_enablePersistence) {
                for (auto* item : m_allItems) {
                    if (!item || item->filePath().isEmpty()) continue;
                    QString normPath = QDir::toNativeSeparators(item->filePath());
                    while (normPath.endsWith('\\')) normPath.chop(1);
                    if (item->pos() != QPoint(0, 0)) {
                        m_savedIconPositions[normPath] = { item->x(), item->y(), item->boxId() };
                    }
                }
            }

            qDebug() << "Comprehensive State Sync (Mirroring Initialize)...";
            
            if (monitor) {
                monitor->refreshIconsState();
                sysIconRegisterInfo = monitor->getCurrentIcons();
            }

            QVector<ListItem*> desktopVisible;
            m_hiddenIcons.clear(); 

            for (auto* item : m_allItems) {
                if (!item || item->filePath().isEmpty()) continue;
                
                // Refresh icon for each item
                item->refreshIcon();
                
                QString boxId = getBoxIdForItem(item->filePath());
                item->setBoxId(boxId);

                bool isVisibleSpecial = false;
                bool isSpecial = (item->getItemData().type() == ObjectType::SpecialFolder ||
                                  SystemFolderCLSIDs.contains(item->getItemData().getCLSID()) ||
                                  item->getItemData().fullName() == "desktop.ini" ||
                                  item->filePath().startsWith("::"));
                
                const bool isOsHidden = fileHasHiddenAttribute(item->filePath());
                const bool isDiskLetter = item->type() == ObjectType::DiskLetter;
                const bool isAppHiddenShellFolder = DesktopItemLoader::shouldTreatAsHiddenShellFolder(item->getItemData());
                applyWindowsHiddenVisualState(item);

                if (isSpecial) {
                    QString itemClsid = item->getItemData().getCLSID();
                    itemClsid.replace("::", "").replace("{", "").replace("}", "");
                    bool systemSaysVisible = false;
                    bool foundInRegistry = false;

                    for (const auto& info : sysIconRegisterInfo) {
                        QString sysClsidStr = info.clsid;
                        sysClsidStr.replace("{", "").replace("}", "");
                        if (sysClsidStr.toLower() == itemClsid.toLower()) {
                            foundInRegistry = true;
                            if (!info.isHidden) {
                                systemSaysVisible = true;
                            }
                            break;
                        }
                    }
                    isVisibleSpecial = systemSaysVisible;
                }

                if (isDiskLetter) {
                    item->hide();
                    m_hiddenIcons.push_back(item);
                } else if (isAppHiddenShellFolder) {
                    item->hide();
                    m_hiddenIcons.push_back(item);
                } else if (!boxId.isEmpty()) {
                    if (item->parent() == this) item->hide();
                    m_hiddenIcons.push_back(item);
                } else if (isOsHidden && !isSpecial && !m_showHiddenFiles) {
                    item->hide();
                    m_hiddenIcons.push_back(item);
                } else if (isSpecial) {
                    if (isVisibleSpecial) {
                        desktopVisible.push_back(item);
                        if (m_iconShowRule.showDesktopIcons) item->show();
                    } else {
                        item->hide();
                        m_hiddenIcons.push_back(item);
                    }
                } else {
                    desktopVisible.push_back(item);
                    if (m_iconShowRule.showDesktopIcons) item->show();
                }
            }

            m_items = desktopVisible;
            applyAutoArrangeAfterDesktopMutation(false, false);

            // Refresh icons for items in boxes and ensure boxes are re-shown
            for (const auto& box : allBoxes) {
                if (box) {
                    box->refreshLayout();
                    box->show();
                    box->raise();
                }
            }

            for (const auto& box : allBoxes) {
                if (box) {
                    box->setUpdatesEnabled(true);
                    box->update();
                }
            }
            this->setUpdatesEnabled(true);
            this->update();
        });
    });
}

void ListView::onMenuPaste() {
    const QStringList filePaths = ListViewFileOpsHelper::clipboardLocalFilePaths();
    if (filePaths.isEmpty()) {
        qDebug() << "ListView: Paste skipped, clipboard has no local files";
        return;
    }

    const bool isCutOperation = ListViewFileOpsHelper::clipboardContainsCutItems();
    bool ok = true;

    if (isCutOperation) {
        std::vector<std::wstring> srcPaths;
        srcPaths.reserve(filePaths.size());
        for (const QString& filePath : filePaths) {
            srcPaths.push_back(QDir::toNativeSeparators(filePath).toStdWString());
        }

        const std::wstring desktopDir = QDir::toNativeSeparators(m_desktopDir).toStdWString();
        ok = CSystemApi::MoveFiles(srcPaths, desktopDir);
    } else {
        ok = ListViewFileOpsHelper::copyFilesToDirectoryWithRule(filePaths, m_desktopDir);
    }

    qDebug() << "ListView: Paste triggered" << (isCutOperation ? "[move]" : "[copy]") << "result:" << ok;
    if (ok) {
        if (isCutOperation) {
            clearCutMarks();
            QGuiApplication::clipboard()->clear();
        }
    }
}

void ListView::onMenuPasteShortcut() {
    const QStringList filePaths = ListViewFileOpsHelper::clipboardLocalFilePaths();
    if (filePaths.isEmpty()) {
        qDebug() << "ListView: Paste Shortcut skipped, clipboard has no local files";
        return;
    }

    int createdCount = 0;
    for (const QString& filePath : filePaths) {
        const QString shortcutPath = ListViewFileOpsHelper::uniqueShortcutPathForTarget(filePath, m_desktopDir);
        if (ListViewFileOpsHelper::createShellShortcut(filePath, shortcutPath)) {
            ++createdCount;
        }
    }

    qDebug() << "ListView: Paste Shortcut triggered, created:" << createdCount << "of" << filePaths.size();
}

void ListView::onFileSystemChanged(const FileSystemEventInfo& eventInfo)
{
    // 閻熸瑥瀚€垫牠宕犻弽顒傜唴鐎垫澘瀚哥槐娆戠磼閻斿墎顏遍柣顫妼瀵粙寮鍕祮闁挎稑鑻獮鎾诲箳婢跺姹為悘蹇曞亾閺嬧晠寮堕悪鍛
    auto normalizePath = [](const QString& p) -> QString {
        QString s = QDir::toNativeSeparators(p);
        while (s.endsWith('\\') || s.endsWith('/'))
            s.chop(1);
        return s;
    };

    const QString newPath = normalizePath(eventInfo.newPath);
    const QString oldPath = normalizePath(eventInfo.oldPath);

    qDebug() << "[onFileSystemChanged]"
             << "type:" << (int)eventInfo.eventType
             << "isDir:" << eventInfo.isDirectory
             << "old:" << oldPath
             << "new:" << newPath;

    // 闂佹彃绉甸弻濠勬暜閸愩劎婀版鐐舵硾閸╂盯寮?
    auto relayout = [this]() {
        if (m_iconShowRule.autoArrange) {
            applyAutoArrangeLayout(false, false);
        } else {
            loadIconLayer();
            update();
            repaint();
        }
    };

    // 闁圭顦紞瀣礈瀹ュ棗绗撻幖鏉戠箺椤宕氬▎鎾虫闁?
    auto resort = [this]() {
        this->performSort(m_iconShowRule.sortType);
    };

    switch (eventInfo.eventType) {
    case FileSystemEvent::FileCreated:
    case FileSystemEvent::DirectoryCreated:
    {
        if (newPath.isEmpty()) break;
        if (findAnyItemStateByPath(newPath)) break;
        
        qDebug() << "[onFileSystemChanged] Creating item:" << newPath;
        QPointer<LinBox> pendingTargetBox = m_pendingCreatedItemBoxes.value(newPath);
        if (pendingTargetBox.isNull()) {
            pendingTargetBox = m_pendingShellNewTargetBox;
        }
        addDesktopModelItemFromPath(newPath);

        auto boxIt = m_pendingCreatedItemBoxes.find(newPath);
        if (boxIt != m_pendingCreatedItemBoxes.end()) {
            QPointer<LinBox> targetBox = boxIt.value();
            m_pendingCreatedItemBoxes.erase(boxIt);
            const bool hasPendingInsertIndex = m_pendingCreatedItemInsertIndices.contains(newPath);
            const int insertIndex = hasPendingInsertIndex ? m_pendingCreatedItemInsertIndices.take(newPath) : -1;
            if (targetBox) {
                moveItemToBox(newPath, targetBox, false, insertIndex);
                targetBox->refreshLayout();
                targetBox->raise();
                if (m_iconShowRule.autoArrange) {
                    applyAutoArrangeAfterDesktopMutation(false, false);
                } else {
                    update();
                }
            }
        }
        else {
            handlePendingShellCreatedItem(newPath);
        }
        
        // 濞ｅ洦绻傞悺銊╁箰娴ｉ鐣介柛鏍ㄧ墬閺嬪啯绂掔拋鍦閻犱焦婢樼紞宥夊棘閻楀牊鐎ù鐘虫构缂嶅懐绱?
        requestSaveBoxes();
        break;
    }
    case FileSystemEvent::FileDeleted:
    case FileSystemEvent::DirectoryDeleted:
    {
        const QString& deletePath = !oldPath.isEmpty() ? oldPath : newPath;
        if (deletePath.isEmpty()) break;
        if (consumePendingExternalRemoval(deletePath)) break;
        
        ListItem* item = nullptr;
        if (item) {
            removeItem(item);
            
            // 濞ｅ洦绻傞悺銊╁箰娴ｉ鐣介柛鏍ㄧ墬閺嬪啯绂掔拋鍦缂佸顭峰▍搴☆啅閹绘帒鐏╅梻鍕╁€栭弸鍐╃閹偊鍞剁憸?
            requestSaveBoxes();
        } else if (findDesktopStateByPath(deletePath)) {
            removeDesktopStateByPath(deletePath);
            requestSaveBoxes();
        }
        break;
    }
    case FileSystemEvent::FileRenamed:
    case FileSystemEvent::DirectoryRenamed:
    {
        if (oldPath.isEmpty() || newPath.isEmpty()) break;
        if (consumePendingExternalRemoval(oldPath)) break;

        QPointer<LinBox> pendingTargetBox = m_pendingCreatedItemBoxes.value(newPath);
        if (pendingTargetBox.isNull()) {
            pendingTargetBox = m_pendingShellNewTargetBox;
        }
        if (findDesktopStateByPath(oldPath)) {
            const QString oldDir = QFileInfo(oldPath).absolutePath();
            const QString newDir = QFileInfo(newPath).absolutePath();

            if (oldDir == newDir) {
                if (refreshDesktopModelItemPath(oldPath, newPath)) {
                    const DesktopRootItemState* updatedState = findDesktopStateByPath(newPath);
                    if (updatedState && !updatedState->model.visible) {
                        refreshVisibilityForWindowsConfig();
                        requestSaveBoxes();
                        break;
                    }
                    resort();
                    if (!m_iconShowRule.autoArrange) {
                        relayout();
                    }
                }
            } else {
                removeDesktopStateByPath(oldPath);
            }
        } else if (LinBox* sourceBox = BoxManager::instance()->findBoxContainingItem(oldPath)) {
            const QString oldDir = QFileInfo(oldPath).absolutePath();
            const QString newDir = QFileInfo(newPath).absolutePath();

            if (oldDir == newDir) {
                if (DesktopRootItemState* state = sourceBox->findItemStateByPath(oldPath)) {
                    const QString oldNormalizedPath = normalizeDesktopItemPath(state->model.filePath);
                    const ListItemData data = DesktopItemLoader().loadItem(newPath);
                    DesktopRootItemModel updatedModel;
                    initDesktopRootItemModel(updatedModel, data, currentDesktopItemInitContext());
                    updatedModel.layoutMode = state->model.layoutMode;
                    updatedModel.position = state->model.position;
                    updatedModel.baseSize = state->model.baseSize;
                    updatedModel.effectiveSize = state->model.effectiveSize;
                    updatedModel.visibleTextHeight = state->model.visibleTextHeight;
                    updatedModel.isSelected = state->model.isSelected;
                    updatedModel.isHovered = state->model.isHovered;
                    updatedModel.focused = state->model.focused;
                    updatedModel.isCutMarked = state->model.isCutMarked;
                    updatedModel.isFullNameExpanded = state->model.isFullNameExpanded;
                    updatedModel.customSortOrder = state->model.customSortOrder;
                    updatedModel.expandedPreviewText = state->model.expandedPreviewText;
                    updatedModel.isRenaming = state->model.isRenaming;
                    updatedModel.isReady = true;
                    state->model = updatedModel;

                    const QString newNormalizedPath = normalizeDesktopItemPath(state->model.filePath);
                    for (QString& selectedPath : m_selectedBoxPaths) {
                        if (selectedPath == oldNormalizedPath) {
                            selectedPath = newNormalizedPath;
                        }
                    }
                    if (m_focusedBoxPath == oldNormalizedPath) {
                        m_focusedBoxPath = newNormalizedPath;
                    }
                    if (m_anchorBoxPath == oldNormalizedPath) {
                        m_anchorBoxPath = newNormalizedPath;
                    }
                    sourceBox->refreshLayout();
                    sourceBox->update();
                }
            } else {
                sourceBox->removeItemState(oldPath, true);
            }
        } else if (!findAnyItemStateByPath(newPath)) {
            addDesktopModelItemFromPath(newPath);
        }

        auto boxIt = m_pendingCreatedItemBoxes.find(newPath);
        if (boxIt != m_pendingCreatedItemBoxes.end()) {
            QPointer<LinBox> targetBox = boxIt.value();
            m_pendingCreatedItemBoxes.erase(boxIt);
            const bool hasPendingInsertIndex = m_pendingCreatedItemInsertIndices.contains(newPath);
            const int insertIndex = hasPendingInsertIndex ? m_pendingCreatedItemInsertIndices.take(newPath) : -1;
            if (targetBox) {
                moveItemToBox(newPath, targetBox, false, insertIndex);
                targetBox->refreshLayout();
                targetBox->raise();
                if (m_iconShowRule.autoArrange) {
                    applyAutoArrangeAfterDesktopMutation(false, false);
                } else {
                    update();
                }
            }
        }
        else {
            handlePendingShellCreatedItem(newPath);
        }
        
        // 濞ｅ洦绻傞悺銊╁箰娴ｉ鐣介柛鏍ㄧ墬閺嬪啯绂掔拋鍦闁哄洤鐡ㄩ弻濠囨煂瀹ュ懏鍤掗柛姘У閺嬪啯绂掗幆閭﹀敹鐟?
        requestSaveBoxes();
        break;
    }
    case FileSystemEvent::FileUpdated:
    case FileSystemEvent::FileAttributesChanged:
    {
        const QString& updatePath = !newPath.isEmpty() ? newPath : oldPath;
        if (updatePath.isEmpty()) break;

        if (findDesktopStateByPath(updatePath)) {
            refreshDesktopModelItemPath(updatePath, updatePath);
            if (eventInfo.eventType == FileSystemEvent::FileAttributesChanged) {
                update();
            }
        } else if (LinBox* box = BoxManager::instance()->findBoxContainingItem(updatePath)) {
            if (DesktopRootItemState* state = box->findItemStateByPath(updatePath)) {
                const ListItemData data = DesktopItemLoader().loadItem(updatePath);
                DesktopRootItemModel updatedModel;
                initDesktopRootItemModel(updatedModel, data, currentDesktopItemInitContext());
                updatedModel.layoutMode = state->model.layoutMode;
                updatedModel.position = state->model.position;
                updatedModel.baseSize = state->model.baseSize;
                updatedModel.effectiveSize = state->model.effectiveSize;
                updatedModel.visibleTextHeight = state->model.visibleTextHeight;
                updatedModel.isSelected = state->model.isSelected;
                updatedModel.isHovered = state->model.isHovered;
                updatedModel.focused = state->model.focused;
                updatedModel.isCutMarked = state->model.isCutMarked;
                updatedModel.isFullNameExpanded = state->model.isFullNameExpanded;
                updatedModel.customSortOrder = state->model.customSortOrder;
                updatedModel.expandedPreviewText = state->model.expandedPreviewText;
                updatedModel.isRenaming = state->model.isRenaming;
                updatedModel.isReady = true;
                state->model = updatedModel;
                box->refreshLayout();
                box->update();
            }
        }
        break;
    }
    }
    
    // Ensure all LinBoxes stay above desktop items
    for (const auto& box : BoxManager::instance()->allBoxes()) {
        if (box) box->raise();
    }
}

void ListView::onMenuQuitApp() {
    qDebug() << "LinDesk: Global Quit Requested...";
    qApp->setProperty("LinDeskForceQuit", true);
    
    // Close the settings window if open
    if (m_settingsWindow) {
        m_settingsWindow->close();
    }

    // Close all remaining windows (boxes, etc.)
    QApplication::closeAllWindows();
    
    // Final quit
    qApp->quit();
}

void ListView::onMenuCreateBox()
{
    BoxManager* manager = BoxManager::instance();
	LinBox* box = manager->createBox(m_lastRightClickPos, this);
    
    // Connect box signals to ListView
    manager->setupConnections(box, this);
}

void ListView::onMenuMapFolder()
{
    const QString selectedDir = QFileDialog::getExistingDirectory(
        this,
        tr("Select Folder to Map"),
        QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (selectedDir.isEmpty()) {
        return;
    }

    const QString normalizedPath = normalizeMappedFolderPathForLookup(selectedDir);
    if (findMappedFolderBox(normalizedPath)) {
        showTransientTip(tr("This folder is already on the desktop."), 2600);
        return;
    }

    BoxManager* manager = BoxManager::instance();
	LinBox* box = manager->createBox(m_lastRightClickPos, this);
    manager->setupConnections(box, this);

    box->setBoxTitle(QString());
    box->setMappedFolderPath(normalizedPath);
    loadMappedFolderItemsForBox(box);
    box->refreshLayout();
    box->raise();
    requestSaveBoxes();
}

void ListView::onMenuOrganizeDesktop()
{
    if (!m_enablePersistence) {
        m_enablePersistence = true;
    }

    m_iconShowRule.autoArrange = true;
    m_iconShowRule.gridAlignment = true;
    m_iconShowRule.iconPosMode = IconPosMode::AlignIconToGrid;
    m_iconShowRule.sortTypeBeforeAutoArrange = SortOrderType::SortByName_ASC;
    m_iconShowRule.sortType = SortOrderType::SortByCustom;

    QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
    sortAutoArrangeStates(states, SortOrderType::SortByName_ASC);
    assignCustomSortOrders(states);
    const QVector<QPoint> positions = layoutAutoArrangeItems(states);
    finalizeAutoArrangeLayout(states, positions, true, false);
    updateRightMenuState();
}

void ListView::onMenuOrganizeDesktopByNameSort()
{
    onMenuSortByName();
}

QVector<LinBox*> ListView::boxes() const {
    QVector<LinBox*> result;
    for (const QPointer<LinBox>& box : BoxManager::instance()->allBoxes()) {
        if (box) result.append(box.data());
    }
    return result;
}

void ListView::setupItemConnections(ListItem* item, LinBox* box)
{
	if (!item) return;
	
	// 闁稿繐鐗婇弻鍥ь嚕閳ь剛鎷?item 闁汇劌瀚晶宥夊嫉婢跺绻嗛柛娆戞焿缁绘盯骞掗妷顖滅闂傚啫寮堕娑㈡煂瀹ュ拋妲婚弶鈺冨仦鐢?
	disconnect(item, &ListItem::itemClicked, this, nullptr);
	disconnect(item, &ListItem::itemMoved, this, nullptr);
	disconnect(item, &ListItem::itemReleased, this, nullptr);
	disconnect(item, &ListItem::itemDoubleClicked, this, nullptr);
	disconnect(item, &ListItem::itemContextMenuRequested, this, nullptr);
	
	connect(item, &ListItem::itemClicked, this, [this, box](ListItem* i, QMouseEvent* e) {
		onItemRelayClicked(box, i, e);
	});
	connect(item, &ListItem::itemMoved, this, [this, box](ListItem* i, QMouseEvent* e) {
		onItemRelayMoved(box, i, e);
	});
	connect(item, &ListItem::itemReleased, this, [this, box](ListItem* i, QMouseEvent* e) {
		onItemRelayReleased(box, i, e);
	});
	connect(item, &ListItem::itemDoubleClicked, this, [this, box](ListItem* i, QMouseEvent* e) {
		onItemRelayDoubleClicked(box, i, e);
	});
	connect(item, &ListItem::itemContextMenuRequested, this, [this, box](ListItem* i, const QPoint& p) {
		onItemRelayContextMenuRequested(box, i, p);
	});
}

void ListView::onItemRelayClicked(LinBox* box, ListItem* item, QMouseEvent* event)
{
	if (!item) return;

    const ListItemData itemData = item->getItemData();
    qDebug().noquote()
        << QString("[ItemClick] name=\"%1\" path=\"%2\" clsid=\"%3\" type=%4 boxId=\"%5\" normalizedPath=\"%6\"")
            .arg(item->baseName(),
                 item->filePath(),
                 itemData.getCLSID(),
                 QString::number(static_cast<int>(item->type())),
                 item->boxId(),
                 item->normalizedPath());

	if (event->button() == Qt::LeftButton) {
        if (!box) {
            this->activateWindow();
            this->setFocus(Qt::MouseFocusReason);

            const QString itemPath = normalizeDesktopItemPath(item->filePath());
            const Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();

            if (modifiers & Qt::ControlModifier) {
                const bool selected = isDesktopPathSelected(itemPath);
                setDesktopItemSelected(itemPath, !selected);
                m_focusedDesktopPath = itemPath;
                if (m_anchorDesktopPath.isEmpty()) {
                    m_anchorDesktopPath = itemPath;
                }
                m_wasSelectedBeforeClick = false;
            } else if (modifiers & Qt::ShiftModifier) {
                QStringList orderedPaths;
                for (const auto& state : m_itemStates) {
                    if (shouldPaintDesktopModelState(state)) {
                        orderedPaths.append(normalizeDesktopItemPath(state.model.filePath));
                    }
                }

                const QString anchorPath = m_anchorDesktopPath.isEmpty() ? itemPath : m_anchorDesktopPath;
                const int anchorIndex = orderedPaths.indexOf(anchorPath);
                const int itemIndex = orderedPaths.indexOf(itemPath);
                if (anchorIndex >= 0 && itemIndex >= 0) {
                    QStringList rangePaths;
                    const int start = qMin(anchorIndex, itemIndex);
                    const int end = qMax(anchorIndex, itemIndex);
                    for (int i = start; i <= end; ++i) {
                        rangePaths.append(orderedPaths.at(i));
                    }
                    applyDesktopSelection(rangePaths, itemPath, false);
                } else {
                    applyDesktopSelection({ itemPath }, itemPath, true);
                    m_anchorDesktopPath = itemPath;
                }
                m_wasSelectedBeforeClick = false;
            } else {
                if (!isDesktopPathSelected(itemPath)) {
                    RenameManager::instance()->cancelRename();
                    applyDesktopSelection({ itemPath }, itemPath, true);
                    m_anchorDesktopPath = itemPath;
                    m_wasSelectedBeforeClick = false;
                } else {
                    m_focusedDesktopPath = itemPath;
                    if (DesktopRootItemState* state = findDesktopStateByPath(itemPath)) {
                        state->model.isFullNameExpanded = true;
                    }
                    m_wasSelectedBeforeClick = (m_selectedDesktopPaths.size() == 1);
                }
            }

            if (m_wasSelectedBeforeClick &&
                m_lastClickedDesktopPath == itemPath &&
                m_selectedDesktopPaths.size() == 1) {
                if (m_clickTimer.isValid() && m_clickTimer.elapsed() > 500 && m_clickTimer.elapsed() < 5000) {
                    RenameManager::instance()->startRename(itemPath);
                    m_isDragging = false;
                    return;
                }
            }

            m_lastClickedDesktopPath = itemPath;
            m_clickTimer.restart();
            if (!(modifiers & Qt::ControlModifier) && !(modifiers & Qt::ShiftModifier)) {
                m_anchorDesktopPath = itemPath;
            }

            m_draggedDesktopPath = itemPath;
            m_draggedItem = nullptr;
            QPoint globalPos = item->mapToGlobal(event->pos());
            QPoint clickPosInListView = this->mapFromGlobal(globalPos);
            QPoint globalItemTopLeft = item->mapToGlobal(QPoint(0, 0));
            QPoint itemPosOnListView = this->mapFromGlobal(globalItemTopLeft);
            m_dragOffset = clickPosInListView - itemPosOnListView;
            m_isDragging = true;
            m_startPos = clickPosInListView;
            return;
        }

		// Ensure focus is on the correct window to receive Delete shortcuts
		if (box) {
            box->raise();
            box->activateWindow();
            box->setFocus(Qt::MouseFocusReason);
        } else {
            this->activateWindow();
            this->setFocus(Qt::MouseFocusReason);
        }

		Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();
		
		if (modifiers & Qt::ControlModifier) {
			// Ctrl: 闁告帒娲﹀畷鏌ュ础閺囨岸鍤?item 闂侇偄顦懙鎴︽偐閼哥鍋?
			toggleItemSelection(item->filePath());
            m_wasSelectedBeforeClick = false;
		} else if (modifiers & Qt::ShiftModifier) {
			// Shift: 閺夆晞妫勬慨鐐烘焻婢跺鍘?
			selectRangeToPath(item->filePath(), box);
            m_wasSelectedBeforeClick = false;
		} else {
			// 闁哄啰濮抽幈銊︻殰娴煎瓨鏆涢柨?
			// 濠碘€冲€归悘澶愭倷閻熸澘姣婇柣?item 鐎规瓕灏欑划锟犲捶閵娿儺妯嬮梺顐㈩樀濞夛箓宕ラ崼銉ユ闁挎稑濂旂换姘舵偩濞嗘劖娈诲☉鎿冧邯閳ь剙顦懙鎴犵磼閸曞墎绀勯柛鎴濇椤︻剚寰勫鍨涘亾婢跺鐝涢柛鏃戠厜缁?
			// 闁告熬绠戦崹顖氥€掗崨顖楁晞闂侇偄顦懙鎴︽晬鐏炶棄娑ч梺顐㈩槼缁绘牗绋夐埀顒佺▔?
			const QString itemPath = normalizeDesktopItemPath(item->filePath());
			if (!m_selectedBoxPaths.contains(itemPath)) {
                RenameManager::instance()->cancelRename();
				setSelectedItemPaths({ itemPath }, itemPath, true);
                m_anchorBoxPath = itemPath;
                m_wasSelectedBeforeClick = false;
			} else {
                setFocusedItemPath(itemPath);
                setItemFullNameExpanded(itemPath, true);
                item->raise();
                m_wasSelectedBeforeClick = (m_selectedBoxPaths.size() == 1);
            }
		}

        // Windows Rename Rhythm: 
        // If single item was already selected, and we click it again after a delay
        if (m_wasSelectedBeforeClick && m_lastClickedItem == item && m_selectedBoxPaths.size() == 1) {
            if (m_clickTimer.isValid() && m_clickTimer.elapsed() > 500 && m_clickTimer.elapsed() < 5000) {
                RenameManager::instance()->startRename(item->filePath());
                m_isDragging = false; 
                return;
            }
        }

        m_lastClickedItem = item;
        m_clickTimer.restart();
        if (!(modifiers & Qt::ControlModifier) && !(modifiers & Qt::ShiftModifier)) {
            m_anchorItem = item;
            m_anchorBoxPath = normalizeDesktopItemPath(item->filePath());
        }

		// Start Dragging
		m_draggedItem = item;
		QPoint globalPos = item->mapToGlobal(event->pos());
		QPoint clickPosInListView = this->mapFromGlobal(globalPos);
		
		// Use global mapping to ensure correct coordinates for items in top-level Box windows
		QPoint globalItemTopLeft = item->mapToGlobal(QPoint(0,0));
		QPoint itemPosOnListView = this->mapFromGlobal(globalItemTopLeft);
		
		m_dragOffset = clickPosInListView - itemPosOnListView;
		m_isDragging = true;
        m_startPos = clickPosInListView; // 閻犱焦婢樼紞宥咁嚕閳ь剚鎱ㄧ€ｂ晝绉寸紓鍐惧枙娴滄帡宕氶妶鍡樼劷闁归攱鐗曢崵顓㈡⒓閸績鍋?
	}
}

void ListView::resetRenameClickState()
{
    m_lastClickedItem = nullptr;
    m_lastClickedDesktopPath.clear();
    m_wasSelectedBeforeClick = false;
    m_clickTimer.invalidate();
}

void ListView::onItemRelayMoved(LinBox* box, ListItem* item, QMouseEvent* event)
{
    RenameManager::instance()->cancelRename(); // Any move cancels rename
    resetRenameClickState();
	if (m_isDragging) {
		QPoint globalPos = item->mapToGlobal(event->pos());
		QPoint mousePos = this->mapFromGlobal(globalPos);
        
        // 婵☆偀鍋撻柡灞诲劚閹酣宕?QDrag (濠㈣埖鐗犻崕鎾箯閺嵮冩瘔) 闁汇劌瀚板Σ鍥磹?
        if ((mousePos - m_startPos).manhattanLength() > QApplication::startDragDistance()) {
       
            // For internal tracking, create visual ghosts only when actually moving
            if (m_ghosts.isEmpty()) {
                createDragGhosts(m_startPos);
            } else {
                // Update ghosts while moving within or over the Box
                for (auto& ghost : m_ghosts) {
                    if (ghost.widget) {
                        ghost.widget->move(globalPos + ghost.offset);
                    }
                }
            }

            // 闁告垵妫楅ˇ顒勫箯閺嵮冩瘔闁轰胶澧楀畵?
            QStringList dragSelection;
            const QStringList draggedSelectionPaths = selectedItemPaths();
            for (const QString& path : draggedSelectionPaths) {
                if (const DesktopRootItemState* state = findAnyItemStateByPath(path)) {
                    const QString filePath = state->model.filePath;
                    if (!filePath.isEmpty() && !dragSelection.contains(filePath)) {
                        dragSelection.append(filePath);
                    }
                }
            }

            WindowsDragMimeData* mimeData = new WindowsDragMimeData();
            QList<QUrl> urls;
            for (const QString& filePath : dragSelection) {
                urls << QUrl::fromLocalFile(filePath);
            }
            mimeData->setUrls(urls);
            
            QDrag* drag = new QDrag(this);
            drag->setMimeData(mimeData);
            
            // --- 濠㈣埖鑹惧ù姗€寮介崶銊ョ彌闁瑰嘲鈧噥娼掗悷娆忣槹閺呫儵寮稿鍐炬澔鐎?---
            if (!dragSelection.isEmpty()) {
                // 1. 閻犱緤绱曢悾濠氬箥閳ь剟寮垫径鎰ㄥ亾婢跺鍘銈呮贡濞堟垿鎳曢弬鎸庡€ら柛鏍ф噹濞插潡鎯?(濞达綀娉曢弫銈夊礂閵娿儳婀伴悘鐐茬箰缁犵兘宕搁幇顓犲灱闁挎稑鐭傚Σ璇差潰椤⑩暙x闁告劕鎳橀崕瀵镐沪閳ь剟鏌堥妸銉︾稄闁哄秴娲﹂挅鍕蓟?
                QRect globalBounding;
                for (const QString& path : dragSelection) {
                    const DesktopRootItemState* state = findAnyItemStateByPath(path);
                    if (!state) {
                        continue;
                    }
                    const QRect itemRect = state->model.renderData.itemRect;
                    const QPoint globalTopLeft = state->model.boxId.isEmpty()
                        ? mapToGlobal(itemRect.topLeft())
                        : BoxManager::instance()->findBoxContainingItem(path)->mapToGlobal(itemRect.topLeft());
                    QRect globalRect(globalTopLeft, itemRect.size());
                    globalBounding = globalBounding.united(globalRect);
                }

                // 2. 闁告帗绋戠紓鎾寸▔閳ь剚绋夐鍥у枙濠㈠墎鍠庨妵鍥儍閸曨垪鍋撹箛鏃€顫?Pixmap 闁哄鍎叉竟娆愭姜閼恒儱顣查柡鍫濐槸濞存﹢寮?
                QPixmap fullPix(globalBounding.size());
                fullPix.fill(Qt::transparent);
                QPainter painter(&fullPix);
                painter.setRenderHint(QPainter::Antialiasing);

                for (const QString& path : dragSelection) {
                    // 闁哄棗鍊瑰鍌炲矗閺嶃劎啸闂侇偄顦懙鎴﹀椽鐏炴儳浜炬繛鎼枤婵悂骞€娴ｈ渹绨伴柟鑸垫尭瑜板洭鐛幓鎺戞珝闁汇劌瀚ù姗€宕?
                    const DesktopRootItemState* state = findAnyItemStateByPath(path);
                    if (!state) {
                        continue;
                    }

                    // 閻?item 婵炴挸寮堕悡瀣礆?Pixmap 闁汇劌瀚ù澶屸偓鐢糕偓娑氱Т缂傚喚鍣槐娆撳礂閵娿儳婀板ù锝呯Ф閻ゅ棝鎯勭粙娆惧殸濞存粌楠搁崣蹇曚沪閳ь剟宕犻崨顓熺函闁烩晜甯掓稊蹇旂▔婵犲拋娼￠柨?
                    const QRect itemRect = state->model.renderData.itemRect;
                    const QPoint globalItemPos = state->model.boxId.isEmpty()
                        ? mapToGlobal(itemRect.topLeft())
                        : BoxManager::instance()->findBoxContainingItem(path)->mapToGlobal(itemRect.topLeft());
                    painter.save();
                    painter.translate(globalItemPos - globalBounding.topLeft() - itemRect.topLeft());
                    paintDesktopRootItem(painter, buildDesktopRootItemRenderData(*state, false));
                    painter.restore();

                    // 闁诡厹鍨归ˇ鏌ユ偐閼哥鍋?
                }
                painter.end();

                drag->setPixmap(fullPix);
                
                // 3. 閻犱礁澧介悿鍡涙倻椤撶姴浠柨娑欐皑閺併倝骞嬮柨瀣粯闁告帗绻勯崑锝夊礄閼姐倕浠柛锔哄妼閸欏繒浠﹂埀顒勫礌閸涱厽绾柣鈺傚笒閸炴挳鎯冮崟顓犵勘缁绢収鍠氬ù澶屸偓鐢垫嚀濞兼寮?
                // m_startPos 闂傚洠鍋撻悷鏇氱閸樻稒娼鈧ú鏍礂閵娿儳婀伴柛褎鍔栭悥锝夋晬鐏炴儳顤呴柤铏灊缁?globalBounding 婵繐绲块垾妯兼媼閿涘嫮鏆?
                QPoint globalStartPos = this->mapToGlobal(m_startPos);
                QPoint hotSpot = globalStartPos - globalBounding.topLeft();
                drag->setHotSpot(hotSpot);
            }
            
            // 鐎殿喒鍋撳┑顔碱儐鐎氬骞忛挊澶婎枀闂傚懏鍔樺Λ?ghosts (闁搞儳濮崇拹?QDrag 濞村吋纰嶇敮瀵哥不?
            for (auto& ghost : m_ghosts) {
                if (ghost.widget) ghost.widget->hide();
            }
            
            // 闁圭瑳鍡╂斀闁归攱鐗楃€?(閺夆晜鐟ょ槐浼存⒓鐠囧樊鏁氶柣鈺傛綑閸╁矂鏌屾繝鍐╂澒)
            Qt::DropAction dropAction = drag->exec(Qt::CopyAction | Qt::MoveAction | Qt::LinkAction);
            
            // 闁归攱鐗楃€氳法绱掗幘瀛樺皢闁告艾娴峰▓鎴濄€掗崨顖涘€?
            m_isDragging = false;
            m_draggedItem = nullptr;
            clearDesktopAutoArrangeReorderPreview();
            for (auto& ghost : m_ghosts) {
                if (ghost.widget) ghost.widget->deleteLater();
            }
            m_ghosts.clear();
            
            // 濞ｅ洦绻傞悺銊╁炊閻愵剛鍨煎ù锝呯Ф閻ゅ棝宕氭导鏉戝赋缂傚喚鍠楅弸鍐╃鐠佸湱绀勯柟閿嬬墬鐎氳法绱掗幘瀛樺皢闁告艾閰ｅ〒鍓佹啺娴ｉ绠介悗娑欙公缁?
            requestSaveBoxes();
            
            return;
        }

		for (auto& ghost : m_ghosts) {
			if (ghost.widget) {
				ghost.widget->move(globalPos + ghost.offset);
			}
		}
	}
}

void ListView::onItemRelayReleased(LinBox* box, ListItem* item, QMouseEvent* event)
{
	if (m_isDragging) {
        Q_UNUSED(item);
        Q_UNUSED(event);

        if (!box && m_iconShowRule.autoArrange) {
            for (auto& ghost : m_ghosts) {
                if (ghost.widget) {
                    ghost.widget->hide();
                    ghost.widget->deleteLater();
                }
            }
            m_ghosts.clear();
            m_isDragging = false;
            m_draggedItem = nullptr;
            m_draggedDesktopPath.clear();
            clearDesktopAutoArrangeReorderPreview();
            applyAutoArrangeLayout(true, false);
            return;
        }

		bool hasCollision = false;
		QVector<QPoint> targetPositions;

		// 1. Calculate target positions and check collisions for desktop icons
		if (!box) {
			if (m_iconShowRule.gridAlignment) {
                const int itemHeight = desktopItemFootprintHeight();
                const int strideX = m_iconWidth + m_colSpacing;
                const int strideY = itemHeight + m_rowSpacing;
                const int layoutLeft = desktopLayoutLeft();
                const int layoutTop = desktopLayoutTop();
				const int usableRight = desktopUsableRight();
				const int usableBottom = desktopUsableBottom();
				for (const auto& ghost : m_ghosts) {
                    DesktopRootItemState* draggedState = ghost.filePath.isEmpty()
                        ? nullptr
                        : findDesktopStateByPath(ghost.filePath);
					if (ghost.widget && (ghost.item || draggedState)) {
						QPoint pos = ghost.widget->pos();
						int gridX = qRound((double)(pos.x() - layoutLeft) / strideX);
						int gridY = qRound((double)(pos.y() - layoutTop) / strideY);
						
						QPoint snappedPos;
						snappedPos.setX(layoutLeft + gridX * strideX);
						snappedPos.setY(layoutTop + gridY * strideY);
						snappedPos.setX(qMax(layoutLeft, qMin(snappedPos.x(), usableRight - m_iconWidth + 1)));
						snappedPos.setY(qMax(layoutTop, qMin(snappedPos.y(), usableBottom - itemHeight + 1)));
						
						targetPositions.append(snappedPos);

                        for (const auto& otherState : m_itemStates) {
                            if (!shouldPaintDesktopModelState(otherState)) {
                                continue;
                            }
                            if (isDesktopPathSelected(otherState.model.filePath)) {
                                continue;
                            }
                            if (otherState.model.position == snappedPos) {
									hasCollision = true;
									break;
                            }
                        }
					} else {
						targetPositions.append(QPoint(-1, -1));
					}
					if (hasCollision) break;
				}
			}
		}

		// 2. Apply movement
		int idx = 0;
		for (auto& ghost : m_ghosts) {
            DesktopRootItemState* draggedState = ghost.filePath.isEmpty()
                ? nullptr
                : findDesktopStateByPath(ghost.filePath);
			if (ghost.widget && (ghost.item || draggedState)) {
				if (!hasCollision) {
					QPoint finalPos = this->mapFromGlobal(ghost.widget->pos());
					if (!box && m_iconShowRule.gridAlignment) {
						finalPos = targetPositions[idx];
					}
					
					// Only move if it's a desktop icon (parent is ListView)
					// Items in box stay where they are (refreshLayout handles them)
					if (!box) {
                        if (ghost.item) {
						    ghost.item->move(finalPos);
						    ghost.item->updateLocationInfo(finalPos);
                        }
                        if (draggedState) {
                            draggedState->model.position = finalPos;
                            draggedState->model.renderData.itemRect.moveTopLeft(finalPos);
                        }
					}
				}
				ghost.widget->hide();
				ghost.widget->deleteLater();
			}
			idx++;
		}

		m_ghosts.clear();
		m_isDragging = false;
		m_draggedItem = nullptr;
        m_draggedDesktopPath.clear();
        if (!box) {
            updateAllItemRenderData();
            update();
        }
        clearDesktopAutoArrangeReorderPreview();
		
		// Snap back to grid in LinBox if applicable
		if (box) {
			box->refreshLayout();
		}
		
		// 濞ｅ洦绻傞悺銊╁炊閻愵剛鍨煎ù锝呯Ф閻ゅ棝宕氭导鏉戝赋缂傚喚鍠楅弸鍐╃?
		requestSaveBoxes();
	}
}

void ListView::createDragGhosts(const QPoint& clickPos)
{
	// 婵炴挸鎳愰幃濠囧籍瑜忓▓?ghost
	for (auto& ghost : m_ghosts) {
		if (ghost.widget) ghost.widget->deleteLater();
	}
	m_ghosts.clear();

    QVector<ListItem*> dragItems;
    const QStringList allSelectedPaths = selectedItemPaths();
    Q_UNUSED(allSelectedPaths);

	for (auto* item : dragItems) {
		if (!item) continue;

		QLabel* ghostWidget = new QLabel(nullptr);
        ghostWidget->setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowTransparentForInput);
		ghostWidget->setAttribute(Qt::WA_TranslucentBackground);
		ghostWidget->setAttribute(Qt::WA_NoSystemBackground);
        ghostWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
		ghostWidget->setStyleSheet("background: transparent;");

		// 闁硅埖鎸歌ぐ鍥疀椤愩倕寮鹃柨娑樼墔婢跺秹寮捄鍝勭悼婵炴垵鐗撻埀顒€顦懙鎴炵閵夈劌绠€电増顨呴崗閬嶅礄閳ь剟宕堕悙顒傚灱闁?
		bool wasSelected = item->isSelected;
		item->isSelected = false;
		item->update();
		QPixmap pixmap = item->grab();
		item->isSelected = wasSelected;
		item->update();

		ghostWidget->setPixmap(pixmap);
		ghostWidget->setFixedSize(item->size());
		
		QPoint globalItemPos = item->mapToGlobal(QPoint(0,0));
		QPoint globalClickPos = this->mapToGlobal(clickPos);
		QPoint offset = globalItemPos - globalClickPos;
		
		ghostWidget->move(QCursor::pos() + offset);
		ghostWidget->show();
        if (HWND hwnd = reinterpret_cast<HWND>(ghostWidget->winId())) {
            SetWindowPos(
                hwnd,
                HWND_NOTOPMOST,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW
            );
        }

		m_ghosts.append({ ghostWidget, item, normalizeDesktopItemPath(item->filePath()), offset });
	}

    for (const QString& desktopPath : selectedDesktopPaths()) {
        bool alreadyTracked = false;
        for (const auto& ghost : m_ghosts) {
            if (ghost.filePath == desktopPath) {
                alreadyTracked = true;
                break;
            }
        }
        if (alreadyTracked) {
            continue;
        }

        DesktopRootItemState* state = findDesktopStateByPath(desktopPath);
        if (!state) {
            continue;
        }

        const DesktopRootItemRenderData& renderData = state->model.renderData;
        const QSize ghostSize = renderData.itemRect.size();
        if (!ghostSize.isValid()) {
            continue;
        }

        QLabel* ghostWidget = new QLabel(nullptr);
        ghostWidget->setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowTransparentForInput);
        ghostWidget->setAttribute(Qt::WA_TranslucentBackground);
        ghostWidget->setAttribute(Qt::WA_NoSystemBackground);
        ghostWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
        ghostWidget->setStyleSheet("background: transparent;");

        QPixmap pixmap(ghostSize);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.translate(-renderData.itemRect.topLeft());
        paintDesktopRootItem(painter, renderData);
        painter.end();

        ghostWidget->setPixmap(pixmap);
        ghostWidget->setFixedSize(ghostSize);

        QPoint globalItemPos = this->mapToGlobal(renderData.itemRect.topLeft());
        QPoint globalClickPos = this->mapToGlobal(clickPos);
        QPoint offset = globalItemPos - globalClickPos;

        ghostWidget->move(QCursor::pos() + offset);
        ghostWidget->show();
        if (HWND hwnd = reinterpret_cast<HWND>(ghostWidget->winId())) {
            SetWindowPos(
                hwnd,
                HWND_NOTOPMOST,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW
            );
        }

        m_ghosts.append({ ghostWidget, nullptr, desktopPath, offset });
    }
}

void ListView::onItemRelayDoubleClicked(LinBox* box, ListItem* item, QMouseEvent* event)
{
    RenameManager::instance()->cancelRename(); // Double click cancels rename
	if (item) {
        ensureModelStateForLegacyItem(item);
        if (DesktopRootItemState* state = findModelStateForLegacyItem(item)) {
            openDesktopRootItem(state->model);
        } else {
            DesktopRootItemModel model;
            model.filePath = item->filePath();
            openDesktopRootItem(model);
        }
	}
}

void ListView::onItemRelayContextMenuRequested(LinBox* box, ListItem* item, const QPoint& globalPos)
{
    RenameManager::instance()->cancelRename(); // Context menu cancels rename
	if (!item) return;

    if (!box) {
        const QString itemPath = normalizeDesktopItemPath(item->filePath());
        if (!isDesktopPathSelected(itemPath)) {
            applyDesktopSelection({ itemPath }, itemPath, true);
            m_anchorDesktopPath = itemPath;
        } else {
            m_focusedDesktopPath = itemPath;
        }

        QVector<ListItem*> desktopLegacyItems = selectedDesktopLegacyItems();
        requestSystemRightMenu(item->type(), globalPos, desktopLegacyItems);
        return;
    }

	// Ensure item is selected for context menu
    const QString itemPath = normalizeDesktopItemPath(item->filePath());
	if (!m_selectedBoxPaths.contains(itemPath)) {
		setSelectedItemPaths({ itemPath }, itemPath, true);
        m_anchorBoxPath = itemPath;
	} else {
        setFocusedItemPath(itemPath);
	}

    QVector<ListItem*> selectedLegacyItems;
	requestSystemRightMenu(item->type(), globalPos, selectedLegacyItems);
}

void ListView::ensureDragTip() {
    if (!m_dragTip) {
        m_dragTip = new QLabel(this);
        m_dragTip->setTextFormat(Qt::RichText);
        m_dragTip->setStyleSheet(QStringLiteral(
            "QLabel {"
            "  background-color: rgba(255, 255, 255, 235);"
            "  color: #202020;"
            "  border: 1px solid rgba(0, 0, 0, 40);"
            "  border-radius: 8px;"
            "  padding: 10px 16px;"
            "  font-size: 13px;"
            "  font-family: %1;"
            "}"
        ).arg(AppFonts::uiCssFamily()));
        m_dragTip->setAttribute(Qt::WA_TransparentForMouseEvents);
        QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(m_dragTip);
        shadow->setBlurRadius(20);
        shadow->setColor(QColor(0, 0, 0, 80));
        shadow->setOffset(0, 4);
        m_dragTip->setGraphicsEffect(shadow);
        m_dragTip->hide();
    }
}

void ListView::ensureInfoTip()
{
    if (!m_infoTip) {
        m_infoTip = new FluentInfoTip(this);
        m_infoTip->hide();
    }
}

void ListView::showTransientTip(const QString& message, int autoHideMs)
{
    ensureInfoTip();
    m_infoTip->setText(message);
    const QRect globalRect(mapToGlobal(QPoint(0, 0)), size());
    m_infoTip->showCentered(globalRect, autoHideMs);
}

bool ListView::createNewFolderInHost(LinBox* targetBox)
{
    if (RenameManager::instance()->isRenaming()) {
        return false;
    }

    const bool createInMappedFolder = targetBox && targetBox->isFolderMapped();
    if (createInMappedFolder && targetBox->mappedFolderState() != LinBox::MappedFolderState::Normal) {
        return false;
    }

    const QString targetDirectory = createInMappedFolder
        ? normalizeMappedFolderPathForLookup(targetBox->mappedFolderPath())
        : normalizeDesktopPath(m_desktopDir);
    if (targetDirectory.isEmpty()) {
        return false;
    }

    const QString newFolderPath = ListViewFileOpsHelper::uniqueNewFolderPath(targetDirectory);
    if (newFolderPath.isEmpty()) {
        return false;
    }

    const bool reserveDesktopPosition = !targetBox;
    if (targetBox && !createInMappedFolder) {
        m_pendingCreatedItemBoxes[newFolderPath] = targetBox;
        m_pendingCreatedItemInsertIndices.remove(newFolderPath);
    } else if (reserveDesktopPosition) {
        if (!m_iconShowRule.autoArrange) {
            const DesktopRootItemState* focusedDesktopState = m_focusedDesktopPath.isEmpty()
                ? nullptr
                : findDesktopStateByPath(m_focusedDesktopPath);
            const QPoint requestedPos = focusedDesktopState
                ? focusedDesktopState->model.position
                : generateItemPosition();
            reserveCreatedItemPosition(newFolderPath, requestedPos);
        }
    }

    if (!ListViewFileOpsHelper::createDirectoryPath(newFolderPath)) {
        m_pendingCreatedItemBoxes.remove(newFolderPath);
        m_pendingCreatedItemInsertIndices.remove(newFolderPath);
        if (reserveDesktopPosition) {
            m_pendingCreatedItemPositions.remove(normalizeDesktopPath(newFolderPath));
        }
        return false;
    }

    if (createInMappedFolder && targetBox) {
        QTimer::singleShot(0, this, [this, targetBox]() {
            if (!targetBox) {
                return;
            }
            reloadMappedFolderBox(targetBox);
            targetBox->refreshLayout();
            targetBox->raise();
            update();
        });
    }

    return true;
}

void ListView::onMenuDelete(bool permanently)
{
    std::vector<std::wstring> paths;
    QVector<ListItem*> itemsToDelete;
    QStringList desktopPathsToDelete;
    QStringList boxPathsToDelete;
    QSet<QString> seenDeletePaths;

    for (const QString& path : selectedDesktopPaths()) {
        const DesktopRootItemState* state = findDesktopStateByPath(path);
        if (!state) {
            continue;
        }
        if (state->model.itemData.isRealSpecialFolderIcon()) {
            continue;
        }
        const QString normalizedPath = normalizeDesktopItemPath(state->model.filePath);
        if (normalizedPath.isEmpty() || seenDeletePaths.contains(normalizedPath)) {
            continue;
        }
        seenDeletePaths.insert(normalizedPath);
        desktopPathsToDelete.append(normalizedPath);
        paths.push_back(QDir::toNativeSeparators(state->model.filePath).toStdWString());
    }

    for (const QString& path : selectedBoxPaths()) {
        const DesktopRootItemState* state = findAnyItemStateByPath(path);
        if (!state || state->model.itemData.isRealSpecialFolderIcon()) continue;
        const QString normalizedPath = normalizeDesktopItemPath(state->model.filePath);
        if (normalizedPath.isEmpty() || seenDeletePaths.contains(normalizedPath)) {
            continue;
        }
        seenDeletePaths.insert(normalizedPath);
        boxPathsToDelete.append(normalizedPath);
        paths.push_back(QDir::toNativeSeparators(state->model.filePath).toStdWString());
    }

    if (paths.empty()) return;

    const bool deleteOk = CSystemApi::DeleteFiles(paths, !permanently, true);
    if (!deleteOk) {
        return;
    }

    trackPendingExternalRemovals(itemsToDelete);
    removeItemsBatch(itemsToDelete);
    for (const QString& path : desktopPathsToDelete) {
        if (!findItemByPath(path)) {
            m_pendingExternalRemovalPaths.insert(normalizeDesktopPath(path));
            removeDesktopStateByPath(path);
        }
    }
    for (const QString& path : boxPathsToDelete) {
        if (LinBox* box = BoxManager::instance()->findBoxContainingItem(path)) {
            box->removeItemState(path, true);
        }
    }
    requestSaveBoxes();
}

void ListView::saveBoxes() {
    if (m_isLoadingPersistence) {
        return;
    }

    LayoutPersistenceService persistenceService;
    LayoutPersistenceState state;
    state.enablePersistence = m_enablePersistence;
    state.viewMode = (int)m_iconShowRule.viewMode;
    state.sortType = (int)m_iconShowRule.sortType;
    state.preAutoSort = (int)m_iconShowRule.sortTypeBeforeAutoArrange;
    state.iconPosMode = (int)m_iconShowRule.iconPosMode;
    state.autoArrange = m_iconShowRule.autoArrange;
    state.gridAlignment = m_iconShowRule.gridAlignment;
    state.showDesktopIcons = m_iconShowRule.showDesktopIcons;
    state.doubleClickHideIcons = m_iconShowRule.doubleClickHideIcons;
    state.showShortCutArrow = m_iconShowRule.showShortCutArrow;
    state.iconPositions = persistenceService.collectIconPositions(
        m_itemStates,
        BoxManager::instance()->allBoxes()
    );

    m_savedIconPositions = persistenceService.toSavedIconPositions(state.iconPositions);
    persistenceService.save(m_persistencePath, state);
}

void ListView::requestSaveBoxes() {
    if (m_isLoadingPersistence) {
        return;
    }
    BoxManager::instance()->requestSave();
}



void ListView::applyBoxAssignmentsFromConfig(const QVector<QMap<QString, QVariant>>& boxData) {
    qDebug() << "[applyBoxAssignmentsFromConfig] Processing" << boxData.size() << "boxes";
    QSet<LinBox*> boxesToRefresh;
    
    for (const auto& data : boxData) {
        const QString boxId = data.value("id").toString();
        LinBox* targetBox = BoxManager::instance()->findBoxById(boxId);
        if (!targetBox) {
            qWarning() << "[applyBoxAssignmentsFromConfig] Box not found:" << boxId;
            continue;
        }

        if (targetBox->isFolderMapped()) {
            continue;
        }
        
        const QStringList files = data.value("files").toStringList();
        qDebug() << "[applyBoxAssignmentsFromConfig] Box" << boxId << "has" << files.size() << "files";
        
        for (const QString& path : files) {
            ListItem* item = nullptr;
            if (!item) {
                DesktopRootItemState* state = findDesktopStateByPath(path);
                if (!state) {
                    qWarning() << "[applyBoxAssignmentsFromConfig] Item not found:" << path;
                    continue;
                }

                qDebug() << "[applyBoxAssignmentsFromConfig] Moving model item to box:" << state->model.displayName
                         << "path:" << state->model.filePath
                         << "to box:" << targetBox;
                DesktopRootItemState stateCopy = *state;
                stateCopy.model.boxId = boxId;
                stateCopy.model.visible = true;
                targetBox->addItemState(stateCopy, false);
                removeDesktopStateByPath(path);
                boxesToRefresh.insert(targetBox);
                continue;
            }
            
            qDebug() << "[applyBoxAssignmentsFromConfig] Moving item to box:" << item->baseName() 
                     << "from parent:" << item->parentWidget() 
                     << "to box:" << targetBox;
            
            // 閻犱礁澧介悿?boxId 妤犵偠娉涢惃?item 閻庡湱鍋ゅ顖毲庣拠鎻掝潱闁?box 濞?
            item->setBoxId(boxId);
            item->hide();
            
            // 濞达綀娉曢弫?addListItem 闁哄鍎查崸濠囧礉閻欏懐绀夐悗鐟板暕缁辨澘顫㈤敐鍥ｂ偓妯兼媼閸撗呮瀭闁绘牕澧庨悰銉╁矗閿濆嫯绀?m_contentArea
            setupItemConnections(item, targetBox);
            DesktopRootItemState boxedState = buildDesktopRootItemState(*item);
            boxedState.model.boxId = boxId;
            boxedState.model.visible = true;
            targetBox->addItemState(boxedState, false);
            boxesToRefresh.insert(targetBox);
            
            qDebug() << "[applyBoxAssignmentsFromConfig] Item moved, new parent:" << item->parentWidget();
        }
    }

    for (LinBox* box : boxesToRefresh) {
        if (!box) continue;
        box->refreshLayout();
        box->raise();
    }
}

QStringList ListView::mappedFolderEntryPaths(const QString& folderPath) const {
    return ListViewMappedBoxHelper::mappedFolderEntryPaths(folderPath, m_showHiddenFiles);
}

void ListView::clearMappedFolderItemsForBox(LinBox* box) {
    ListViewMappedBoxHelper::clearMappedFolderItemsForBox(*this, box);
}

void ListView::loadMappedFolderItemsForBox(LinBox* box) {
    ListViewMappedBoxHelper::loadMappedFolderItemsForBox(*this, box);
}

void ListView::applySavedPositionsToItems(const QMap<QString, QMap<QString, QVariant>>& iconPositions) {
    for (auto it = iconPositions.begin(); it != iconPositions.end(); ++it) {
        ListItem* item = nullptr;
        const QMap<QString, QVariant>& posInfo = it.value();
        const QString boxIdFromConfig = posInfo.value("boxId").toString();
        const QPoint savedPos(posInfo.value("x").toInt(), posInfo.value("y").toInt());
        if (item) {
            const QString resolvedBoxId = !boxIdFromConfig.isEmpty() ? boxIdFromConfig : item->boxId();
            item->setBoxId(resolvedBoxId);
            item->setCustomSortOrder(posInfo.value("ord").toInt());
            item->updateLocationInfo(savedPos);
        }

        if (DesktopRootItemState* state = findDesktopStateByPath(it.key())) {
            state->model.boxId = boxIdFromConfig;
            state->model.customSortOrder = posInfo.value("ord").toInt();
            state->model.position = savedPos;
        } else if (LinBox* box = BoxManager::instance()->findBoxContainingItem(it.key())) {
            if (DesktopRootItemState* boxState = box->findItemStateByPath(it.key())) {
                boxState->model.boxId = boxIdFromConfig;
                boxState->model.customSortOrder = posInfo.value("ord").toInt();
                boxState->model.position = savedPos;
            }
        } else if (!item) {
            qWarning() << "[ListView] No desktop item matched config path:" << it.key();
        }
    }
}

void ListView::loadBoxes(bool autoUpdateState) {
    const ListViewSelectionSnapshot selectionSnapshot = ListViewSelectionSnapshot::capture(*this);

    m_isLoadingPersistence = true;
    m_forceConfigLayout = false;
    m_hasLoadedConfigLayout = false;

    QVector<LinBox*> existingBoxes;
    for (const QPointer<LinBox>& box : BoxManager::instance()->allBoxes()) {
        if (box) existingBoxes.append(box.data());
    }
    for (auto* box : existingBoxes) {
        if (box) {
            if (box->isFolderMapped()) {
                clearMappedFolderItemsForBox(box);
            }
            for (const DesktopRootItemState& boxedState : box->itemStates()) {
                if (!boxedState.model.filePath.isEmpty()) {
                    DesktopRootItemState restoredState = boxedState;
                    restoredState.model.boxId.clear();
                    restoredState.model.visible = true;
                    m_itemStates.push_back(restoredState);
                }
            }
            box->deleteLater();
        }
    }
    BoxManager::instance()->clearBoxes();
    m_mappedBoxFilePaths.clear();
    for (auto* item : m_allItems) {
        if (item) {
            item->setBoxId("");
        }
    }

    LayoutPersistenceService persistenceService;
    LayoutPersistenceState state;
    state.enablePersistence = m_enablePersistence;
    state.viewMode = (int)m_iconShowRule.viewMode;
    state.sortType = (int)m_iconShowRule.sortType;
    state.preAutoSort = (int)m_iconShowRule.sortTypeBeforeAutoArrange;
    state.iconPosMode = (int)m_iconShowRule.iconPosMode;
    state.autoArrange = m_iconShowRule.autoArrange;
    state.gridAlignment = m_iconShowRule.gridAlignment;
    state.showDesktopIcons = m_iconShowRule.showDesktopIcons;
    state.doubleClickHideIcons = m_iconShowRule.doubleClickHideIcons;
    state.showShortCutArrow = m_iconShowRule.showShortCutArrow;

    persistenceService.load(m_persistencePath, autoUpdateState, state);

    m_enablePersistence = state.enablePersistence;
    m_iconShowRule.viewMode = (ViewMode)state.viewMode;
    m_iconShowRule.sortType = (SortOrderType)state.sortType;
    m_iconShowRule.sortTypeBeforeAutoArrange = (SortOrderType)state.preAutoSort;
    m_iconShowRule.iconPosMode = (IconPosMode)state.iconPosMode;
    m_iconShowRule.autoArrange = state.autoArrange;
    m_iconShowRule.gridAlignment = state.gridAlignment;
    m_iconShowRule.showDesktopIcons = state.showDesktopIcons;
    m_iconShowRule.doubleClickHideIcons = state.doubleClickHideIcons;
    m_iconShowRule.showShortCutArrow = state.showShortCutArrow;
    if (m_iconShowRule.autoArrange) {
        if (m_iconShowRule.sortType != SortOrderType::SortByCustom) {
            m_iconShowRule.sortTypeBeforeAutoArrange = m_iconShowRule.sortType;
            m_iconShowRule.sortType = SortOrderType::SortByCustom;
        }
        m_iconShowRule.gridAlignment = true;
        m_iconShowRule.iconPosMode = IconPosMode::AlignIconToGrid;
    }

    QVector<QMap<QString, QVariant>>& boxData = state.boxData;
    QMap<QString, QMap<QString, QVariant>>& iconPositions = state.iconPositions;

    m_forceConfigLayout = !boxData.isEmpty() || !iconPositions.isEmpty();
    m_hasLoadedConfigLayout = m_forceConfigLayout;

    syncViewModeSizes();
    updateRightMenuState();

    for (const auto& data : boxData) {
        createBoxFromSavedData(
            data.value("title").toString(),
            QRect(data.value("x").toInt(), data.value("y").toInt(), 
                  data.value("w").toInt(), data.value("h").toInt()),
            data.value("collapsed").toBool(),
            data.value("h").toInt(),
            data.value("showType").toInt(),
            data.value("files").toStringList(),
            QColor(data.value("color").toString()),
            normalizedSavedBoxOpacity(data.value("opacity")),
            data.value("glass").toBool(),
            data.value("id").toString(),
            data.value("mappedFolderPath").toString(),
            false
        );
    }
    applyBoxAssignmentsFromConfig(boxData);
    applySavedPositionsToItems(iconPositions);

    m_savedIconPositions = persistenceService.toSavedIconPositions(iconPositions);

    loadIconLayer();
    m_forceConfigLayout = false;
    update();
    m_allItems;
    m_items;
    m_isLoadingPersistence = false;
    if (m_iconShowRule.autoArrange) {
        applyAutoArrangeLayout(false, false);
    }
    selectionSnapshot.restore(*this, false);

    qDebug() << "Loaded. Persistence:" << m_enablePersistence;
}

void ListView::createBoxFromSavedData(const QString& title, const QRect& geom, bool collapsed, int origH, int showType, const QStringList& files, const QColor& color, float opacity, bool glass, const QString& explicitId, const QString& mappedFolderPath, bool refresh) {
    LinBox* box = new LinBox(this);
    if (explicitId.isEmpty()) {
        box->setBoxId(QUuid::createUuid().toString());
    } else {
        box->setBoxId(explicitId);
    }
    
    if (!mappedFolderPath.isEmpty()) {
        if (title.isEmpty()) {
            box->setBoxTitle(QString());
        } else {
            box->setBoxTitle(title);
        }
        box->setMappedFolderPath(mappedFolderPath);
    } else if (!title.isEmpty()) {
        box->setBoxTitle(title);
    }
    
    if (color.isValid()) box->setBackgroundColor(color);
    box->setBackgroundOpacity(opacity);
    box->setGlassEffect(glass);
    
    // Connect to BoxManager and ListView
    BoxManager::instance()->setupConnections(box, this);
    BoxManager::instance()->addBox(box);
    
    box->setGeometry(geom);
    box->setShowType((LinBox::BoxShowType)showType);
    if (collapsed) box->toggleCollapse();

    box->show();
    box->setGlassEffect(glass); // Apply blur AFTER window is fully visible to prevent black screen
    if (box->isFolderMapped()) {
        loadMappedFolderItemsForBox(box);
    }
    box->refreshLayout(); // Force initial tight packing
    box->raise();

    if (refresh) {
        loadIconLayer();
        update();
        requestSaveBoxes();
    }
}

void ListView::onMenuDestroyBox(LinBox* box) {
    if (!box) return;

    if (box->isFolderMapped()) {
        if (LinDirectoryMonitor* monitor = box->mappedFolderMonitor()) {
            monitor->stopListening();
            box->setMappedFolderMonitor(nullptr);
        }
        clearMappedFolderItemsForBox(box);
        BoxManager::instance()->destroyBox(box, this);
        update();
        return;
    }

    RenameManager::instance()->cancelRename();
    resetRenameClickState();
    const QVector<DesktopRootItemState> boxStates = box->itemStates();
    for (const DesktopRootItemState& boxedState : boxStates) {
        DesktopRootItemState restoredState = boxedState;
        restoredState.model.boxId.clear();
        restoredState.model.visible = true;
        if (!m_iconShowRule.autoArrange) {
            restoredState.model.position = generateItemPosition();
        }
        m_itemStates.push_back(restoredState);
    }
    
    BoxManager::instance()->destroyBox(box, this);
    applyAutoArrangeAfterDesktopMutation(true, false);
    update();
}

void ListView::onMenuCustomBox(LinBox* box) {
    // Placeholder for color customizations
    qDebug() << "Customize box requested:" << box->boxTitle();
}

void ListView::moveItemToBox(const QString& filePath, LinBox* targetBox, bool refresh, int insertIndex) {
    if (!targetBox || filePath.isEmpty()) {
        return;
    }

    ListItem* item = findItemByPath(filePath);
    LinBox* sourceBox = nullptr;
    if (item && !item->boxId().isEmpty()) {
        sourceBox = BoxManager::instance()->findBoxById(item->boxId());
    }

    DesktopRootItemState stateToMove;
    bool hasStateToMove = false;

    if (item) {
        if (sourceBox) {
            DesktopRootItemState boxedState;
            if (sourceBox->takeItemState(filePath, boxedState, false)) {
                stateToMove = boxedState;
                stateToMove.model.boxId = targetBox->boxId();
                stateToMove.model.visible = true;
                hasStateToMove = true;
            }
        } else {
            ensureModelStateForLegacyItem(item);
            if (DesktopRootItemState* state = findModelStateForLegacyItem(item)) {
                stateToMove = *state;
                stateToMove.model.boxId = targetBox->boxId();
                stateToMove.model.visible = true;
                hasStateToMove = true;
                removeModelStateForLegacyItem(item);
            }
        }

        if (!hasStateToMove) {
            stateToMove = buildDesktopRootItemState(*item);
            stateToMove.model.boxId = targetBox->boxId();
            stateToMove.model.visible = true;
            hasStateToMove = true;
        }
        removeItemNoDelete(item);
    } else if (DesktopRootItemState* desktopState = findDesktopStateByPath(filePath)) {
        stateToMove = *desktopState;
        stateToMove.model.boxId = targetBox->boxId();
        stateToMove.model.visible = true;
        hasStateToMove = true;
        removeDesktopStateByPath(filePath);
    } else if (LinBox* currentBox = BoxManager::instance()->findBoxContainingItem(filePath)) {
        DesktopRootItemState boxedState;
        if (currentBox->takeItemState(filePath, boxedState, false)) {
            stateToMove = boxedState;
            stateToMove.model.boxId = targetBox->boxId();
            stateToMove.model.visible = true;
            hasStateToMove = true;
            sourceBox = currentBox;
        }
    }

    if (!hasStateToMove) {
        return;
    }

    if (insertIndex >= 0) {
        targetBox->insertItemState(stateToMove, insertIndex, false);
    } else {
        targetBox->addItemState(stateToMove, false);
    }

    if (sourceBox && sourceBox != targetBox) {
        sourceBox->refreshLayout();
        sourceBox->update();
    }

    if (targetBox) {
        targetBox->refreshLayout();
        targetBox->update();
        targetBox->raise();
    }
    
    if (refresh) {
        updateAllItemRenderData();
        update();
        requestSaveBoxes();
    }
    
    qDebug() << "Moved item to box (re-parented):" << filePath << "->" << targetBox->boxTitle() << "index=" << insertIndex;
}

void ListView::moveItemsToBox(const QVector<ListItem*>& items, LinBox* targetBox, int insertIndex, bool refresh)
{
    if (!targetBox || items.isEmpty()) {
        return;
    }

    QVector<ListItem*> orderedItems;
    QSet<ListItem*> seenItems;
    QSet<LinBox*> sourceBoxes;
    for (ListItem* item : items) {
        if (!item || seenItems.contains(item)) {
            continue;
        }
        seenItems.insert(item);
        orderedItems.append(item);
        if (!item->boxId().isEmpty()) {
            if (LinBox* sourceBox = BoxManager::instance()->findBoxById(item->boxId())) {
                if (sourceBox != targetBox) {
                    sourceBoxes.insert(sourceBox);
                }
            }
        }
    }

    for (int i = 0; i < orderedItems.size(); ++i) {
        moveItemToBox(orderedItems[i]->filePath(), targetBox, false, insertIndex + i);
    }

    for (LinBox* sourceBox : sourceBoxes) {
        if (!sourceBox) continue;
        sourceBox->refreshLayout();
        sourceBox->update();
    }

    targetBox->refreshLayout();
    targetBox->update();
    targetBox->raise();

    if (refresh) {
        update();
        requestSaveBoxes();
    }
}

QStringList ListView::orderedDraggedPaths(const QStringList& paths) const
{
    QStringList resolvedPaths;
    QSet<QString> seenPaths;
    for (const QString& path : paths) {
        const QString normalizedPath = normalizeDesktopItemPath(path);
        if (normalizedPath.isEmpty() || seenPaths.contains(normalizedPath) || !findAnyItemStateByPath(normalizedPath)) {
            continue;
        }
        seenPaths.insert(normalizedPath);
        resolvedPaths.append(normalizedPath);
    }

    if (resolvedPaths.isEmpty()) {
        return resolvedPaths;
    }

    QSet<QString> sourceBoxIds;
    bool hasDesktopItems = false;
    for (const QString& path : resolvedPaths) {
        if (const DesktopRootItemState* state = findAnyItemStateByPath(path)) {
            if (state->model.boxId.isEmpty()) {
                hasDesktopItems = true;
            } else {
                sourceBoxIds.insert(state->model.boxId);
            }
        }
    }

    QStringList orderedPaths;
    if (sourceBoxIds.size() == 1 && !hasDesktopItems) {
        if (LinBox* sourceBox = BoxManager::instance()->findBoxById(*sourceBoxIds.begin())) {
            for (const DesktopRootItemState& state : sourceBox->itemStates()) {
                const QString normalizedPath = normalizeDesktopItemPath(state.model.filePath);
                if (seenPaths.contains(normalizedPath)) {
                    orderedPaths.append(normalizedPath);
                }
            }
        }
    } else if (sourceBoxIds.isEmpty() && hasDesktopItems) {
        QVector<DesktopRootItemState*> orderedStates = collectAutoArrangeStates();
        for (DesktopRootItemState* state : orderedStates) {
            if (!state) {
                continue;
            }
            const QString normalizedPath = normalizeDesktopItemPath(state->model.filePath);
            if (seenPaths.contains(normalizedPath)) {
                orderedPaths.append(normalizedPath);
            }
        }
    }

    if (!orderedPaths.isEmpty()) {
        return orderedPaths;
    }
    return resolvedPaths;
}

bool ListView::importPathsToBox(const QStringList& sourcePaths, LinBox* targetBox, bool copyMode, int insertIndex, bool preferShortcut)
{
    if (sourcePaths.isEmpty() || !targetBox) {
        return false;
    }

    bool ok = true;
    QStringList createdPaths;

    int nextInsertIndex = insertIndex;
    for (const QString& sourcePath : sourcePaths) {
        if (sourcePath.isEmpty()) {
            continue;
        }

        const QString destinationPath = preferShortcut
            ? ListViewFileOpsHelper::uniqueShortcutPathForTarget(sourcePath, m_desktopDir)
            : ListViewFileOpsHelper::uniquePasteDestinationPath(sourcePath, m_desktopDir);
        const QString normalizedDestination = normalizeDesktopPath(destinationPath);

        bool singleOk = false;
        if (preferShortcut) {
            singleOk = ListViewFileOpsHelper::createShellShortcut(sourcePath, destinationPath);
        } else if (copyMode) {
            singleOk = ListViewFileOpsHelper::copyPathToExactDestination(sourcePath, destinationPath);
        } else {
            singleOk = ListViewFileOpsHelper::movePathToExactDestination(sourcePath, destinationPath);
        }

        if (singleOk) {
            createdPaths.append(normalizedDestination);
            m_pendingCreatedItemBoxes[normalizedDestination] = targetBox;
            if (nextInsertIndex >= 0) {
                m_pendingCreatedItemInsertIndices[normalizedDestination] = nextInsertIndex++;
            }
        } else {
            ok = false;
        }
    }

    return ok && !createdPaths.isEmpty();
}

QStringList ListView::resolveDraggedLocalPaths(const QMimeData* mimeData) const
{
    return ListViewFileOpsHelper::draggedLocalFilePaths(mimeData);
}

bool ListView::resolveItemModelByPath(const QString& filePath, DesktopRootItemModel& modelOut) const
{
    if (const DesktopRootItemState* state = findDesktopStateByPath(filePath)) {
        modelOut = state->model;
        return true;
    }

    if (LinBox* box = BoxManager::instance()->findBoxContainingItem(filePath)) {
        if (const DesktopRootItemState* state = box->findItemStateByPath(filePath)) {
            modelOut = state->model;
            return true;
        }
    }

    return false;
}

QRect ListView::globalRenameEditorRectForPath(const QString& filePath) const
{
    if (const DesktopRootItemState* state = findDesktopStateByPath(filePath)) {
        return QRect(mapToGlobal(state->model.renderData.renameEditorRect.topLeft()),
                     state->model.renderData.renameEditorRect.size());
    }

    if (LinBox* box = BoxManager::instance()->findBoxContainingItem(filePath)) {
        return box->globalRenameEditorRectForPath(filePath);
    }

    return QRect();
}

bool ListView::openItemByPath(const QString& filePath)
{
    DesktopRootItemModel model;
    if (!resolveItemModelByPath(filePath, model)) {
        return false;
    }
    clearCutMarks();
    return openDesktopRootItem(model);
}

bool ListView::showPropertiesForPath(const QString& filePath) const
{
    if (filePath.isEmpty()) {
        return false;
    }

    const QString nativePath = QDir::toNativeSeparators(filePath);
    CSystemApi::File_RightMenuOperations(reinterpret_cast<const TCHAR*>(nativePath.utf16()), Z_PROPERTIES);
    return true;
}

bool ListView::handleDroppedPathsOnModel(const QStringList& sourcePaths, const DesktopRootItemModel& targetModel, bool copyMode, bool preferShortcut)
{
    if (sourcePaths.isEmpty()) {
        return false;
    }

    const QString targetPath = targetModel.filePath;
    const QString clsid = targetModel.itemData.getCLSID();

    if (clsid == APP_MY_COMPUTER_CLS || clsid == APP_NETWORK_CLS || clsid == APP_CONTROL_PANEL_CLS) {
        return false;
    }

    std::vector<std::wstring> srcPaths;
    QStringList args;
    for (const QString& localPath : sourcePaths) {
        if (!localPath.isEmpty()) {
            const QString nativePath = QDir::toNativeSeparators(localPath);
            args << "\"" + nativePath + "\"";
            srcPaths.push_back(nativePath.toStdWString());
        }
    }

    if (srcPaths.empty()) {
        return false;
    }

    const bool isRecycleBin = (clsid == APP_RECYCLE_BIN_CLS);
    if (isRecycleBin) {
        const QStringList orderedPaths = orderedDraggedPaths(sourcePaths);
        QVector<ListItem*> draggedItems;
        draggedItems.reserve(orderedPaths.size());
        for (const QString& draggedPath : orderedPaths) {
            Q_UNUSED(draggedPath);
        }
        if (!draggedItems.isEmpty()) {
            setItemsTransientHidden(draggedItems, true);
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }

        const bool deleteOk = CSystemApi::DeleteFiles(srcPaths, true, true);
        if (!deleteOk) {
            if (!draggedItems.isEmpty()) {
                setItemsTransientHidden(draggedItems, false);
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            }
            return false;
        }

        if (!draggedItems.isEmpty()) {
            trackPendingExternalRemovals(draggedItems);
            removeItemsBatch(draggedItems);
            requestSaveBoxes();
        }
        return true;
    }

    if (targetModel.objectType == ObjectType::Folder_ObjectType ||
        targetModel.objectType == ObjectType::SpecialFolder ||
        targetModel.objectType == ObjectType::DiskLetter) {
        if (preferShortcut) {
            return createShortcutsInFolder(sourcePaths, targetPath);
        }
        if (copyMode) {
            return CSystemApi::CopyFiles(srcPaths, QDir::toNativeSeparators(targetPath).toStdWString());
        }
        return CSystemApi::MoveFiles(srcPaths, QDir::toNativeSeparators(targetPath).toStdWString());
    }

    std::wstring wApp = QDir::toNativeSeparators(targetPath).toStdWString();
    std::wstring wArgs = args.join(" ").toStdWString();
    HINSTANCE result = ShellExecuteW(NULL, L"open", wApp.c_str(), wArgs.c_str(), NULL, SW_SHOWNORMAL);
    return (INT_PTR)result > 32;
}

bool ListView::shouldCreateShortcutForExternalDrop(const QStringList& sourcePaths) const
{
    for (const QString& sourcePath : sourcePaths) {
        if (ListViewFileOpsHelper::shouldCreateShortcutForExternalPath(sourcePath)) {
            return true;
        }
    }
    return false;
}

bool ListView::shouldPreferShortcutForDrop(const QStringList& sourcePaths,
                                           Qt::KeyboardModifiers modifiers,
                                           bool isExternalSource) const
{
    if (modifiers & Qt::AltModifier) {
        return true;
    }

    if (!isExternalSource) {
        return false;
    }

    return shouldCreateShortcutForExternalDrop(sourcePaths);
}

bool ListView::createShortcutsInFolder(const QStringList& sourcePaths, const QString& targetFolder) const
{
    if (sourcePaths.isEmpty() || targetFolder.isEmpty()) {
        return false;
    }

    bool ok = true;
    for (const QString& sourcePath : sourcePaths) {
        if (sourcePath.isEmpty()) {
            continue;
        }
        const QString shortcutPath = ListViewFileOpsHelper::uniqueShortcutPathForTarget(sourcePath, targetFolder);
        ok = ListViewFileOpsHelper::createShellShortcut(sourcePath, shortcutPath) && ok;
    }

    return ok;
}

void ListView::reloadMappedFolderBox(LinBox* box)
{
    const ListViewSelectionSnapshot selectionSnapshot = ListViewSelectionSnapshot::capture(*this);
    loadMappedFolderItemsForBox(box);
    selectionSnapshot.restore(*this, false);
}

void ListView::clearMappedFolderBox(LinBox* box)
{
    clearMappedFolderItemsForBox(box);
}

bool ListView::isItemInAnyBox(const QString& filePath) const {
    return BoxManager::instance()->isItemInAnyBox(filePath);
}

QString ListView::getBoxIdForItem(const QString& filePath) const {
    return BoxManager::instance()->getBoxIdForItem(filePath);
}

LinBox* ListView::findMappedFolderBox(const QString& folderPath, const LinBox* excludeBox) const
{
    return ListViewMappedBoxHelper::findMappedFolderBox(*this, folderPath, excludeBox);
}

QString ListView::dumpAllItemsJson() const
{
    return DesktopItemLoader::dumpDesktopRootStatesJson(m_itemStates);
}

QString ListView::dumpVisibleItemsJson() const
{
    QVector<DesktopRootItemState> visibleStates;
    for (const auto& state : m_itemStates) {
        if (shouldPaintDesktopModelState(state)) {
            visibleStates.push_back(state);
        }
    }
    return DesktopItemLoader::dumpDesktopRootStatesJson(visibleStates);
}

QString ListView::dumpHiddenItemsJson() const
{
    QVector<DesktopRootItemState> hiddenStates;
    for (const auto& state : m_itemStates) {
        if (!state.model.visible || !state.model.boxId.isEmpty()) {
            hiddenStates.push_back(state);
        }
    }
    return DesktopItemLoader::dumpDesktopRootStatesJson(hiddenStates);
}

void ListView::setShortcutArrowVisible(bool visible) {
    m_iconShowRule.showShortCutArrow = visible;
    
    // Immediately refresh all items to show/hide the arrow
    for (auto* item : m_allItems) {
        if (item) item->initData();
    }
    
    requestSaveBoxes();
}

void ListView::loadPersistentLayout() {
    const bool useConfigLayout = m_isLoadingPersistence || m_forceConfigLayout;
    QVector<ListItem*> allItemsSnapshot = m_items;
    QSet<LinBox*> boxesToRefresh;

    if (useConfigLayout) {
        for (auto* item : allItemsSnapshot) {
            if (!item || !item->boxId().isEmpty()) {
                continue;
            }

            const QString normalizedPath = normalizeDesktopPath(item->filePath());
            if (!m_savedIconPositions.contains(normalizedPath)) {
                item->updateLocationInfo(QPoint(0, 0));
                item->move(0, 0);
            }
        }
    }

    //for (auto box : BoxManager::instance()->allBoxes()) {
    //    if (!box) continue;
    //    QVector<ListItem*> boxItems = box->items();
    //    for (auto* item : boxItems) {
    //        if (item) {
    //            box->removeListItem(item);
    //        }
    //    }
    //}

    for (auto* item : allItemsSnapshot) {
        if (!item) continue;

        const QString savedBoxId = item->boxId();
        if (!savedBoxId.isEmpty()) {
            LinBox* targetBox = BoxManager::instance()->findBoxById(savedBoxId);
            if (targetBox) {
                item->setBoxId(savedBoxId);
                item->hide();
                setupItemConnections(item, targetBox);
                DesktopRootItemState boxedState = buildDesktopRootItemState(*item);
                boxedState.model.boxId = savedBoxId;
                boxedState.model.visible = true;
                targetBox->addItemState(boxedState, false);
                boxesToRefresh.insert(targetBox);
                continue;
            }
        }

        item->setItemSize(m_iconWidth, m_iconHeight);
        item->initData();
        item->setLayoutMode(ListItem::LayoutMode::IconMode);
        // Note: Do NOT set parent here. It will be set later based on boxId.
        setupItemConnections(item);

        // 婵☆偀鍋撻柡?item 闁哄嫷鍨伴幆浣割啅閸欏绠掗柡鍫濐槹閺呫儲鎷呭鍥╂瀭
        QPoint currentPos = item->getLocationInfo();
        const QString normalizedPath = normalizeDesktopPath(item->filePath());
        const bool hasPersistedPosition = !useConfigLayout || m_savedIconPositions.contains(normalizedPath);
        bool hasValidPosition = hasPersistedPosition && (currentPos != QPoint(0, 0));
        
        if (!hasValidPosition) {
            QPoint pos = useConfigLayout ? generateItemPosition() : currentPos;
            if (pos == QPoint(0, 0)) {
                pos = generateItemPosition();
            }
            item->updateLocationInfo(pos);
            item->move(pos);
            continue;
        }

        const QPoint savedPos = item->getLocationInfo();
        if (savedPos.x() <= 0 || savedPos.y() <= 0) {
            QPoint newPos = generateItemPosition();
            item->updateLocationInfo(newPos);
            item->move(newPos);
        } else {
            item->updateLocationInfo(savedPos);
            item->move(savedPos);
        }
        // Set parent to ListView for desktop items
        item->setParent(this);
    }

    for (LinBox* box : boxesToRefresh) {
        if (!box) continue;
        box->refreshLayout();
        box->raise();
    }
}
void ListView::setPersistenceEnabled(bool enabled) {
    if (m_enablePersistence == enabled) return;
    m_enablePersistence = enabled;
    
    if (m_enablePersistence) {
        loadBoxes(false); // Don't let the file turn us back off!
        loadIconLayer();
    } else {
        m_hasLoadedConfigLayout = false;
        // Destroy all boxes and restore items to desktop
        for (auto box : BoxManager::instance()->allBoxes()) {
            if (!box) continue;
            const QVector<DesktopRootItemState> boxStates = box->itemStates();
            for (const DesktopRootItemState& boxedState : boxStates) {
                DesktopRootItemState restoredState = boxedState;
                restoredState.model.boxId.clear();
                restoredState.model.visible = true;
                m_itemStates.push_back(restoredState);
            }
            box->deleteLater();
        }
        BoxManager::instance()->clearBoxes();
        loadIconLayer();
    }
    
    // Always commit state change to file
    saveBoxes();

    if (!enabled) {
        FluentDialog* dlg = new FluentDialog(nullptr);
        dlg->setDialogTitle(tr("Desktop Organization"));
        dlg->setMessage(tr("Do you want to organize the current desktop icon layout now?"));
        dlg->addActionButton(tr("Organize Now"), FluentDialog::ActionType::Primary, [this]() {
            this->setPersistenceEnabled(true);
            this->onMenuOrganizeDesktop();
        });
        dlg->addActionButton(tr("Cancel"), FluentDialog::ActionType::Close);
        dlg->show();
        dlg->raise();
        dlg->activateWindow();
    }
}

void ListView::updateRightMenuState() {
    if (!rightMenu) return;
    rightMenu->setLargeIconsChecked(m_iconShowRule.viewMode == ViewMode::LargeIcon);
    rightMenu->setMediumIconsChecked(m_iconShowRule.viewMode == ViewMode::MediumIcon);
    rightMenu->setSmallIconsChecked(m_iconShowRule.viewMode == ViewMode::SmallIcon);
    
    rightMenu->setShowDesktopIconsChecked(m_iconShowRule.showDesktopIcons);
    rightMenu->setAutoArrangeIconsChecked(m_iconShowRule.autoArrange);
    rightMenu->setAlignIconsToGridChecked(m_iconShowRule.gridAlignment);
    rightMenu->setDoubleClickedHideIconsChecked(m_iconShowRule.doubleClickHideIcons);
    
    const SortOrderType visibleSortType =
        (m_iconShowRule.autoArrange && m_iconShowRule.sortType == SortOrderType::SortByCustom)
            ? m_iconShowRule.sortTypeBeforeAutoArrange
            : m_iconShowRule.sortType;

    if (visibleSortType == SortOrderType::Default) {
        rightMenu->setSortByNameChecked(false);
        rightMenu->setSortBySizeChecked(false);
        rightMenu->setSortByTypeChecked(false);
        rightMenu->setSortByDateChecked(false);
    } else {
        rightMenu->setSortByNameChecked(visibleSortType == SortOrderType::SortByName_ASC || visibleSortType == SortOrderType::SortByName_DESC);
        rightMenu->setSortBySizeChecked(visibleSortType == SortOrderType::SortBySize_ASC || visibleSortType == SortOrderType::SortBySize_DESC);
        rightMenu->setSortByTypeChecked(visibleSortType == SortOrderType::SortByType_ASC || visibleSortType == SortOrderType::SortByType_DESC);
        rightMenu->setSortByDateChecked(visibleSortType == SortOrderType::SortByDateModified_ASC || visibleSortType == SortOrderType::SortByDateModified_DESC);
    }
}
