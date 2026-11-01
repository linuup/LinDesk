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
#include "desktop/widgets/runtime_export.h"
#include "desktop/runtime/RuntimeInstaller.h"
#include "desktop/widgets/linwidget/WidgetRegistryManager.h"




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
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (!bridge.isLoaded() || !bridge.isEnabled() || !bridge.isCefOsrEnabled()) {
        return {};
    }
    return bridge.occupiedRects();
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

QPoint ListView::localPosForGhost(const GhostInfo& ghost) const
{
    const QPoint mousePos = m_lastGhostMousePos.isNull() ? QCursor::pos() : m_lastGhostMousePos;
    return mapFromGlobal(mousePos) + ghost.offset;
}

void ListView::clearDragGhosts()
{
    if (m_combinedGhostWidget) {
        m_combinedGhostWidget->hide();
        m_combinedGhostWidget->deleteLater();
        m_combinedGhostWidget = nullptr;
    }
    for (auto& ghost : m_ghosts) {
        if (ghost.widget && ghost.widget != m_combinedGhostWidget) {
            ghost.widget->hide();
            ghost.widget->deleteLater();
        }
        ghost.widget = nullptr;
    }
    m_ghosts.clear();
}

void ListView::loadSelectionColors()
{
    QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
    const QColor baseColor(settings.value(QStringLiteral("Desktop/SelectionRectangleColor"), QStringLiteral("#64A0F0")).toString());
    const int opacity = qBound(20, settings.value(QStringLiteral("Desktop/SelectionRectangleOpacity"), 75).toInt(), 100);
    const int contrast = qBound(0, settings.value(QStringLiteral("Desktop/SelectionRectangleContrast"), 60).toInt(), 100);
    m_fluentSelectionEnabled = settings.value(QStringLiteral("Desktop/FluentSelectionMarquee"), true).toBool();
    m_selectionCountBadgeEnabled = settings.value(QStringLiteral("Desktop/SelectionCountBadgeEnabled"), false).toBool();
    m_fluentSelectionCornerRadius = qBound(0, settings.value(QStringLiteral("Desktop/SelectionCornerRadius"), 5).toInt(), 16);
    m_selectionBadgeRadius = qBound(0, settings.value(QStringLiteral("Desktop/SelectionBadgeRadius"), 11).toInt(), 11);
    m_selectionBorderWidth = qBound(0.0, settings.value(QStringLiteral("Desktop/SelectionBorderWidth"), 1.0).toReal(), 3.0);
    QColor fillColor = baseColor.isValid() ? baseColor : QColor(100, 160, 240);
    QColor borderColor = fillColor.darker(110);
    fillColor.setAlpha(qBound(35, opacity * 140 / 100, 180));
    borderColor.setAlpha(qBound(95, contrast * 230 / 100, 230));
    m_selectionFillColor = fillColor;
    m_selectionBorderColor = borderColor;
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


// 单例指针与图标尺寸初始化
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
    m_instance = this;
    setAcceptDrops(true);
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);

    setGeometry(QApplication::primaryScreen()->geometry());
    //setAutoFillBackground(true);

    monitor = new DesktopInfoMonitor(this);
    startListeningSpecialIcon();
    m_windowsConfigMonitor = new WindowsConfigMonitor(this);
    connect(m_windowsConfigMonitor, &WindowsConfigMonitor::showHiddenFilesChanged,
        this, &ListView::onWindowsShowHiddenFilesChanged);
    m_windowsConfigMonitor->startListening();
    m_showHiddenFiles = m_windowsConfigMonitor->showHiddenFiles();

    m_iconFadeTimer = new QTimer(this);
    m_iconFadeTimer->setInterval(16);
    connect(m_iconFadeTimer, &QTimer::timeout, this, &ListView::onIconFadeTimerTimeout);
    connect(&IconCacheManager::instance(), &IconCacheManager::iconLoaded, this, &ListView::onIconLoaded);

    m_fsRelayoutTimer = new QTimer(this);
    m_fsRelayoutTimer->setSingleShot(true);
    connect(m_fsRelayoutTimer, &QTimer::timeout, this, [this]() {
        if (m_iconShowRule.autoArrange) {
            applyAutoArrangeLayout(false, false);
        } else {
            updateAllItemRenderData();
            rebuildSpatialIndex();
            update();
        }
    });

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
    
    // 获取系统标准桌面路径
    QString desktopPath;
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &path))) {
        desktopPath = QString::fromWCharArray(path);
        CoTaskMemFree(path);
    } else {
        desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    }
    m_desktopDir = desktopPath;
    qDebug() << "[ListView] Desktop path:" << desktopPath;
    
    // 注册跨线程信号槽元类型
    qRegisterMetaType<FileSystemEventInfo>("FileSystemEventInfo");
    
    // 初始化并启动桌面目录监控
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

    // Initialize BoxManager
    BoxManager::instance(this);
    BoxManager::instance()->setPersistencePath(m_persistencePath);
    connect(BoxManager::instance(), &BoxManager::saveRequested, this, [this]() {
        saveBoxes();
    });

    this->initialize();
    this->initRightMenuConnections();
    this->syncViewModeSizes(); // Call syncViewModeSizes after initialization

    // Synchronously load all boxes and assign items before any window is displayed
    this->loadBoxes(true);

    const QString configPath = AppPaths::configFilePath();
    QSettings settings(configPath, QSettings::IniFormat);
    const bool isFirstLaunch = settings.value("Firslanuch", true).toBool();
    if (isFirstLaunch) {
        this->setPersistenceEnabled(true); 
        settings.setValue("Firslanuch", false);
        settings.sync();
    }

    // Initialize LinWidgets asynchronously in next event loop tick so startup is instantaneous
    QTimer::singleShot(0, this, [this]() {
        this->raise();
        this->initializeLinWidgets();
    });

    loadSelectionColors();
}

void* ListView::linWidgetManager()
{
    return LinWidgetRuntimeBridge::instance().widgetManager();
}

void ListView::openLinWidgetSettings(const QString& instanceId)
{
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (!bridge.isLoaded() || instanceId.trimmed().isEmpty()) {
        return;
    }
    // settings will be handled via bridge or settings dialog
}

void ListView::showWidgetContextMenu(const QString& instanceId, const QPoint& globalPos)
{
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (!bridge.isLoaded() || instanceId.trimmed().isEmpty()) {
        return;
    }

    const QRect curGeom = bridge.widgetGeometry(instanceId);
    QMenu menu(this);
    menu.setAttribute(Qt::WA_DeleteOnClose);
    menu.setStyleSheet(
        "QMenu { background: rgba(32, 34, 38, 245); border: 1px solid rgba(255, 255, 255, 38); border-radius: 8px; padding: 5px; color: #f0f0f0; font-size: 13px; }"
        "QMenu::item { padding: 6px 24px 6px 14px; border-radius: 4px; margin: 1px 2px; }"
        "QMenu::item:selected { background: rgba(255, 255, 255, 30); color: #ffffff; }"
        "QMenu::separator { height: 1px; background: rgba(255, 255, 255, 25); margin: 4px 6px; }"
    );

    QMenu* sizeSubMenu = menu.addMenu(QString::fromUtf8("尺寸规格 (Size)"));
    sizeSubMenu->setStyleSheet(menu.styleSheet());

    auto addSizeAction = [&](const QString& label, int w, int h) {
        QAction* act = sizeSubMenu->addAction(label);
        if (curGeom.width() == w && curGeom.height() == h) {
            act->setCheckable(true);
            act->setChecked(true);
        }
        connect(act, &QAction::triggered, this, [instanceId, w, h, curGeom]() {
            LinWidgetRuntimeBridge::instance().setWidgetGeometry(instanceId, QRect(curGeom.topLeft(), QSize(w, h)));
            if (ListView::instance()) {
                ListView::instance()->update();
            }
        });
    };

    addSizeAction(QString::fromUtf8("小卡片 (200 × 200)"), 200, 200);
    addSizeAction(QString::fromUtf8("标准条状 (360 × 200)"), 360, 200);
    addSizeAction(QString::fromUtf8("大卡片 (400 × 300)"), 400, 300);
    addSizeAction(QString::fromUtf8("全宽展板 (540 × 320)"), 540, 320);

    menu.addSeparator();

    QAction* actReload = menu.addAction(QString::fromUtf8("重新加载小组件"));
    connect(actReload, &QAction::triggered, this, [instanceId]() {
        LinWidgetRuntimeBridge::instance().restartWidget(instanceId);
    });

    QAction* actSettings = menu.addAction(QString::fromUtf8("小组件设置..."));
    connect(actSettings, &QAction::triggered, this, [this, instanceId]() {
        openLinWidgetSettings(instanceId);
    });

    menu.addSeparator();

    QAction* actClose = menu.addAction(QString::fromUtf8("关闭并移除组件"));
    connect(actClose, &QAction::triggered, this, [this, instanceId]() {
        LinWidgetRuntimeBridge::instance().stopWidget(instanceId);
        update();
    });

    menu.exec(globalPos);
}

int ListView::widgetResizeEdgeAt(const QPoint& clientPos, const QRect& widgetGeom, int hitMargin) const
{
    if (!widgetGeom.isValid() || widgetGeom.isEmpty()) {
        return ResizeEdgeNone;
    }

    const QRect outer = widgetGeom.adjusted(-hitMargin, -hitMargin, hitMargin, hitMargin);
    if (!outer.contains(clientPos)) {
        return ResizeEdgeNone;
    }

    int edge = ResizeEdgeNone;
    const bool nearLeft = std::abs(clientPos.x() - widgetGeom.left()) <= hitMargin;
    const bool nearRight = std::abs(clientPos.x() - widgetGeom.right()) <= hitMargin;
    const bool nearTop = std::abs(clientPos.y() - widgetGeom.top()) <= hitMargin;
    const bool nearBottom = std::abs(clientPos.y() - widgetGeom.bottom()) <= hitMargin;

    if (nearLeft) {
        edge |= ResizeEdgeLeft;
    } else if (nearRight) {
        edge |= ResizeEdgeRight;
    }

    if (nearTop) {
        edge |= ResizeEdgeTop;
    } else if (nearBottom) {
        edge |= ResizeEdgeBottom;
    }

    return edge;
}

Qt::CursorShape ListView::cursorForResizeEdge(int edge) const
{
    switch (edge) {
    case ResizeEdgeTopLeft:
    case ResizeEdgeBottomRight:
        return Qt::SizeFDiagCursor;
    case ResizeEdgeTopRight:
    case ResizeEdgeBottomLeft:
        return Qt::SizeBDiagCursor;
    case ResizeEdgeLeft:
    case ResizeEdgeRight:
        return Qt::SizeHorCursor;
    case ResizeEdgeTop:
    case ResizeEdgeBottom:
        return Qt::SizeVerCursor;
    case ResizeEdgeNone:
    default:
        return Qt::SizeAllCursor;
    }
}

int ListView::refreshWidgetResizeHint(const QPoint& pos, bool ctrlHeld)
{
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (!ctrlHeld || !bridge.isLoaded() || !bridge.isWidgetLayerEmbedded()) {
        if (!m_resizeHintWidgetId.isEmpty() && !m_resizingWidget && !m_draggingWidget) {
            m_resizeHintWidgetId.clear();
            unsetCursor();
            update();
        }
        return ResizeEdgeNone;
    }

    const QString wid = bridge.widgetAt(pos);
    if (wid.isEmpty()) {
        if (!m_resizeHintWidgetId.isEmpty() && !m_resizingWidget && !m_draggingWidget) {
            m_resizeHintWidgetId.clear();
            unsetCursor();
            update();
        }
        return ResizeEdgeNone;
    }

    const QRect geom = bridge.widgetGeometry(wid);
    const int edge = widgetResizeEdgeAt(pos, geom, 10);
    const Qt::CursorShape shape = cursorForResizeEdge(edge);
    setCursor(shape);

    if (m_resizeHintWidgetId != wid) {
        m_resizeHintWidgetId = wid;
        update();
    }

    return edge;
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
    const bool linWidgetsEnabled = settings.value("LinWidgets/Enabled", true).toBool();
    const bool showWidgetBounds = settings.value("LinWidgets/ShowWidgetBounds", false).toBool();
    setShowLinWidgetBlockedCells(settings.value("LinWidgets/ShowBlockedCells", false).toBool());

    auto& registry = LinWidget::WidgetRegistryManager::instance();
    QString registryError;
    registry.load(&registryError);
    registry.syncFromFolder(AppPaths::userLinWidgetsDir(), &registryError);
    registry.save(&registryError);

    // 运行时未安装时保持完全休眠状态，零开销
    if (!RuntimeInstaller::instance().isRuntimeInstalled()) {
        qDebug() << "[LinWidget] Runtime is not installed. Subsystem is in dormant state.";
        return;
    }

    // 尝试动态挂载已安装的运行时
    if (!RuntimeInstaller::instance().loadInstalledRuntime()) {
        qWarning() << "[LinWidget] Failed to load installed runtime.";
        return;
    }

    auto& bridge = LinWidgetRuntimeBridge::instance();
    LinWidgetDesktopCtx ctx = {};
    ctx.desktopWindowHwnd = reinterpret_cast<quintptr>(reinterpret_cast<HWND>(winId()));
    ctx.desktopWidgetPtr = this;
    ctx.onWidgetSettingsRequested = [](const char* instanceId, void* userData) {
        auto* lv = static_cast<ListView*>(userData);
        if (lv && instanceId) {
            lv->openLinWidgetSettings(QString::fromUtf8(instanceId));
        }
    };
    ctx.onImeCursorRectChanged = [](int x, int y, int w, int h, void* userData) {
        auto* lv = static_cast<ListView*>(userData);
        if (lv) {
            lv->m_imeCursorRect = (w > 0 && h > 0) ? QRect(x, y, w, h) : QRect();
            lv->updateMicroFocus();
        }
    };
    ctx.onCanvasNeedsRepaint = [](void* userData) {
        auto* lv = static_cast<ListView*>(userData);
        if (lv) {
            lv->update();
        }
    };
    ctx.userData = this;

    if (!bridge.init(ctx)) {
        qWarning() << "[LinWidget] Failed to initialize runtime bridge context.";
        return;
    }

    bridge.setEnabled(linWidgetsEnabled);
    bridge.setShowWidgetBounds(showWidgetBounds);
    setAttribute(Qt::WA_InputMethodEnabled, true);

    if (!linWidgetsEnabled) {
        qDebug() << "[LinWidgetAutoLoad] runtime disabled in config";
        return;
    }

    if (!bridge.restoreFromConfig(QString())) {
        qDebug() << "[LinWidgetAutoLoad] restore from config returned false or empty";
    }
}

void ListView::syncLinWidgetLayer()
{
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (!bridge.isLoaded()) {
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

    bridge.syncAllWidgetWindowLayers();
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
    
    // 规范化 CLSID 并匹配对应系统图标
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
    // 启动特殊图标监控并获取当前系统图标
    if (monitor->startListening()) {
        sysIconRegisterInfo = monitor->getCurrentIcons();
    }
    // 关联图标可见性变化信号
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

        // ListView 统一接管桌面画布的全部输入消息（包括桌面图标、LinBox、小组件与动态交互壁纸），统一由顶层状态机仲裁
        if (rect().contains(clientPos)) {
            if (result) {
                *result = HTCLIENT;
            }
            return true;
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
    if (m_instance == this) {
        m_instance = nullptr;
    }
    saveBoxes(false);
    
    // Clean up drag tip
    if (m_dragTip) {
        delete m_dragTip;
        m_dragTip = nullptr;
    }

    if (m_infoTip) {
        delete m_infoTip;
        m_infoTip = nullptr;
    }
    
    clearDragGhosts();

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


    auto& bridge = LinWidgetRuntimeBridge::instance();
    const bool runtimeLoaded = bridge.isLoaded();
    if (runtimeLoaded && bridge.hasActiveWallpaper() && !bridge.isWallpaperInteractive()) {
        painter.fillRect(event->rect(), QColor(0, 0, 0, 1));
    }

    const bool widgetsEmbedded = runtimeLoaded ? bridge.isWidgetLayerEmbedded() : true;

    // 1. 如果处于嵌入模式（小组件在图标下方），优先绘制 CEF OSR 画布
    if (widgetsEmbedded && runtimeLoaded) {
        bridge.renderCanvas(&painter, dirtyRegion);
    }

    if (m_hideDesktopItemsForRefresh) {
        return;
    }

    const bool isFading = (m_desktopHideAnim && m_desktopHideAnim->state() == QAbstractAnimation::Running);
    const qreal currentOpacity = isFading ? m_desktopElementsOpacity : (m_iconShowRule.showDesktopIcons ? 1.0 : 0.0);
    if (currentOpacity <= 0.0) {
        return;
    }
    painter.setOpacity(currentOpacity);

    auto paintDesktopState = [&](const DesktopRootItemState& state) {
        if (!shouldPaintDesktopModelState(state)) {
            return;
        }

        if (!dirtyRegion.intersects(state.model.renderData.itemRect) &&
            !dirtyRegion.intersects(state.model.renderData.textRect)) {
            return;
        }

        paintDesktopRootItemDynamic(painter, state.model.renderData);
        paintDesktopRootItemStatic(painter, state.model.renderData);
    };

    const QRect dirtyBoundingRect = dirtyRegion.boundingRect();
    const bool useSpatialIndex = m_spatialIndex &&
                                 !m_spatialIndexDirty &&
                                 m_spatialIndexBounds == this->rect() &&
                                 dirtyBoundingRect.isValid() &&
                                 dirtyBoundingRect != rect() &&
                                 dirtyRegion.rectCount() <= 20 &&
                                 m_itemStates.size() >= 16;

    if (useSpatialIndex) {
        const QRect queryRect = dirtyBoundingRect.adjusted(-32, -32, 32, 32);
        const QVector<DesktopRootItemState*> candidates = m_spatialIndex->query(queryRect);

        for (const DesktopRootItemState* state : candidates) {
            if (!state || state->model.renderData.expandedFullName) {
                continue;
            }
            paintDesktopState(*state);
        }

        for (const DesktopRootItemState* state : candidates) {
            if (!state || !state->model.renderData.expandedFullName) {
                continue;
            }
            paintDesktopState(*state);
        }
    } else {
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
    }

    // 2. 如果处于浮动模式（小组件在图标上方），后置绘制 CEF OSR 画布
    if (!widgetsEmbedded && runtimeLoaded) {
        bridge.renderCanvas(&painter, dirtyRegion);
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

    if (m_isSelecting && !m_renderedSelectionRect.isNull() && m_renderedSelectionRect.isValid() && dirtyRegion.intersects(m_renderedSelectionRect)) {
        painter.save();
        const QRect selRect = m_renderedSelectionRect;

        if (m_fluentSelectionEnabled) {
            painter.setRenderHint(QPainter::Antialiasing, true);
            const qreal radius = qBound(0.0, static_cast<qreal>(m_fluentSelectionCornerRadius), 16.0);
            const qreal bWidth = qBound(0.0, static_cast<qreal>(m_selectionBorderWidth), 3.0);

            // 1. Apple 级极简高透光光学质感填充 (Optical Glass Fill)
            // 绝不堆砌浓重发光与机械角标，通透、克制、自然，与壁纸完美融合
            QLinearGradient fillGrad(selRect.topLeft(), selRect.bottomRight());
            QColor topFill = m_selectionFillColor;
            topFill.setAlpha(qBound(24, topFill.alpha() * 70 / 100, 110));
            QColor bottomFill = m_selectionFillColor;
            bottomFill.setAlpha(qBound(16, bottomFill.alpha() * 45 / 100, 80));
            fillGrad.setColorAt(0.0, topFill);
            fillGrad.setColorAt(1.0, bottomFill);
            painter.setBrush(fillGrad);

            // 2. 选框边框 (支持 0px 无描边 ~ 3px)
            if (bWidth > 0.05) {
                QColor strokeColor = m_selectionBorderColor;
                strokeColor.setAlpha(qBound(110, strokeColor.alpha() * 95 / 100, 220));
                painter.setPen(QPen(strokeColor, bWidth));
                const qreal halfW = bWidth / 2.0;
                if (radius > 0.1) {
                    painter.drawRoundedRect(QRectF(selRect).adjusted(halfW, halfW, -halfW, -halfW), radius, radius);
                } else {
                    painter.drawRect(QRectF(selRect).adjusted(halfW, halfW, -halfW, -halfW));
                }
            } else {
                painter.setPen(Qt::NoPen);
                if (radius > 0.1) {
                    painter.drawRoundedRect(QRectF(selRect), radius, radius);
                } else {
                    painter.drawRect(QRectF(selRect));
                }
            }

            // 3. 极细顶沿玻璃微光折射 (仅在有描边且有一定尺寸时渲染)
            if (bWidth > 0.05 && selRect.width() > 14 && selRect.height() > 10) {
                painter.setPen(QPen(QColor(255, 255, 255, 45), 1.0));
                const qreal inset = radius > 0.1 ? (radius + 1.0) : 1.0;
                painter.drawLine(QPointF(selRect.left() + inset, selRect.top() + bWidth / 2.0),
                                 QPointF(selRect.right() - inset, selRect.top() + bWidth / 2.0));
            }

            // 4. 独立开关控制的极简轻量微胶囊徽章 (纯净数字，彻底移除任何圆点)
            if (m_selectionCountBadgeEnabled) {
                const int count = m_selectedDesktopPaths.size();
                if (count > 0 && selRect.width() >= 44 && selRect.height() >= 28) {
                    const QString badgeText = QString::number(count);
                    QFont badgeFont = painter.font();
                    badgeFont.setPixelSize(10);
                    badgeFont.setWeight(QFont::Medium);
                    painter.setFont(badgeFont);

                    QFontMetrics fm(badgeFont);
                    const int textW = fm.horizontalAdvance(badgeText);
                    const int badgeH = 18;
                    const int padH = 6;
                    const int badgeW = qMax(18, textW + padH * 2);

                    const qreal badgeCorner = qBound(0.0, static_cast<qreal>(m_selectionBadgeRadius), badgeH / 2.0);
                    const qreal badgeX = selRect.right() - badgeW - 5;
                    const qreal badgeY = selRect.bottom() - badgeH - 5;
                    const QRectF badgeRect(badgeX, badgeY, badgeW, badgeH);

                    // a. 极轻柔环境微阴影
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(QColor(0, 0, 0, 40));
                    painter.drawRoundedRect(badgeRect.translated(0, 1.0), badgeCorner, badgeCorner);

                    // b. 半透明极简微磨砂黑底
                    painter.setBrush(QColor(22, 24, 28, 205));

                    // c. 0.75px 极细微透描边
                    painter.setPen(QPen(QColor(255, 255, 255, 40), 0.75));
                    painter.drawRoundedRect(badgeRect, badgeCorner, badgeCorner);

                    // d. 纯白清晰数字 (无红点，无任何多余元素，纯净通透)
                    painter.setPen(QColor(250, 252, 255));
                    painter.drawText(badgeRect, Qt::AlignCenter, badgeText);
                }
            }
        } else {
            // 经典 Windows 原生矩形选框 (先填充内部半透明底色，再绘制可选边框)
            const qreal bWidth = qBound(0.0, m_selectionBorderWidth, 3.0);
            painter.fillRect(selRect, m_selectionFillColor);
            if (bWidth > 0.05) {
                painter.setPen(QPen(m_selectionBorderColor, bWidth));
                painter.setBrush(Qt::NoBrush);
                const qreal halfW = bWidth / 2.0;
                painter.drawRect(QRectF(selRect).adjusted(halfW, halfW, -halfW, -halfW));
            }
        }

        painter.restore();
    }

    // 绘制小组件在按住 Ctrl 调整大小时的高亮辅助框和手柄
    const QString activeResizeId = m_resizingWidget ? m_resizingWidgetId :
                                  (m_draggingWidget ? m_draggedWidgetId : m_resizeHintWidgetId);
    if (!activeResizeId.isEmpty() && runtimeLoaded) {
        const QRect widgetGeom = bridge.widgetGeometry(activeResizeId);
        if (widgetGeom.isValid()) {
            painter.save();
            painter.setRenderHint(QPainter::Antialiasing, true);

            // 1. Fluent 强调色发光高亮边框
            QColor accentColor(0, 120, 215);
            painter.setPen(QPen(accentColor, 2.0));
            painter.setBrush(QColor(accentColor.red(), accentColor.green(), accentColor.blue(), 24));
            painter.drawRoundedRect(widgetGeom, 6, 6);

            // 2. 8个控制点小手柄 (Corners & Midpoints)
            const int handleSize = 7;
            const int half = handleSize / 2;
            const QPoint handles[8] = {
                widgetGeom.topLeft(),
                QPoint(widgetGeom.center().x(), widgetGeom.top()),
                widgetGeom.topRight(),
                QPoint(widgetGeom.right(), widgetGeom.center().y()),
                widgetGeom.bottomRight(),
                QPoint(widgetGeom.center().x(), widgetGeom.bottom()),
                widgetGeom.bottomLeft(),
                QPoint(widgetGeom.left(), widgetGeom.center().y())
            };

            for (const QPoint& pt : handles) {
                QRect handleRect(pt.x() - half, pt.y() - half, handleSize, handleSize);
                painter.setPen(QPen(accentColor, 1.5));
                painter.setBrush(Qt::white);
                painter.drawEllipse(handleRect);
            }

            // 3. 尺寸胶囊徽章 (e.g. 320 × 240)
            const QString sizeText = QString("%1 × %2").arg(widgetGeom.width()).arg(widgetGeom.height());
            QFont tipFont = painter.font();
            tipFont.setPixelSize(11);
            tipFont.setWeight(QFont::Medium);
            painter.setFont(tipFont);
            QFontMetrics fm(tipFont);
            const int textW = fm.horizontalAdvance(sizeText);
            const int badgeW = textW + 16;
            const int badgeH = 22;
            int badgeX = widgetGeom.center().x() - badgeW / 2;
            int badgeY = widgetGeom.top() - badgeH - 6;
            if (badgeY < 10) {
                badgeY = widgetGeom.top() + 8;
            }
            QRect badgeRect(badgeX, badgeY, badgeW, badgeH);

            painter.setPen(QPen(QColor(255, 255, 255, 60), 1.0));
            painter.setBrush(QColor(20, 20, 20, 210));
            painter.drawRoundedRect(badgeRect, 11, 11);

            painter.setPen(Qt::white);
            painter.drawText(badgeRect, Qt::AlignCenter, sizeText);

            painter.restore();
        }
    }
}

void ListView::contextMenuEvent(QContextMenuEvent* event)
{
#ifdef Q_OS_WIN
    logListViewDpiTrace("contextMenu", this, event->pos(), event->globalPos());
#endif
    m_pendingWallpaperClick = false;
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

        auto& bridge = LinWidgetRuntimeBridge::instance();
        if (bridge.isLoaded() && bridge.isWidgetLayerEmbedded() && bridge.isWidgetAt(event->pos())) {
            const QString wid = bridge.widgetAt(event->pos());
            if (!wid.isEmpty()) {
                clearCheckedItem();
                showWidgetContextMenu(wid, mapToGlobal(event->pos()));
                event->accept();
                return;
            }
        }

        auto rightClickPoint = mapToGlobal(event->pos());
        this->m_lastRightClickPos = event->pos();

        // Since ListItems now intercept their own contextMenuEvent and emit signals,
        // this handler is only reached if the background (desktop) is clicked.
        
        // 点击背景时清空选中项
        clearCheckedItem();
        // 弹出桌面背景系统右键菜单
        requestSystemRightMenu(ObjectType::BackGround, rightClickPoint);
    }
}

void ListView::mousePressEvent(QMouseEvent* event)
{
    auto& bridge = LinWidgetRuntimeBridge::instance();
    const bool ctrlHeld = (event->modifiers() & Qt::ControlModifier) != 0 || (GetAsyncKeyState(VK_CONTROL) & 0x8000);
    if (bridge.isLoaded()) {
        if (bridge.isWidgetLayerEmbedded() && !findDesktopStateAt(event->pos()) && bridge.isWidgetAt(event->pos())) {
            if (ctrlHeld && event->button() == Qt::LeftButton) {
                clearSelectionState();
                const QString wid = bridge.widgetAt(event->pos());
                const QRect geom = bridge.widgetGeometry(wid);
                const int edge = widgetResizeEdgeAt(event->pos(), geom, 10);
                if (edge != ResizeEdgeNone) {
                    m_resizingWidget = true;
                    m_resizingWidgetId = wid;
                    m_widgetResizeEdge = edge;
                    m_widgetResizeStartPos = event->pos();
                    m_widgetResizeStartGeometry = geom;
                    setCursor(cursorForResizeEdge(edge));
                } else {
                    m_draggingWidget = true;
                    m_draggedWidgetId = wid;
                    m_widgetDragStartPos = event->pos();
                    m_widgetDragStartGeometry = geom;
                    setCursor(Qt::SizeAllCursor);
                }
                this->activateWindow();
                this->setFocus(Qt::MouseFocusReason);
                update();
                event->accept();
                return;
            }

            clearSelectionState();
            if (bridge.forwardMouseEvent(event)) {
                m_forwardingMouseToWidget = true;
                this->activateWindow();
                this->setFocus(Qt::MouseFocusReason);
                event->accept();
                return;
            }
        } else {
            bridge.clearWidgetFocus();
        }
    }

#ifdef Q_OS_WIN
    if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
        logListViewDpiTrace("mousePress", this, event->pos(), event->globalPos());
    }
#endif
    RenameManager::instance()->cancelRename();
    if (m_hideDesktopItemsForRefresh) {
        m_hideDesktopItemsForRefresh = false;
        for (const auto& box : BoxManager::instance()->allBoxes()) {
            if (box && !box->isVisible()) {
                box->show();
            }
        }
        update();
    }
    if (event->button() == Qt::LeftButton) {
        if (DesktopRootItemState* hitState = findDesktopStateAt(event->pos())) {
            clearWidgetFocus();
            this->activateWindow();
            this->setFocus(Qt::MouseFocusReason);

            const QString itemPath = normalizeDesktopItemPath(hitState->model.filePath);
            Qt::KeyboardModifiers modifiers = event->modifiers() | QApplication::keyboardModifiers();
            if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) {
                modifiers |= Qt::ControlModifier;
            }
            if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) {
                modifiers |= Qt::ShiftModifier;
            }

            if (modifiers & Qt::ControlModifier) {
                const bool selected = isDesktopPathSelected(itemPath);
                setDesktopItemSelected(itemPath, !selected);
                m_focusedDesktopPath = itemPath;
                if (m_anchorDesktopPath.isEmpty()) {
                    m_anchorDesktopPath = itemPath;
                }
                m_wasSelectedBeforeClick = false;
            } else if (modifiers & Qt::ShiftModifier) {
                selectRangeToPath(itemPath, nullptr);
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
        clearWidgetFocus();

        const bool hasInteractiveWallpaper = bridge.isLoaded() &&
                                             bridge.hasActiveWallpaper() &&
                                             bridge.isWallpaperInteractive();
        if (hasInteractiveWallpaper) {
            // 待决的壁纸点击：记录按下点与按钮，进入拖拽防抖仲裁（等待判断用户是原地点击壁纸还是拉框选桌面图标）
            m_pendingWallpaperClick = true;
            m_pendingWallpaperPressPos = event->pos();
            m_pendingWallpaperButton = event->button();
            event->accept();
            return;
        }

        beginBackgroundSelection(event->pos());
        event->accept();
        return;
    }

    event->ignore();
}

void ListView::mouseMoveEvent(QMouseEvent* event)
{
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (m_resizingWidget && bridge.isLoaded()) {
        const int dx = event->pos().x() - m_widgetResizeStartPos.x();
        const int dy = event->pos().y() - m_widgetResizeStartPos.y();
        QRect newGeom = m_widgetResizeStartGeometry;

        if (m_widgetResizeEdge & ResizeEdgeLeft) {
            newGeom.setLeft(m_widgetResizeStartGeometry.left() + dx);
        }
        if (m_widgetResizeEdge & ResizeEdgeRight) {
            newGeom.setRight(m_widgetResizeStartGeometry.right() + dx);
        }
        if (m_widgetResizeEdge & ResizeEdgeTop) {
            newGeom.setTop(m_widgetResizeStartGeometry.top() + dy);
        }
        if (m_widgetResizeEdge & ResizeEdgeBottom) {
            newGeom.setBottom(m_widgetResizeStartGeometry.bottom() + dy);
        }

        if (newGeom.width() < 60) {
            if (m_widgetResizeEdge & ResizeEdgeLeft) {
                newGeom.setLeft(newGeom.right() - 60);
            } else {
                newGeom.setRight(newGeom.left() + 60);
            }
        }
        if (newGeom.height() < 60) {
            if (m_widgetResizeEdge & ResizeEdgeTop) {
                newGeom.setTop(newGeom.bottom() - 60);
            } else {
                newGeom.setBottom(newGeom.top() + 60);
            }
        }

        bridge.setWidgetGeometry(m_resizingWidgetId, newGeom);
        setCursor(cursorForResizeEdge(m_widgetResizeEdge));
        update();
        event->accept();
        return;
    }

    if (m_draggingWidget && bridge.isLoaded()) {
        const int dx = event->pos().x() - m_widgetDragStartPos.x();
        const int dy = event->pos().y() - m_widgetDragStartPos.y();
        QRect newGeom = m_widgetDragStartGeometry.translated(dx, dy);

        QRect screenRect = rect();
        newGeom.moveLeft(qBound(0, newGeom.left(), qMax(0, screenRect.width() - newGeom.width())));
        newGeom.moveTop(qBound(0, newGeom.top(), qMax(0, screenRect.height() - newGeom.height())));

        bridge.setWidgetGeometry(m_draggedWidgetId, newGeom);
        setCursor(Qt::SizeAllCursor);
        update();
        event->accept();
        return;
    }

    if (bridge.isLoaded()) {
        if (m_forwardingMouseToWidget) {
            if (bridge.forwardMouseEvent(event)) {
                event->accept();
                return;
            }
        }
    }

    // 拖拽阈值仲裁：检查是否有待决的壁纸点击
    if (m_pendingWallpaperClick) {
        const int dragDistance = (event->pos() - m_pendingWallpaperPressPos).manhattanLength();
        if (dragDistance > QApplication::startDragDistance()) {
            // 移动超出防抖阈值（通常 6px）：用户意图明确为“桌面框选”，放弃壁纸点击，立即启动桌面框选
            m_pendingWallpaperClick = false;
            beginBackgroundSelection(m_pendingWallpaperPressPos);
            QRect rect;
            updateBackgroundSelection(event->pos(), &rect);
            event->accept();
            return;
        }
        // 仍在 6px 防抖阈值内，暂缓派发
        event->accept();
        return;
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
        updateBackgroundSelection(event->pos(), &rect);
        event->accept();
        return;
    }

    // 正常移动状态：
    // 1. 如果光标位于小组件上方，判断是否按住 Ctrl
    const bool ctrlHeld = (event->modifiers() & Qt::ControlModifier) != 0 || (GetAsyncKeyState(VK_CONTROL) & 0x8000);
    if (bridge.isLoaded() && bridge.isWidgetLayerEmbedded() && !findDesktopStateAt(event->pos()) && bridge.isWidgetAt(event->pos())) {
        if (ctrlHeld) {
            refreshWidgetResizeHint(event->pos(), true);
        } else {
            if (!m_resizeHintWidgetId.isEmpty()) {
                refreshWidgetResizeHint(event->pos(), false);
            }
            bridge.forwardMouseEvent(event);
        }
    } else {
        if (!m_resizeHintWidgetId.isEmpty() && !m_resizingWidget && !m_draggingWidget) {
            refreshWidgetResizeHint(event->pos(), false);
        }
    }

    // 2. 桌面图标响应 hover
    updateHoveredDesktopItem(event->pos());

    // 3. 动态壁纸粒子跟随 (非互斥广播，60fps 节流)
    if (bridge.isLoaded() && bridge.hasActiveWallpaper() && bridge.isWallpaperInteractive()) {
        bridge.forwardMouseMoveToWallpaper(event->pos());
    }

    event->ignore();

}

void ListView::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_resizingWidget) {
        m_resizingWidget = false;
        m_resizingWidgetId.clear();
        m_widgetResizeEdge = ResizeEdgeNone;
        unsetCursor();
        update();
        event->accept();
        return;
    }

    if (m_draggingWidget) {
        m_draggingWidget = false;
        m_draggedWidgetId.clear();
        unsetCursor();
        update();
        event->accept();
        return;
    }

    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (bridge.isLoaded()) {
        if (m_forwardingMouseToWidget) {
            m_forwardingMouseToWidget = false;
            if (bridge.forwardMouseEvent(event)) {
                event->accept();
                return;
            }
        }
    }

    // 1. 待决的壁纸点击（原地释放，移动距离未超出 6px）
    if (m_pendingWallpaperClick) {
        m_pendingWallpaperClick = false;
        // 桌面空白处点击常规行为：清空图标选中
        clearCheckedItem();
        // 派发 Click 给动态壁纸 (Press + Release)
        if (bridge.isLoaded() && bridge.hasActiveWallpaper() && bridge.isWallpaperInteractive()) {
            bridge.forwardMouseClickToWallpaper(m_pendingWallpaperPressPos, m_pendingWallpaperButton, false, 1);
            bridge.forwardMouseClickToWallpaper(event->pos(), event->button(), true, 1);
        }
        event->accept();
        return;
    }

    // 2. 图标拖拽结束
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
    
    // 3. 桌面框选结束
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

void ListView::clearWidgetFocus()
{
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (bridge.isLoaded()) {
        bridge.clearWidgetFocus();
    }
}

void ListView::focusInEvent(QFocusEvent* event)
{
    clearWidgetFocus();
    QWidget::focusInEvent(event);
}

void ListView::focusOutEvent(QFocusEvent* event)
{
    m_pendingWallpaperClick = false;
    QWidget::focusOutEvent(event);
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
    m_spatialIndexDirty = true;
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
    states.reserve(m_itemStates.size());
    for (auto& state : m_itemStates) {
        if (state.model.visible && state.model.boxId.isEmpty()) {
            states.append(&state);
        }
    }

    m_spatialIndex->rebuild(states);
    m_spatialIndexBounds = currentBounds;
    m_spatialIndexDirty = false;
}

QVector<const DesktopRootItemState*> ListView::queryItemsInRect(const QRect& rect) const
{
    QVector<const DesktopRootItemState*> results;
    if (rect.isEmpty()) {
        return results;
    }

    if (m_spatialIndexDirty || !m_spatialIndex || m_spatialIndexBounds != this->rect()) {
        const_cast<ListView*>(this)->rebuildSpatialIndex();
    }

    if (m_spatialIndex) {
        const QVector<DesktopRootItemState*> candidates = m_spatialIndex->query(rect);
        const DesktopRootItemState* startPtr = m_itemStates.isEmpty() ? nullptr : m_itemStates.constData();
        const DesktopRootItemState* endPtr = startPtr ? (startPtr + m_itemStates.size()) : nullptr;
        results.reserve(candidates.size());
        for (const DesktopRootItemState* state : candidates) {
            if (state && startPtr && state >= startPtr && state < endPtr) {
                if (state->model.renderData.itemRect.intersects(rect)) {
                    results.push_back(state);
                }
            }
        }
    }
    return results;
}





void ListView::mouseDoubleClickEvent(QMouseEvent* event)
{
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (bridge.isLoaded() && bridge.isWidgetLayerEmbedded() && !findDesktopStateAt(event->pos()) && bridge.isWidgetAt(event->pos())) {
        if (bridge.forwardMouseEvent(event)) {
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

        // 空白处且未启用双击隐藏图标时，可将双击转发给壁纸
        if (bridge.isLoaded() && bridge.hasActiveWallpaper() && bridge.isWallpaperInteractive()) {
            bridge.forwardMouseClickToWallpaper(event->pos(), event->button(), false, 2);
            event->accept();
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

void ListView::wheelEvent(QWheelEvent* event)
{
    auto& bridge = LinWidgetRuntimeBridge::instance();
    const bool ctrlHeld = (event->modifiers() & Qt::ControlModifier) != 0 || (GetAsyncKeyState(VK_CONTROL) & 0x8000);

    if (bridge.isLoaded() && bridge.isWidgetLayerEmbedded() && !findDesktopStateAt(event->pos()) && bridge.isWidgetAt(event->pos())) {
        if (ctrlHeld) {
            const QString wid = bridge.widgetAt(event->pos());
            if (!wid.isEmpty()) {
                const QRect curGeom = bridge.widgetGeometry(wid);
                if (curGeom.isValid() && curGeom.width() > 0 && curGeom.height() > 0) {
                    const int deltaY = event->angleDelta().y();
                    if (deltaY != 0) {
                        const qreal factor = (deltaY > 0) ? 1.08 : 0.92;
                        const int newW = qRound(curGeom.width() * factor);
                        const int newH = qRound(curGeom.height() * factor);

                        const QPoint mousePos = event->pos();
                        qreal rx = (qreal)(mousePos.x() - curGeom.left()) / curGeom.width();
                        qreal ry = (qreal)(mousePos.y() - curGeom.top()) / curGeom.height();
                        rx = qBound(0.0, rx, 1.0);
                        ry = qBound(0.0, ry, 1.0);

                        int newLeft = mousePos.x() - qRound(newW * rx);
                        int newTop = mousePos.y() - qRound(newH * ry);

                        newLeft = qBound(0, newLeft, qMax(0, width() - newW));
                        newTop = qBound(0, newTop, qMax(0, height() - newH));

                        QRect newGeom(newLeft, newTop, newW, newH);
                        bridge.setWidgetGeometry(wid, newGeom);
                        m_resizeHintWidgetId = wid;
                        update();
                        event->accept();
                        return;
                    }
                }
            }
        } else {
            if (bridge.forwardWheelEvent(event)) {
                event->accept();
                return;
            }
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

    // 转发给动态交互壁纸（如缩放或粒子滚动交互）
    if (bridge.isLoaded() && bridge.hasActiveWallpaper() && bridge.isWallpaperInteractive()) {
        if (bridge.forwardWheelToWallpaper(event->pos(), event->angleDelta())) {
            event->accept();
            return;
        }
    }

    QWidget::wheelEvent(event);
}

void ListView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Control) {
        refreshWidgetResizeHint(mapFromGlobal(QCursor::pos()), true);
    }
    const bool hasDesktopSelection = !m_selectedDesktopPaths.isEmpty() || !m_focusedDesktopPath.isEmpty() ||
                                     !selectedBoxPaths().isEmpty() || !focusedItemPath().isEmpty();
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (hasDesktopSelection) {
        if (bridge.isLoaded()) {
            bridge.clearWidgetFocus();
        }
    } else if (bridge.isLoaded() && bridge.forwardKeyEvent(event)) {
        event->accept();
        return;
    }
    // 键盘按键事件处理：检测修饰键与快捷键组合
    const bool shiftPressed = (event->modifiers() & Qt::ShiftModifier) || ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
    const bool ctrlPressed = (event->modifiers() & Qt::ControlModifier) || ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0);

    const bool shortcutUndo = (ctrlPressed && event->key() == Qt::Key_Z) || event->matches(QKeySequence::Undo);
    if (shortcutUndo) {
        if (handleUndo()) {
            event->accept();
            qDebug() << "Ctrl+Z Undo triggered";
            return;
        }
    }

    const bool extendSelection = shiftPressed;
    const bool shortcutPaste = (ctrlPressed && event->key() == Qt::Key_V) || event->matches(QKeySequence::Paste);
    const bool shortcutNewFolder = ctrlPressed && extendSelection && (event->key() == Qt::Key_N);

    // Delete 键处理（包括 Shift+Delete 永久删除与常规删除）
    if (event->key() == Qt::Key_Delete) {
        onMenuDelete(shiftPressed);
        event->accept();
        qDebug() << QString("Delete triggered: permanent=%1").arg(shiftPressed ? "Yes" : "No");
        return;
    }

    if ((ctrlPressed && event->key() == Qt::Key_A) || event->matches(QKeySequence::SelectAll)) {
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
    if ((ctrlPressed && event->key() == Qt::Key_C) || event->matches(QKeySequence::Copy)) {
        writeSelectionToClipboard(false);
        event->accept();
        qDebug() << "Copy triggered";
        return;
    }
    if ((ctrlPressed && event->key() == Qt::Key_X) || event->matches(QKeySequence::Cut)) {
        writeSelectionToClipboard(true);
        event->accept();
        qDebug() << "Cut triggered";
        return;
    }
    if (shortcutPaste) {
        clearCutMarks();
        onMenuPaste();
        event->accept();
        qDebug() << "Paste triggered";
        return;
    }
    
    if (ctrlPressed && (event->modifiers() & Qt::ShiftModifier) && event->key() == Qt::Key_V) {
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

    
    const QString text = event->text();
    if (!text.isEmpty() && text.at(0).isPrint() &&
        !ctrlPressed && !(event->modifiers() & Qt::AltModifier) &&
        event->key() != Qt::Key_Escape && event->key() != Qt::Key_Return && event->key() != Qt::Key_Enter &&
        event->key() != Qt::Key_Delete && event->key() != Qt::Key_Backspace && event->key() != Qt::Key_Tab &&
        event->key() != Qt::Key_Space) {
        if (handleTypeToSelect(text, nullptr)) {
            event->accept();
            return;
        }
    }

    QWidget::keyPressEvent(event);
}

void ListView::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Control) {
        refreshWidgetResizeHint(mapFromGlobal(QCursor::pos()), false);
    }
    const bool hasDesktopSelection = !m_selectedDesktopPaths.isEmpty() || !m_focusedDesktopPath.isEmpty() ||
                                     !selectedBoxPaths().isEmpty() || !focusedItemPath().isEmpty();
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (!hasDesktopSelection && bridge.isLoaded() && bridge.forwardKeyEvent(event)) {
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void ListView::inputMethodEvent(QInputMethodEvent* event)
{
    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (bridge.isLoaded() && bridge.forwardInputMethodEvent(event)) {
        event->accept();
        return;
    }
    QWidget::inputMethodEvent(event);
}

QVariant ListView::inputMethodQuery(Qt::InputMethodQuery query) const
{
    if ((query == Qt::ImMicroFocus || query == Qt::ImCursorRectangle) && m_imeCursorRect.isValid()) {
        return m_imeCursorRect;
    }
    return QWidget::inputMethodQuery(query);
}

void ListView::enterEvent(QEvent* event)
{
    QWidget::enterEvent(event);
}

void ListView::leaveEvent(QEvent* event)
{
    if (!m_resizeHintWidgetId.isEmpty() && !m_resizingWidget && !m_draggingWidget) {
        m_resizeHintWidgetId.clear();
        unsetCursor();
        update();
    }
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
    QSettings deskSettings(AppPaths::configFilePath(), QSettings::IniFormat);
    m_iconShowRule.doubleClickHideIcons = deskSettings.value(QStringLiteral("Desktop/DoubleClickHideIcons"), true).toBool();
    m_fadeTransitionOnHide = deskSettings.value(QStringLiteral("Desktop/FadeTransitionOnHide"), true).toBool();
    // === 读取 Windows 原生桌面配置（自动排列、对齐网格）===
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
    
    // 同步持久化存储
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
/// 初始化桌面右键菜单信号与槽连接
/// </summary>


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

    const qreal dpr = qMax<qreal>(1.0, devicePixelRatioF());
    const int newTier = ListItemData::calculateIconSourceTier(qRound(m_iconWidth * dpr));
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
    params.devicePixelRatio = qMax<qreal>(1.0, devicePixelRatioF());
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

bool ListView::addDesktopModelItemFromPath(const QString& filePath, bool deferRelayout)
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
    markSpatialIndexDirty();
    markDesktopRenderDataDirty(&m_itemStates.back());

    if (deferRelayout) {
        if (m_fsRelayoutTimer) {
            m_fsRelayoutTimer->start(120);
        }
        return true;
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
        const DesktopRootItemState* startPtr = m_itemStates.isEmpty() ? nullptr : m_itemStates.constData();
        const DesktopRootItemState* endPtr = startPtr ? (startPtr + m_itemStates.size()) : nullptr;
        for (DesktopRootItemState* state : candidates) {
            if (state && startPtr && state >= startPtr && state < endPtr) {
                if (shouldPaintDesktopModelState(*state) && state->model.renderData.itemRect.contains(pos)) {
                    return state;
                }
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

        if (DesktopRootItemState* state = findDesktopStateByPath(normalizedPath)) {
            IconCacheManager::instance().invalidateItemCache(state->model.iconCacheKey, normalizedPath);
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
            IconCacheManager::instance().invalidateItemCache(it->model.iconCacheKey, normalizedPath);
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
    const bool isFading = (m_desktopHideAnim && m_desktopHideAnim->state() == QAbstractAnimation::Running);
    if (!m_iconShowRule.showDesktopIcons && (!isFading || m_desktopElementsOpacity <= 0.0)) {
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

// 重置重命名点击检测状态
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



void ListView::onIconLoaded(const QString& memKey, const QString& filePath)
{
    Q_UNUSED(memKey);
    DesktopRootItemState* state = findDesktopStateByPath(filePath);
    if (state && state->model.visible) {
        state->renderDataDirty = true;
        m_fadingIconStartTimes[filePath] = QDateTime::currentMSecsSinceEpoch();
        if (!m_iconFadeTimer->isActive()) {
            m_iconFadeTimer->start();
        }
        update(state->model.renderData.itemRect);
    }
}

void ListView::onIconFadeTimerTimeout()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool anyFading = false;
    for (auto it = m_fadingIconStartTimes.begin(); it != m_fadingIconStartTimes.end();) {
        const qint64 elapsed = now - it.value();
        DesktopRootItemState* state = findDesktopStateByPath(it.key());
        if (state && state->model.visible) {
            if (elapsed < 100) {
                state->model.renderData.iconOpacity = qBound(0.1, (qreal)elapsed / 100.0, 1.0);
                update(state->model.renderData.itemRect);
                anyFading = true;
                ++it;
            } else {
                state->model.renderData.iconOpacity = 1.0;
                prepareDesktopRootItemStaticContentCache(state->model.renderData);
                update(state->model.renderData.itemRect);
                it = m_fadingIconStartTimes.erase(it);
            }
        } else {
            it = m_fadingIconStartTimes.erase(it);
        }
    }
    if (!anyFading || m_fadingIconStartTimes.isEmpty()) {
        m_iconFadeTimer->stop();
    }
}
