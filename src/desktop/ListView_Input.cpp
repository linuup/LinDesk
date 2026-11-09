#include "desktop/ListView_Internal.h"

// ============================================================================
// ListView Mouse, Keyboard, Wheel & Window Event Implementation
// ============================================================================

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
    LinPower::WallpaperPowerStateManager::instance().stop();
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
        m_pendingWallpaperClick = false;
    }
}

void ListView::mousePressEvent(QMouseEvent* event)
{
    if (RightMenu::isMenuShowing() || RightMenu::isMenuRecentlyClosed(400)) {
        m_pendingWallpaperClick = false;
        event->accept();
        return;
    }

    auto& bridge = LinWidgetRuntimeBridge::instance();
    const bool ctrlHeld = (event->modifiers() & Qt::ControlModifier) != 0 || (GetAsyncKeyState(VK_CONTROL) & 0x8000);
    if (bridge.isLoaded()) {
        if (bridge.isWidgetLayerEmbedded() && !findDesktopStateAt(event->pos()) && bridge.isWidgetAt(event->pos())) {
            const QString wid = bridge.widgetAt(event->pos());
            const QRect geom = bridge.widgetGeometry(wid);
            const int edge = widgetResizeEdgeAt(event->pos(), geom, 8);
            const bool inHeader = (event->pos().y() >= geom.top() && event->pos().y() <= geom.top() + 24);

            if (event->button() == Qt::LeftButton && (ctrlHeld || edge != ResizeEdgeNone || inHeader)) {
                clearSelectionState();
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
        const int snapDist = 16;
        if (std::abs(newGeom.left() - screenRect.left()) < snapDist) {
            newGeom.moveLeft(screenRect.left());
        } else if (std::abs(newGeom.right() - screenRect.right()) < snapDist) {
            newGeom.moveRight(screenRect.right());
        }

        if (std::abs(newGeom.top() - screenRect.top()) < snapDist) {
            newGeom.moveTop(screenRect.top());
        } else if (std::abs(newGeom.bottom() - screenRect.bottom()) < snapDist) {
            newGeom.moveBottom(screenRect.bottom());
        }

        newGeom.moveLeft(qBound(0, newGeom.left(), qMax(0, screenRect.width() - newGeom.width())));
        newGeom.moveTop(qBound(0, newGeom.top(), qMax(0, screenRect.height() - newGeom.height())));

        const QVector<QRect> otherRects = bridge.occupiedRects();
        for (const QRect& other : otherRects) {
            if (other == m_widgetDragStartGeometry || other == newGeom) continue;
            if (std::abs(newGeom.left() - (other.right() + 8)) < snapDist) {
                newGeom.moveLeft(other.right() + 8);
            }
            if (std::abs(newGeom.right() - (other.left() - 8)) < snapDist) {
                newGeom.moveRight(other.left() - 8);
            }
            if (std::abs(newGeom.top() - other.top()) < snapDist) {
                newGeom.moveTop(other.top());
            }
            if (std::abs(newGeom.top() - (other.bottom() + 8)) < snapDist) {
                newGeom.moveTop(other.bottom() + 8);
            }
        }

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
    // 1. 如果光标位于小组件上方，判断是否在边缘缩放区、顶部抓手区或按住 Ctrl
    const bool ctrlHeld = (event->modifiers() & Qt::ControlModifier) != 0 || (GetAsyncKeyState(VK_CONTROL) & 0x8000);
    if (bridge.isLoaded() && bridge.isWidgetLayerEmbedded() && !findDesktopStateAt(event->pos()) && bridge.isWidgetAt(event->pos())) {
        const QString wid = bridge.widgetAt(event->pos());
        const QRect geom = bridge.widgetGeometry(wid);
        const int edge = widgetResizeEdgeAt(event->pos(), geom, 8);
        const bool inHeader = (event->pos().y() >= geom.top() && event->pos().y() <= geom.top() + 24);

        if (ctrlHeld || edge != ResizeEdgeNone || inHeader) {
            refreshWidgetResizeHint(event->pos(), ctrlHeld);
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
        if (!RightMenu::isMenuShowing()) {
            bridge.forwardMouseMoveToWallpaper(event->pos());
        }
    }

    event->ignore();

}

void ListView::mouseReleaseEvent(QMouseEvent* event)
{
    if (RightMenu::isMenuShowing() || RightMenu::isMenuRecentlyClosed(400)) {
        m_pendingWallpaperClick = false;
        event->accept();
        return;
    }

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
    if (RightMenu::isMenuShowing() || RightMenu::isMenuRecentlyClosed(400)) {
        m_pendingWallpaperClick = false;
        event->accept();
        return;
    }

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
            m_hideAnimationOriginPos = event->pos();
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

