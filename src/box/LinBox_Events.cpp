#include "box/LinBox_Internal.h"

// ============================================================================
// LinBox Mouse, Focus, Keyboard & Selection Event Implementation
// ============================================================================

bool LinBox::eventFilter(QObject* watched, QEvent* event) {
    const bool isTrackedCursorWidget =
        watched == m_titleBar ||
        watched == m_titleEdit ||
        watched == m_viewSwitchBtn ||
        watched == m_collapseBtn ||
        watched == m_scrollArea ||
        watched == m_contentArea ||
        (m_scrollArea && watched == m_scrollArea->viewport());

    if (isTrackedCursorWidget) {
        if (event->type() == QEvent::Enter) {
            handleBoxMouseEnter();
        } else if (event->type() == QEvent::Leave) {
            handleBoxMouseLeave();
        }

        if (ListView* lv = ListView::instance()) {
            if (lv->isBackgroundSelectionActive()) {
                if (event->type() == QEvent::Enter ||
                    event->type() == QEvent::Leave ||
                    event->type() == QEvent::HoverEnter ||
                    event->type() == QEvent::HoverLeave ||
                    event->type() == QEvent::HoverMove ||
                    event->type() == QEvent::MouseMove ||
                    event->type() == QEvent::MouseButtonPress ||
                    event->type() == QEvent::MouseButtonRelease ||
                    event->type() == QEvent::MouseButtonDblClick ||
                    event->type() == QEvent::Wheel) {
                    return true;
                }
            }
        }
        if (hasActiveItemRename() && event->type() == QEvent::Wheel) {
            return true;
        }
    }

    if (watched == m_titleBar && event->type() == QEvent::Paint) {
        if (!m_titleEdit->isVisible()) {
            QPainter p(m_titleBar);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::white);
            p.setFont(AppFonts::uiFont(9, QFont::Bold));
            
            // Adjust rect to avoid buttons: collapseBtn(30px) and viewSwitchBtn(right)
            // Left gap for collapse button + padding
            QRect textRect = mappedTitleTextRect();
            
            if (isFolderMapped()) {
                const QRect iconRect = mappedTitleIconRect();
                const QString title = displayTitleText();
                bool iconDrawn = false;
                QSvgRenderer renderer(mappedFolderTitleIconResourcePath(m_mappedFolderPath));
                if (renderer.isValid()) {
                    renderer.render(&p, iconRect);
                    iconDrawn = true;
                } else if (isDriveRootPath(m_mappedFolderPath)) {
                    QSvgRenderer fallbackRenderer(QStringLiteral(":/Assets/folder-share.svg"));
                    if (fallbackRenderer.isValid()) {
                        fallbackRenderer.render(&p, iconRect);
                        iconDrawn = true;
                    }
                }

                if (iconDrawn) {
                    p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, title);
                } else {
                    QFontMetrics fm(p.font());
                    QString elidedTitle = fm.elidedText(displayTitleText(), Qt::ElideRight, textRect.width());
                    p.drawText(textRect, Qt::AlignCenter, elidedTitle);
                }
            } else {
                QFontMetrics fm(p.font());
                QString elidedTitle = fm.elidedText(displayTitleText(), Qt::ElideRight, textRect.width());
                p.drawText(textRect, Qt::AlignCenter, elidedTitle);
            }
        }
        return false; // Let the widget continue painting its own background/hover if it has style
    }

    if (isTrackedCursorWidget && (event->type() == QEvent::Enter || event->type() == QEvent::MouseMove)) {
        if (!m_isSelectingItems && !m_isResizing && !m_isMoving) {
            const QPoint mappedPos = mapFromGlobal(QCursor::pos());
            updateCursorShape(mappedPos);
        }
    }

    if (watched == m_titleBar || watched == m_contentArea) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (m_titleEdit->isVisible()) {
                const QWidget* sourceWidget = qobject_cast<QWidget*>(watched);
                const QPoint titlePos = sourceWidget == m_titleBar
                    ? mouseEvent->pos()
                    : m_titleBar->mapFromGlobal(sourceWidget->mapToGlobal(mouseEvent->pos()));
                if (!m_titleEdit->geometry().contains(titlePos)) {
                    commitTitleEditing();
                }
            }
            if (mouseEvent->button() == Qt::LeftButton || mouseEvent->button() == Qt::RightButton) {
                BoxManager::instance()->bringToFront(this);
                activateForKeyboardFocus();
            }
            if (mouseEvent->button() == Qt::LeftButton) {
                const QWidget* sourceWidget = qobject_cast<QWidget*>(watched);
                const QPoint mappedPos = watched == this
                    ? mouseEvent->pos()
                    : sourceWidget->mapTo(this, mouseEvent->pos());
                if (watched == m_contentArea && handleItemMousePress(mouseEvent, mappedPos)) {
                    return true;
                }
                if (tryRefreshMappedFolderFromPlaceholderClick(mappedPos)) {
                    return true;
                }
                m_resizeDir = getResizeDir(mappedPos);

                if (m_resizeDir != None && m_resizeDir != Move) {
                    m_isResizing = true;
                    m_dragStartPos = mouseEvent->globalPos();
                    m_dragStartGeometry = geometry();
                    m_geometryChangedDuringInteraction = false;
                    return true;
                }

                if (watched == m_contentArea || watched == m_scrollArea || (m_scrollArea && watched == m_scrollArea->viewport()) || watched == this) {
                    if (!hitItemStateAt(mappedPos) && m_resizeDir == None && !isDragArea(mappedPos)) {
                        const QPoint contentPos = m_contentArea ? m_contentArea->mapFrom(this, mappedPos) : mappedPos;
                        beginItemSelection(contentPos);
                        return true;
                    }
                }

                if (isDragArea(mappedPos)) {
                    m_isMoving = true;
                    raise();
                    m_dragStartPos = mouseEvent->globalPos();
                    m_dragStartGeometry = geometry();
                    m_geometryChangedDuringInteraction = false;
                    return true;
                }
            } else if (mouseEvent->button() == Qt::RightButton) {
                const QWidget* sourceWidget = qobject_cast<QWidget*>(watched);
                const QPoint mappedPos = watched == this
                    ? mouseEvent->pos()
                    : sourceWidget->mapTo(this, mouseEvent->pos());
                if (watched == m_contentArea && handleItemContextMenu(mouseEvent->globalPos(), mappedPos)) {
                    return true;
                }
                const bool extendedVerbs = (mouseEvent->modifiers() & Qt::ShiftModifier) != 0;
                showCustomContextMenu(mouseEvent->globalPos(), extendedVerbs);
                return true;
            }
        } else if (event->type() == QEvent::MouseMove) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            const QWidget* sourceWidget = qobject_cast<QWidget*>(watched);
            const QPoint mappedPos = watched == this
                ? mouseEvent->pos()
                : sourceWidget->mapTo(this, mouseEvent->pos());

            if (watched == m_contentArea && !m_isSelectingItems) {
                updateHoveredItem(mappedPos);
            }
            if (watched == m_contentArea && handleItemMouseMove(mouseEvent, mappedPos)) {
                return true;
            }
            if (watched == m_contentArea && m_isSelectingItems) {
                updateItemSelection(mouseEvent->pos());
                return true;
            }
            if (!m_isSelectingItems && !m_isResizing && !m_isMoving) {
                updateCursorShape(mappedPos);
            }
        } else if (watched == m_contentArea && event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            const QWidget* sourceWidget = qobject_cast<QWidget*>(watched);
            const QPoint mappedPos = watched == this
                ? mouseEvent->pos()
                : sourceWidget->mapTo(this, mouseEvent->pos());
            if (handleItemMouseRelease(mouseEvent, mappedPos)) {
                return true;
            }
            if (m_isSelectingItems) {
                finishItemSelection();
                return true;
            }
        } else if (watched == m_contentArea && event->type() == QEvent::MouseButtonDblClick) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            const QWidget* sourceWidget = qobject_cast<QWidget*>(watched);
            const QPoint mappedPos = watched == this
                ? mouseEvent->pos()
                : sourceWidget->mapTo(this, mouseEvent->pos());
            if (handleItemMouseDoubleClick(mouseEvent, mappedPos)) {
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonDblClick && watched == m_titleBar) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (isFolderMapped() && mappedTitleIconRect().contains(mouseEvent->pos())) {
                openMappedFolderInExplorer();
                return true;
            }
            if (mappedTitleTextRect().contains(mouseEvent->pos())) {
                beginTitleEditing();
                return true;
            }
            toggleCollapse(false);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void LinBox::mousePressEvent(QMouseEvent* event) {
    if (m_hoverExpandTimer && m_hoverExpandTimer->isActive()) m_hoverExpandTimer->stop();
    if (m_hoverCollapseTimer && m_hoverCollapseTimer->isActive()) m_hoverCollapseTimer->stop();

    if (m_titleEdit->isVisible()) {
        const QPoint titlePos = m_titleBar->mapFrom(this, event->pos());
        if (!m_titleEdit->geometry().contains(titlePos)) {
            commitTitleEditing();
        }
    }
    if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
        BoxManager::instance()->bringToFront(this);
        activateForKeyboardFocus();
    }
    if (event->button() == Qt::LeftButton) {
        if (handleItemMousePress(event, event->pos())) {
            event->accept();
            return;
        }
        if (tryRefreshMappedFolderFromPlaceholderClick(event->pos())) {
            event->accept();
            return;
        }
        m_resizeDir = getResizeDir(event->pos());
        if (isDragArea(event->pos())) {
            m_isMoving = true;
            raise();
            m_dragStartPos = event->globalPos();
            m_dragStartGeometry = geometry();
            m_geometryChangedDuringInteraction = false;

            event->accept();
            return;
        } else if (m_resizeDir != None) {
            m_isResizing = true;
            m_dragStartPos = event->globalPos();
            m_dragStartGeometry = geometry();
            m_geometryChangedDuringInteraction = false;
        } else if (!isDragArea(event->pos()) && !hitItemStateAt(event->pos())) {
            const QPoint contentPos = m_contentArea ? m_contentArea->mapFrom(this, event->pos()) : event->pos();
            beginItemSelection(contentPos);
            event->accept();
            return;
        }
    } else if (event->button() == Qt::RightButton) {
        if (handleItemContextMenu(event->globalPos(), event->pos())) {
            event->accept();
            return;
        }
        QWidget* child = childAt(event->pos());
        if (m_titleBar->geometry().contains(event->pos()) || !child || child == m_contentArea || child == this) {
            const bool extendedVerbs = (event->modifiers() & Qt::ShiftModifier) != 0;
            showCustomContextMenu(event->globalPos(), extendedVerbs);
        }
    }
}

void LinBox::activateForKeyboardFocus()
{
    if (ListView* lv = ListView::instance()) {
        lv->clearWidgetFocus();
    }
    raise();
    activateWindow();
    setFocus(Qt::MouseFocusReason);
}

void LinBox::focusInEvent(QFocusEvent* event)
{
    if (ListView* lv = ListView::instance()) {
        lv->clearWidgetFocus();
    }
    QWidget::focusInEvent(event);
}

void LinBox::mouseMoveEvent(QMouseEvent* event) {
    if (handleItemMouseMove(event, event->pos())) {
        event->accept();
        return;
    }
    if (m_isResizing) {
        QPoint delta = event->globalPos() - m_dragStartPos;
        QRect newGeom = m_dragStartGeometry;
        if (m_resizeDir & Left) newGeom.setLeft(m_dragStartGeometry.left() + delta.x());
        if (m_resizeDir & Right) newGeom.setRight(m_dragStartGeometry.right() + delta.x());
        if (m_resizeDir & Top) newGeom.setTop(m_dragStartGeometry.top() + delta.y());
        if (m_resizeDir & Bottom) newGeom.setBottom(m_dragStartGeometry.bottom() + delta.y());
        const int minResizeSize = scaledPx(this, 100);
        if (newGeom.width() >= minResizeSize && newGeom.height() >= minResizeSize) {
            if (geometry() != newGeom) {
                setGeometry(newGeom);
                m_geometryChangedDuringInteraction = true;
                update();
            }
        }
    } else if (m_isMoving) {
        // 计算鼠标移动增量并更新盒子位置
        const QPoint delta = event->globalPos() - m_dragStartPos;
        QRect targetRect(m_dragStartGeometry.topLeft() + delta, size());

        const bool altPressed = (event->modifiers() & Qt::AltModifier) || (GetAsyncKeyState(VK_MENU) & 0x8000);
        if (!altPressed) {
            const int snapDist = scaledPx(this, 12);

            // 获取盒子视觉可见内容矩形（排除外围透明的 m_borderWidth 调整手柄边距）
            const QRect thisVis = targetRect.adjusted(m_borderWidth, m_borderWidth, -m_borderWidth, -m_borderWidth);

            // 1. 获取当前屏幕可用工作区（排除任务栏）
            QScreen* screen = QGuiApplication::screenAt(event->globalPos());
            if (!screen) {
                screen = windowHandle() ? windowHandle()->screen() : QGuiApplication::primaryScreen();
            }
            const QRect screenArea = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);

            int bestDx = 0;
            int minAbsDx = snapDist + 1;

            auto checkSnapX = [&](int currentVal, int targetVal) {
                int diff = targetVal - currentVal;
                if (qAbs(diff) <= snapDist && qAbs(diff) < minAbsDx) {
                    minAbsDx = qAbs(diff);
                    bestDx = diff;
                }
            };

            // 屏幕边缘横向视觉贴边吸附（0间距）
            checkSnapX(thisVis.left(), screenArea.left());
            checkSnapX(thisVis.right(), screenArea.right());

            int bestDy = 0;
            int minAbsDy = snapDist + 1;

            auto checkSnapY = [&](int currentVal, int targetVal) {
                int diff = targetVal - currentVal;
                if (qAbs(diff) <= snapDist && qAbs(diff) < minAbsDy) {
                    minAbsDy = qAbs(diff);
                    bestDy = diff;
                }
            };

            // 屏幕边缘纵向视觉贴边吸附（0间距）
            checkSnapY(thisVis.top(), screenArea.top());
            checkSnapY(thisVis.bottom(), screenArea.bottom());

            // 2. 收集其他可见盒子的视觉吸附（0间距无缝紧贴与对齐）
            if (BoxManager* bm = BoxManager::instance()) {
                const auto boxes = bm->allBoxes();
                for (const auto& boxPtr : boxes) {
                    LinBox* otherBox = boxPtr.data();
                    if (!otherBox || otherBox == this || !otherBox->isVisible()) {
                        continue;
                    }
                    const int oBorder = otherBox->resizeBorderWidth();
                    const QRect otherVis = otherBox->geometry().adjusted(oBorder, oBorder, -oBorder, -oBorder);

                    // 横向视觉对齐（左对左、右对右、外侧0间距无缝贴靠、水平中心对齐）
                    checkSnapX(thisVis.left(), otherVis.left());
                    checkSnapX(thisVis.right(), otherVis.right());
                    checkSnapX(thisVis.left(), otherVis.right() + 1);
                    checkSnapX(thisVis.right(), otherVis.left() - 1);
                    checkSnapX(thisVis.center().x(), otherVis.center().x());

                    // 纵向视觉对齐（顶对顶、底对底、外侧0间距无缝贴靠、垂直中心对齐）
                    checkSnapY(thisVis.top(), otherVis.top());
                    checkSnapY(thisVis.bottom(), otherVis.bottom());
                    checkSnapY(thisVis.top(), otherVis.bottom() + 1);
                    checkSnapY(thisVis.bottom(), otherVis.top() - 1);
                    checkSnapY(thisVis.center().y(), otherVis.center().y());
                }
            }

            if (minAbsDx <= snapDist) {
                targetRect.moveLeft(targetRect.left() + bestDx);
            }
            if (minAbsDy <= snapDist) {
                targetRect.moveTop(targetRect.top() + bestDy);
            }
        }

        const QPoint newPos = targetRect.topLeft();
        if (pos() != newPos) {
            move(newPos);
            m_geometryChangedDuringInteraction = true;
            update();
        }
    } else {
        updateCursorShape(event->pos());
    }
}

void LinBox::mouseReleaseEvent(QMouseEvent* event) {
    if (handleItemMouseRelease(event, event->pos())) {
        event->accept();
        return;
    }
    if (m_isSelectingItems) {
        finishItemSelection();
        event->accept();
        return;
    }
    if (m_isResizing && !m_isCollapsed && height() > m_titleHeight + 2 * m_borderWidth) {
        m_originalHeight = height();
    }
    if ((m_isResizing || m_isMoving) && m_geometryChangedDuringInteraction) {
        refreshStyles();
        emit geometryChanged(this);
    }
    const bool shouldRefreshBlur = m_glassEffect && m_geometryChangedDuringInteraction;
    m_isResizing = false;
    m_isMoving = false;
    m_geometryChangedDuringInteraction = false;
    if (shouldRefreshBlur) {
        updateBlurCache();
        if (QWidget* p = parentWidget()) {
            p->update(geometry().adjusted(-16, -16, 16, 16));
        }
        update();
    }
    updateCursorShape(event->pos());
}

bool LinBox::handleItemMousePress(QMouseEvent* event, const QPoint& boxPos)
{
    if (!event || event->button() != Qt::LeftButton) {
        return false;
    }

    DesktopRootItemState* hitState = hitItemStateAt(boxPos);
    if (!hitState) {
        m_pressedItemPath.clear();
        m_itemPressArmed = false;
        return false;
    }

    ListView* lv = ListView::instance();
    if (!lv) {
        return false;
    }

    const QString itemPath = hitState->model.filePath;
    Qt::KeyboardModifiers modifiers = event->modifiers() | QApplication::keyboardModifiers();
    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) {
        modifiers |= Qt::ControlModifier;
    }
    if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) {
        modifiers |= Qt::ShiftModifier;
    }
    const QString normalizedItemPath = normalizeBoxItemPath(itemPath);
    const bool wasAlreadySelected = lv->selectedItemPaths().contains(normalizedItemPath);
    activateForKeyboardFocus();

    if (modifiers & Qt::ControlModifier) {
        lv->toggleItemSelection(itemPath);
    } else if (modifiers & Qt::ShiftModifier) {
        lv->selectRangeToPath(itemPath, this);
    } else if (wasAlreadySelected) {
        // Keep selection set unchanged so user can drag any selected item directly
    } else {
        lv->setSelectedItemPaths({ itemPath }, itemPath, true);
    }

    m_pressedItemPath = itemPath;
    m_pressedItemBoxPos = boxPos;
    m_itemPressArmed = true;
    m_itemClickTimer.restart();
    return true;
}

bool LinBox::startItemDrag(const QPoint& boxPos)
{
    Q_UNUSED(boxPos);
    if (!m_itemPressArmed || m_pressedItemPath.isEmpty()) {
        return false;
    }

    ListView* lv = ListView::instance();
    if (!lv) {
        return false;
    }

    QStringList dragPaths = lv->selectedItemPaths();
    const QString normalizedPressedPath = normalizeBoxItemPath(m_pressedItemPath);
    if (!dragPaths.contains(normalizedPressedPath)) {
        dragPaths.clear();
        dragPaths.append(m_pressedItemPath);
        lv->setSelectedItemPaths(dragPaths, m_pressedItemPath, true);
    }

    QList<QUrl> urls;
    for (const QString& path : dragPaths) {
        if (!path.isEmpty()) {
            urls.append(QUrl::fromLocalFile(path));
        }
    }
    if (urls.isEmpty()) {
        return false;
    }

    auto* mimeData = new WindowsDragMimeData();
    mimeData->setUrls(urls);
    mimeData->setDropDescriptionForAction(Qt::MoveAction, boxTitle());

    lv->setDraggedItemPaths(dragPaths, true);
    QDrag drag(this);
    drag.setMimeData(mimeData);

    QVector<DesktopRootItemRenderData> ghostItems;
    QRect ghostBounds;
    for (const QString& path : dragPaths) {
        DesktopRootItemRenderData renderData;
        const DesktopRootItemState* state = findItemStateByPath(path);
        if (state && !state->model.renderData.itemRect.isNull() && state->model.renderData.itemRect.size().isValid()) {
            renderData = state->model.renderData;
        } else if (LinBox* otherBox = BoxManager::instance()->findBoxContainingItem(path)) {
            const DesktopRootItemState* otherState = otherBox->findItemStateByPath(path);
            if (otherState && !otherState->model.renderData.itemRect.isNull() && otherState->model.renderData.itemRect.size().isValid()) {
                renderData = otherState->model.renderData;
            }
        } else {
            DesktopRootItemModel model;
            if (lv->resolveItemModelByPath(path, model) && !model.renderData.itemRect.isNull() && model.renderData.itemRect.size().isValid()) {
                renderData = model.renderData;
            }
        }

        if (!renderData.itemRect.isNull() && renderData.itemRect.size().isValid()) {
            ghostItems.append(renderData);
            ghostBounds = ghostBounds.isNull()
                ? renderData.itemRect
                : ghostBounds.united(renderData.itemRect);
        }
    }

    if (ghostItems.isEmpty()) {
        const DesktopRootItemState* dragState = findItemStateByPath(m_pressedItemPath);
        if (dragState && !dragState->model.renderData.itemRect.isNull() && dragState->model.renderData.itemRect.size().isValid()) {
            ghostItems.append(dragState->model.renderData);
            ghostBounds = dragState->model.renderData.itemRect;
        }
    }

    if (!ghostItems.isEmpty() && ghostBounds.isValid()) {
        const qreal dpr = qMax<qreal>(1.0, devicePixelRatioF());
        const QSize physicalSize(qMax(1, qRound(ghostBounds.width() * dpr)),
                                 qMax(1, qRound(ghostBounds.height() * dpr)));
        QPixmap ghostPixmap(physicalSize);
        if (dpr > 1.0) {
            ghostPixmap.setDevicePixelRatio(dpr);
        }
        ghostPixmap.fill(Qt::transparent);

        QPainter painter(&ghostPixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.translate(-ghostBounds.topLeft());
        for (const DesktopRootItemRenderData& renderData : ghostItems) {
            paintDesktopRootItemGhost(painter, renderData);
        }
        painter.end();

        QPoint contentPressPos = m_contentArea
            ? m_contentArea->mapFrom(this, m_pressedItemBoxPos)
            : m_pressedItemBoxPos;
        QPoint hotSpot = contentPressPos - ghostBounds.topLeft();
        hotSpot.setX(qBound(0, hotSpot.x(), qMax(0, ghostPixmap.width() - 1)));
        hotSpot.setY(qBound(0, hotSpot.y(), qMax(0, ghostPixmap.height() - 1)));

        drag.setPixmap(ghostPixmap);
        drag.setHotSpot(hotSpot);
    }

    const Qt::DropAction resultAction = drag.exec(Qt::CopyAction | Qt::MoveAction | Qt::LinkAction, Qt::MoveAction);
    if (resultAction == Qt::MoveAction) {
        QStringList removedSourcePaths;
        for (const QString& path : dragPaths) {
            const QString normPath = normalizeBoxItemPath(path);
            if (!path.isEmpty() && !normPath.startsWith(QLatin1String("::")) && CSystemApi::GetPathType(path) == PathType::InvalidPath) {
                removedSourcePaths.append(path);
            }
        }
        lv->finalizeMovedModelSourcesAfterFileOp(removedSourcePaths);
    }
    lv->clearDraggedItemPath(true);
    return true;
}

bool LinBox::handleItemMouseMove(QMouseEvent* event, const QPoint& boxPos)
{
    if (!event || !(event->buttons() & Qt::LeftButton) || !m_itemPressArmed || m_pressedItemPath.isEmpty()) {
        return false;
    }

    if ((boxPos - m_pressedItemBoxPos).manhattanLength() < QApplication::startDragDistance()) {
        return false;
    }

    const bool started = startItemDrag(boxPos);
    m_itemPressArmed = false;
    m_pressedItemPath.clear();
    return started;
}

bool LinBox::handleItemMouseRelease(QMouseEvent* event, const QPoint& boxPos)
{
    if (!event || event->button() != Qt::LeftButton || !m_itemPressArmed) {
        return false;
    }

    m_itemPressArmed = false;
    m_pressedItemPath.clear();
    updateHoveredItem(boxPos);
    return hitItemStateAt(boxPos) != nullptr;
}

bool LinBox::handleItemMouseDoubleClick(QMouseEvent* event, const QPoint& boxPos)
{
    if (!event || event->button() != Qt::LeftButton) {
        return false;
    }

    const DesktopRootItemState* hitState = hitItemStateAt(boxPos);
    if (!hitState) {
        return false;
    }

    if (ListView* lv = ListView::instance()) {
        lv->clearCutMarks();
        lv->openItemByPath(hitState->model.filePath);
    }
    m_itemPressArmed = false;
    m_pressedItemPath.clear();
    return true;
}

bool LinBox::handleItemContextMenu(const QPoint& globalPos, const QPoint& boxPos)
{
    const DesktopRootItemState* hitState = hitItemStateAt(boxPos);
    if (!hitState) {
        return false;
    }

    ListView* lv = ListView::instance();
    if (!lv) {
        return false;
    }

    const QString itemPath = hitState->model.filePath;
    const QString normalizedItemPath = normalizeBoxItemPath(itemPath);
    if (!lv->selectedItemPaths().contains(normalizedItemPath)) {
        lv->setSelectedItemPaths({ itemPath }, itemPath, true);
    } else {
        lv->setFocusedItemPath(itemPath);
    }

    lv->setLastRightClickPos(lv->mapFromGlobal(globalPos));
    lv->requestSystemRightMenu(hitState->model.objectType, globalPos, lv->selectedItemPaths());
    return true;
}


void LinBox::beginItemSelection(const QPoint& contentPos)
{
    ListView* listView = ListView::instance();
    if (listView) {
        listView->clearCheckedItem();
    }

    m_isSelectingItems = true;
    m_selectionStartPos = contentPos;
    m_selectionRect = QRect(contentPos, QSize(1, 1));
    updateItemSelection(contentPos);
}

void LinBox::updateItemSelection(const QPoint& contentPos)
{
    if (!m_isSelectingItems) {
        return;
    }

    const QRect selectionRect = QRect(m_selectionStartPos, contentPos).normalized();
    const QRect previousRect = m_selectionRect;
    m_selectionRect = selectionRect;

    const QPoint contentOrigin = m_contentArea ? m_contentArea->mapTo(this, QPoint(0, 0)) : QPoint(0, 0);
    const QRect dirty = previousRect.united(selectionRect).adjusted(-2, -2, 2, 2);
    update(dirty.translated(contentOrigin));

    QStringList selectedPaths;
    selectedPaths.reserve(m_itemStates.size());
    for (const DesktopRootItemState& state : m_itemStates) {
        if (!selectionRect.intersects(state.model.renderData.selectionContentRect)) {
            continue;
        }
        selectedPaths.push_back(state.model.filePath);
    }

    if (ListView* listView = ListView::instance()) {
        const QString focusedPath = selectedPaths.isEmpty() ? QString() : selectedPaths.last();
        const QStringList currentSelectedPaths = listView->selectedItemPaths();
        const QString currentFocusedPath = listView->focusedItemPath();

        if (selectedPaths.isEmpty()) {
            if (currentSelectedPaths.isEmpty() && currentFocusedPath.isEmpty()) {
                return;
            }
            listView->clearCheckedItem();
            return;
        }

        if (currentSelectedPaths == selectedPaths &&
            currentFocusedPath == focusedPath) {
            return;
        }

        listView->setSelectedItemPaths(selectedPaths, focusedPath, false);
    }
}

void LinBox::clearSelectionOverlay()
{
    const QRect previousRect = m_selectionRect;
    m_selectionRect = QRect();
    if (!previousRect.isNull()) {
        const QPoint contentOrigin = m_contentArea ? m_contentArea->mapTo(this, QPoint(0, 0)) : QPoint(0, 0);
        update(previousRect.adjusted(-2, -2, 2, 2).translated(contentOrigin));
    }
}

void LinBox::finishItemSelection()
{
    m_isSelectingItems = false;
    clearSelectionOverlay();
    ListView* lv = ListView::instance();
    if (lv && !lv->selectedItemPaths().isEmpty()) {
        m_pressedItemPath = lv->selectedItemPaths().first();
        m_pressedItemBoxPos = m_contentArea ? m_contentArea->mapTo(this, m_selectionStartPos) : m_selectionStartPos;
        m_itemPressArmed = true;
    }
}

void LinBox::updateHoveredItem(const QPoint& boxPos)
{
    const DesktopRootItemState* hitState = hitItemStateAt(boxPos);
    const QString nextHoveredPath = hitState ? normalizeBoxItemPath(hitState->model.filePath) : QString();
    if (m_hoveredItemPath == nextHoveredPath) {
        return;
    }

    bool dirty = false;
    for (DesktopRootItemState& state : m_itemStates) {
        const QString statePath = normalizeBoxItemPath(state.model.filePath);
        const bool hovered = !nextHoveredPath.isEmpty() && statePath == nextHoveredPath;
        if (state.model.isHovered != hovered || state.model.renderData.hovered != (hovered && !state.model.isSelected)) {
            state.model.isHovered = hovered;
            state.model.renderData.hovered = hovered && !state.model.isSelected;
            dirty = true;
        }
    }

    m_hoveredItemPath = nextHoveredPath;
    if (dirty) {
        update();
    }
}

void LinBox::clearHoveredItem()
{
    if (m_hoveredItemPath.isEmpty()) {
        return;
    }

    bool dirty = false;
    for (DesktopRootItemState& state : m_itemStates) {
        if (!state.model.isHovered && !state.model.renderData.hovered) {
            continue;
        }
        state.model.isHovered = false;
        state.model.renderData.hovered = false;
        dirty = true;
    }

    m_hoveredItemPath.clear();
    if (dirty) {
        update();
    }
}

void LinBox::enterEvent(QEvent* event) {
    QWidget::enterEvent(event);
    handleBoxMouseEnter();
}

void LinBox::leaveEvent(QEvent* event) {
    handleBoxMouseLeave();
    clearHoveredItem();
    setCursor(Qt::ArrowCursor);
    QWidget::leaveEvent(event);
}

LinBox::ResizeDir LinBox::getResizeDir(const QPoint& pos) {
    const int x = pos.x();
    const int y = pos.y();
    const int w = width();
    const int h = height();
    const int resizeMargin = qMax(scaledPx(this, 4), m_borderWidth);
    const int cornerMargin = qMax(resizeMargin * 2, scaledPx(this, 16));

    if (x < 0 || y < 0 || x >= w || y >= h) {
        return None;
    }

    const bool nearLeft = x < resizeMargin;
    const bool nearRight = x >= w - resizeMargin;
    const bool nearTop = y < resizeMargin;
    const bool nearBottom = y >= h - resizeMargin;

    const bool inTopLeftCorner = x < cornerMargin && y < cornerMargin;
    const bool inTopRightCorner = x >= w - cornerMargin && y < cornerMargin;
    const bool inBottomLeftCorner = x < cornerMargin && y >= h - cornerMargin;
    const bool inBottomRightCorner = x >= w - cornerMargin && y >= h - cornerMargin;

    if (nearLeft && nearTop && inTopLeftCorner) return TopLeft;
    if (nearRight && nearTop && inTopRightCorner) return TopRight;
    if (nearLeft && nearBottom && inBottomLeftCorner) return BottomLeft;
    if (nearRight && nearBottom && inBottomRightCorner) return BottomRight;
    if (nearLeft) return Left;
    if (nearRight) return Right;
    if (nearBottom) return Bottom;
    if (nearTop) return Top;

    if (y >= resizeMargin && y <= m_titleHeight && x >= resizeMargin && x < w - resizeMargin) {
        return Move;
    }
    return None;
}

bool LinBox::nativeEvent(const QByteArray& eventType, void* message, long* result) {
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_DPICHANGED) {
        updateDpiMetrics();
        if (msg->lParam) {
            const RECT* suggested = reinterpret_cast<const RECT*>(msg->lParam);
            const QRect suggestedGeometry(
                QPoint(suggested->left, suggested->top),
                QSize(suggested->right - suggested->left, suggested->bottom - suggested->top));
            if (suggestedGeometry.isValid()) {
                setGeometry(suggestedGeometry);
            }
        }
        refreshLayout();
        update();
        if (m_glassEffect) {
            scheduleBlurRefresh();
        }
        if (result) {
            *result = 0;
        }
        return true;
    }

    if (msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN) {
        if (ListView* lv = ListView::instance()) {
            if (msg->wParam == 'N' &&
                !RenameManager::instance()->isRenaming() &&
                (GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
                (GetKeyState(VK_SHIFT) & 0x8000) != 0) {
                lv->createNewFolderInHost(this);
                if (result) {
                    *result = 0;
                }
                return true;
            }

            if (msg->wParam == VK_DELETE && !RenameManager::instance()->isRenaming()) {
                const bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                lv->onMenuDelete(shiftPressed);
                if (result) {
                    *result = 0;
                }
                return true;
            }
        }
    }
    if (msg->message == WM_NCHITTEST) {
        if (m_glassEffect) {
            int x = GET_X_LPARAM(msg->lParam);
            int y = GET_Y_LPARAM(msg->lParam);
            QPoint localPos = mapFromGlobal(QPoint(x, y));
            
            if (rect().contains(localPos)) {
                // 1. Full Title bar area must be solid to capture ALL mouse events (drag + buttons)
                if (m_titleBar->geometry().contains(localPos)) {
                    *result = HTCLIENT;
                    return true;
                }
                
                // 2. Content area (within scroll area): Icons and ScrollBars are solid; empty space is click-through
                if (m_scrollArea->geometry().contains(localPos)) {
                    QPoint scrollPos = m_scrollArea->mapFrom(this, localPos);
                    QWidget* child = m_scrollArea->childAt(scrollPos);
                    const QPoint contentPos = m_contentArea->mapFrom(this, localPos);
                    const bool hitPaintedItem = hitItemStateAt(localPos) != nullptr;
                    
                    // If it's a scrollbar or a child of contentArea, it should be solid
                    if ((child && child == m_scrollArea->verticalScrollBar()) ||
                        m_contentArea->childAt(contentPos) ||
                        hitPaintedItem) {
                        *result = HTCLIENT;
                        return true;
                    }
                }
                
                // 3. Fallback for all other internal area (empty content background) -> Click through
                *result = HTTRANSPARENT; 
                return true; 
            }
        }
    }
    return QWidget::nativeEvent(eventType, message, result);
}



void LinBox::keyPressEvent(QKeyEvent* event) {
    ListView* lv = ListView::instance();
    if (lv) {
        lv->clearWidgetFocus();

        const bool shiftPressed = (event->modifiers() & Qt::ShiftModifier) || ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
        const bool ctrlPressed = (event->modifiers() & Qt::ControlModifier) || ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0);
        const bool extendSelection = shiftPressed;
        const bool shortcutUndo = (ctrlPressed && event->key() == Qt::Key_Z) || event->matches(QKeySequence::Undo);
        const bool shortcutPaste = (ctrlPressed && event->key() == Qt::Key_V) || event->matches(QKeySequence::Paste);
        const bool shortcutPasteLink = ctrlPressed && shiftPressed && event->key() == Qt::Key_V;
        const bool shortcutNewFolder = ctrlPressed && extendSelection && event->key() == Qt::Key_N;

        if (shortcutUndo) {
            if (lv->handleUndo()) {
                event->accept();
                return;
            }
        }

        if (event->key() == Qt::Key_Delete) {
            lv->onMenuDelete(shiftPressed);
            event->accept();
            qDebug() << "[Box]-Delete triggered, permanent=" << shiftPressed;
            return;
        }

        if ((ctrlPressed && event->key() == Qt::Key_A) || event->matches(QKeySequence::SelectAll)) {
            QStringList allPaths;
            allPaths.reserve(m_itemStates.size());
            for (const DesktopRootItemState& state : m_itemStates) {
                allPaths.push_back(state.model.filePath);
            }
            lv->setSelectedItemPaths(allPaths, allPaths.isEmpty() ? QString() : allPaths.last(), false);
            event->accept();
            return;
        }
        if ((ctrlPressed && event->key() == Qt::Key_C) || event->matches(QKeySequence::Copy)) {
            lv->writeSelectionToClipboard(false);
            event->accept();
            return;
        }
        if ((ctrlPressed && event->key() == Qt::Key_X) || event->matches(QKeySequence::Cut)) {
            lv->writeSelectionToClipboard(true);
            event->accept();
            return;
        }
        if (shortcutPaste || shortcutPasteLink) {
            lv->clearCutMarks();
            const QStringList filePaths = clipboardLocalFilePathsForBox();
            if (filePaths.isEmpty()) {
                event->accept();
                return;
            }

            const bool isCutOperation = clipboardContainsCutItemsForBox();
            const bool preferShortcut = shortcutPasteLink || lv->shouldCreateShortcutForExternalDrop(filePaths);
            bool ok = false;

            if (isFolderMapped()) {
                const QString targetFolder = normalizeMappedFolderPath(mappedFolderPath());
                const QStringList srcPaths = toNativeQStringListPaths(filePaths);
                if (preferShortcut) {
                    ok = lv->createShortcutsInFolder(filePaths, targetFolder);
                } else if (isCutOperation) {
                    ok = CSystemApi::MoveFiles(srcPaths, targetFolder);
                } else {
                    ok = CSystemApi::CopyFiles(srcPaths, targetFolder);
                }
                if (ok) {
                    lv->reloadMappedFolderBox(this);
                    refreshLayout();
                    update();
                }
            } else {
                ok = lv->importPathsToBox(filePaths, this, !isCutOperation, -1, preferShortcut);
                if (ok) {
                    refreshLayout();
                    update();
                    raise();
                }
            }

            if (ok && isCutOperation) {
                QGuiApplication::clipboard()->clear();
            }
            event->accept();
            return;
        }
        if (shortcutNewFolder) {
            lv->createNewFolderInHost(this);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Escape) {
            lv->clearSelectionState();
            RenameManager::instance()->cancelRename();
            event->accept();
            return;
        }
        if ((event->modifiers() & Qt::AltModifier) && event->key() == Qt::Key_Return) {
            const QString targetPath = !lv->focusedItemPath().isEmpty()
                ? lv->focusedItemPath()
                : (lv->selectedItemPaths().isEmpty() ? QString() : lv->selectedItemPaths().first());
            if (!targetPath.isEmpty() && lv->showPropertiesForPath(targetPath)) {
                event->accept();
                return;
            }
        }
        if (ctrlPressed && event->key() == Qt::Key_Space) {
            const QString targetPath = lv->focusedItemPath();
            if (!targetPath.isEmpty()) {
                lv->toggleItemSelection(targetPath);
            }
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Space) {
            const DesktopRootItemState* hoveredState = m_hoveredItemPath.isEmpty()
                ? nullptr
                : findItemStateByPath(m_hoveredItemPath);
            const QString targetPath = hoveredState
                ? hoveredState->model.filePath
                : (!lv->focusedItemPath().isEmpty()
                    ? lv->focusedItemPath()
                    : (lv->selectedItemPaths().isEmpty() ? QString() : lv->selectedItemPaths().first()));
            if (!targetPath.isEmpty() && lv->sendPreviewRequestByPath(targetPath)) {
                event->accept();
                return;
            }
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            const QString targetPath = !lv->focusedItemPath().isEmpty()
                ? lv->focusedItemPath()
                : (lv->selectedItemPaths().isEmpty() ? QString() : lv->selectedItemPaths().first());
            if (!targetPath.isEmpty() && lv->openItemByPath(targetPath)) {
                event->accept();
                return;
            }
        }
        if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Up) {
            lv->moveFocusInHost(this,
                event->key() == Qt::Key_Left ? ListView::FocusMoveDirection::Left : ListView::FocusMoveDirection::Up,
                extendSelection,
                ctrlPressed);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Right || event->key() == Qt::Key_Down) {
            lv->moveFocusInHost(this,
                event->key() == Qt::Key_Right ? ListView::FocusMoveDirection::Right : ListView::FocusMoveDirection::Down,
                extendSelection,
                ctrlPressed);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_F2 && lv->selectedItemPaths().size() == 1) {
            RenameManager::instance()->startRename(lv->selectedItemPaths().first());
            event->accept();
            return;
        }
    }
    const QString text = event->text();
    if (!text.isEmpty() && text.at(0).isPrint() &&
        !(event->modifiers() & Qt::ControlModifier) && !(event->modifiers() & Qt::AltModifier) &&
        event->key() != Qt::Key_Escape && event->key() != Qt::Key_Return && event->key() != Qt::Key_Enter &&
        event->key() != Qt::Key_Delete && event->key() != Qt::Key_Backspace && event->key() != Qt::Key_Tab &&
        event->key() != Qt::Key_Space) {
        if (ListView* lv = ListView::instance()) {
            if (lv->handleTypeToSelect(text, this)) {
                event->accept();
                return;
            }
        }
    }

    QWidget::keyPressEvent(event);
}

bool LinBox::isDragArea(const QPoint& pos) {
    if (m_titleBar->geometry().contains(pos)) {
        QPoint titlePos = m_titleBar->mapFrom(this, pos);
        QWidget* child = m_titleBar->childAt(titlePos);
        // Drag allowed if clicking the title bar itself or an invisible part
        // Exclude specific interaction widgets
        if (child && (child == m_collapseBtn || child == m_viewSwitchBtn || child == m_titleEdit)) {
            return false;
        }
        return true;
    }
    
    return false;
}

void LinBox::updateCursorShape(const QPoint& pos) {
    if (mappedPlaceholderRect().contains(pos)) {
        setCursor(Qt::PointingHandCursor);
        return;
    }
    if (m_titleBar && isFolderMapped() && m_titleBar->geometry().contains(pos)) {
        const QPoint titlePos = m_titleBar->mapFrom(this, pos);
        if (mappedTitleIconRect().contains(titlePos)) {
            setCursor(Qt::PointingHandCursor);
            return;
        }
    }
    ResizeDir dir = getResizeDir(pos);
    switch (dir) {
        case Top: case Bottom: setCursor(Qt::SizeVerCursor); break;
        case Left: case Right: setCursor(Qt::SizeHorCursor); break;
        case TopLeft: case BottomRight: setCursor(Qt::SizeFDiagCursor); break;
        case TopRight: case BottomLeft: setCursor(Qt::SizeBDiagCursor); break;
        case Move: setCursor(Qt::ArrowCursor); break;
        default: setCursor(Qt::ArrowCursor); break;
    }
}

