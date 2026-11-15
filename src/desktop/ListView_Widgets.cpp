#include "desktop/ListView_Internal.h"
#include "ui/hub/WallpaperStudioWindow.h"

// ============================================================================
// ListView LinWidgets Host Integration & Blocked Cells Overlay Implementation
// ============================================================================

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


bool ListView::rectIntersectsDesktopBlockedArea(const QRect& rect) const
{
    for (const QRect& componentRect : desktopBlockedOccupiedRects()) {
        if (rect.intersects(componentRect)) {
            return true;
        }
    }
    return false;
}


void* ListView::linWidgetManager()
{
    return LinWidgetRuntimeBridge::instance().widgetManager();
}

void ListView::openLinWidgetSettings(const QString& instanceId)
{
    Q_UNUSED(instanceId);
    LinHub::WallpaperStudioWindow::showStudio(2);
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
    if (!bridge.isLoaded() || !bridge.isWidgetLayerEmbedded()) {
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

    if (!ctrlHeld && !m_resizingWidget && !m_draggingWidget) {
        if (!m_resizeHintWidgetId.isEmpty()) {
            m_resizeHintWidgetId.clear();
            unsetCursor();
            update();
        }
        return ResizeEdgeNone;
    }

    const QRect geom = bridge.widgetGeometry(wid);
    const int edge = widgetResizeEdgeAt(pos, geom, 8);

    if (edge != ResizeEdgeNone) {
        setCursor(cursorForResizeEdge(edge));
    } else {
        setCursor(Qt::SizeAllCursor);
    }

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

    // 监听运行时就地安装就绪事件，一旦安装完成立即自动热加载恢复组件与壁纸
    connect(&RuntimeInstaller::instance(), &RuntimeInstaller::runtimeInstalledAndReady,
            this, &ListView::hotReloadLinWidgetsSubsystem, Qt::UniqueConnection);

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
    ctx.onRuntimeEnabledChanged = [](bool enabled, void* userData) {
        LinWidgetRuntimeBridge::instance().notifyEnabledChanged(enabled);
        auto* lv = static_cast<ListView*>(userData);
        if (lv) {
            lv->syncLinWidgetLayer();
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
    LinPower::WallpaperPowerStateManager::instance().start();

    if (!linWidgetsEnabled) {
        qDebug() << "[LinWidgetAutoLoad] runtime disabled in config";
        return;
    }

    if (!bridge.restoreFromConfig(QString())) {
        qDebug() << "[LinWidgetAutoLoad] restore from config returned false or empty";
    }
}

void ListView::hotReloadLinWidgetsSubsystem()
{
    qInfo() << "[ListView] Hot-reloading LinWidgets subsystem...";
    initializeLinWidgets();
    syncLinWidgetLayer();
    update();
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

