#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDataStream>
#include <QFile>
#include <QSet>
#include <QCoreApplication>
#include <QSettings>
#include "desktop/ListView.h"
#include "desktop/ListView_Helpers.h"
#include "desktop/ListViewSelectionHelper.h"
#include "desktop/ListViewSelectionController.h"
#include "desktop/ListViewDragDropHelper.h"
#include "desktop/LinDesktop.h"
#include "desktop/widgets/linwidget/WidgetPackage.h"
#include "desktop/widgets/linwidget/WidgetSettingsDialog.h"
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
#include <QScreen>
#include <QClipboard>
#include <QCursor>
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
#include <QInputDialog>
#include <QMenu>
#include <QMoveEvent>
#include <QRegion>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>
#include <climits>
#include <shobjidl.h>
#include "services/SystemApi/resources.h"
#include "ui/Fluent/FluentDialog.h"
#include "ui/Fluent/FluentInfoTip.h"
#include "desktop/ListViewFileOpsHelper.h"
#include "desktop/ListViewSelectionSnapshot.h"
#include "box/LinBox.h"
#include "plugins/PluginManager.h"
#include "services/HotkeyManager.h"
#include "services/AppPaths.h"
#include "services/IconCacheManager.h"
#include "services/SpaceLookIpcClient.h"
#include "services/DpiScaleHelper.h"
#include "box/BoxManager.h"
#include "desktop/ListViewDebugPanel.h"
#include "desktop/DesktopSnakeGame.h"
#include "desktop/widgets/linwidget/WidgetManager.h"
#include "desktop/widgets/linwidget/WidgetRegistryManager.h"
#include "desktop/widgets/cef/LinWidgetCanvasWidget.h"




static LinDesktop* findLinDesktopAncestor(QWidget* w)
{
    while (w) {
        if (LinDesktop* desk = qobject_cast<LinDesktop*>(w)) {
            return desk;
        }
        w = w->parentWidget();
    }
    return nullptr;
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
        return scalePx(1) + iconSide + scalePx(kDesktopIconTextGap) + qMax(lineHeight, lineHeight * 2) + scalePx(2);
    }

    if (m_iconShowRule.viewMode == ViewMode::LargeIcon) {
        const int iconSide = scalePx(96);
        const int lineHeight = QFontMetrics(AppFonts::uiFont(9)).lineSpacing();
        return scalePx(1) + iconSide + scalePx(kDesktopIconTextGap) + qMax(lineHeight, lineHeight * 2) + scalePx(2);
    }

    const int iconSide = scalePx(48);
    const int lineHeight = QFontMetrics(AppFonts::uiFont(9)).lineSpacing();
    return scalePx(1) + iconSide + scalePx(kDesktopIconTextGap) + qMax(lineHeight, lineHeight * 2) + scalePx(2);
}

QVector<QRect> ListView::desktopLinWidgetOccupiedRects() const
{
    QVector<QRect> rects;
    if (!m_linWidgetManager || !m_linWidgetManager->isEnabled() || !m_linWidgetManager->isCefOsrEnabled()) {
        return rects;
    }

    for (const LinWidget::WidgetLayoutState& state : m_linWidgetManager->layoutStates()) {
        if (!state.visible || state.geometry.isEmpty() || !state.geometry.isValid()) {
            continue;
        }
        if (!m_linWidgetManager->hasRunningWidget(state.instanceId)) {
            continue;
        }
        rects.append(state.geometry);
    }
    return rects;
}

QSet<quint64> ListView::desktopLinWidgetOccupiedCells() const
{
    QSet<quint64> cells;
    const QVector<QRect> widgetRects = desktopLinWidgetOccupiedRects();
    if (widgetRects.isEmpty()) {
        return cells;
    }

    const int itemHeight = desktopItemFootprintHeight();
    const int strideX = qMax(1, m_iconWidth + m_colSpacing);
    const int strideY = qMax(1, itemHeight + m_rowSpacing);
    const int layoutLeft = desktopLayoutLeft();
    const int layoutTop = desktopLayoutTop();
    const int maxRows = qMax(1, maxGridRowsForBounds(layoutTop, desktopUsableBottom(), itemHeight, strideY));
    const int maxCols = qMax(1, maxGridColsForBounds(layoutLeft, desktopUsableRight(), m_iconWidth, strideX));

    const QSize itemSize(m_iconWidth, itemHeight);
    for (const QRect& widgetRect : widgetRects) {
        for (int x = 0; x < maxCols; ++x) {
            for (int y = 0; y < maxRows; ++y) {
                const QRect itemRect(gridCellToPoint(x, y, layoutLeft, layoutTop, strideX, strideY), itemSize);
                const QRect overlap = itemRect.intersected(widgetRect);
                if (overlap.isEmpty()) {
                    continue;
                }
                const double xOverlapRatio = static_cast<double>(overlap.width()) / static_cast<double>(qMax(1, itemSize.width()));
                const double yOverlapRatio = static_cast<double>(overlap.height()) / static_cast<double>(qMax(1, itemSize.height()));
                if (xOverlapRatio <= kLinWidgetCellBlockThreshold || yOverlapRatio <= kLinWidgetCellBlockThreshold) {
                    continue;
                }
                cells.insert(packGridCell(x, y));
            }
        }
    }

    return cells;
}

QVector<QRect> ListView::desktopBlockedOccupiedRects() const
{
    return desktopLinWidgetOccupiedRects();
}

QSet<quint64> ListView::desktopBlockedOccupiedCells() const
{
    return desktopLinWidgetOccupiedCells();
}

void ListView::paintLinWidgetBlockedCellsOverlay(QPainter& painter, const QRegion& dirtyRegion) const
{
    if (!m_showLinWidgetBlockedCells) {
        return;
    }

    const QSet<quint64> cells = desktopLinWidgetOccupiedCells();
    if (cells.isEmpty()) {
        return;
    }

    const int itemHeight = desktopItemFootprintHeight();
    const int strideX = qMax(1, m_iconWidth + m_colSpacing);
    const int strideY = qMax(1, itemHeight + m_rowSpacing);
    const int layoutLeft = desktopLayoutLeft();
    const int layoutTop = desktopLayoutTop();
    const int maxRows = qMax(1, maxGridRowsForBounds(layoutTop, desktopUsableBottom(), itemHeight, strideY));
    const int maxCols = qMax(1, maxGridColsForBounds(layoutLeft, desktopUsableRight(), m_iconWidth, strideX));

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(220, 38, 38, 190), 1));
    painter.setBrush(QColor(239, 68, 68, 58));

    for (int x = 0; x < maxCols; ++x) {
        for (int y = 0; y < maxRows; ++y) {
            if (!cells.contains(packGridCell(x, y))) {
                continue;
            }

            const QRect cellRect(gridCellToPoint(x, y, layoutLeft, layoutTop, strideX, strideY),
                                 QSize(m_iconWidth, itemHeight));
            if (!dirtyRegion.intersects(cellRect)) {
                continue;
            }

            painter.drawRoundedRect(cellRect.adjusted(1, 1, -1, -1), 5, 5);
        }
    }

    painter.setPen(QPen(QColor(14, 165, 233, 230), 2));
    painter.setBrush(QColor(14, 165, 233, 34));
    for (const QRect& widgetRect : desktopLinWidgetOccupiedRects()) {
        if (!dirtyRegion.intersects(widgetRect)) {
            continue;
        }
        painter.drawRoundedRect(widgetRect.adjusted(1, 1, -1, -1), 6, 6);
    }

    painter.restore();
}

QPoint ListView::resolveNearestGridCreatePosition(const QPoint& requestedPos, const QSet<quint64>& occupiedSlots) const
{
    return ListViewDragDropHelper::resolveNearestGridCreatePosition(*this, requestedPos, occupiedSlots);
}

QPoint ListView::resolveNearestFreeCreatePosition(const QPoint& requestedPos) const
{
    return ListViewDragDropHelper::resolveNearestFreeCreatePosition(*this, requestedPos);
}

QPoint ListView::localPosFromGhostWidget(const QWidget* ghostWidget) const
{
    if (!ghostWidget) {
        return QPoint();
    }
    return mapFromGlobal(ghostWidget->pos());
}

bool ListView::rectIntersectsDesktopBlockedArea(const QRect& rect) const
{
    for (const QRect& componentRect : desktopBlockedOccupiedRects()) {
        if (rect.intersects(componentRect)) {
            return true;
        }
    }
    return false;
}

QPoint ListView::resolveNearestGridAreaForDrag(const QVector<QPoint>& relativeCells,
                                              const QPoint& preferredTopLeftCell,
                                              const QSet<quint64>& occupiedSlots) const
{
    return ListViewDragDropHelper::resolveNearestGridAreaForDrag(*this, relativeCells, preferredTopLeftCell, occupiedSlots);
}

QMap<QString, QPoint> ListView::resolveDesktopDragFinalPositions(const QStringList& draggedPaths,
                                                                 const QPoint& releasePos,
                                                                 const QHash<QString, QPoint>& previewPositions) const
{
    return ListViewDragDropHelper::resolveDesktopDragFinalPositions(*this, draggedPaths, releasePos, previewPositions);
}


// 闂傚倸鍊搁悧濠囨偡閿曞倸鐒垫い鎴ｆ硶閸斿秹鏌涢妸锕€鍝虹€规洏鍔岃灒闁告繂瀚禒瀛樼節濞堝灝鏋涙い鎴濇噽閳?
ListView* ListView::m_instance = nullptr;
int ListItemData::s_targetIconSize = ListItemData::calculateIconSourceTier(ListView::kMediumIconW);

ListView::ListView(QWidget* parent) : QWidget(parent),
    m_viewModel(new ListViewModel(this)),
    m_itemStates(m_viewModel->itemStates()),
    m_selectedDesktopPathSet(m_viewModel->selectedDesktopPathSet()),
    m_selectedDesktopPaths(m_viewModel->selectedDesktopPaths()),
    m_focusedDesktopPath(m_viewModel->focusedDesktopPath()),
    m_anchorDesktopPath(m_viewModel->anchorDesktopPath()),
    m_lastRightClickPos(-1, -1),
    m_forwardingMouseToWidget(false)
{
    setAcceptDrops(true);
    // 闂佽崵濮崇粈浣规櫠娴犲鍋柛鈩冾殢濞存牜鈧厜鍋撻柛鏇ㄥ墯閵嗗啴姊洪悡搴℃毐闁哄牜鍓欓埢宥呪枎閹惧磭鍊為梺鍛婃寙閸愮偓姣?
    //SystemParametersInfo(SPI_SETDESKWALLPAPER, 0, (PVOID)imagePath.toStdString().c_str(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);


    // 闂佽崵濮崇粈浣规櫠娴犲鍋柛鈩冪☉缁秹鏌ｅΟ鎸庮仩闁绘繆鍩栨穱濠偽旈埀顒€螞濞戙垺鍋橀柕澶嗘櫅閻?
    // 闂佽崵濮崇粈浣规櫠娴犲鍋柛鈩冪⊕閻掑鐓崶銊︹拹妞ゎ偅鐗犻弻銈夊礃閸欏鎷卞┑?
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    // 闂佽崵濮崇粈浣规櫠娴犲鍋柛鈩冪⊕閻掑鐓崶銊︹拹妞ゎ偅鐗楅〃銉╂倷閼哥數銆愰梺鍛娚戦崹褰掑箯鐎ｎ喖绠瑰ù锝嗗絻娴滈箖鐓崶銊︾殤缂佺姵鐗滅槐鎺楀箛椤撗勭暥閻熸粎澧楅惄顖炲箚閸モ斁鍋撻敐搴濈盎闁绘挸鍊块弻娑橆潩椤掑嫬寮板┑鐐烘？閸楁娊骞嗛崘顔肩妞ゆ劑鍨洪弳銉╂煟?
    //setAttribute(Qt::WA_TransparentForMouseEvents); // 闂備胶顭堢换瀣归崶顒夋晩闊洦绋掗崐鐑芥煟閻斿憡绶叉慨锝咃躬濮婃椽顢曢姀銏㈩啈缂備浇顕ч崯鎵垝閿濆憘鏃堝焵椤掑啨浜?
    //setAttribute(Qt::WA_OpaquePaintEvent);  // 闂備礁鎲￠崙褰掑垂閹惰棄鏋侀柕鍫濐槹閸ゅ嫰鎮橀悙闈涗壕闁绘繍鍨跺娲敃閵忋垻顔夊┑鈥冲级閸ㄥ灝鐣?

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
    
    // 闂備礁鍚嬮崕鎶藉床閼艰翰浜归柛銉墯閸庢垿鏌ｉ弮鍫闁哄绮欓弻锝夊Ω閵夈儺浠惧銈冨灪閸ㄥ灝螞閸愵喖顫呴柨婵嗘川閸炴挳鎮?
    QString desktopPath;
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &path))) {
        desktopPath = QString::fromWCharArray(path);
        CoTaskMemFree(path);
    } else {
        // 闂傚倸鍊哥€氼剛绮旈懜娈挎闁搞儺鍓欏Λ姗€鎮峰▎蹇擃伌闁衡偓閹惰姤鐓ユ繛鎴炵懅閻擃垰鈹戦悙鍙夊枠闁诡喕绮欓崺澶愩€冪粻鍍dardPaths
        desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    }
    m_desktopDir = desktopPath; // 濠电儑绲藉ú锔炬崲閸岀偞鍋ら柕濞炬櫅缁€鍡涙煟濡も偓閻楀棝鎮橀崶顒佺厱婵炴垶顏鍥ヤ汗濠㈣埖鍔栭悡?
    
    qDebug() << "闂備胶鍎甸弲婊堝垂閸撲焦鏆滈悹杞拌濞存牜鈧厜鍋撻柛鏇ㄥ墯閵嗗啴鏌ｉ悩杈劅闁搞劍澹嗗▎?" << desktopPath;
    
    // 婵犵數鍋涢ˇ顓㈠礉瀹€鍕埞濞寸姴顑嗛崵濠冦亜韫囨挸顏柡鍡樻緲閳藉寮▎鐐ㄩ柣鐐村嚬閸嬪﹤鐣烽崷顓涘亾閿濆懎顣崇紒鈧径鎰厱闊洤瀛╃粊鐗堛亜閺傝法绠抽柟鐟板婵℃瓕顦叉繛鍫㈠Х缁辨帞鈧綆浜堕崕鏃€淇婄拠褏鐣电€规洘鑹鹃—鍐矗閵壯呯暰闂備礁鎲￠悷銊ャ€掗崷顓燁潟闁割偅娲橀悞?
    qRegisterMetaType<FileSystemEventInfo>("FileSystemEventInfo");
    
    // 濠电偠鎻紞鈧繛澶嬫礋瀵偊藟閻闂備胶鍎甸弲鈺呭窗閺嶎偆绀婄€广儱顦伴崕搴ㄦ煙鐎电浠滃ù鐘筹耿閺?
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
    m_pendingShellNewTargetTimer->setInterval(30000);
    connect(m_pendingShellNewTargetTimer, &QTimer::timeout, this, &ListView::clearPendingShellNewTargetBox);
    m_backgroundSelectionThrottleTimer = new QTimer(this);
    m_backgroundSelectionThrottleTimer->setSingleShot(true);
    connect(m_backgroundSelectionThrottleTimer, &QTimer::timeout, this, &ListView::flushBackgroundSelectionUpdate);
    m_snakeGameController = new DesktopSnakeGameController(this);
    m_persistencePath = AppPaths::desktopDataFilePath();
    
    // Load view mode configuration before initializing desktop item models.
    // This keeps icon sizing and render state consistent during startup.

    this->initialize();
    this->initRightMenuConnections();
    this->syncViewModeSizes(); // Call syncViewModeSizes after initialization
    
    // Initialize BoxManager
    BoxManager::instance(this);
    BoxManager::instance()->setPersistencePath(m_persistencePath);
    connect(BoxManager::instance(), &BoxManager::saveRequested, this, &ListView::saveBoxes);
    this->hide();
    // Defer loading boxes to ensure the event loop is running and windows can be properly shown/mapped
    QTimer::singleShot(100, [this](){
        this->loadBoxes(true);
        const QString configPath = AppPaths::configFilePath();
        QSettings settings(configPath, QSettings::IniFormat);
        const bool isFirstLaunch = settings.value("Firslanuch", true).toBool();
        if (isFirstLaunch) {
            this->setPersistenceEnabled(true); 
            settings.setValue("Firslanuch", false);
            settings.sync();
        }
        this->onMenuRefresh();
        this->raise();
        this->initializeLinWidgets();

    });
}

LinWidget::WidgetManager* ListView::linWidgetManager()
{
    if (!m_linWidgetManager) {
        m_linWidgetManager = new LinWidget::WidgetManager(this);
        setAttribute(Qt::WA_InputMethodEnabled, true);
        m_linWidgetManager->setDesktopLayerWindow(winId());
        connect(m_linWidgetManager, &LinWidget::WidgetManager::widgetSettingsRequested,
                this, &ListView::openLinWidgetSettings);
        connect(m_linWidgetManager, &LinWidget::WidgetManager::imeCursorRectChanged,
                this, [this](const QRect&) {
                    updateMicroFocus();
                });
    } else if (m_linWidgetManager->desktopLayerWindow() != winId()) {
        m_linWidgetManager->setDesktopLayerWindow(winId());
    }
    return m_linWidgetManager;
}

void ListView::openLinWidgetSettings(const QString& instanceId)
{
    LinWidget::WidgetManager* manager = linWidgetManager();
    if (!manager || instanceId.trimmed().isEmpty()) {
        return;
    }

    const LinWidget::WidgetInstance instance = manager->widgetInstance(instanceId.trimmed());
    if (instance.instanceId.isEmpty()) {
        qWarning() << "[LinWidgetSettings] instance not found" << instanceId;
        return;
    }

    LinWidget::WidgetManifest manifest;
    QString errorMessage;
    if (!LinWidget::WidgetPackage::loadManifest(instance.packagePath, &manifest, &errorMessage)) {
        qWarning() << "[LinWidgetSettings] manifest load failed" << instanceId << errorMessage;
        return;
    }

    const QString settingsPath = manifest.settings.trimmed();
    if (settingsPath.isEmpty() || !QFileInfo::exists(QDir(instance.packagePath).filePath(settingsPath))) {
        qWarning() << "[LinWidgetSettings] settings page missing" << instanceId << settingsPath;
        return;
    }

    auto* dialog = LinWidget::WidgetSettingsDialog::openOrCreate(
        instance.packagePath,
        manifest,
        instance.grantedPermissions,
        instance.isDevelopment,
        instance.instanceId,
        this,
        &errorMessage);
    if (dialog) {
        connect(dialog, &QDialog::finished, this, [this, instanceId]() {
            if (LinWidget::WidgetManager* manager = linWidgetManager()) {
                manager->restartWidget(instanceId);
            }
        }, Qt::UniqueConnection);
    }
    if (!errorMessage.isEmpty()) {
        qWarning() << "[LinWidgetSettings]" << errorMessage;
    }
}

void ListView::setShowLinWidgetBlockedCells(bool show)
{
    if (m_showLinWidgetBlockedCells == show) {
        return;
    }
    m_showLinWidgetBlockedCells = show;
    update();
}

void ListView::ensureBundledLinWidgetsInstalled()
{
    // Do not auto-install any bundled widgets on first launch.
    // The user can install them via the Settings UI's Official widgets panel.
    const QString targetRoot = AppPaths::userLinWidgetsDir();
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

void ListView::initializeLinWidgets()
{
    ensureBundledLinWidgetsInstalled();

    QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
    LinWidget::WidgetManager* manager = linWidgetManager();
    const bool linWidgetsEnabled = settings.value("LinWidgets/Enabled", true).toBool();
    manager->setEnabled(linWidgetsEnabled);
    manager->setShowWidgetBounds(settings.value("LinWidgets/ShowWidgetBounds", false).toBool());
    setShowLinWidgetBlockedCells(settings.value("LinWidgets/ShowBlockedCells", false).toBool());
    setAttribute(Qt::WA_InputMethodEnabled, true);

    auto& registry = LinWidget::WidgetRegistryManager::instance();
    QString registryError;
    registry.load(&registryError);
    registry.syncFromFolder(AppPaths::userLinWidgetsDir(), &registryError);
    registry.save(&registryError);

    if (!linWidgetsEnabled) {
        qDebug() << "[LinWidgetAutoLoad] runtime disabled";
        return;
    }

    QString restoreError;
    if (!manager->restoreFromConfig(QString(), &restoreError) && !restoreError.isEmpty()) {
        qDebug() << "[LinWidgetAutoLoad] restore failed" << restoreError;
    }
}

void ListView::syncLinWidgetLayer()
{
    if (!m_linWidgetManager) {
        return;
    }

#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (hwnd) {
        LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
        LONG_PTR newStyle = style & ~WS_CLIPSIBLINGS;

        if (style != newStyle) {
            SetWindowLongPtr(hwnd, GWL_STYLE, newStyle);
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }
    }
#endif

    m_linWidgetManager->syncAllWidgetWindowLayers();
}

void ListView::showDebugPanel()
{
    if (!m_debugPanel) {
        m_debugPanel = new ListViewDebugPanel(this);
        m_debugPanel->setAttribute(Qt::WA_DeleteOnClose, true);
    }

    m_debugPanel->show();
    m_debugPanel->raise();
    m_debugPanel->activateWindow();
}

void ListView::updateSystemIconVisibility(const QString& clsid, bool hidden)
{
    qDebug() << "updateSystemIconVisibility:" << clsid << "->" << (hidden ? "hiding" : "showing");
    
    // 闂佽崵鍠愰悷銉р偓姘煎灣閳ь剙鐏氶悧鐘茬暦閻樿瀚夐柛銏°€桰D闂備礁鎼粔鍫曞储瑜忓Σ鎰版晸閻樿櫕娅栭梺鍓插亐閹筹綁鍩￠崨顔规寖闂侀潧鐗嗛幊搴ｆ媼閺屻儲鐓犻悗鍦У椤ュ鏌ｆ幊閸旀垵鐣烽悜钘壩╃憸搴ㄦ偩?:闂備礁鎲￠幐鍝ョ矓閸撲焦顫曢柟杈鹃檮閺?
    QString normalizedClsid = clsid;
    normalizedClsid.replace("::", "");

    if (monitor) {
        sysIconRegisterInfo = monitor->getCurrentIcons();
    }

    DesktopItemInitContext context = currentDesktopItemInitContext();
    for (auto& state : m_itemStates) {
        QString itemClsid = state.model.itemData.getCLSID();
        itemClsid.replace("::", "");
        if (itemClsid != normalizedClsid) {
            continue;
        }

        DesktopRootItemModel updatedModel;
        initDesktopRootItemModel(updatedModel, state.model.itemData, context);
        updatedModel.layoutMode = state.model.layoutMode;
        updatedModel.position = state.model.position;
        updatedModel.baseSize = state.model.baseSize;
        updatedModel.effectiveSize = state.model.effectiveSize;
        updatedModel.visibleTextHeight = state.model.visibleTextHeight;
        updatedModel.isSelected = state.model.isSelected;
        updatedModel.isHovered = state.model.isHovered;
        updatedModel.focused = state.model.focused;
        updatedModel.isCutMarked = state.model.isCutMarked;
        updatedModel.isFullNameExpanded = state.model.isFullNameExpanded;
        updatedModel.customSortOrder = state.model.customSortOrder;
        updatedModel.expandedPreviewText = state.model.expandedPreviewText;
        updatedModel.isRenaming = state.model.isRenaming;
        updatedModel.isReady = true;
        if (hidden) {
            updatedModel.visible = false;
        }
        state.model = updatedModel;
        markDesktopRenderDataDirty(&state);
    }

    updateAllItemRenderData();
    rebuildSpatialIndex();
    loadIconLayer();
    update();
    return;
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
    // 闁诲孩顔栭崰鎺楀磻閹炬枼鏀芥い鏃傗拡閸庢垶绻涢幓鎺戞殻鐎?
    if (monitor->startListening()) {
        sysIconRegisterInfo = monitor->getCurrentIcons();

        /*for (const auto& icon : monitor->getCurrentIcons()) {
            qDebug() << icon.name << ":" << (icon.isHidden ? "hiddening" : "showing");
        }*/

        // 闂佽崵濮崇粈浣规櫠娴犲鍋柛鈩冪☉閻愬﹪鏌ｉ幇顒傛憼闁哄棭浜炵槐鎺楀籍閳ь剟鎮烽埡浣叉灁闁挎繂顦扮€?
        //monitor->setIconVisibility("{645FF040-5081-101B-9F08-00AA002F954E}", true);
    }
    // 闂佸搫顦弲婵嬪磻閻愬灚鏆滈悗娑欘焽閳瑰秹鏌嶉妷銉ユ毐濠?
    connect(monitor, &DesktopInfoMonitor::iconVisibilityChanged,
        this, &ListView::updateSystemIconVisibility);
}

bool ListView::nativeEvent(const QByteArray& eventType, void* message, long* result)
{
    Q_UNUSED(eventType);
    MSG* msg = static_cast<MSG*>(message);
    if (msg && msg->message == WM_NCHITTEST) {
        QPoint globalPos = QCursor::pos();
        QPoint clientPos = mapFromGlobal(globalPos);

        if (m_linWidgetManager) {
            // Ordinary widgets (Clock, Weather, Notes, etc.) always pass through,
            // EXCEPT if we are using composition windowless hosting where ListView needs to forward the input!
            if (m_linWidgetManager->isWidgetAt(clientPos)) {
                if (result) {
                    *result = HTCLIENT;
                }
                return true;
            }

            const bool isWallpaperActive = m_linWidgetManager->hasActiveWallpaper();
            const bool isWallpaperInteractive = isWallpaperActive && m_linWidgetManager->isWallpaperInteractive();

            if (isWallpaperInteractive) {
                // Keep HTCLIENT if cursor is over a desktop icon
                if (findDesktopStateAt(clientPos)) {
                    // Fall through to default HTCLIENT handling
                } else {
                    // Check if cursor is over any LinBox child widget
                    bool overBox = false;
                    for (LinBox* box : boxes()) {
                        if (!box || !box->isVisible()) {
                            continue;
                        }
                        // mapFromGlobal to get position relative to the box
                        const QPoint boxLocal = box->mapFromGlobal(globalPos);
                        if (box->rect().contains(boxLocal)) {
                            overBox = true;
                            break;
                        }
                    }
                    if (!overBox) {
                        // Empty desktop space with active interactive wallpaper: pass through
                        if (result) {
                            *result = HTTRANSPARENT;
                        }
                        return true;
                    }
                }
            }
        }
    }

    if (msg && msg->message == WM_HOTKEY) {
        handleHotkey(static_cast<int>(msg->wParam));
        if (result) {
            *result = 0;
        }
        return true;
    }
    if (msg && (msg->message == WM_SHOWWINDOW ||
                msg->message == WM_WINDOWPOSCHANGED ||
                msg->message == WM_ACTIVATE ||
                msg->message == WM_ACTIVATEAPP)) {
        QTimer::singleShot(0, this, [this]() {
            syncLinWidgetLayer();
        });
    }

    return QWidget::nativeEvent(eventType, message, result);
}

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
    resetRenameClickState();
    
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
                        state.model.renderData.hovered = state.model.isHovered;
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
    ListViewSelectionController::clearSelectionState(*this);
}

void ListView::setSelectedItemPaths(const QStringList& filePaths, const QString& focusedPath, bool expandSingle)
{
    ListViewSelectionController::setSelectedItemPaths(*this, filePaths, focusedPath, expandSingle);
}

void ListView::setSelectedItems(const QStringList& filePaths, bool expandSingle)
{
    ListViewSelectionController::setSelectedItems(*this, filePaths, expandSingle);
}

void ListView::applySelectionState(const QStringList& filePaths, const QString& focusedPath, bool expandSingle)
{
    ListViewSelectionController::applySelectionState(*this, filePaths, focusedPath, expandSingle);
}

void ListView::setItemFullNameExpanded(const QString& filePath, bool expanded)
{
    if (DesktopRootItemState* state = findDesktopStateByPath(filePath)) {
        if (state->model.isFullNameExpanded == expanded &&
            (expanded || state->model.expandedPreviewText.isEmpty())) {
            return;
        }

        const QRect oldDirty = desktopRootItemHighlightDirtyRect(state->model.renderData)
            .united(state->model.renderData.itemRect);
        state->model.isFullNameExpanded = expanded;
        if (!expanded) {
            state->model.expandedPreviewText.clear();
        }
        markDesktopRenderDataDirty(state);
        updateAllItemRenderData();
        update(oldDirty.united(
            desktopRootItemHighlightDirtyRect(state->model.renderData)
            .united(state->model.renderData.itemRect)));
        return;
    }

    if (LinBox* box = BoxManager::instance()->findBoxContainingItem(filePath)) {
        if (DesktopRootItemState* state = box->findItemStateByPath(filePath)) {
            if (state->model.isFullNameExpanded == expanded &&
                (expanded || state->model.expandedPreviewText.isEmpty())) {
                return;
            }
            state->model.isFullNameExpanded = expanded;
            if (!expanded) {
                state->model.expandedPreviewText.clear();
            }
            state->renderDataDirty = true;
            box->refreshLayout();
            box->update();
            return;
        }
    }
}

void ListView::setItemExpandedPreviewText(const QString& filePath, const QString& text)
{
    if (DesktopRootItemState* state = findDesktopStateByPath(filePath)) {
        if (state->model.expandedPreviewText == text) {
            return;
        }

        const QRect oldDirty = desktopRootItemHighlightDirtyRect(state->model.renderData)
            .united(state->model.renderData.itemRect);
        state->model.expandedPreviewText = text;
        markDesktopRenderDataDirty(state);
        updateAllItemRenderData();
        update(oldDirty.united(
            desktopRootItemHighlightDirtyRect(state->model.renderData)
            .united(state->model.renderData.itemRect)));
        return;
    }

    if (LinBox* box = BoxManager::instance()->findBoxContainingItem(filePath)) {
        if (DesktopRootItemState* state = box->findItemStateByPath(filePath)) {
            if (state->model.expandedPreviewText == text) {
                return;
            }
            state->model.expandedPreviewText = text;
            state->renderDataDirty = true;
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
        if (state->model.isRenaming == renaming) {
            return;
        }

        const QRect oldDirty = desktopRootItemHighlightDirtyRect(state->model.renderData)
            .united(state->model.renderData.itemRect);
        state->model.isRenaming = renaming;
        markDesktopRenderDataDirty(state);
        updateAllItemRenderData();
        update(oldDirty.united(
            desktopRootItemHighlightDirtyRect(state->model.renderData)
            .united(state->model.renderData.itemRect)));
        return;
    }

    if (LinBox* box = BoxManager::instance()->findBoxContainingItem(filePath)) {
        if (DesktopRootItemState* state = box->findItemStateByPath(filePath)) {
            if (state->model.isRenaming == renaming) {
                return;
            }
            state->model.isRenaming = renaming;
            state->renderDataDirty = true;
            box->refreshLayout();
            box->update();
            return;
        }
    }
}
    
void ListView::setFocusedItemPath(const QString& filePath)
{
    ListViewSelectionController::setFocusedItemPath(*this, filePath);
}

void ListView::clearFocusedItem()
{
    ListViewSelectionController::clearFocusedItem(*this);
}

void ListView::toggleItemSelection(const QString& filePath)
{
    ListViewSelectionController::toggleItemSelection(*this, filePath);
}

void ListView::selectRangeToPath(const QString& filePath, LinBox* box)
{
    ListViewSelectionController::selectRangeToPath(*this, filePath, box);
}

void ListView::moveFocusInHost(LinBox* box, FocusMoveDirection direction, bool extendSelection, bool focusOnly)
{
    ListViewSelectionController::moveFocusInHost(*this, box, direction, extendSelection, focusOnly);
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

    updateAllItemRenderData();
    rebuildSpatialIndex();
    update();
}

void ListView::applyCutMarks(const QStringList& filePaths)
{
    clearCutMarks();
    for (const QString& path : filePaths) {
        const QString normalizedPath = normalizeDesktopItemPath(path);
        if (DesktopRootItemState* state = findDesktopStateByPath(normalizedPath)) {
            state->model.isCutMarked = true;
            state->model.renderData.cutMarked = true;
            continue;
        }
        if (LinBox* box = BoxManager::instance()->findBoxContainingItem(normalizedPath)) {
            if (DesktopRootItemState* state = box->findItemStateByPath(normalizedPath)) {
                state->model.isCutMarked = true;
                state->model.renderData.cutMarked = true;
                box->refreshLayout();
                box->update();
            }
        }
    }
    updateAllItemRenderData();
    rebuildSpatialIndex();
    update();
}

QStringList ListView::selectedItemPaths() const
{
    return ListViewSelectionController::selectedItemPaths(*this);
}

QString ListView::focusedItemPath() const
{
    return ListViewSelectionController::focusedItemPath(*this);
}

QString ListView::currentSpaceLookHoveredPath() const
{
    const QPoint cursorPos = QCursor::pos();
    const QVector<LinBox*> allBoxes = boxes();
    for (LinBox* box : allBoxes) {
        if (!box || !box->isVisible()) {
            continue;
        }

        const QRect globalRect(box->mapToGlobal(QPoint(0, 0)), box->size());
        if (!globalRect.contains(cursorPos)) {
            continue;
        }

        const QString hoveredPath = box->hoveredItemPath();
        if (!hoveredPath.trimmed().isEmpty()) {
            return normalizeDesktopItemPath(hoveredPath);
        }
    }

    if (!m_hoveredDesktopPath.trimmed().isEmpty()) {
        return effectiveFocusedDesktopPath().isEmpty()
            ? normalizeDesktopItemPath(m_hoveredDesktopPath)
            : normalizeDesktopItemPath(m_hoveredDesktopPath);
    }

    return QString();
}

QString ListView::anchorItemPath() const
{
    return ListViewSelectionController::anchorItemPath(*this);
}

QString ListView::draggedItemPath() const
{
    if (!m_draggedDesktopPath.isEmpty()) {
        return normalizeDesktopItemPath(m_draggedDesktopPath);
    }
    if (!m_draggedBoxPath.isEmpty()) {
        return normalizeDesktopItemPath(m_draggedBoxPath);
    }
    return QString();
}

QStringList ListView::draggedItemPaths() const
{
    if (!m_draggedBoxPaths.isEmpty()) {
        return m_draggedBoxPaths;
    }
    if (!m_draggedDesktopPaths.isEmpty()) {
        return m_draggedDesktopPaths;
    }
    if (!m_draggedBoxPath.isEmpty()) {
        return { m_draggedBoxPath };
    }
    if (!m_draggedDesktopPath.isEmpty()) {
        return { m_draggedDesktopPath };
    }
    return {};
}

void ListView::setDraggedItemPath(const QString& filePath, bool fromBox)
{
    const QString normalizedPath = normalizeDesktopItemPath(filePath);
    setDraggedItemPaths(normalizedPath.isEmpty() ? QStringList() : QStringList{ normalizedPath }, fromBox);
}

void ListView::setDraggedItemPaths(const QStringList& filePaths, bool fromBox)
{
    QStringList normalized;
    normalized.reserve(filePaths.size());
    for (const QString& path : filePaths) {
        const QString norm = normalizeDesktopItemPath(path);
        if (!norm.isEmpty()) {
            normalized.append(norm);
        }
    }

    if (fromBox) {
        m_draggedBoxPaths = normalized;
        m_draggedBoxPath = normalized.isEmpty() ? QString() : normalized.first();
        m_draggedDesktopPaths.clear();
        m_draggedDesktopPath.clear();
        return;
    }

    m_draggedDesktopPaths = normalized;
    m_draggedDesktopPath = normalized.isEmpty() ? QString() : normalized.first();
    m_draggedBoxPaths.clear();
    m_draggedBoxPath.clear();
}

void ListView::clearDraggedItemPath(bool fromBoxOnly)
{
    if (fromBoxOnly) {
        m_draggedBoxPath.clear();
        m_draggedBoxPaths.clear();
        return;
    }

    m_draggedDesktopPath.clear();
    m_draggedBoxPath.clear();
    m_draggedDesktopPaths.clear();
    m_draggedBoxPaths.clear();
}

void ListView::restoreSelectionStateByPaths(const QStringList& selectedPaths, const QString& focusedPath, const QString& anchorPath, bool expandSingle)
{
    ListViewSelectionController::restoreSelectionStateByPaths(*this, selectedPaths, focusedPath, anchorPath, expandSingle);
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

void ListView::requestSystemRightMenu(ObjectType objType, QPoint point, const QStringList& checkedPaths)
{
    RightMenu* r = RightMenu::getInstance();
    QVector<DesktopRootItemModel> models;
    models.reserve(qMax<int>(checkedPaths.size(), selectedItemPaths().size()));
    for (const QString& path : checkedPaths) {
        if (const DesktopRootItemState* state = findAnyItemStateByPath(path)) {
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

    if (checkedPaths.size() == 1 || models.size() == 1) {
        r->setShowType(objType);
        if (!models.isEmpty()) {
            const bool extendedVerbs = (QApplication::queryKeyboardModifiers() & Qt::ShiftModifier) != 0;
            r->showMenu(point, models.first(), extendedVerbs);
        }
        return;
    }
    else if (objType == ObjectType::BackGround) {
        r->setShowType(ObjectType::BackGround);
        const bool extendedVerbs = (QApplication::queryKeyboardModifiers() & Qt::ShiftModifier) != 0;
        r->showMenu(point, DesktopRootItemModel{}, extendedVerbs);
        return;
    }
    else {
        r->setShowType(ObjectType::MultObject);
        if (!models.isEmpty()) {
            const bool extendedVerbs = (QApplication::queryKeyboardModifiers() & Qt::ShiftModifier) != 0;
            r->showMultObjectMenu(point, models, extendedVerbs);
        }
    }
}

void ListView::refreshCheckedItem(QRect rect)
{
    QRegion uncoveredSelectionRegion(rect);
    if (!m_backgroundSelectionOcclusionRegion.isEmpty()) {
        uncoveredSelectionRegion = uncoveredSelectionRegion.subtracted(m_backgroundSelectionOcclusionRegion);
    }

    if (uncoveredSelectionRegion.isEmpty()) {
        resetBackgroundSelectionCandidateCache();
        clearCheckedItem();
        return;
    }

    updateBackgroundSelectionCandidateCache(uncoveredSelectionRegion);

    m_backgroundSelectionNextSelectionSet.clear();
    m_backgroundSelectionNextSelectionStates.clear();
    QString fallbackFocusPath;

    m_backgroundSelectionNextSelectionSet.reserve(m_backgroundSelectionCandidateStates.size());
    m_backgroundSelectionNextSelectionStates.reserve(m_backgroundSelectionCandidateStates.size());
    for (DesktopRootItemState* state : m_backgroundSelectionCandidateStates) {
        if (!state || !shouldPaintDesktopModelState(*state)) {
            continue;
        }

        const QString normalizedPath = normalizeDesktopItemPath(state->model.filePath);
        if (normalizedPath.isEmpty()) {
            continue;
        }

        if (!uncoveredSelectionRegion.intersects(state->model.renderData.selectionContentRect)) {
            continue;
        }

        if (!m_backgroundSelectionNextSelectionSet.contains(normalizedPath)) {
            m_backgroundSelectionNextSelectionSet.insert(normalizedPath);
            m_backgroundSelectionNextSelectionStates.append(state);
            fallbackFocusPath = normalizedPath;
        }
    }

    if (m_selectedDesktopPathSet == m_backgroundSelectionNextSelectionSet) {
        return;
    }

    QRegion dirtyRegion;
    m_backgroundSelectionPreviousSelectionPaths = m_selectedDesktopPaths;
    for (const QString& path : m_backgroundSelectionPreviousSelectionPaths) {
        if (m_backgroundSelectionNextSelectionSet.contains(path)) {
            continue;
        }
        clearItemExpandedState(path);
        if (DesktopRootItemState* state = findDesktopStateByPath(path)) {
            setDesktopItemSelected(state, path, false, &dirtyRegion);
        }
    }

    for (DesktopRootItemState* state : m_backgroundSelectionNextSelectionStates) {
        if (!state) {
            continue;
        }
        const QString normalizedPath = normalizeDesktopItemPath(state->model.filePath);
        if (normalizedPath.isEmpty() || m_selectedDesktopPathSet.contains(normalizedPath)) {
            continue;
        }
        setDesktopItemSelected(state, normalizedPath, true, &dirtyRegion);
    }

    if (!dirtyRegion.isEmpty()) {

        update(dirtyRegion);
    }

    m_focusedDesktopPath = m_backgroundSelectionNextSelectionSet.isEmpty()
        ? QString()
        : (m_backgroundSelectionNextSelectionSet.contains(m_focusedDesktopPath) ? m_focusedDesktopPath : fallbackFocusPath);

    if (!m_backgroundSelectionNextSelectionSet.isEmpty() && m_anchorDesktopPath.isEmpty()) {
        m_anchorDesktopPath = fallbackFocusPath;
    }
}

void ListView::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    const QRegion dirtyRegion = event->region();
    const bool reducePaintCostForSelection = m_isSelecting;
    painter.setRenderHint(QPainter::Antialiasing, !reducePaintCostForSelection);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, !reducePaintCostForSelection);
    painter.setRenderHint(QPainter::TextAntialiasing);


    if (m_linWidgetManager && m_linWidgetManager->hasActiveWallpaper() && !m_linWidgetManager->isWallpaperInteractive()) {
        painter.fillRect(event->rect(), QColor(0, 0, 0, 1));
    }

    const bool widgetsEmbedded = m_linWidgetManager ? m_linWidgetManager->isWidgetLayerEmbedded() : true;

    // 1. 如果处于嵌入模式（小组件在图标下方），优先绘制 CEF OSR 画布
    if (widgetsEmbedded && m_linWidgetManager && m_linWidgetManager->canvasWidget()) {
        m_linWidgetManager->canvasWidget()->renderCanvas(painter, dirtyRegion);
    }

    if (m_hideDesktopItemsForRefresh) {
        return;
    }

    auto paintDesktopState = [&](const DesktopRootItemState& state) {
        if (!shouldPaintDesktopModelState(state)) {
            return;
        }

        if (!dirtyRegion.intersects(state.model.renderData.itemRect)) {
            return;
        }

        paintDesktopRootItemDynamic(painter, state.model.renderData);
        paintDesktopRootItemStatic(painter, state.model.renderData);
    };

    // Paint normal items first so expanded filename labels can sit above siblings.
    for (const auto& state : m_itemStates) {
        if (state.model.renderData.expandedFullName) {
            continue;
        }
        paintDesktopState(state);
    }

    for (const auto& state : m_itemStates) {
        if (!state.model.renderData.expandedFullName) {
            continue;
        }
        paintDesktopState(state);
    }

    // 2. 如果处于浮动模式（小组件在图标上方），后置绘制 CEF OSR 画布
    if (!widgetsEmbedded && m_linWidgetManager && m_linWidgetManager->canvasWidget()) {
        m_linWidgetManager->canvasWidget()->renderCanvas(painter, dirtyRegion);
    }

    const QRect indicatorRect = desktopAutoArrangeInsertionIndicatorRect();
    if (!indicatorRect.isNull() && dirtyRegion.intersects(indicatorRect.adjusted(-4, -3, 4, 3))) {
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 220, 220, 80));
        painter.drawRoundedRect(indicatorRect.adjusted(-4, -3, 4, 3), 5, 5);
        painter.setBrush(QColor(0, 220, 220, 230));
        painter.drawRoundedRect(indicatorRect, 2, 2);
        painter.restore();
    }

    paintLinWidgetBlockedCellsOverlay(painter, dirtyRegion);
}

void ListView::contextMenuEvent(QContextMenuEvent* event)
{
#ifdef Q_OS_WIN
    logListViewDpiTrace("contextMenu", this, event->pos(), event->globalPos());
#endif
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
            const bool extendedVerbs = (event->modifiers() & Qt::ShiftModifier) != 0;
            if (desktopModels.size() == 1) {
                menu->setShowType(hitState->model.objectType);
                menu->showMenu(event->globalPos(), desktopModels.first(), extendedVerbs);
            } else if (!desktopModels.isEmpty()) {
                menu->setShowType(ObjectType::MultObject);
                menu->showMultObjectMenu(event->globalPos(), desktopModels, extendedVerbs);
            }
            event->accept();
            return;
        }

        auto rightClickPoint = mapToGlobal(event->pos());
        this->m_lastRightClickPos = event->pos();

        // Since ListItems now intercept their own contextMenuEvent and emit signals,
        // this handler is only reached if the background (desktop) is clicked.
        
        // 濠电姷顣介埀顒€鍟块埀顒€缍婇幃妯诲緞閹邦剛鐓戦梺瑙勫劶婵倝寮冲☉銏＄厽闁归偊鍠楅崵鈧梺鎼炲€ら崰妤呭箯閸涱収鍚嬮柛銉㈡櫆閻庮厽绻涢幋鐐村碍缂佸娼欓敃銏ゎ敂閸涱垼娴勯柣鐘辩绾绢厾绮旈崸妤佺厱闁靛／鍐у闂佺粯鐗紞浣哥暦閻樿浼犻柛鏇ㄥ幖楠炲秹姊洪崨濠傚闁归攱鍨瑰Σ鎰攽閸℃劏鍋撻幒妤€宸濋悗娑欘焽瑜版垿鎮楅崷顓х劸鐎殿喛娉涢敃銏＄瑹閳ь剟寮婚崨顔肩窞濠㈣泛楠搁獮鈧?
        clearCheckedItem();
        // 闂備礁鎼€氼剚鏅舵禒瀣︽慨姗嗗墻濞存牜鈧厜鍋撻柛鏇ㄥ墯閵嗗啴姊哄Ч鍥у閻庢凹鍓濋妵鎰板箹娴ｅ湱鍙嗗銈嗘煥閻忔繄绱為崨瀛樼厱?
        requestSystemRightMenu(ObjectType::BackGround, rightClickPoint);
    }
}

void ListView::mousePressEvent(QMouseEvent* event)
{
    qDebug() << "[LinWidgetInput] mousePressEvent: pos =" << event->pos() << "over widget =" << (m_linWidgetManager ? m_linWidgetManager->isWidgetAt(event->pos()) : false);
    if (m_linWidgetManager) {
        if (m_linWidgetManager->isWidgetLayerEmbedded() && !findDesktopStateAt(event->pos()) && m_linWidgetManager->isWidgetAt(event->pos())) {
            if (m_linWidgetManager->forwardMouseEvent(event)) {
                m_forwardingMouseToWidget = true;
                event->accept();
                return;
            }
        } else {
            m_linWidgetManager->clearWidgetFocus();
        }
    }

#ifdef Q_OS_WIN
    if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
        logListViewDpiTrace("mousePress", this, event->pos(), event->globalPos());
    }
#endif
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
                    setItemFullNameExpanded(itemPath, true);
                    m_wasSelectedBeforeClick = (m_selectedDesktopPaths.size() == 1);
                }
            }

            m_lastClickedDesktopPath = itemPath;
            m_clickTimer.restart();
            if (!(modifiers & Qt::ControlModifier) && !(modifiers & Qt::ShiftModifier)) {
                m_anchorDesktopPath = itemPath;
            }

            m_draggedDesktopPath = itemPath;
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
    if (m_forwardingMouseToWidget) {
        if (m_linWidgetManager && m_linWidgetManager->forwardMouseEvent(event)) {
            event->accept();
            return;
        }
    } else if (m_linWidgetManager && m_linWidgetManager->isWidgetLayerEmbedded() && !m_isDragging && !m_isSelecting && m_linWidgetManager->isWidgetAt(event->pos())) {
        if (m_linWidgetManager->forwardMouseEvent(event)) {
            event->accept();
            return;
        }
    }
    // handle multi-dragging
    if (m_isDragging && !m_ghosts.isEmpty()) {
        QPoint mousePos = event->globalPos();
        moveDesktopGhostsForPreview(mousePos);
        updateGhostDragHoverPreview(mousePos);
        return;
    }

    if (m_isDragging && !m_draggedDesktopPath.isEmpty()) {
        if ((event->pos() - m_startPos).manhattanLength() > QApplication::startDragDistance()) {
            startDesktopItemDrag(m_startPos);
        }
        event->accept();
        return;
    }

    if (m_isSelecting) {
        QRect rect;
        const bool shouldShowSelection = updateBackgroundSelection(event->pos(), &rect);
        if (LinDesktop* desktop = findLinDesktopAncestor(parentWidget())) {
            if (desktop->selectArea) {
                desktop->selectArea->setSelectionRect(rect);
            }
            if (shouldShowSelection) {
                if (desktop->selectArea && !desktop->selectArea->isVisible()) {
                    desktop->selectArea->show();
                }
                if (desktop->selectArea) {
                    desktop->selectArea->lower();
                }
            } else {
                desktop->clearSelectionOverlay();
            }
        }
        event->accept();
        return;
    }

    updateHoveredDesktopItem(event->pos());


    event->ignore();

}

void ListView::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_forwardingMouseToWidget) {
        m_forwardingMouseToWidget = false;
        if (m_linWidgetManager && m_linWidgetManager->forwardMouseEvent(event)) {
            event->accept();
            return;
        }
    } else if (m_linWidgetManager && m_linWidgetManager->isWidgetLayerEmbedded() && !m_isDragging && !m_isSelecting && m_linWidgetManager->isWidgetAt(event->pos())) {
        if (m_linWidgetManager->forwardMouseEvent(event)) {
            event->accept();
            return;
        }
    }
    // No-op for Item-based drags, as QDropEvent now handles them
    if (m_isDragging) {
        if (!m_draggedDesktopPath.isEmpty()) {
            finalizeActiveDrag(nullptr, event);
            m_draggedDesktopPath.clear();
            event->accept();
            return;
        }
        m_isDragging = false;
    }
    clearDesktopAutoArrangeReorderPreview();
    
    // Clear selection state if we were selecting background
    if (m_isSelecting) {
        if (LinDesktop* desktop = findLinDesktopAncestor(parentWidget())) {
            desktop->clearSelectionOverlay();
        }
        endBackgroundSelection();
        event->accept();
        return;
    }
    event->ignore();
}

void ListView::beginBackgroundSelection(const QPoint& startPos)
{
    ListViewSelectionController::beginBackgroundSelection(*this, startPos);
}

bool ListView::updateBackgroundSelection(const QPoint& currentPos, QRect* selectionRect)
{
    return ListViewSelectionController::updateBackgroundSelection(*this, currentPos, selectionRect);
}

void ListView::endBackgroundSelection()
{
    ListViewSelectionController::endBackgroundSelection(*this);
}

void ListView::flushBackgroundSelectionUpdate()
{
    ListViewSelectionController::flushBackgroundSelectionUpdate(*this);
}

void ListView::markSpatialIndexDirty()
{
    m_spatialIndexDirty = true;
}

void ListView::resetBackgroundSelectionCandidateCache()
{
    ListViewSelectionController::resetBackgroundSelectionCandidateCache(*this);
}

void ListView::updateBackgroundSelectionCandidateCache(const QRegion& uncoveredSelectionRegion)
{
    ListViewSelectionController::updateBackgroundSelectionCandidateCache(*this, uncoveredSelectionRegion);
}

void ListView::rebuildBackgroundSelectionOcclusionRegion()
{
    ListViewSelectionController::rebuildBackgroundSelectionOcclusionRegion(*this);
}

int ListView::currentBackgroundSelectionThrottleMs() const
{
    return ListViewSelectionController::currentBackgroundSelectionThrottleMs(*this);
}

void ListView::updateBackgroundSelectionThrottle(qint64 refreshElapsedMs, const QRect& selectionRect)
{
    ListViewSelectionController::updateBackgroundSelectionThrottle(*this, refreshElapsedMs, selectionRect);
}

void ListView::markDesktopRenderDataDirty(DesktopRootItemState* state)
{
    if (!state) {
        return;
    }

    state->renderDataDirty = true;
    m_spatialIndexDirty = true;
}

void ListView::markAllDesktopRenderDataDirty()
{
    for (auto& state : m_itemStates) {
        state.renderDataDirty = true;
    }
    m_spatialIndexDirty = true;
}

void ListView::markDesktopStateIndexDirty()
{
    m_desktopStateIndexDirty = true;
}

void ListView::rebuildDesktopStateIndex()
{
    if (!m_desktopStateIndexDirty) {
        return;
    }

    m_desktopStateIndex.clear();
    m_desktopStateIndex.reserve(m_itemStates.size());
    for (auto& state : m_itemStates) {
        const QString normalizedPath = normalizeDesktopItemPath(state.model.filePath);
        if (!normalizedPath.isEmpty()) {
            m_desktopStateIndex.insert(normalizedPath, &state);
        }
    }
    m_desktopStateIndexDirty = false;
}

void ListView::rebuildSpatialIndex()
{
    const QRect currentBounds = rect();
    if (!m_spatialIndex) {
        m_spatialIndex = std::make_unique<DesktopQuadTree>(currentBounds);
    } else {
        m_spatialIndex->setBounds(currentBounds);
    }

    QVector<DesktopRootItemState*> states;
    states.reserve(m_itemStates.size() + 64);
    for (auto& state : m_itemStates) {
        if (state.model.visible) {
            states.append(&state);
        }
    }
    if (BoxManager* bm = BoxManager::instance()) {
        for (const auto& boxPtr : bm->allBoxes()) {
            if (LinBox* box = boxPtr.data()) {
                for (auto& state : const_cast<QVector<DesktopRootItemState>&>(box->itemStates())) {
                    if (state.model.visible) {
                        states.append(&state);
                    }
                }
            }
        }
    }

    m_spatialIndex->rebuild(states);
    m_spatialIndexBounds = currentBounds;
    m_spatialIndexDirty = false;
}

QVector<DesktopRootItemState> ListView::queryItemsInRect(const QRect& rect) const
{
    QVector<DesktopRootItemState> results;
    if (rect.isEmpty()) {
        return results;
    }

    if (m_spatialIndexDirty || !m_spatialIndex || m_spatialIndexBounds != this->rect()) {
        const_cast<ListView*>(this)->rebuildSpatialIndex();
    }

    if (m_spatialIndex) {
        const QVector<DesktopRootItemState*> candidates = m_spatialIndex->query(rect);
        results.reserve(candidates.size());
        for (DesktopRootItemState* state : candidates) {
            if (state && state->model.renderData.itemRect.intersects(rect)) {
                results.push_back(*state);
            }
        }
    }
    return results;
}





void ListView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_linWidgetManager && m_linWidgetManager->isWidgetLayerEmbedded() && !findDesktopStateAt(event->pos()) && m_linWidgetManager->isWidgetAt(event->pos())) {
        if (m_linWidgetManager->forwardMouseEvent(event)) {
            event->accept();
            return;
        }
    }

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

void ListView::wheelEvent(QWheelEvent* event)
{
    if (m_linWidgetManager && m_linWidgetManager->isWidgetLayerEmbedded() && !findDesktopStateAt(event->pos()) && m_linWidgetManager->isWidgetAt(event->pos())) {
        if (m_linWidgetManager->forwardWheelEvent(event)) {
            event->accept();
            return;
        }
    }

    if (event->modifiers() & Qt::ControlModifier) {
        const QPoint angleDelta = event->angleDelta();
        const int delta = angleDelta.y() != 0 ? angleDelta.y() : angleDelta.x();
        if (delta != 0) {
            stepIconViewMode(delta > 0 ? 1 : -1);
            event->accept();
            return;
        }
    }

    QWidget::wheelEvent(event);
}

void ListView::keyPressEvent(QKeyEvent* event)
{
    if (m_linWidgetManager && m_linWidgetManager->forwardKeyEvent(event)) {
        event->accept();
        return;
    }
    // 闂佸吋鍎抽崲鑼躲亹閸モ斁鍋撻崷顓炰沪婵＄偛鍊块幆鍐礋椤栨稒娈梺鐑╂櫆椤洦鎱ㄩ幖浣哥畱?
    const bool shiftPressed = GetAsyncKeyState(VK_SHIFT) & 0x8000;
    const bool ctrlPressed = GetAsyncKeyState(VK_CONTROL) & 0x8000;

    const bool extendSelection = (event->modifiers() & Qt::ShiftModifier) || shiftPressed;
    const bool shortcutPaste = ctrlPressed && (event->key() == Qt::Key_V);
    const bool shortcutNewFolder = ctrlPressed && extendSelection && (event->key() == Qt::Key_N);
    const bool permanentDelete = shiftPressed && (event->key() == Qt::Key_Delete);

    qDebug() << QString("Press Key: %1, Shift: %2, Delete: %3, permanentDelete: %4")
            .arg(QKeySequence(event->key()).toString())
            .arg(shiftPressed ? "Yes" : "No")
            .arg(event->key() == Qt::Key_Delete ? "Yes" : "No")
            .arg(permanentDelete ? "Yes" : "No");
    if (event->matches(QKeySequence::SelectAll)) {
        QStringList desktopPaths;
        for (const auto& state : m_itemStates) {
            if (shouldPaintDesktopModelState(state)) {
                desktopPaths.append(normalizeDesktopItemPath(state.model.filePath));
            }
        }
        applyDesktopSelection(desktopPaths, m_focusedDesktopPath, false);
        applySelectionState({}, QString(), false);
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
            CSystemApi::File_RightMenuOperations(QDir::toNativeSeparators(desktopTarget->model.filePath), Z_PROPERTIES);
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
    if (event->key() == Qt::Key_Space) {
        const DesktopRootItemState* desktopTarget = !m_hoveredDesktopPath.isEmpty()
            ? findDesktopStateByPath(m_hoveredDesktopPath)
            : (!m_focusedDesktopPath.isEmpty()
                ? findDesktopStateByPath(m_focusedDesktopPath)
                : (m_selectedDesktopPaths.isEmpty() ? nullptr : findDesktopStateByPath(m_selectedDesktopPaths.first())));
        const QString targetPath = desktopTarget
            ? desktopTarget->model.filePath
            : (focusedItemPath().isEmpty()
                ? (selectedBoxPaths().isEmpty() ? QString() : selectedBoxPaths().first())
                : focusedItemPath());
        if (!targetPath.isEmpty() && sendPreviewRequestByPath(targetPath)) {
            event->accept();
            qDebug() << "Space triggered: Sent preview request for" << QFileInfo(targetPath).completeBaseName();
            return;
        }
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

void ListView::keyReleaseEvent(QKeyEvent* event)
{
    if (m_linWidgetManager && m_linWidgetManager->forwardKeyEvent(event)) {
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void ListView::inputMethodEvent(QInputMethodEvent* event)
{
    if (m_linWidgetManager && m_linWidgetManager->forwardInputMethodEvent(event)) {
        event->accept();
        return;
    }
    QWidget::inputMethodEvent(event);
}

QVariant ListView::inputMethodQuery(Qt::InputMethodQuery query) const
{
    if (query == Qt::ImCursorRectangle && m_linWidgetManager && !m_linWidgetManager->focusedWidget().isEmpty()) {
        const QRect rect = m_linWidgetManager->focusedWidgetCursorRect();
        if (rect.isValid()) {
            return rect;
        }
    }
    return QWidget::inputMethodQuery(query);
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

void ListView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    syncLinWidgetLayer();
}

void ListView::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    syncLinWidgetLayer();
}

void ListView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    syncLinWidgetLayer();
    QTimer::singleShot(100, this, [this]() {
        syncLinWidgetLayer();
    });
}

ListView* ListView::instance(QWidget* parent) {
    if (!m_instance) {
        m_instance = new ListView(parent);
    }
    return m_instance;
}

void ListView::initialize()
{
    // === 闂備礁鎲￠懝楣冨嫉椤掑嫷鏁?Windows 婵犵鍓濋〃鍛存儗閸屾凹鐒藉ù鍏兼綑缁犳娊鏌熼悜妯虹仴闁诲繑鐟╅幃瑙勬媴缁嬪簱鎸冮梺?===
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

    m_itemStates.clear();
    markDesktopStateIndexDirty();
    if (monitor) {
        sysIconRegisterInfo = monitor->getCurrentIcons();
    }

    auto normalizeClsid = [](QString clsid) {
        return clsid.replace("::", "").replace("{", "").replace("}", "");
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
            state.model.fileInfo.absoluteFilePath().isEmpty() ? 0 : data.itemData().position.x(),
            state.model.fileInfo.absoluteFilePath().isEmpty() ? 0 : data.itemData().position.y());

        state.model.position = (nativePos.x() != 0 || nativePos.y() != 0)
            ? nativePos
            : QPoint(0, 0);
        m_itemStates.push_back(state);
        markDesktopStateIndexDirty();
    }


    // --- Compact Native Layout Algorithm (Fill the gaps) ---
    // 1. Gather all unique physical coordinates from the entire system desktop
    QVector<QPoint> nativeSlots;
    for (const auto& data : allDatas) {
        QPoint p(data.itemData().position.x(), data.itemData().position.y());
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
    qDebug() << "[ListView] Starting reload. Model item count:" << m_itemStates.size();
    const bool shouldRestorePersistedLayout = m_isLoadingPersistence || m_forceConfigLayout;
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

    updateAllItemRenderData();
    rebuildSpatialIndex();
    update();

    // Refresh all boxes to ensure their icons are positioned.
    for (auto& box : BoxManager::instance()->allBoxes()) {
        if (box) {
            box->refreshLayout();
        }
    }

    syncBoxVisibilityWithDesktopIcons();
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

void ListView::applyWindowsHiddenVisualState(DesktopRootItemModel& model)
{
    const bool isSpecial = !model.isShortcut && (model.itemData.type() == ObjectType::SpecialFolder ||
        SystemFolderCLSIDs.contains(model.itemData.getCLSID()) ||
        model.filePath.startsWith("::"));
    const bool shouldFade = !isSpecial && m_showHiddenFiles && fileHasHiddenAttribute(model.filePath);
    model.isHiddenFileVisual = shouldFade;
}

bool ListView::shouldShowFileSystemItem(const DesktopRootItemModel& model) const
{
    if (model.objectType == ObjectType::DiskLetter) {
        return false;
    }

    const bool isSpecial = !model.isShortcut && (model.itemData.type() == ObjectType::SpecialFolder ||
        SystemFolderCLSIDs.contains(model.itemData.getCLSID()) ||
        model.filePath.startsWith("::"));
    if (isSpecial) {
        return true;
    }

    return m_showHiddenFiles || !fileHasHiddenAttribute(model.filePath);
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

void ListView::syncBoxVisibilityWithDesktopIcons()
{
    const QVector<QPointer<LinBox>> boxes = BoxManager::instance()->allBoxes();
    for (const QPointer<LinBox>& box : boxes) {
        if (!box) {
            continue;
        }

        if (m_iconShowRule.showDesktopIcons) {
            if (!box->isVisible()) {
                box->show();
            }
            box->raise();
        } else {
            box->hide();
        }
    }
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
    {
        if (m_iconShowRule.sortType == order) {
            order = getOppositeDirection();
        }

        performSort(order);
        return;
    }

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
    {
        QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
        sortAutoArrangeStates(states, SortOrderType::SortByCategorized);
        assignCustomSortOrders(states);
        return;
    }

}

void ListView::sortByCustom()
{
    QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
    sortAutoArrangeStates(states, SortOrderType::SortByCustom);
    assignCustomSortOrders(states);
}

void ListView::sortByName(bool ascending)
{
    {
        QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
        sortAutoArrangeStates(states, ascending ? SortOrderType::SortByName_ASC : SortOrderType::SortByName_DESC);
        assignCustomSortOrders(states);
        return;
    }

}

void ListView::sortBySize(bool ascending)
{
    {
        QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
        sortAutoArrangeStates(states, ascending ? SortOrderType::SortBySize_ASC : SortOrderType::SortBySize_DESC);
        assignCustomSortOrders(states);
        return;
    }

}

void ListView::sortByType(bool ascending)
{
    {
        QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
        sortAutoArrangeStates(states, ascending ? SortOrderType::SortByType_ASC : SortOrderType::SortByType_DESC);
        assignCustomSortOrders(states);
        return;
    }

}

void ListView::sortByDate(bool ascending)
{
    {
        QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
        sortAutoArrangeStates(states, ascending ? SortOrderType::SortByDateModified_ASC : SortOrderType::SortByDateModified_DESC);
        assignCustomSortOrders(states);
        return;
    }

}

bool ListView::addItem(const QString& filePath, bool refresh)
{
    const QString normalizedPath = normalizeDesktopItemPath(filePath);
    if (normalizedPath.isEmpty()) {
        return false;
    }

    const bool added = !findAnyItemStateByPath(normalizedPath) && addDesktopModelItemFromPath(filePath);
    
    // 闂備礁婀遍。浠嬪磻閹剧粯鐓涢柛顐ｇ箥濡插綊鏌＄€ｎ亜鏆炵紒鍌涘浮瀹曞ジ寮撮悙鎻掔闂備礁鎲″缁樻叏閹绢喖鐭?m_items 闂?m_allItems
    if (refresh) {
        requestSaveBoxes();
    }
    return added;
}

bool ListView::addItemNoDelete(const QString& filePath)
{
    const QString normalizedPath = normalizeDesktopItemPath(filePath);
    if (normalizedPath.isEmpty() || findDesktopStateByPath(normalizedPath)) {
        return false;
    }

    return addDesktopModelItemFromPath(filePath);
}

void ListView::removeItem(const QString& filePath)
{
    removeItemPathsBatch({ filePath });
}

void ListView::removeItemNoDelete(const QString& filePath)
{
    setItemPathsTransientHidden({ filePath }, true);
}

void ListView::removeItemsBatch(const QStringList& paths, bool refresh)
{
    removeItemPathsBatch(paths, refresh);
}

void ListView::setItemsTransientHidden(const QStringList& paths, bool hidden)
{
    setItemPathsTransientHidden(paths, hidden);
}

void ListView::trackPendingExternalRemovals(const QStringList& paths)
{
    trackPendingExternalRemovalPaths(paths);
}

bool ListView::consumePendingExternalRemoval(const QString& path)
{
    const QString normalizedPath = normalizeDesktopPath(path);
    if (normalizedPath.isEmpty()) {
        return false;
    }
    return m_pendingExternalRemovalPaths.remove(normalizedPath);
}

DesktopRootItemState* ListView::findItemByPath(const QString& filePath)
{
    return findAnyItemStateByPath(filePath);
}

const DesktopRootItemState* ListView::findItemByPath(const QString& filePath) const
{
    return findAnyItemStateByPath(filePath);
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
        Q_UNUSED(maxRows);
        Q_UNUSED(maxCols);

        QSet<quint64> occupiedSlots;
        occupiedSlots.unite(desktopBlockedOccupiedCells());
        for (const auto& state : m_itemStates) {
            if (!state.model.boxId.isEmpty() || !state.model.visible) continue;
            const QPoint gridCell = pointToGridCell(state.model.position, layoutLeft, layoutTop, strideX, strideY);
            occupiedSlots.insert(packGridCell(qBound(0, gridCell.x(), maxCols - 1), qBound(0, gridCell.y(), maxRows - 1)));
        }
        for (auto it = m_pendingCreatedItemPositions.constBegin(); it != m_pendingCreatedItemPositions.constEnd(); ++it) {
            const QPoint gridCell = pointToGridCell(it.value(), layoutLeft, layoutTop, strideX, strideY);
            occupiedSlots.insert(packGridCell(qBound(0, gridCell.x(), maxCols - 1), qBound(0, gridCell.y(), maxRows - 1)));
        }

        return resolveNearestGridCreatePosition(pos, occupiedSlots);
    }

    Q_UNUSED(usableRight);
    Q_UNUSED(usableBottom);
    return resolveNearestFreeCreatePosition(pos);
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
    const QString effectivePath = ListViewFileOpsHelper::normalizeNewFolderCasePath(newPath);
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
            removeDesktopStateByPath(effectivePath);
            removeDesktopStateByPath(newPath);
            update();
            return false;
        }

        const QString destinationPath = ListViewFileOpsHelper::uniquePasteDestinationPath(effectivePath, mappedFolderPath);
        if (!ListViewFileOpsHelper::movePathToExactDestination(effectivePath, destinationPath)) {
            qWarning() << "[handlePendingShellCreatedItem] Move to mapped folder failed:"
                       << effectivePath << "->" << destinationPath;
            removeDesktopStateByPath(effectivePath);
            removeDesktopStateByPath(newPath);
            update();
            return false;
        }

        removeDesktopStateByPath(effectivePath);
        removeDesktopStateByPath(newPath);

        loadMappedFolderItemsForBox(targetBox);
        targetBox->refreshLayout();
        targetBox->raise();
        update();
        return true;
    }

    if (!findDesktopStateByPath(effectivePath)) {
        const ListItemData data = DesktopItemLoader().loadItem(effectivePath);
        DesktopRootItemState state;
        initDesktopRootItemModel(state.model, data, currentDesktopItemInitContext());
        state.model.layoutMode = DesktopLayoutMode::IconMode;
        state.model.baseSize = QSize(m_iconWidth, m_iconHeight);
        state.model.effectiveSize = state.model.baseSize;
        state.model.boxId = targetBox->boxId();
        state.model.visible = true;
        targetBox->addItemState(state, false);
    } else {
        moveItemToBox(effectivePath, targetBox, false, -1);
    }
    targetBox->refreshLayout();
    targetBox->raise();
    update();
    return true;
}

/// <summary>
/// 闂備焦鐪归崹濠氬窗閹版澘鍨傛慨妯块哺閸犲棝鏌涢弴銊ヤ簻闁?
/// 1.unGrid婵犵妲呴崹顏堝焵椤掆偓绾绢厾娑甸埀?
/// UI闂備礁鎲＄敮妤冪矙閹寸姷纾介柟缁樺敻em,闂備胶鍎甸弲娑㈡偤閵娧勬殰闁圭虎鍠栫粻鐢告煙闁缚绨介弶鈺傛煥椤顦村┑顔炬暬閹儵鏁愭径濠勭厬闂佽鍎兼慨銈夊汲濞戙垺鐓曠憸搴ㄥ礉閺嶎厽鍋?
/// 闂傚倸鍊搁悧濠囧箯缁嬧晠姊洪崨濠傜瑲缂佸鍨圭槐鎾诲箻缂佹ɑ娅栭柣蹇曞仧閸樠囧煝韫囨稒鐓熼柍鍨涙櫅婢хid闂備礁鎲＄敮妤冪矙閹寸姷纾介柟鎹愬煐閸犲棝鏌涢弴銊ヤ簻闁?
/// 2.Grid婵犵妲呴崹顏堝焵椤掆偓绾绢厾娑甸埀?
/// 闂備礁鎼悮顐﹀磿閹绢噮鏁嬫俊銈呮噹閸愨偓闂佺粯鎸稿ù鐑藉箺瀹ュ鍋ㄦい鏍ㄨ壘閺嗘瑩鏌ｉ宥呭鐞氭瑩鐓崶褔鍙勯柛銈咁儔閺屾盯寮借閹牓鏌ｉ幘瀵告噰鐎殿喕鍗抽、娑橆潩椤撶偛澧鹃梻浣告惈閻楀棝藝娴兼潙鍑犻柛鎰ㄦ櫇椤╃兘鎮规ウ鎸庮仩闁圭晫濞€閺屾盯鏁傞懡銈呭辅缂備浇椴哥换鍐╃┍婵犲洤纾兼繛鎴炵煯閸戣姤绻涢幋鐐村碍缂佸鎸抽幃娲Ω閳轰焦宓嶉梺闈涱煭鐎靛苯顫忛崜褏纾煎〒姘仢閻忣噣鏌嶇憴鍕⒌鐎殿喕绮欓幃鈩冩償閿濆棙鍟㈤梻浣瑰缁嬫垶绺介弮鈧换娑㈠炊椤掍礁浠洪梺闈浤涚仦钘夊濠电偞鍨堕幐鎾磻閹炬枼妲堥柟鍨暕缁ㄥジ鏌ｅΔ鈧幗婊堝焵椤掍胶鈯曢柛鏃€娲栭…鍥旈崨顓炲敤?
/// </summary>
/// <returns></returns>


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
        menu->setLargeIconsChecked(false);
        menu->setMediumIconsChecked(false);
        menu->setSmallIconsChecked(false);
        break;
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
        menu->setSortByNameChecked(true); // 濠殿喗甯楃粙鎺椻€﹂崼銉晣濠电姵纰嶉悞璇差熆鐠轰警妲奸柛姗嗗墴閺屾稑顫濋鍌傘倗鎮?
        break;
    }
}

void ListView::syncViewModeSizes() {
    m_marginTop = scalePx(kBaseMarginTop);
    m_marginLeft = scalePx(kBaseMarginLeft);
    m_bottomPadding = scalePx(kBaseBottomPadding);

    switch (m_iconShowRule.viewMode) {
        case ViewMode::HugeIcon:
            m_iconWidth = scalePx(kHugeIconW);
            m_iconHeight = iconCellHeightForTwoTextLines(m_iconWidth - scalePx(4));
            m_rowSpacing = scalePx(kLargeRowSpacing);
            m_colSpacing = scalePx(kLargeColSpacing);
            break;
        case ViewMode::LargeIcon:
            m_iconWidth  = scalePx(kLargeIconW);
            m_rowSpacing = scalePx(kLargeRowSpacing);
            m_colSpacing = scalePx(kLargeColSpacing);
            m_iconHeight = iconCellHeightForTwoTextLines(m_iconWidth - scalePx(4));
            if (rightMenu) rightMenu->setLargeIconsChecked(true);
            break;
        case ViewMode::MediumLargeIcon:
            m_iconWidth = scalePx(kMediumLargeIconW);
            m_iconHeight = iconCellHeightForTwoTextLines(m_iconWidth - scalePx(4));
            m_rowSpacing = scalePx(kMediumRowSpacing);
            m_colSpacing = scalePx(kMediumColSpacing);
            break;
        case ViewMode::MediumIcon:
            m_iconWidth  = scalePx(kMediumIconW);
            m_iconHeight = iconCellHeightForTwoTextLines(m_iconWidth - scalePx(4));
            m_rowSpacing = scalePx(kMediumRowSpacing);
            m_colSpacing = scalePx(kMediumColSpacing);
            if (rightMenu) rightMenu->setMediumIconsChecked(true);
            break;
        case ViewMode::SmallMediumIcon:
            m_iconWidth = scalePx(kSmallMediumIconW);
            m_iconHeight = iconCellHeightForTwoTextLines(m_iconWidth - scalePx(4));
            m_rowSpacing = scalePx(kSmallRowSpacing);
            m_colSpacing = scalePx(kSmallColSpacing);
            break;
        case ViewMode::SmallIcon:
            m_iconWidth = scalePx(kSmallIconW);
            m_iconHeight = iconCellHeightForTwoTextLines(m_iconWidth - scalePx(4));
            m_rowSpacing = scalePx(kSmallRowSpacing);
            m_colSpacing = scalePx(kSmallColSpacing);
            if (rightMenu) rightMenu->setSmallIconsChecked(true);
            break;
        default:
            m_iconShowRule.viewMode = ViewMode::MediumIcon;
            m_iconWidth = scalePx(kMediumIconW);
            m_iconHeight = iconCellHeightForTwoTextLines(m_iconWidth - scalePx(4));
            m_rowSpacing = scalePx(kMediumRowSpacing);
            m_colSpacing = scalePx(kMediumColSpacing);
            if (rightMenu) rightMenu->setMediumIconsChecked(true);
            break;
    }

    const int newTier = ListItemData::calculateIconSourceTier(m_iconWidth);
    if (ListItemData::s_targetIconSize != newTier) {
        ListItemData::s_targetIconSize = newTier;
        IconCacheManager::instance().invalidateIconsByPrefix(QStringLiteral(""));
    }
    IconCacheManager::instance().invalidatePixmapsByPrefix(QStringLiteral(""));

    markAllDesktopRenderDataDirty();
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
    params.iconTextGap = kDesktopIconTextGap;
    params.dpiScale = currentDpiScale();
    params.labelFont = font();
    params.showShortcutArrow = m_iconShowRule.showShortCutArrow;
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
    markDesktopStateIndexDirty();
    markDesktopRenderDataDirty(&m_itemStates.back());
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
    markDesktopStateIndexDirty();
    markDesktopRenderDataDirty(state);

    const QString newNormalizedPath = normalizeDesktopItemPath(state->model.filePath);
    for (QString& selectedPath : m_selectedDesktopPaths) {
        if (selectedPath == oldNormalizedPath) {
            selectedPath = newNormalizedPath;
        }
    }
    if (m_selectedDesktopPathSet.remove(oldNormalizedPath)) {
        m_selectedDesktopPathSet.insert(newNormalizedPath);
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

void ListView::invalidateFolderItemIconCache(const QString& folderPath)
{
    const QString normalizedPath = normalizeDesktopItemPath(folderPath);
    if (normalizedPath.isEmpty()) {
        return;
    }

    const QString iconKey = QString("dir:v3:%1").arg(normalizedPath);
    IconCacheManager::instance().invalidateIconsByPrefix(QString("%1|base|").arg(iconKey));
    IconCacheManager::instance().invalidatePixmapsByPrefix(QString("%1|render|").arg(iconKey));
}

bool ListView::refreshBoxModelItemPath(LinBox* box, const QString& oldPath, const QString& newPath)
{
    if (!box) {
        return false;
    }

    DesktopRootItemState* state = box->findItemStateByPath(oldPath);
    if (!state) {
        return false;
    }

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
    state->renderDataDirty = true;

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

    box->refreshLayout();
    box->update();
    return true;
}

bool ListView::refreshFolderItemByPath(const QString& folderPath)
{
    const QString normalizedPath = normalizeDesktopItemPath(folderPath);
    if (normalizedPath.isEmpty()) {
        return false;
    }

    invalidateFolderItemIconCache(normalizedPath);

    if (refreshDesktopModelItemPath(normalizedPath, folderPath)) {
        return true;
    }

    if (LinBox* box = BoxManager::instance()->findBoxContainingItem(normalizedPath)) {
        return refreshBoxModelItemPath(box, normalizedPath, folderPath);
    }

    return false;
}

DesktopRootItemState* ListView::findDesktopStateAt(const QPoint& pos)
{
    if (m_spatialIndexDirty || !m_spatialIndex || m_spatialIndexBounds != rect()) {
        rebuildSpatialIndex();
    }

    if (m_spatialIndex) {
        const QVector<DesktopRootItemState*> candidates = m_spatialIndex->queryPoint(pos);
        for (DesktopRootItemState* state : candidates) {
            if (state && shouldPaintDesktopModelState(*state) && state->model.renderData.itemRect.contains(pos)) {
                return state;
            }
        }
    }
    return nullptr;
}

const DesktopRootItemState* ListView::findDesktopStateAt(const QPoint& pos) const
{
    return const_cast<ListView*>(this)->findDesktopStateAt(pos);
}

QString ListView::normalizeDesktopItemPath(const QString& filePath)
{
    if (filePath.isEmpty()) {
        return QString();
    }

    QString normalized = QDir::toNativeSeparators(filePath);
    while (normalized.endsWith('\\')) {
        normalized.chop(1);
    }
    int colonColonIndex = normalized.indexOf(QStringLiteral("::"));
    if (colonColonIndex > 0) {
        bool allSlashes = true;
        for (int i = 0; i < colonColonIndex; ++i) {
            if (normalized[i] != '\\' && normalized[i] != '/') {
                allSlashes = false;
                break;
            }
        }
        if (allSlashes) {
            normalized = normalized.mid(colonColonIndex);
        }
    }
    return normalized.toLower();
}

DesktopRootItemState* ListView::findDesktopStateByPath(const QString& filePath)
{
    const QString normalizedPath = normalizeDesktopItemPath(filePath);
    if (normalizedPath.isEmpty()) {
        return nullptr;
    }

    rebuildDesktopStateIndex();
    const auto it = m_desktopStateIndex.constFind(normalizedPath);
    if (it != m_desktopStateIndex.cend()) {
        return it.value();
    }
    for (auto& state : m_itemStates) {
        if (normalizeDesktopItemPath(state.model.filePath).compare(normalizedPath, Qt::CaseInsensitive) == 0) {
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

    ListView* self = const_cast<ListView*>(this);
    self->rebuildDesktopStateIndex();
    const auto it = self->m_desktopStateIndex.constFind(normalizedPath);
    if (it != self->m_desktopStateIndex.cend()) {
        return it.value();
    }
    for (const auto& state : m_itemStates) {
        if (normalizeDesktopItemPath(state.model.filePath).compare(normalizedPath, Qt::CaseInsensitive) == 0) {
            return &state;
        }
    }
    return nullptr;
}

bool ListView::isDesktopPathSelected(const QString& filePath) const
{
    return ListViewSelectionController::isDesktopPathSelected(*this, filePath);
}

QVector<DesktopRootItemModel> ListView::selectedDesktopModels() const
{
    return ListViewSelectionController::selectedDesktopModels(*this);
}

QStringList ListView::selectedDesktopPaths() const
{
    return ListViewSelectionController::selectedDesktopPaths(*this);
}

QStringList ListView::selectedBoxPaths() const
{
    return ListViewSelectionController::selectedBoxPaths(*this);
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
    return ListViewSelectionController::selectableDesktopPaths(*this);
}

QStringList ListView::selectableBoxPaths(LinBox* box) const
{
    return ListViewSelectionController::selectableBoxPaths(*this, box);
}

QString ListView::findDirectionalDesktopFocusCandidate(const QString& focusedPath, FocusMoveDirection direction) const
{
    return ListViewSelectionController::findDirectionalDesktopFocusCandidate(*this, focusedPath, direction);
}

QString ListView::findDirectionalBoxFocusCandidate(LinBox* box, const QString& focusedPath, FocusMoveDirection direction) const
{
    return ListViewSelectionController::findDirectionalBoxFocusCandidate(*this, box, focusedPath, direction);
}

void ListView::setDesktopItemSelected(DesktopRootItemState* state, const QString& normalizedPath, bool selected, QRegion* dirtyRegion)
{
    ListViewSelectionController::setDesktopItemSelected(*this, state, normalizedPath, selected, dirtyRegion);
}

void ListView::setDesktopItemSelected(const QString& filePath, bool selected, QRegion* dirtyRegion)
{
    ListViewSelectionController::setDesktopItemSelected(*this, filePath, selected, dirtyRegion);
}

void ListView::clearDesktopSelection(QRegion* dirtyRegion)
{
    ListViewSelectionController::clearDesktopSelection(*this, dirtyRegion);
}

void ListView::applyDesktopSelection(const QStringList& filePaths, const QString& focusedPath, bool expandSingle)
{
    ListViewSelectionController::applyDesktopSelection(*this, filePaths, focusedPath, expandSingle);
}

void ListView::removeItemPathsBatch(const QStringList& paths, bool refresh)
{
    if (paths.isEmpty()) {
        return;
    }

    QSet<QString> seenPaths;
    QSet<LinBox*> sourceBoxes;
    bool removedAny = false;

    for (const QString& rawPath : paths) {
        const QString normalizedPath = normalizeDesktopItemPath(rawPath);
        if (normalizedPath.isEmpty() || seenPaths.contains(normalizedPath)) {
            continue;
        }
        seenPaths.insert(normalizedPath);

        if (findDesktopStateByPath(normalizedPath)) {
            removeDesktopStateByPath(normalizedPath);
            removedAny = true;
            continue;
        }

        if (LinBox* box = BoxManager::instance()->findBoxContainingItem(normalizedPath)) {
            if (box->removeItemState(normalizedPath, false)) {
                sourceBoxes.insert(box);
                removedAny = true;
            }
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

    if (refresh) {
        if (m_iconShowRule.autoArrange) {
            applyAutoArrangeLayout(false, false);
        } else {
            rebuildSpatialIndex();
            update();
        }
    }
}

void ListView::setItemPathsTransientHidden(const QStringList& paths, bool hidden)
{
    if (paths.isEmpty()) {
        return;
    }

    QSet<LinBox*> dirtyBoxes;
    bool desktopDirty = false;
    for (const QString& rawPath : paths) {
        const QString normalizedPath = normalizeDesktopItemPath(rawPath);
        if (normalizedPath.isEmpty()) {
            continue;
        }

        if (DesktopRootItemState* state = findDesktopStateByPath(normalizedPath)) {
            state->model.visible = !hidden;
            desktopDirty = true;
            continue;
        }

        if (LinBox* box = BoxManager::instance()->findBoxContainingItem(normalizedPath)) {
            if (DesktopRootItemState* state = box->findItemStateByPath(normalizedPath)) {
                state->model.visible = !hidden;
                dirtyBoxes.insert(box);
            }
        }
    }

    for (LinBox* box : dirtyBoxes) {
        if (!box) {
            continue;
        }
        box->refreshLayout();
        box->update();
    }

    if (desktopDirty) {
        updateAllItemRenderData();
        rebuildSpatialIndex();
        update();
    }
}

void ListView::trackPendingExternalRemovalPaths(const QStringList& paths)
{
    for (const QString& rawPath : paths) {
        const QString normalizedPath = normalizeDesktopPath(rawPath);
        if (!normalizedPath.isEmpty()) {
            m_pendingExternalRemovalPaths.insert(normalizedPath);
        }
    }
}

void ListView::removeDesktopStateByPath(const QString& filePath)
{
    const QString normalizedPath = normalizeDesktopItemPath(filePath);
    if (normalizedPath.isEmpty()) {
        return;
    }

    for (auto it = m_itemStates.begin(); it != m_itemStates.end(); ) {
        if (normalizeDesktopItemPath(it->model.filePath).compare(normalizedPath, Qt::CaseInsensitive) == 0) {
            const QRect dirty = desktopRootItemHighlightDirtyRect(it->model.renderData);
            const QString foundPath = normalizeDesktopItemPath(it->model.filePath);
            it = m_itemStates.erase(it);
            markDesktopStateIndexDirty();
            markSpatialIndexDirty();
            if (m_hoveredDesktopPath.compare(foundPath, Qt::CaseInsensitive) == 0) {
                m_hoveredDesktopPath.clear();
            }
            m_selectedDesktopPathSet.remove(foundPath);
            m_selectedDesktopPaths.removeOne(foundPath);
            m_selectedDesktopPathSet.remove(normalizedPath);
            m_selectedDesktopPaths.removeOne(normalizedPath);
            if (m_focusedDesktopPath.compare(foundPath, Qt::CaseInsensitive) == 0) {
                m_focusedDesktopPath.clear();
            }
            if (m_anchorDesktopPath.compare(foundPath, Qt::CaseInsensitive) == 0) {
                m_anchorDesktopPath.clear();
            }
            if (m_lastClickedDesktopPath.compare(foundPath, Qt::CaseInsensitive) == 0) {
                m_lastClickedDesktopPath.clear();
            }
            if (m_draggedDesktopPath.compare(foundPath, Qt::CaseInsensitive) == 0) {
                m_draggedDesktopPath.clear();
            }
            update(dirty);
            break;
        } else {
            ++it;
        }
    }
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

DesktopRootItemState* ListView::findDesktopItemAt(const QPoint& pos)
{
    return findDesktopStateAt(pos);
}

const DesktopRootItemState* ListView::findDesktopItemAt(const QPoint& pos) const
{
    return findDesktopStateAt(pos);
}

QRect ListView::desktopSelectionRectForItem(const DesktopRootItemState& state) const
{
    return ListViewSelectionController::desktopSelectionRectForItem(*this, state);
}

void ListView::updateHoveredDesktopItem(const QPoint& pos)
{
    if (m_isSelecting) {
        return;
    }

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
    updateHoverState(m_hoveredDesktopPath, true);
}

void ListView::clearHoveredDesktopItem()
{
    if (m_hoveredDesktopPath.isEmpty()) {
        return;
    }

    updateHoveredDesktopItem(QPoint(-1, -1));
    m_hoveredDesktopPath.clear();
}


int ListView::scalePx(int logicalPx) const
{
    return DpiScaleHelper::scalePx(this, logicalPx);
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





























QVector<LinBox*> ListView::boxes() const {
    QVector<LinBox*> result;
    for (const QPointer<LinBox>& box : BoxManager::instance()->allBoxes()) {
        if (box) result.append(box.data());
    }
    return result;
}

bool ListView::desktopIconsVisible() const
{
    return m_iconShowRule.showDesktopIcons;
}

QVector<DesktopSnakeGameIcon> ListView::desktopSnakeIcons() const
{
    QVector<DesktopSnakeGameIcon> icons;

    auto appendIcon = [&icons](const DesktopRootItemState& state) {
        QPixmap pixmap = state.model.renderData.iconPixmap;
        if (pixmap.isNull() && !state.model.renderData.iconImage.isNull()) {
            pixmap = QPixmap::fromImage(state.model.renderData.iconImage);
        }
        if (pixmap.isNull() && !state.model.sourceIcon.isNull()) {
            pixmap = state.model.sourceIcon.pixmap(QSize(64, 64));
        }
        if (!pixmap.isNull()) {
            DesktopSnakeGameIcon icon;
            icon.pixmap = pixmap;
            icon.displayName = state.model.displayName.isEmpty()
                ? QFileInfo(state.model.filePath).completeBaseName()
                : state.model.displayName;
            icons.append(icon);
        }
    };

    for (const DesktopRootItemState& state : m_itemStates) {
        if (state.model.isReady) {
            appendIcon(state);
        }
    }

    for (const QPointer<LinBox>& boxPtr : BoxManager::instance()->allBoxes()) {
        const LinBox* box = boxPtr.data();
        if (!box) {
            continue;
        }
        for (const DesktopRootItemState& state : box->itemStates()) {
            if (state.model.isReady) {
                appendIcon(state);
            }
        }
    }

    return icons;
}

	// 闂備胶顭堢换鎰版偋婵犲洤钃熼柛銉戝苯娈ㄩ梺閫炲苯澧伴柟?item 闂備焦鐪归崝宀€鈧凹鍘介弲璺侯吋婢跺﹤鐝樻繝銏ｆ硾椤︻喚鎹㈤崱娑欑厱婵炲棙鍨堕悞璺ㄧ磼缂佹娲存鐐村浮婵＄兘顢涘鍜佹Х闂傚倸鍊搁崯顐㈩嚕閸洦鏁嗘繛鎴欏灪閻撳倻鈧箍鍎遍幏瀣ｆ繝姘骇闁冲搫鍟禒锕傛偨?
void ListView::resetRenameClickState()
{
    m_lastClickedDesktopPath.clear();
    m_lastClickedBoxPath.clear();
    m_wasSelectedBeforeClick = false;
    m_clickTimer.invalidate();
}



void ListView::createDragGhosts(const QPoint& clickPos)
{
    ListViewDragDropHelper::createDragGhosts(*this, clickPos);
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









QStringList ListView::mappedFolderEntryPaths(const QString& folderPath) const {
    return ListViewMappedBoxHelper::mappedFolderEntryPaths(folderPath, m_showHiddenFiles);
}

void ListView::clearMappedFolderItemsForBox(LinBox* box) {
    ListViewMappedBoxHelper::clearMappedFolderItemsForBox(*this, box);
}

void ListView::loadMappedFolderItemsForBox(LinBox* box) {
    ListViewMappedBoxHelper::loadMappedFolderItemsForBox(*this, box);
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





void ListView::moveDesktopGhostsForPreview(const QPoint& globalPos)
{
    ListViewDragDropHelper::moveDesktopGhostsForPreview(*this, globalPos);
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




void ListView::setShortcutArrowVisible(bool visible) {
    m_iconShowRule.showShortCutArrow = visible;
    
    markAllDesktopRenderDataDirty();
    updateAllItemRenderData();
    rebuildSpatialIndex();
    update();
    requestSaveBoxes();
}


