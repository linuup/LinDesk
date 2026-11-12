#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDataStream>
#include <QFile>
#include <QSet>
#include <QCoreApplication>
#include <QSettings>
#include "desktop/ListView.h"
#include "desktop/widgets/runtime_export.h"
#include "services/IconCacheManager.h"
#include "desktop/ListViewSelectionHelper.h"
#include "desktop/ListViewSelectionController.h"
#include "desktop/ListViewDragDropHelper.h"
#include "desktop/LinDesktop.h"
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

#include "desktop/ListView_Helpers.h"

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

void ListView::dragEnterEvent(QDragEnterEvent* event)
{
    RenameManager::instance()->cancelRename();
    if (event->mimeData()->hasUrls()) {
        const QStringList draggedPaths = ListViewFileOpsHelper::draggedLocalFilePaths(event->mimeData());
        const bool desktopSource = (event->source() == this);
        const bool boxSource = isBoxDragSource(event->source());
        const bool externalSource = !desktopSource && !boxSource;
        const QPoint globalPos = mapToGlobal(event->pos());
        if (isMappedDragStillInsideSourceBox(event->source(), globalPos)) {
            if (m_dragTip) {
                m_dragTip->hide();
            }
            clearDesktopAutoArrangeReorderPreview();
            event->ignore();
            return;
        }

        if (shouldPreferShortcutForDrop(draggedPaths, event->keyboardModifiers(), externalSource)) {
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
    const QPoint globalPos = mapToGlobal(event->pos());
    if (isMappedDragStillInsideSourceBox(event->source(), globalPos)) {
        if (m_dragTip) {
            m_dragTip->hide();
        }
        clearDesktopAutoArrangeReorderPreview();
        event->ignore();
        return;
    }
    const QStringList draggedPaths = ListViewFileOpsHelper::draggedLocalFilePaths(event->mimeData());
    const bool syntheticDesktopPreview =
        event->source() == nullptr &&
        m_isDragging &&
        !m_draggedDesktopPath.isEmpty() &&
        !m_ghosts.isEmpty();
    const bool desktopSource = (event->source() == this) || syntheticDesktopPreview;
    const bool boxSource = !syntheticDesktopPreview && isBoxDragSource(event->source());
    const bool externalSource = !desktopSource && !boxSource;
    const bool preferShortcutForDrop = shouldPreferShortcutForDrop(
        draggedPaths, event->keyboardModifiers(), externalSource);
    if (preferShortcutForDrop) {
        event->setDropAction(Qt::LinkAction);
    } else if (event->keyboardModifiers() & Qt::ControlModifier) {
        event->setDropAction(Qt::CopyAction);
    } else {
        event->setDropAction(Qt::MoveAction);
    }
    event->accept();

    const bool isInternalDrag = desktopSource;

    // 1. Find if we are hovering over an item
    const DesktopRootItemState* hitItem = findDesktopItemAt(event->pos());
    if (hitItem && isDesktopPathSelected(hitItem->model.filePath)) {
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
        const QString currentDraggedPath = this->draggedItemPath();
        if (isInternalDrag && !currentDraggedPath.isEmpty()) {
            DesktopRootItemModel draggedModel;
            const bool hasDraggedModel = resolveItemModelByPath(currentDraggedPath, draggedModel);
            const QString draggedClsid = hasDraggedModel ? draggedModel.itemData.getCLSID() : QString();
            const QString draggedFilePath = hasDraggedModel ? draggedModel.filePath : QString();
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
                    hitItem->model.displayName,
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

        QString clsid = hitItem->model.itemData.getCLSID();
        bool isRecycleBin = (clsid == APP_RECYCLE_BIN_CLS);
        bool isPC = (clsid == APP_MY_COMPUTER_CLS);
        bool isNetwork = (clsid == APP_NETWORK_CLS);
        bool isControlPanel = (clsid == APP_CONTROL_PANEL_CLS);
        bool isFolder = (hitItem->model.objectType == ObjectType::Folder_ObjectType ||
                         hitItem->model.objectType == ObjectType::SpecialFolder ||
                         hitItem->model.objectType == ObjectType::DiskLetter);

        if (isPC || isNetwork || isControlPanel) {
            ListViewFileOpsHelper::updateInternalDragDescription(
                event->mimeData(),
                Qt::IgnoreAction,
                hitItem->model.displayName,
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
        DesktopRootItemModel draggedModel;
        const QString draggedPathForTip = this->draggedItemPath();
        const bool isDraggedDiskLetter = !draggedPathForTip.isEmpty()
            && resolveItemModelByPath(draggedPathForTip, draggedModel)
            && draggedModel.objectType == ObjectType::DiskLetter;
        QString htmlText;

        if (isRecycleBin) {
            actionKeyword = "Move to";
            highlightColor = "#ff4d4d";
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, hitItem->model.displayName);
        } else if (isFolder && preferShortcutForDrop) {
            actionKeyword = "Create shortcut in";
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, hitItem->model.displayName);
        } else if (isFolder && isDraggedDiskLetter) {
            htmlText = QString("<b style='color:%1'>Open</b> <span>%2</span> <b style='color:%1'>here</b>")
                .arg(highlightColor, hitItem->model.displayName);
        } else if (isFolder) {
            actionKeyword = isCtrl ? "Copy to" : "Move to";
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, hitItem->model.displayName);
        } else {
            actionKeyword = "Open with";
            htmlText = QString("<b style='color:%1'>%2</b> <span>%3</span>")
                .arg(highlightColor, actionKeyword, hitItem->model.displayName);
        }
        if (isRecycleBin) {
            ListViewFileOpsHelper::updateInternalDragDescriptionCustom(
                event->mimeData(),
                DROPIMAGE_MOVE,
                QCoreApplication::translate("ListView", "Move to %1"),
                hitItem->model.displayName);
        } else if (isFolder && preferShortcutForDrop) {
            ListViewFileOpsHelper::updateInternalDragDescriptionCustom(
                event->mimeData(),
                DROPIMAGE_LINK,
                QCoreApplication::translate("ListView", "Create shortcut in %1"),
                hitItem->model.displayName);
        } else if (!isFolder) {
            ListViewFileOpsHelper::updateInternalDragDescriptionCustom(
                event->mimeData(),
                DROPIMAGE_LABEL,
                QCoreApplication::translate("ListView", "Open with %1"),
                hitItem->model.displayName);
        } else {
            ListViewFileOpsHelper::updateInternalDragDescription(
                event->mimeData(),
                event->dropAction(),
                hitItem->model.displayName);
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
        } else if (boxSource && !isNormalBoxDragSource(event->source())) {
            ensureDragTip();
            const QString htmlText = QString("<b style='color:#70a1ff'>Move to</b> <span>Desktop</span>");
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
    const QPoint globalPos = mapToGlobal(event->pos());
    if (isMappedDragStillInsideSourceBox(event->source(), globalPos)) {
        clearDesktopAutoArrangeReorderPreview();
        event->ignore();
        return;
    }

    const DesktopRootItemState* hitItem = findDesktopItemAt(event->pos());
    if (hitItem && isDesktopPathSelected(hitItem->model.filePath)) {
        hitItem = nullptr;
    }

    const QMimeData* mime = event->mimeData();
    if (!mime->hasUrls()) {
        clearDesktopAutoArrangeReorderPreview();
        return;
    }
    const QStringList draggedPaths = ListViewFileOpsHelper::draggedLocalFilePaths(mime);
    const bool desktopSource = (event->source() == this);
    const bool boxSource = isBoxDragSource(event->source());
    const bool normalBoxSource = isNormalBoxDragSource(event->source());
    const bool mappedBoxSource = isMappedBoxDragSource(event->source());
    const bool externalSource = !desktopSource && !boxSource;
    const bool desktopReorderCopyMode = (event->keyboardModifiers() & Qt::ControlModifier);
    const bool desktopReorderShortcutMode = shouldPreferShortcutForDrop(
        draggedPaths, event->keyboardModifiers(), externalSource);
    const QStringList desktopReorderDraggedPaths = desktopSource
        ? orderedDraggedPaths(draggedPaths)
        : QStringList();
    const bool forceDesktopReorder = desktopSource &&
        m_iconShowRule.autoArrange &&
        m_iconShowRule.sortType == SortOrderType::SortByCustom &&
        !desktopReorderCopyMode &&
        !desktopReorderShortcutMode &&
        !desktopReorderDraggedPaths.isEmpty() &&
        !draggedPathsContainMappedSource(draggedPaths);

    const bool plainNormalBoxRestore = normalBoxSource &&
        !(event->keyboardModifiers() & Qt::ControlModifier) &&
        !shouldPreferShortcutForDrop(draggedPaths, event->keyboardModifiers(), false);

    // Drop onto an APP/File: Open with it; Drop onto Folder: Copy to it
    if (hitItem && !isDesktopPathSelected(hitItem->model.filePath) && !forceDesktopReorder && !plainNormalBoxRestore) {
        const bool isCopyAction = (event->keyboardModifiers() & Qt::ControlModifier);
        const bool preferShortcutForDrop = shouldPreferShortcutForDrop(
            draggedPaths, event->keyboardModifiers(), externalSource || mappedBoxSource);
        DropInteractionOptions options;
        options.copyMode = isCopyAction;
        options.preferShortcut = preferShortcutForDrop;
        options.preHideDesktopSources = desktopSource;
        options.finalizeMovedSourcesAfterSuccess = boxSource || mappedBoxSource;
        const bool handled = handleDroppedPathsOnItemTarget(draggedPaths, hitItem->model, options);
        if (handled) {
            event->setDropAction(preferShortcutForDrop ? Qt::LinkAction : (isCopyAction ? Qt::CopyAction : Qt::MoveAction));
            event->accept();
            return;
        }

        event->ignore();
        return;
    }

    if (draggedPaths.isEmpty()) {
        clearDesktopAutoArrangeReorderPreview();
        return;
    }

    // Handle dropping onto the desktop background (no hitItem)
    if (!hitItem) {
        bool isCopyAction = (event->keyboardModifiers() & Qt::ControlModifier);
        const bool preferShortcutForDrop = shouldPreferShortcutForDrop(
            draggedPaths, event->keyboardModifiers(), externalSource || mappedBoxSource);
        const bool internalBoxSource = normalBoxSource && !draggedPathsContainMappedSource(draggedPaths);
        if ((externalSource || mappedBoxSource) || isCopyAction || preferShortcutForDrop) {
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
                // 处理拖拽文件放置到桌面
                bool result = m_viewModel->handleDropFiles(draggedPaths, m_desktopDir, preferShortcutForDrop, isCopyAction);
                if (result) {
                    applyAutoArrangeAfterDesktopMutation(false, false);
                    event->setDropAction(preferShortcutForDrop ? Qt::LinkAction : (isCopyAction ? Qt::CopyAction : Qt::MoveAction));
                    event->accept();
                    return;
                }
                event->ignore();
                return;
            }
        }

        if (internalBoxSource && !isCopyAction && !preferShortcutForDrop) {
            QPoint dropPos = event->pos();
            QMap<QString, QPoint> targetPositions;
            QMap<QString, QString> targetReservationKeys;
            int offsetIndex = 0;
            const QStringList orderedPaths = orderedDraggedPaths(draggedPaths);
            const QStringList pathsToRestore = orderedPaths.isEmpty() ? draggedPaths : orderedPaths;

            for (const QString& draggedPath : pathsToRestore) {
                const QString normalizedPath = normalizeDesktopItemPath(draggedPath);
                LinBox* sourceBox = BoxManager::instance()->findBoxContainingItem(normalizedPath);
                if (!sourceBox) {
                    continue;
                }

                const QPoint requestedPos = dropPos + QPoint(
                    offsetIndex * qMax(8, m_colSpacing / 2),
                    offsetIndex * qMax(8, m_rowSpacing / 2));
                ++offsetIndex;
                const QString reservationKey = QString("__box_restore__%1").arg(normalizedPath);
                reserveCreatedItemPosition(reservationKey, requestedPos);
                targetReservationKeys.insert(normalizedPath, reservationKey);
            }

            if (!targetReservationKeys.isEmpty()) {
                QSet<LinBox*> sourceBoxes;
                for (auto it = targetReservationKeys.begin(); it != targetReservationKeys.end(); ++it) {
                    DesktopRootItemState restoredState;
                    LinBox* sourceBox = BoxManager::instance()->findBoxContainingItem(it.key());
                    if (!sourceBox || !sourceBox->takeItemState(it.key(), restoredState, false)) {
                        continue;
                    }

                    QPoint targetPos;
                    if (!takeReservedCreatedItemPosition(it.value(), targetPos)) {
                        targetPos = resolveCreatePosition(dropPos);
                    }
                    targetPositions.insert(it.key(), targetPos);
                    resetStateForDesktopView(restoredState, m_iconWidth, m_iconHeight);
                    restoredState.model.position = targetPos;
                    m_itemStates.push_back(restoredState);
                    markDesktopStateIndexDirty();
                    sourceBoxes.insert(sourceBox);
                }

                for (LinBox* sourceBox : sourceBoxes) {
                    if (!sourceBox) {
                        continue;
                    }
                    sourceBox->refreshLayout();
                    sourceBox->update();
                }

                updateAllItemRenderData();
                rebuildSpatialIndex();
                update();
                requestSaveBoxes();
                event->accept();
                return;
            }

            event->ignore();
            return;
        }
    }

    if (desktopSource) {
        const bool copyMode = (event->keyboardModifiers() & Qt::ControlModifier);
        const QStringList activeDesktopDragPaths = orderedDraggedPaths(draggedPaths);
        const bool hasMappedSourceItems = draggedPathsContainMappedSource(draggedPaths);
        if (hasMappedSourceItems) {
            bool ok = false;
            ok = m_viewModel->handleDropFiles(draggedPaths, m_desktopDir, false, copyMode);

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

        QPoint dropPos = event->pos();

        if (!activeDesktopDragPaths.isEmpty()) {
            if (m_iconShowRule.autoArrange) {
                const bool customSortDrag = (m_iconShowRule.sortType == SortOrderType::SortByCustom);
                const QStringList draggedDesktopPaths = activeDesktopDragPaths;
                const int customInsertIndex = customSortDrag
                    ? (m_desktopReorderInsertIndex >= 0
                        ? m_desktopReorderInsertIndex
                        : desktopCustomInsertionIndexForPos(dropPos, draggedDesktopPaths))
                    : -1;

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
            const QPoint displacement = dropPos - m_startPos;

            QMap<QString, QPoint> targetPositions;
            bool canPlaceAll = true;
            QSet<QString> activeDraggedPathSet;
            QSet<quint64> occupiedSlots;
            int maxRows = 1;
            int maxCols = 1;
            if (m_iconShowRule.gridAlignment) {
                maxRows = qMax(1, maxGridRowsForBounds(layoutTop, desktopUsableBottom(), itemHeight, strideY));
                maxCols = qMax(1, maxGridColsForBounds(layoutLeft, desktopUsableRight(), m_iconWidth, strideX));
                for (const QString& draggedPath : activeDesktopDragPaths) {
                    activeDraggedPathSet.insert(normalizeDesktopItemPath(draggedPath));
                }
                occupiedSlots.unite(desktopBlockedOccupiedCells());
                for (const auto& otherState : m_itemStates) {
                    if (!shouldPaintDesktopModelState(otherState)) {
                        continue;
                    }
                    const QString otherPath = normalizeDesktopItemPath(otherState.model.filePath);
                    if (!otherPath.isEmpty() && activeDraggedPathSet.contains(otherPath)) {
                        continue;
                    }
                    const QPoint otherCell = pointToGridCell(otherState.model.position, layoutLeft, layoutTop, strideX, strideY);
                    occupiedSlots.insert(packGridCell(qBound(0, otherCell.x(), maxCols - 1), qBound(0, otherCell.y(), maxRows - 1)));
                }
                for (auto it = m_pendingCreatedItemPositions.constBegin(); it != m_pendingCreatedItemPositions.constEnd(); ++it) {
                    const QPoint pendingCell = pointToGridCell(it.value(), layoutLeft, layoutTop, strideX, strideY);
                    occupiedSlots.insert(packGridCell(qBound(0, pendingCell.x(), maxCols - 1), qBound(0, pendingCell.y(), maxRows - 1)));
                }
            }

            for (const QString& draggedPath : activeDesktopDragPaths) {
                DesktopRootItemState* state = findDesktopStateByPath(draggedPath);
                if (!state) {
                    continue;
                }

                QPoint targetPos = state->model.position + displacement;
                if (m_iconShowRule.gridAlignment) {
                    const int gridX = qRound((double)(targetPos.x() - layoutLeft) / strideX);
                    const int gridY = qRound((double)(targetPos.y() - layoutTop) / strideY);
                    targetPos.setX(layoutLeft + gridX * strideX);
                    targetPos.setY(layoutTop + gridY * strideY);

                    QPoint targetCell = pointToGridCell(targetPos, layoutLeft, layoutTop, strideX, strideY);
                    targetCell.setX(qBound(0, targetCell.x(), maxCols - 1));
                    targetCell.setY(qBound(0, targetCell.y(), maxRows - 1));
                    if (occupiedSlots.contains(packGridCell(targetCell.x(), targetCell.y()))) {
                        targetPos = resolveNearestGridCreatePosition(targetPos, occupiedSlots);
                        targetCell = pointToGridCell(targetPos, layoutLeft, layoutTop, strideX, strideY);
                        targetCell.setX(qBound(0, targetCell.x(), maxCols - 1));
                        targetCell.setY(qBound(0, targetCell.y(), maxRows - 1));
                    }
                    occupiedSlots.insert(packGridCell(targetCell.x(), targetCell.y()));
                }

                targetPos.setX(qMax(layoutLeft, qMin(targetPos.x(), desktopUsableRight() - m_iconWidth + 1)));
                targetPos.setY(qMax(layoutTop, qMin(targetPos.y(), desktopUsableBottom() - itemHeight + 1)));
                targetPositions.insert(draggedPath, targetPos);
            }

            if (canPlaceAll) {
                for (auto it = targetPositions.begin(); it != targetPositions.end(); ++it) {
                    if (DesktopRootItemState* state = findDesktopStateByPath(it.key())) {
                        state->model.position = it.value();
                        state->model.boxId.clear();
                        state->model.visible = true;
                        markDesktopRenderDataDirty(state);
                    }
                }
                updateAllItemRenderData();
                rebuildSpatialIndex();
                update();
            }
            requestSaveBoxes();
        }
    } else {
        // --- 移动外部文件到桌面目录 ---
        QString desktopDir = m_desktopDir;
        QStringList srcPaths;
        for (const QString& local : draggedPaths) {
            if (!local.isEmpty() && QDir::toNativeSeparators(local) != QDir::toNativeSeparators(desktopDir)) {
                srcPaths.append(QDir::toNativeSeparators(local));
            }
        }
        
        if (!srcPaths.isEmpty()) {
            qDebug() << "External drop into Desktop:" << desktopDir;
            CSystemApi::CopyFiles(srcPaths, desktopDir);
        }
    }
    applyAutoArrangeAfterDesktopMutation(true, false);
    if (m_dragTip) m_dragTip->hide();
    event->acceptProposedAction();
}

bool ListView::applyIconViewMode(ViewMode mode)
{
    if (m_iconShowRule.viewMode == mode) {
        return false;
    }

    const int oldIconWidth = m_iconWidth;
    const int oldIconHeight = m_iconHeight;
    const int oldRowSpacing = m_rowSpacing;
    const int oldColSpacing = m_colSpacing;
    const int oldMarginTop = m_marginTop;
    const int oldMarginLeft = m_marginLeft;

    m_iconShowRule.viewMode = mode;
    syncViewModeSizes();
    if (m_iconShowRule.autoArrange) {
        applyAutoArrangeLayout(false, false);
    } else {
        applyViewModeMetricsChange(oldIconWidth, oldIconHeight, oldRowSpacing, oldColSpacing, oldMarginTop, oldMarginLeft);
        loadIconLayer();
    }
    update();
    requestSaveBoxes();
    return true;
}

bool ListView::stepIconViewMode(int direction)
{
    if (direction == 0) {
        return false;
    }

    static const QVector<ViewMode> modes = {
        ViewMode::SmallIcon,
        ViewMode::SmallMediumIcon,
        ViewMode::MediumIcon,
        ViewMode::MediumLargeIcon,
        ViewMode::LargeIcon,
        ViewMode::HugeIcon
    };

    int currentIndex = modes.indexOf(m_iconShowRule.viewMode);
    if (currentIndex < 0) {
        currentIndex = modes.indexOf(ViewMode::MediumIcon);
    }

    const int nextIndex = qBound(0, currentIndex + (direction > 0 ? 1 : -1), modes.size() - 1);
    return applyIconViewMode(modes[nextIndex]);
}

void ListView::onMenuLargeIcons() {
    qDebug() << "ListView: Large Icons triggered";
    applyIconViewMode(ViewMode::LargeIcon);
}

void ListView::onMenuMediumIcons() {
    qDebug() << "ListView: Medium Icons triggered";
    applyIconViewMode(ViewMode::MediumIcon);
}

void ListView::onMenuSmallIcons() {
    qDebug() << "ListView: Small Icons triggered";
    applyIconViewMode(ViewMode::SmallIcon);
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
        for (auto it = m_savedIconPositions.begin(); it != m_savedIconPositions.end(); ++it) {
            it.value().x = 0;
            it.value().y = 0;
        }
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

namespace PinyinHelper {
    inline char getPinyinInitial(const QChar& ch) {
        ushort unicode = ch.unicode();
        if (unicode >= 'a' && unicode <= 'z') return ch.toLatin1();
        if (unicode >= 'A' && unicode <= 'Z') return ch.toLower().toLatin1();
        if (unicode >= '0' && unicode <= '9') return ch.toLatin1();

        if (unicode >= 0x4E00 && unicode <= 0x9FA5) {
            wchar_t wch = unicode;
            char gbk[4] = { 0 };
            if (WideCharToMultiByte(936, 0, &wch, 1, gbk, sizeof(gbk), NULL, NULL) == 2) {
                unsigned char b1 = (unsigned char)gbk[0];
                unsigned char b2 = (unsigned char)gbk[1];
                unsigned int code = (b1 << 8) | b2;

                if (code >= 0xB0A1 && code <= 0xB0C4) return 'a';
                if (code >= 0xB0C5 && code <= 0xB2C0) return 'b';
                if (code >= 0xB2C1 && code <= 0xB4ED) return 'c';
                if (code >= 0xB4EE && code <= 0xB6E9) return 'd';
                if (code >= 0xB6EA && code <= 0xB7A1) return 'e';
                if (code >= 0xB7A2 && code <= 0xB8C0) return 'f';
                if (code >= 0xB8C1 && code <= 0xB9FD) return 'g';
                if (code >= 0xB9FE && code <= 0xBBF6) return 'h';
                if (code >= 0xBBF7 && code <= 0xBFA5) return 'j';
                if (code >= 0xBFA6 && code <= 0xC0AB) return 'k';
                if (code >= 0xC0AC && code <= 0xC2E7) return 'l';
                if (code >= 0xC2E8 && code <= 0xC4C2) return 'm';
                if (code >= 0xC4C3 && code <= 0xC5B5) return 'n';
                if (code >= 0xC5B6 && code <= 0xC5BD) return 'o';
                if (code >= 0xC5BE && code <= 0xC6D9) return 'p';
                if (code >= 0xC6DA && code <= 0xC8BA) return 'q';
                if (code >= 0xC8BB && code <= 0xC8F5) return 'r';
                if (code >= 0xC8F6 && code <= 0xCBF9) return 's';
                if (code >= 0xCBFA && code <= 0xCDD9) return 't';
                if (code >= 0xCDDA && code <= 0xCEF3) return 'w';
                if (code >= 0xCEF4 && code <= 0xD1B8) return 'x';
                if (code >= 0xD1B9 && code <= 0xD4D0) return 'y';
                if (code >= 0xD4D1 && code <= 0xD7F9) return 'z';
            }
        }
        return 0;
    }

    inline QString getPinyinInitials(const QString& str) {
        QString initials;
        initials.reserve(str.length());
        for (const QChar& ch : str) {
            char p = getPinyinInitial(ch);
            if (p) {
                initials.append(p);
            }
        }
        return initials;
    }

    inline bool matchesPrefix(const QString& displayName, const QString& prefix) {
        if (prefix.isEmpty()) return false;
        const QString lowerPrefix = prefix.toLower();
        const QString lowerName = displayName.toLower();
        if (lowerName.startsWith(lowerPrefix)) {
            return true;
        }
        const QString initials = getPinyinInitials(displayName);
        if (initials.startsWith(lowerPrefix)) {
            return true;
        }
        return false;
    }
}

namespace {
    void applyBoxFadeOpacity(LinBox* box, qreal opacity) {
        if (!box) return;
        QGraphicsOpacityEffect* effect = qobject_cast<QGraphicsOpacityEffect*>(box->graphicsEffect());
        if (!effect) {
            effect = new QGraphicsOpacityEffect(box);
            box->setGraphicsEffect(effect);
        }
        effect->setOpacity(qBound(0.0, opacity, 1.0));
    }
}

void ListView::onMenuShowDesktopIcons() {
    toggleDesktopIconsVisibility(m_fadeTransitionOnHide);
}

void ListView::toggleDesktopIconsVisibility(bool animated) {
    if (animated && m_hideAnimationOriginPos.isNull()) {
        m_hideAnimationOriginPos = mapFromGlobal(QCursor::pos());
    }
    setDesktopIconsVisibility(!m_iconShowRule.showDesktopIcons, animated);
}

void ListView::setDesktopIconsVisibility(bool visible, bool animated) {
    if (m_iconShowRule.showDesktopIcons == visible && (!animated || (m_desktopHideAnim && m_desktopHideAnim->state() != QAbstractAnimation::Running))) {
        return;
    }

    if (!animated) {
        if (m_desktopHideAnim && m_desktopHideAnim->state() == QAbstractAnimation::Running) {
            m_desktopHideAnim->stop();
        }
        m_desktopElementsOpacity = visible ? 1.0 : 0.0;
        m_iconShowRule.showDesktopIcons = visible;
        if (this->rightMenu) {
            this->rightMenu->setShowDesktopIconsChecked(visible);
        }
        syncBoxVisibilityWithDesktopIcons();
        const auto boxes = BoxManager::instance()->allBoxes();
        for (const auto& boxPtr : boxes) {
            if (LinBox* box = boxPtr.data()) {
                box->setGraphicsEffect(nullptr);
            }
        }
        updateAllItemRenderData();
        rebuildSpatialIndex();
        update();
        if (m_snakeGameController) {
            m_snakeGameController->onDesktopIconsVisibilityChanged();
        }
        requestSaveBoxes();
        return;
    }

    if (!m_desktopHideAnim) {
        m_desktopHideAnim = new QVariantAnimation(this);
        m_desktopHideAnim->setEasingCurve(QEasingCurve::Linear);
        connect(m_desktopHideAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            m_desktopElementsOpacity = value.toReal();
            const auto boxes = BoxManager::instance()->allBoxes();
            for (const auto& boxPtr : boxes) {
                if (LinBox* box = boxPtr.data()) {
                    applyBoxHideTransform(box);
                }
            }
            update();
        });
        connect(m_desktopHideAnim, &QVariantAnimation::finished, this, [this]() {
            const auto boxes = BoxManager::instance()->allBoxes();
            for (const auto& boxPtr : boxes) {
                if (LinBox* box = boxPtr.data()) {
                    if (!m_iconShowRule.showDesktopIcons) {
                        box->hide();
                    }
                    box->setGraphicsEffect(nullptr);
                }
            }
            m_hideAnimationOriginPos = QPoint();
            update();
        });
    }

    if (m_desktopHideAnim->state() == QAbstractAnimation::Running) {
        m_desktopHideAnim->stop();
    }

    m_desktopHideAnim->setDuration(220);

    m_iconShowRule.showDesktopIcons = visible;
    if (this->rightMenu) {
        this->rightMenu->setShowDesktopIconsChecked(visible);
    }

    if (visible) {
        const auto boxes = BoxManager::instance()->allBoxes();
        for (const auto& boxPtr : boxes) {
            if (LinBox* box = boxPtr.data()) {
                if (!box->isVisible()) {
                    box->show();
                }
                box->raise();
                applyBoxHideTransform(box);
            }
        }
        m_desktopHideAnim->setStartValue(m_desktopElementsOpacity);
        m_desktopHideAnim->setEndValue(1.0);
    } else {
        m_desktopHideAnim->setStartValue(m_desktopElementsOpacity);
        m_desktopHideAnim->setEndValue(0.0);
    }

    updateAllItemRenderData();
    rebuildSpatialIndex();
    m_desktopHideAnim->start();

    if (m_snakeGameController) {
        m_snakeGameController->onDesktopIconsVisibilityChanged();
    }
    requestSaveBoxes();
}

void ListView::onMenuDoubleClickHideIcons() {
    setDoubleClickHideIconsEnabled(!m_iconShowRule.doubleClickHideIcons);
}

void ListView::setDoubleClickHideIconsEnabled(bool enabled) {
    m_iconShowRule.doubleClickHideIcons = enabled;
    if (this->rightMenu) {
        this->rightMenu->setDoubleClickedHideIconsChecked(enabled);
    }
    QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("Desktop/DoubleClickHideIcons"), enabled);
    settings.sync();
    requestSaveBoxes();
}

bool ListView::isDoubleClickHideIconsEnabled() const {
    return m_iconShowRule.doubleClickHideIcons;
}

void ListView::setFadeTransitionOnHideEnabled(bool enabled) {
    m_fadeTransitionOnHide = enabled;
    QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("Desktop/FadeTransitionOnHide"), enabled);
    settings.sync();
}

ListView::ItemHideTransform ListView::computeItemHideTransform(const QPointF& itemCenter) const {
    ItemHideTransform xform;
    const qreal P = m_desktopElementsOpacity;
    const bool isTargetVisible = m_iconShowRule.showDesktopIcons;

    const QPointF origin = m_hideAnimationOriginPos.isNull() ? rect().center() : m_hideAnimationOriginPos;
    const qreal dist = QLineF(origin, itemCenter).length();
    const qreal maxDist = qMax<qreal>(120.0, std::hypot(width(), height()));
    const qreal delayFrac = qBound(0.0, (dist / maxDist) * 0.25, 0.25);

    qreal localP = P;
    if (isTargetVisible) {
        localP = qBound(0.0, (P - delayFrac) / (1.0 - delayFrac), 1.0);
    } else {
        const qreal elapsed = 1.0 - P;
        const qreal localElapsed = qBound(0.0, (elapsed - delayFrac) / (1.0 - delayFrac), 1.0);
        localP = 1.0 - localElapsed;
    }

    xform.opacity = localP;
    xform.scale = 1.0;

    if (isTargetVisible) {
        const qreal springOvershoot = 2.0 * std::sin(localP * 3.1415926535) * (1.0 - localP);
        xform.dy = (1.0 - localP) * 8.0 - springOvershoot;
    } else {
        xform.dy = (1.0 - localP) * 8.0;
    }

    return xform;
}

void ListView::applyBoxHideTransform(LinBox* box) {
    if (!box) return;
    const auto xform = computeItemHideTransform(box->geometry().center());
    applyBoxFadeOpacity(box, xform.opacity);
}

bool ListView::handleTypeToSelect(const QString& typedText, LinBox* activeBox) {
    if (typedText.isEmpty()) return false;

    if (!m_typeSearchTimer) {
        m_typeSearchTimer = new QTimer(this);
        m_typeSearchTimer->setSingleShot(true);
        connect(m_typeSearchTimer, &QTimer::timeout, this, [this]() {
            m_typeSearchBuffer.clear();
        });
    }

    const bool timerActive = m_typeSearchTimer->isActive();
    const bool isRepeatSingleChar = timerActive && (m_typeSearchBuffer.length() == 1) &&
                                    (m_typeSearchBuffer.compare(typedText, Qt::CaseInsensitive) == 0);

    if (isRepeatSingleChar) {
        m_typeSearchTimer->start(800);
    } else {
        if (timerActive) {
            m_typeSearchBuffer.append(typedText);
        } else {
            m_typeSearchBuffer = typedText;
        }
        m_typeSearchTimer->start(800);
    }

    struct Candidate {
        QString path;
        QString displayName;
    };
    QVector<Candidate> candidates;

    if (activeBox) {
        for (const DesktopRootItemState& state : activeBox->itemStates()) {
            if (!state.model.visible) continue;
            QString name = state.model.displayName;
            if (name.isEmpty()) name = QFileInfo(state.model.filePath).fileName();
            candidates.append({ state.model.filePath, name });
        }
    } else {
        for (const DesktopRootItemState& state : m_itemStates) {
            if (!shouldPaintDesktopModelState(state)) continue;
            QString name = state.model.displayName;
            if (name.isEmpty()) name = QFileInfo(state.model.filePath).fileName();
            candidates.append({ state.model.filePath, name });
        }
    }

    if (candidates.isEmpty()) return false;

    QVector<int> matchIndices;
    for (int i = 0; i < candidates.size(); ++i) {
        if (PinyinHelper::matchesPrefix(candidates[i].displayName, m_typeSearchBuffer)) {
            matchIndices.append(i);
        }
    }

    if (matchIndices.isEmpty()) return false;

    int targetCandidateIndex = matchIndices.first();

    if (isRepeatSingleChar && matchIndices.size() > 1) {
        QString currentSelectedPath;
        if (activeBox) {
            currentSelectedPath = focusedItemPath().isEmpty()
                ? (selectedItemPaths().isEmpty() ? QString() : selectedItemPaths().first())
                : focusedItemPath();
        } else {
            currentSelectedPath = !m_focusedDesktopPath.isEmpty()
                ? m_focusedDesktopPath
                : (m_selectedDesktopPaths.isEmpty() ? QString() : m_selectedDesktopPaths.first());
        }

        int currentMatchPos = -1;
        for (int m = 0; m < matchIndices.size(); ++m) {
            if (candidates[matchIndices[m]].path == currentSelectedPath) {
                currentMatchPos = m;
                break;
            }
        }

        if (currentMatchPos >= 0) {
            int nextMatchPos = (currentMatchPos + 1) % matchIndices.size();
            targetCandidateIndex = matchIndices[nextMatchPos];
        }
    }

    const QString selectedPath = candidates[targetCandidateIndex].path;

    if (activeBox) {
        setSelectedItemPaths({ selectedPath }, selectedPath, false);
        activeBox->ensureItemVisible(selectedPath);
        activeBox->update();
    } else {
        applyDesktopSelection({ selectedPath }, selectedPath, false);
        update();
    }

    return true;
}

bool ListView::handleUndo() {
    HWND hProgman = ::FindWindowW(L"Progman", L"Program Manager");
    HWND hDefView = ::FindWindowExW(hProgman, NULL, L"SHELLDLL_DefView", NULL);
    if (!hDefView) {
        HWND hWorkerW = NULL;
        while ((hWorkerW = ::FindWindowExW(NULL, hWorkerW, L"WorkerW", NULL)) != NULL) {
            hDefView = ::FindWindowExW(hWorkerW, NULL, L"SHELLDLL_DefView", NULL);
            if (hDefView) break;
        }
    }

    if (hDefView) {
        ::SendMessageTimeoutW(hDefView, WM_COMMAND, 28699, 0, SMTO_ABORTIFHUNG, 500, NULL);
        qDebug() << "Windows Shell Undo (WM_COMMAND 28699) dispatched to SHELLDLL_DefView";
        return true;
    }

    keybd_event(VK_CONTROL, 0, 0, 0);
    keybd_event('Z', 0, 0, 0);
    keybd_event('Z', 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
    return true;
}

void ListView::onMenuLinDesk() {
    showSettings(nullptr);
}

void ListView::showSettings(LinBox* target) {
    static QPointer<SettingUI> sharedSettingsWindow;
    if (sharedSettingsWindow) {
        m_settingsWindow = sharedSettingsWindow;
        sharedSettingsWindow->setTargetBox(target);
        QTimer::singleShot(0, sharedSettingsWindow, [window = QPointer<SettingUI>(sharedSettingsWindow)]() {
            if (window) {
                window->restoreAndActivate();
            }
        });
        return;
    }
    QVector<LinBox*> all = boxes();
    sharedSettingsWindow = new SettingUI(target, all);
    m_settingsWindow = sharedSettingsWindow;
    sharedSettingsWindow->show();
    QTimer::singleShot(0, sharedSettingsWindow, [window = QPointer<SettingUI>(sharedSettingsWindow)]() {
        if (window) {
            window->restoreAndActivate();
        }
    });
}

void ListView::executeMenuSort(SortOrderType sortType, const QString& debugName)
{
    qDebug() << "ListView: Sort By" << debugName << "triggered";
    if (m_iconShowRule.autoArrange) {
        m_iconShowRule.sortTypeBeforeAutoArrange = sortType;
        QVector<DesktopRootItemState*> states = collectAutoArrangeStates();
        sortAutoArrangeStates(states, sortType);
        assignCustomSortOrders(states);
        m_iconShowRule.sortType = SortOrderType::SortByCustom;
        updateRightMenuState();
        applyAutoArrangeLayout(false, false);
        update();
        requestSaveBoxes();
        return;
    }

    this->sortItems(sortType);
    updateRightMenuState();
    if (!m_iconShowRule.autoArrange) {
        applyAutoArrangeLayout(false, false);
    }
    update();
    requestSaveBoxes();
}

void ListView::onMenuSortByName() {
    executeMenuSort(SortOrderType::SortByName_ASC, "Name");
}

void ListView::onMenuSortBySize() {
    executeMenuSort(SortOrderType::SortBySize_ASC, "Size");
}

void ListView::onMenuSortByType() {
    executeMenuSort(SortOrderType::SortByType_ASC, "Type");
}

void ListView::onMenuSortByDate() {
    executeMenuSort(SortOrderType::SortByDateModified_ASC, "Date");
}

void ListView::onMenuRefresh() {
    syncBoxVisibilityWithDesktopIcons();

    const QVector<QPointer<LinBox>> flashBoxes = BoxManager::instance()->allBoxes();
    m_hideDesktopItemsForRefresh = true;
    update();
    for (const auto& box : flashBoxes) {
        if (box) {
            if (box->isVisible()) {
                box->setRefreshFlashing(true);
            }
        }
    }

    QTimer::singleShot(80, this, [this, flashBoxes]() {
        m_hideDesktopItemsForRefresh = false;
        if (m_iconShowRule.showDesktopIcons) {
            for (const auto& box : flashBoxes) {
                if (box) {
                    box->setRefreshFlashing(false);
                }
            }
        }
        syncBoxVisibilityWithDesktopIcons();
        this->update();

        const bool isInteracting = m_isSelecting || m_isDragging || (RenameManager::instance() && RenameManager::instance()->isRenaming());

        if (m_enablePersistence) {
            for (const auto& state : m_itemStates) {
                const QString normPath = normalizeDesktopItemPath(state.model.filePath);
                if (normPath.isEmpty()) {
                    continue;
                }
                if (state.model.position != QPoint(0, 0)) {
                    m_savedIconPositions[normPath] = {
                        state.model.position.x(),
                        state.model.position.y(),
                        state.model.boxId
                    };
                }
            }
        }

        qDebug() << "Comprehensive State Sync (Optimized Refresh)...";
        
        if (monitor) {
            monitor->refreshIconsState();
            sysIconRegisterInfo = monitor->getCurrentIcons();
        }

        DesktopItemInitContext context = currentDesktopItemInitContext();
        bool anyItemChanged = false;

        for (auto& state : m_itemStates) {
            LinWidgetRuntimeBridge::instance().doMessageLoopWork();
            if (state.model.filePath.isEmpty()) {
                continue;
            }

            const QFileInfo currentFi(state.model.filePath);
            const bool exists = currentFi.exists();
            const bool isSpecial = (state.model.objectType == ObjectType::SpecialFolder);
            const bool metadataChanged = !exists ||
                (currentFi.lastModified() != state.model.fileInfo.lastModified()) ||
                (currentFi.size() != state.model.fileInfo.size());

            if (isSpecial || metadataChanged) {
                anyItemChanged = true;
                const ListItemData data = DesktopItemLoader().loadItem(state.model.filePath);
                DesktopRootItemModel updatedModel;
                initDesktopRootItemModel(updatedModel, data, context);
                updatedModel.layoutMode = state.model.layoutMode;
                updatedModel.position = state.model.position;
                updatedModel.baseSize = QSize(m_iconWidth, m_iconHeight);
                updatedModel.effectiveSize = state.model.effectiveSize.isValid()
                    ? state.model.effectiveSize
                    : QSize(m_iconWidth, m_iconHeight);
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
                state.model = updatedModel;
                markDesktopRenderDataDirty(&state);
            } else {
                state.model.baseSize = QSize(m_iconWidth, m_iconHeight);
                state.model.fileInfo = currentFi;
                state.model.isReady = true;
            }
        }

        if (anyItemChanged) {
            updateAllItemRenderData();
        }

        if (!isInteracting) {
            if (m_iconShowRule.autoArrange) {
                applyAutoArrangeAfterDesktopMutation(false, false);
            } else {
                rebuildSpatialIndex();
                update();
            }
        }

        LinWidgetRuntimeBridge::instance().doMessageLoopWork();

        // Refresh icons for items in boxes and keep box visibility consistent with desktop icon visibility.
        const QVector<QPointer<LinBox>> allBoxes = BoxManager::instance()->allBoxes();
        for (const auto& box : allBoxes) {
            if (box) {
                if (box->isFolderMapped()) {
                    loadMappedFolderItemsForBox(box.data());
                }
                box->refreshLayout();
                box->update();
            }
        }
        syncBoxVisibilityWithDesktopIcons();
        this->update();

        QTimer::singleShot(0, this, [this]() {
            syncBoxVisibilityWithDesktopIcons();
        });
    });
}

void ListView::onMenuPaste() {
    const QStringList filePaths = ListViewFileOpsHelper::clipboardLocalFilePaths();
    if (filePaths.isEmpty()) {
        QStringList savedVirtualPaths;
        const int savedCount = ListViewFileOpsHelper::pasteVirtualClipboardFilesToDirectory(m_desktopDir, &savedVirtualPaths);
        qDebug() << "ListView: Virtual paste triggered" << "saved:" << savedCount;
        if (savedCount <= 0) {
            qDebug() << "ListView: Paste skipped, clipboard has no local files";
        }
        return;
    }

    const bool isCutOperation = ListViewFileOpsHelper::clipboardContainsCutItems();
    bool ok = true;

    ok = m_viewModel->handleDropFiles(filePaths, m_desktopDir, false, !isCutOperation);

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

    bool ok = createShortcutsInFolder(filePaths, m_desktopDir);
    qDebug() << "ListView: Paste Shortcut triggered, result:" << ok;
}

void ListView::onFileSystemChanged(const FileSystemEventInfo& eventInfo)
{
    const QString newPath = normalizeDesktopPath(eventInfo.newPath);
    const QString oldPath = normalizeDesktopPath(eventInfo.oldPath);

    qDebug() << "[onFileSystemChanged]"
             << "type:" << (int)eventInfo.eventType
             << "isDir:" << eventInfo.isDirectory
             << "old:" << oldPath
             << "new:" << newPath;
    if (rightMenu) {
        rightMenu->invalidateCustomMenuCaches();
    }

    // 防抖合并重排重绘，避免连续高频文件系统事件卡顿
    auto scheduleRelayout = [this]() {
        if (m_fsRelayoutTimer) {
            m_fsRelayoutTimer->start(120);
        } else {
            if (m_iconShowRule.autoArrange) {
                applyAutoArrangeLayout(false, false);
            } else {
                updateAllItemRenderData();
                rebuildSpatialIndex();
                update();
            }
        }
    };

    // 重新排序
    auto resort = [this]() {
        this->performSort(m_iconShowRule.sortType);
    };

    auto removeTrackedPath = [this, scheduleRelayout](const QString& path) -> bool {
        if (path.isEmpty()) {
            return false;
        }

        IconCacheManager::instance().invalidateItemCache(QString(), path);

        if (DesktopRootItemState* state = findDesktopStateByPath(path)) {
            IconCacheManager::instance().invalidateItemCache(state->model.iconCacheKey, path);
            removeDesktopStateByPath(path);
            scheduleRelayout();
            requestSaveBoxes();
            return true;
        }

        if (LinBox* box = BoxManager::instance()->findBoxContainingItem(path)) {
            if (box->removeItemState(path, true)) {
                requestSaveBoxes();
                return true;
            }
        }

        return false;
    };

    switch (eventInfo.eventType) {
    case FileSystemEvent::FileCreated:
    case FileSystemEvent::DirectoryCreated:
    {
        const QString effectivePath = (eventInfo.eventType == FileSystemEvent::DirectoryCreated)
            ? ListViewFileOpsHelper::normalizeNewFolderCasePath(eventInfo.newPath)
            : eventInfo.newPath;
        const QString actualDiskPath = ListViewFileOpsHelper::canonicalDiskPath(effectivePath);
        const QString resolvedNewPath = normalizeDesktopPath(actualDiskPath);
        if (resolvedNewPath.isEmpty()) break;
        if (findAnyItemStateByPath(resolvedNewPath)) break;
        
        qDebug() << "[onFileSystemChanged] Creating item:" << actualDiskPath;
        QPointer<LinBox> pendingTargetBox = m_pendingCreatedItemBoxes.value(resolvedNewPath);
        if (pendingTargetBox.isNull()) {
            pendingTargetBox = m_pendingShellNewTargetBox;
        }
        if (pendingTargetBox) {
            if (pendingTargetBox->isFolderMapped()) {
                handlePendingShellCreatedItem(actualDiskPath);
            } else {
                auto boxIt = m_pendingCreatedItemBoxes.find(resolvedNewPath);
                const int insertIndex = (boxIt != m_pendingCreatedItemBoxes.end() && m_pendingCreatedItemInsertIndices.contains(resolvedNewPath))
                    ? m_pendingCreatedItemInsertIndices.take(resolvedNewPath) : -1;
                if (boxIt != m_pendingCreatedItemBoxes.end()) {
                    m_pendingCreatedItemBoxes.erase(boxIt);
                }
                clearPendingShellNewTargetBox();

                const ListItemData data = DesktopItemLoader().loadItem(actualDiskPath);
                DesktopRootItemState state;
                initDesktopRootItemModel(state.model, data, currentDesktopItemInitContext());
                state.model.layoutMode = DesktopLayoutMode::IconMode;
                state.model.baseSize = QSize(m_iconWidth, m_iconHeight);
                state.model.effectiveSize = state.model.baseSize;
                state.model.boxId = pendingTargetBox->boxId();
                state.model.visible = true;

                if (insertIndex >= 0) {
                    pendingTargetBox->insertItemState(state, insertIndex, false);
                } else {
                    pendingTargetBox->addItemState(state, false);
                }

                pendingTargetBox->refreshLayout();
                pendingTargetBox->raise();
                pendingTargetBox->update();
                update();
            }
        } else {
            addDesktopModelItemFromPath(actualDiskPath, true);
        }
        
        requestSaveBoxes();
        break;
    }
    case FileSystemEvent::FileDeleted:
    case FileSystemEvent::DirectoryDeleted:
    {
        const QString& deletePath = !oldPath.isEmpty() ? oldPath : newPath;
        if (deletePath.isEmpty()) break;
        if (consumePendingExternalRemoval(deletePath)) break;
        
        removeTrackedPath(deletePath);
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
                if (refreshDesktopModelItemPath(oldPath, eventInfo.newPath)) {
                    if (eventInfo.eventType == FileSystemEvent::DirectoryRenamed) {
                        invalidateFolderItemIconCache(oldPath);
                        invalidateFolderItemIconCache(newPath);
                    }
                    const DesktopRootItemState* updatedState = findDesktopStateByPath(newPath);
                    if (updatedState && !updatedState->model.visible) {
                        refreshVisibilityForWindowsConfig();
                        requestSaveBoxes();
                        break;
                    }
                    resort();
                    if (!m_iconShowRule.autoArrange) {
                        scheduleRelayout();
                    }
                }
            } else {
                removeDesktopStateByPath(oldPath);
            }
        } else if (LinBox* sourceBox = BoxManager::instance()->findBoxContainingItem(oldPath)) {
            const QString oldDir = QFileInfo(oldPath).absolutePath();
            const QString newDir = QFileInfo(newPath).absolutePath();

            if (oldDir == newDir) {
                if (eventInfo.eventType == FileSystemEvent::DirectoryRenamed) {
                    invalidateFolderItemIconCache(oldPath);
                    invalidateFolderItemIconCache(newPath);
                }
                refreshBoxModelItemPath(sourceBox, oldPath, eventInfo.newPath);
            } else {
                sourceBox->removeItemState(oldPath, true);
            }
        } else if (!findAnyItemStateByPath(newPath)) {
            addDesktopModelItemFromPath(eventInfo.newPath, true);
        }

        auto boxIt = m_pendingCreatedItemBoxes.find(newPath);
        if (boxIt != m_pendingCreatedItemBoxes.end()) {
            QPointer<LinBox> targetBox = boxIt.value();
            m_pendingCreatedItemBoxes.erase(boxIt);
            const bool hasPendingInsertIndex = m_pendingCreatedItemInsertIndices.contains(newPath);
            const int insertIndex = hasPendingInsertIndex ? m_pendingCreatedItemInsertIndices.take(newPath) : -1;
            if (targetBox) {
                moveItemToBox(eventInfo.newPath, targetBox, false, insertIndex);
                targetBox->refreshLayout();
                targetBox->raise();
                if (m_iconShowRule.autoArrange) {
                    applyAutoArrangeAfterDesktopMutation(false, false);
                } else {
                    scheduleRelayout();
                }
            }
        }
        else {
            handlePendingShellCreatedItem(eventInfo.newPath);
        }
        
        requestSaveBoxes();
        break;
    }
    case FileSystemEvent::FileUpdated:
    case FileSystemEvent::FileAttributesChanged:
    case FileSystemEvent::DirectoryUpdated:
    {
        const QString& updatePath = !newPath.isEmpty() ? newPath : oldPath;
        if (updatePath.isEmpty()) break;

        // T3: 文件更新时主动失效 image_preview 缓存
        IconCacheManager::instance().invalidatePixmapsByPrefix("image_preview:" + QDir::toNativeSeparators(updatePath));

        if (CSystemApi::GetPathType(updatePath) == PathType::InvalidPath) {
            removeTrackedPath(updatePath);
            break;
        }

        if (eventInfo.eventType == FileSystemEvent::DirectoryUpdated) {
            refreshFolderItemByPath(eventInfo.newPath);
            break;
        }

        if (findDesktopStateByPath(updatePath)) {
            refreshDesktopModelItemPath(updatePath, !eventInfo.newPath.isEmpty() ? eventInfo.newPath : eventInfo.oldPath);
            if (eventInfo.eventType == FileSystemEvent::FileAttributesChanged) {
                update();
            }
        } else if (LinBox* box = BoxManager::instance()->findBoxContainingItem(updatePath)) {
            refreshBoxModelItemPath(box, updatePath, !eventInfo.newPath.isEmpty() ? eventInfo.newPath : eventInfo.oldPath);
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

    // Synchronously save boxes and icon positions before tearing down windows
    saveBoxes(false);

    // Close all remaining windows (boxes, etc.)
    QApplication::closeAllWindows();
    
    // Final quit
    qApp->quit();
}

static QPoint resolveSafeBoxCreationPos(const QPoint& requestedPos, ListView* view)
{
    QPoint targetPos = requestedPos;
    if (targetPos.isNull() || targetPos == QPoint(-1, -1)) {
        targetPos = view->lastRightClickPos();
    }
    if (targetPos.isNull() || targetPos == QPoint(-1, -1)) {
        targetPos = view->mapFromGlobal(QCursor::pos());
    }

    const QRect viewBounds = view->rect();
    if (!viewBounds.contains(targetPos)) {
        targetPos = QPoint(qBound(20, targetPos.x(), qMax(20, viewBounds.width() - 320)),
                           qBound(20, targetPos.y(), qMax(20, viewBounds.height() - 320)));
    }
    return targetPos;
}

void ListView::onMenuCreateBox(const QPoint& pos)
{
    const QPoint creationPos = resolveSafeBoxCreationPos(pos, this);
    BoxManager* manager = BoxManager::instance();
	LinBox* box = manager->createBox(creationPos, this);
    
    // Connect box signals to ListView
    manager->setupConnections(box, this);
    box->show();
    box->raise();
    box->update();
    this->update(box->geometry().adjusted(-16, -16, 16, 16));
}

void ListView::onMenuMapFolder(const QPoint& pos)
{
    const QPoint creationPos = resolveSafeBoxCreationPos(pos, this);

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
	LinBox* box = manager->createBox(creationPos, this);
    manager->setupConnections(box, this);

    box->setBoxTitle(QString());
    box->setMappedFolderPath(normalizedPath);
    manager->applyUnifiedStyleToBox(box);
    box->show();
    box->raise();
    box->update();
    this->update(box->geometry().adjusted(-16, -16, 16, 16));
    loadMappedFolderItemsForBox(box);
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

void ListView::createRecentModifiedItemsBox()
{
    BoxManager* manager = BoxManager::instance();
    if (!manager) {
        return;
    }

    const QPoint boxPos(desktopLayoutLeft() + 24, desktopLayoutTop() + 24);
    LinBox* box = manager->createBox(boxPos, this);
    if (!box) {
        return;
    }
    manager->setupConnections(box, this);
    box->setBoxTitle(QStringLiteral("Recent Changes"));

    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-7);
    QStringList recentPaths;
    for (const DesktopRootItemState& state : m_itemStates) {
        const DesktopRootItemModel& model = state.model;
        if (!model.boxId.isEmpty() || !model.visible || model.filePath.isEmpty() || model.filePath.startsWith(QStringLiteral("::"))) {
            continue;
        }

        QFileInfo fileInfo(model.filePath);
        if (!fileInfo.exists() || fileInfo.fileName().compare(QStringLiteral("desktop.ini"), Qt::CaseInsensitive) == 0) {
            continue;
        }

        if (fileInfo.lastModified() >= cutoff) {
            recentPaths.append(model.filePath);
        }
    }

    if (!recentPaths.isEmpty()) {
        moveItemsToBox(recentPaths, box, -1, false);
    }

    box->refreshLayout();
    box->update();
    box->raise();
    updateAllItemRenderData();
    rebuildSpatialIndex();
    update();
    requestSaveBoxes();
}


bool ListView::startDesktopItemDrag(const QPoint& clickPos)
{
    const QStringList dragPaths = selectedDesktopPaths();
    if (dragPaths.isEmpty()) {
        return false;
    }

    QList<QUrl> urls;
    urls.reserve(dragPaths.size());
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
    mimeData->setDropDescriptionForAction(Qt::MoveAction, QCoreApplication::translate("ListView", "Desktop"));

    QVector<DesktopRootItemRenderData> ghostItems;
    QRect ghostBounds;
    for (const QString& path : dragPaths) {
        const DesktopRootItemState* state = findDesktopStateByPath(path);
        if (!state) {
            continue;
        }

        const DesktopRootItemRenderData& renderData = state->model.renderData;
        if (renderData.itemRect.isNull() || !renderData.itemRect.size().isValid()) {
            continue;
        }

        ghostItems.append(renderData);
        ghostBounds = ghostBounds.isNull()
            ? renderData.itemRect
            : ghostBounds.united(renderData.itemRect);
    }

    QPixmap dragPixmap;
    QPoint hotSpot;
    if (!ghostItems.isEmpty() && ghostBounds.isValid()) {
        constexpr int kMaxDragGhostItemCount = 20;
        const bool isOversized = ghostItems.size() > kMaxDragGhostItemCount || ghostBounds.width() > 2560 || ghostBounds.height() > 2560;
        if (isOversized) {
            const DesktopRootItemState* hitState = findDesktopStateAt(clickPos);
            DesktopRootItemRenderData primaryData = (hitState && hitState->model.visible)
                ? hitState->model.renderData
                : ghostItems.first();

            const int pad = 8;
            const int badgeHeight = 18;
            const QSize baseSize = primaryData.itemRect.size();
            const QSize pixmapSize(baseSize.width() + pad * 2, baseSize.height() + pad * 2);

            const qreal dpr = qMax<qreal>(1.0, devicePixelRatioF());
            const QSize physicalPixmapSize(qMax(1, qRound(pixmapSize.width() * dpr)),
                                          qMax(1, qRound(pixmapSize.height() * dpr)));
            dragPixmap = QPixmap(physicalPixmapSize);
            if (dpr > 1.0) {
                dragPixmap.setDevicePixelRatio(dpr);
            }
            dragPixmap.fill(Qt::transparent);

            QPainter painter(&dragPixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.setRenderHint(QPainter::TextAntialiasing);

            painter.save();
            painter.translate(-primaryData.itemRect.topLeft() + QPoint(pad, pad));
            paintDesktopRootItemGhost(painter, primaryData);
            painter.restore();

            const QString countText = QString::number(ghostItems.size());
            QFont badgeFont = font();
            badgeFont.setPixelSize(11);
            badgeFont.setBold(true);
            painter.setFont(badgeFont);

            const QFontMetrics fm(badgeFont);
            const int textWidth = fm.horizontalAdvance(countText);
            const int badgeWidth = qMax(badgeHeight, textWidth + 10);
            const QRect badgeRect(pixmapSize.width() - badgeWidth - 2, 2, badgeWidth, badgeHeight);

            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 120, 215, 230));
            painter.drawRoundedRect(badgeRect, badgeHeight / 2.0, badgeHeight / 2.0);

            painter.setPen(Qt::white);
            painter.drawText(badgeRect, Qt::AlignCenter, countText);
            painter.end();

            hotSpot = (clickPos - primaryData.itemRect.topLeft()) + QPoint(pad, pad);
            hotSpot.setX(qBound(0, hotSpot.x(), qMax(0, dragPixmap.width() - 1)));
            hotSpot.setY(qBound(0, hotSpot.y(), qMax(0, dragPixmap.height() - 1)));
        } else {
            const qreal dpr = qMax<qreal>(1.0, devicePixelRatioF());
            const QSize physicalGhostSize(qMax(1, qRound(ghostBounds.width() * dpr)),
                                          qMax(1, qRound(ghostBounds.height() * dpr)));
            dragPixmap = QPixmap(physicalGhostSize);
            if (dpr > 1.0) {
                dragPixmap.setDevicePixelRatio(dpr);
            }
            dragPixmap.fill(Qt::transparent);

            QPainter painter(&dragPixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.setRenderHint(QPainter::TextAntialiasing);
            painter.translate(-ghostBounds.topLeft());
            for (const DesktopRootItemRenderData& renderData : ghostItems) {
                paintDesktopRootItemGhost(painter, renderData);
            }
            painter.end();

            hotSpot = clickPos - ghostBounds.topLeft();
            hotSpot.setX(qBound(0, hotSpot.x(), qMax(0, dragPixmap.width() - 1)));
            hotSpot.setY(qBound(0, hotSpot.y(), qMax(0, dragPixmap.height() - 1)));
        }
    }

    clearDragGhosts();

    QDrag drag(this);
    drag.setMimeData(mimeData);
    if (!dragPixmap.isNull()) {
        drag.setPixmap(dragPixmap);
        drag.setHotSpot(hotSpot);
    }

    const Qt::DropAction resultAction = drag.exec(Qt::CopyAction | Qt::MoveAction | Qt::LinkAction, Qt::MoveAction);
    if (resultAction == Qt::MoveAction) {
        QStringList removedSourcePaths;
        for (const QString& path : dragPaths) {
            if (!doesFileExistNoResolve(path)) {
                removedSourcePaths.append(path);
            }
        }
        finalizeMovedModelSourcesAfterFileOp(removedSourcePaths);
    }

    clearDesktopAutoArrangeReorderPreview();
    m_isDragging = false;
    m_draggedDesktopPath.clear();
    m_draggedBoxPath.clear();
    return true;
}

void ListView::finalizeActiveDrag(LinBox* box, QMouseEvent* event)
{
	if (m_isDragging) {
        for (LinBox* existingBox : boxes()) {
            if (existingBox) {
                existingBox->clearDropPreview();
            }
        }
        if (m_dragTip) {
            m_dragTip->hide();
        }
        LinBox* targetBox = box;
        DesktopRootItemState* dropTargetState = nullptr;
        const QPoint releasePos = event ? event->pos() : QPoint();
        if (!targetBox && event) {
            const QPoint globalPos = event->globalPos();
            const QVector<LinBox*> allBoxes = boxes();
            for (LinBox* candidateBox : allBoxes) {
                if (!candidateBox) {
                    continue;
                }
                const QRect candidateGlobalRect(candidateBox->mapToGlobal(QPoint(0, 0)),
                                                candidateBox->size());
                if (candidateGlobalRect.contains(globalPos)) {
                    targetBox = candidateBox;
                    break;
                }
            }
            if (!targetBox) {
                dropTargetState = findDesktopStateAt(releasePos);
                if (dropTargetState && isDesktopPathSelected(dropTargetState->model.filePath)) {
                    dropTargetState = nullptr;
                }
            }
        }

        QStringList draggedPaths;
        for (const auto& ghost : m_ghosts) {
            if (!ghost.filePath.isEmpty() && !draggedPaths.contains(ghost.filePath)) {
                draggedPaths.append(ghost.filePath);
            }
        }

        const Qt::KeyboardModifiers modifiers = event ? event->modifiers() : QApplication::keyboardModifiers();
        const bool copyMode = (modifiers & Qt::ControlModifier);
        const bool preferShortcut = shouldPreferShortcutForDrop(draggedPaths, modifiers, false);
        const bool desktopBackgroundFileOperation = !targetBox && !dropTargetState && !draggedPaths.isEmpty() &&
            (copyMode || preferShortcut);

        if (!targetBox && m_iconShowRule.autoArrange && !desktopBackgroundFileOperation) {
            const QStringList activeDesktopDragPaths = orderedDraggedPaths(draggedPaths);
            if (m_iconShowRule.sortType == SortOrderType::SortByCustom && !activeDesktopDragPaths.isEmpty()) {
                const int customInsertIndex = m_desktopReorderInsertIndex >= 0
                    ? m_desktopReorderInsertIndex
                    : desktopCustomInsertionIndexForPos(releasePos, activeDesktopDragPaths);

                QVector<DesktopRootItemState*> orderedStates = collectAutoArrangeStates();
                sortAutoArrangeStates(orderedStates, SortOrderType::SortByCustom);

                QSet<QString> draggedPathSet;
                QVector<DesktopRootItemState*> orderedDraggedStates;
                for (const QString& path : activeDesktopDragPaths) {
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

            clearDragGhosts();
            m_isDragging = false;
            m_draggedDesktopPath.clear();
            m_draggedBoxPath.clear();
            clearDesktopAutoArrangeReorderPreview();
            applyAutoArrangeLayout(true, false);
            return;
        }

		bool hasCollision = false;
        bool desktopPositionsChanged = false;
        QMap<QString, QPoint> resolvedDesktopPositions;

        if (!targetBox && !dropTargetState && m_iconShowRule.gridAlignment && !draggedPaths.isEmpty() &&
            !desktopBackgroundFileOperation) {
            if (applyGridAlignedDesktopDropLayout(draggedPaths, releasePos)) {
                clearDragGhosts();
                m_isDragging = false;
                m_draggedDesktopPath.clear();
                m_draggedBoxPath.clear();
                clearDesktopAutoArrangeReorderPreview();
                return;
            }
        }

        if (!targetBox && !dropTargetState && !desktopBackgroundFileOperation && !draggedPaths.isEmpty()) {
            resolvedDesktopPositions = resolveDesktopDragFinalPositions(draggedPaths, releasePos);
            hasCollision = resolvedDesktopPositions.isEmpty();
        }

		// 2. Apply movement
		for (auto& ghost : m_ghosts) {
            DesktopRootItemState* draggedState = ghost.filePath.isEmpty()
                ? nullptr
                : findDesktopStateByPath(ghost.filePath);
			if (draggedState) {
				if (!hasCollision) {
					QPoint finalPos = localPosForGhost(ghost);
                    const QString normalizedPath = normalizeDesktopItemPath(ghost.filePath);
                    if (!normalizedPath.isEmpty() && resolvedDesktopPositions.contains(normalizedPath)) {
                        finalPos = resolvedDesktopPositions.value(normalizedPath);
                    }
					
					// Only move if it's a desktop icon (parent is ListView)
					// Items in box stay where they are (refreshLayout handles them)
					if (!targetBox && !dropTargetState && !desktopBackgroundFileOperation) {
                        if (draggedState) {
                            if (draggedState->model.position != finalPos) {
                                draggedState->model.position = finalPos;
                                draggedState->model.renderData.itemRect.moveTopLeft(finalPos);
                                markDesktopRenderDataDirty(draggedState);
                                desktopPositionsChanged = true;
                            }
                        }
					}
				}
			}
		}

		clearDragGhosts();
		m_isDragging = false;
        const QString draggedDesktopPath = m_draggedDesktopPath;
        m_draggedDesktopPath.clear();
        m_draggedBoxPath.clear();
        if (dropTargetState && !draggedPaths.isEmpty()) {
            if (handleDroppedPathsOnDesktopTarget(
                    draggedPaths,
                    dropTargetState->model,
                    copyMode,
                    preferShortcut,
                    true)) {
                clearDesktopAutoArrangeReorderPreview();
                requestSaveBoxes();
                return;
            }

            if (!draggedDesktopPath.isEmpty()) {
                m_draggedDesktopPath = draggedDesktopPath;
            }
            updateAllItemRenderData();
            update();
        } else if (desktopBackgroundFileOperation) {
            if (!m_iconShowRule.autoArrange) {
                for (const QString& localPath : draggedPaths) {
                    if (localPath.isEmpty()) {
                        continue;
                    }
                    const QString reservedPath = preferShortcut
                        ? ListViewFileOpsHelper::uniqueShortcutPathForTarget(localPath, m_desktopDir)
                        : ListViewFileOpsHelper::uniquePasteDestinationPath(localPath, m_desktopDir);
                    reserveCreatedItemPosition(reservedPath, releasePos);
                }
            }

            bool ok = false;
            if (preferShortcut) {
                ok = createShortcutsInFolder(draggedPaths, m_desktopDir);
            } else {
                ok = ListViewFileOpsHelper::copyFilesToDirectoryWithRule(draggedPaths, m_desktopDir);
            }

            if (ok) {
                applyAutoArrangeAfterDesktopMutation(false, false);
            } else {
                updateAllItemRenderData();
                update();
            }
        } else if (targetBox && !draggedPaths.isEmpty()) {
            if (targetBox->isFolderMapped()) {
                const QString targetFolder = normalizeMappedFolderPathForLookup(targetBox->mappedFolderPath());
                const bool canWriteMappedFolder =
                    !targetFolder.isEmpty() &&
                    targetBox->mappedFolderState() == LinBox::MappedFolderState::Normal;

                QStringList srcPaths;
                if (canWriteMappedFolder) {
                    for (const QString& draggedPath : draggedPaths) {
                        if (!draggedPath.isEmpty()) {
                            srcPaths.append(QDir::toNativeSeparators(draggedPath));
                        }
                    }
                }

                bool ok = false;
                if (canWriteMappedFolder && !srcPaths.isEmpty()) {
                    if (preferShortcut) {
                        ok = createShortcutsInFolder(draggedPaths, targetFolder);
                    } else if (copyMode) {
                        ok = CSystemApi::CopyFiles(srcPaths, QDir::toNativeSeparators(targetFolder));
                    } else {
                        ok = CSystemApi::MoveFiles(srcPaths, QDir::toNativeSeparators(targetFolder));
                    }
                }

                if (ok) {
                    if (!copyMode && !preferShortcut) {
                        finalizeMovedModelSourcesAfterFileOp(draggedPaths);
                    }
                    reloadMappedFolderBox(targetBox);
                    targetBox->refreshLayout();
                    targetBox->update();
                    requestSaveBoxes();
                } else {
                    updateAllItemRenderData();
                    update();
                }
            } else {
                moveItemsToBox(draggedPaths, targetBox, -1, true);
            }
        } else if (!targetBox && (desktopPositionsChanged || hasCollision)) {
            updateAllItemRenderData();
            update();
        }
        clearDesktopAutoArrangeReorderPreview();
		
		// Snap back to grid in LinBox if applicable
		if (targetBox) {
			targetBox->refreshLayout();
		}
		
		// 触发持久化保存
        if (targetBox || dropTargetState || desktopBackgroundFileOperation || desktopPositionsChanged) {
		    requestSaveBoxes();
        }
	}
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
        : QDir::toNativeSeparators(m_desktopDir);
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

bool ListView::createNewTextDocumentInHost()
{
    if (RenameManager::instance()->isRenaming()) {
        return false;
    }

    const QString targetDirectory = QDir::toNativeSeparators(m_desktopDir);
    if (targetDirectory.isEmpty()) {
        return false;
    }

    const QString templatePath = QDir(targetDirectory).filePath(tr("New Text Document.txt"));
    const QString newTextPath = ListViewFileOpsHelper::uniquePasteDestinationPath(templatePath, targetDirectory);
    if (newTextPath.isEmpty()) {
        return false;
    }

    if (!m_iconShowRule.autoArrange) {
        reserveCreatedItemPosition(newTextPath, generateItemPosition());
    }

    QFile file(newTextPath);
    if (!file.open(QIODevice::WriteOnly)) {
        m_pendingCreatedItemPositions.remove(normalizeDesktopPath(newTextPath));
        return false;
    }

    file.close();
    return true;
}

void ListView::onMenuDelete(bool permanently)
{
    QStringList desktopPathsToDelete;
    QStringList boxPathsToDelete;
    QSet<QString> seenDeletePaths;

    for (const QString& path : selectedDesktopPaths()) {
        const DesktopRootItemState* state = findDesktopStateByPath(path);
        if (!state || state->model.itemData.isRealSpecialFolderIcon()) {
            continue;
        }
        const QString normalizedPath = normalizeDesktopItemPath(state->model.filePath);
        if (normalizedPath.isEmpty() || seenDeletePaths.contains(normalizedPath)) {
            continue;
        }
        seenDeletePaths.insert(normalizedPath);
        desktopPathsToDelete.append(normalizedPath);
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
    }

    QStringList deletedDesktopPaths;
    QStringList deletedBoxPaths;
    const bool deleteOk = m_viewModel->deleteSelectedItems(desktopPathsToDelete, boxPathsToDelete, permanently, deletedDesktopPaths, deletedBoxPaths);
    if (!deleteOk) {
        return;
    }

    trackPendingExternalRemovalPaths(deletedDesktopPaths);
    removeItemPathsBatch(deletedDesktopPaths);
    for (const QString& path : deletedDesktopPaths) {
        m_pendingExternalRemovalPaths.insert(normalizeDesktopPath(path));
        removeDesktopStateByPath(path);
    }
    for (const QString& path : deletedBoxPaths) {
        if (LinBox* box = BoxManager::instance()->findBoxContainingItem(path)) {
            box->removeItemState(path, true);
        }
    }
    requestSaveBoxes();
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

bool ListView::sendPreviewRequestByPath(const QString& filePath)
{
    QString errorMessage;
    const bool ok = SpaceLookIpcClient::sendPreviewRequest(filePath, &errorMessage);
    if (!ok) {
        qWarning() << "Failed to send SpaceLook preview request:" << errorMessage;
    }
    return ok;
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
    CSystemApi::File_RightMenuOperations(nativePath, Z_PROPERTIES);
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

    QStringList srcPaths;
    QStringList args;
    for (const QString& localPath : sourcePaths) {
        if (!localPath.isEmpty()) {
            const QString nativePath = QDir::toNativeSeparators(localPath);
            args << "\"" + nativePath + "\"";
            srcPaths.append(nativePath);
        }
    }

    if (srcPaths.empty()) {
        return false;
    }

    const bool isRecycleBin = (clsid == APP_RECYCLE_BIN_CLS);
    if (isRecycleBin) {
        const QStringList orderedPaths = orderedDraggedPaths(sourcePaths);
        if (!orderedPaths.isEmpty()) {
            setItemPathsTransientHidden(orderedPaths, true);
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }

        const bool deleteOk = CSystemApi::DeleteFiles(srcPaths, true, true);
        if (!deleteOk) {
            if (!orderedPaths.isEmpty()) {
                setItemPathsTransientHidden(orderedPaths, false);
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            }
            return false;
        }

        if (!orderedPaths.isEmpty()) {
            trackPendingExternalRemovalPaths(orderedPaths);
            removeItemPathsBatch(orderedPaths);
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
            return CSystemApi::CopyFiles(srcPaths, QDir::toNativeSeparators(targetPath));
        }
        return CSystemApi::MoveFiles(srcPaths, QDir::toNativeSeparators(targetPath));
    }

    std::wstring wApp = QDir::toNativeSeparators(targetPath).toStdWString();
    std::wstring wArgs = args.join(" ").toStdWString();
    HINSTANCE result = ShellExecuteW(NULL, L"open", wApp.c_str(), wArgs.c_str(), NULL, SW_SHOWNORMAL);
    return (INT_PTR)result > 32;
}

void ListView::updateGhostDragHoverPreview(const QPoint& globalPos)
{
    const QStringList draggedPaths = orderedDraggedPaths(selectedDesktopPaths());
    if (draggedPaths.isEmpty()) {
        for (LinBox* box : boxes()) {
            if (box) {
                box->clearDropPreview();
            }
        }
        if (m_dragTip) {
            m_dragTip->hide();
        }
        return;
    }

    LinBox* hoveredBox = nullptr;
    for (LinBox* box : boxes()) {
        if (!box) {
            continue;
        }

        const QRect boxGlobalRect(box->mapToGlobal(QPoint(0, 0)), box->size());
        if (boxGlobalRect.contains(globalPos)) {
            hoveredBox = box;
            const QPoint localPos = box->mapFromGlobal(globalPos);
            box->previewGhostDropAt(localPos, draggedPaths, QApplication::keyboardModifiers());
        } else {
            box->clearDropPreview();
        }
    }

    if (hoveredBox) {
        if (m_dragTip) {
            m_dragTip->hide();
        }
        return;
    }

    for (LinBox* box : boxes()) {
        if (box) {
            box->clearDropPreview();
        }
    }

    const QPoint localPos = mapFromGlobal(globalPos);
    if (!rect().contains(localPos)) {
        if (m_dragTip) {
            m_dragTip->hide();
        }
        return;
    }

    QMimeData mimeData;
    QList<QUrl> urls;
    for (const QString& path : draggedPaths) {
        if (!path.isEmpty()) {
            urls.append(QUrl::fromLocalFile(path));
        }
    }
    mimeData.setUrls(urls);

    QDragMoveEvent previewEvent(localPos, Qt::CopyAction | Qt::MoveAction | Qt::LinkAction, &mimeData, Qt::LeftButton, QApplication::keyboardModifiers());
    dragMoveEvent(&previewEvent);
}

bool ListView::finalizeMovedModelSourcesAfterFileOp(const QStringList& sourcePaths)
{
    if (sourcePaths.isEmpty()) {
        return false;
    }

    QSet<LinBox*> touchedMappedBoxes;
    bool removedAny = false;
    for (const QString& sourcePath : sourcePaths) {
        const QString normalizedPath = normalizeDesktopItemPath(sourcePath);
        if (normalizedPath.isEmpty()) {
            continue;
        }

        if (findDesktopStateByPath(normalizedPath)) {
            removeDesktopStateByPath(normalizedPath);
            removedAny = true;
            continue;
        }

        LinBox* sourceBox = BoxManager::instance()->findBoxContainingItem(normalizedPath);
        if (!sourceBox) {
            continue;
        }

        if (sourceBox->isFolderMapped()) {
            touchedMappedBoxes.insert(sourceBox);
            continue;
        }

        if (sourceBox->removeItemState(normalizedPath, false)) {
            sourceBox->refreshLayout();
            sourceBox->update();
            removedAny = true;
        }
    }

    for (LinBox* mappedBox : touchedMappedBoxes) {
        if (!mappedBox) {
            continue;
        }
        reloadMappedFolderBox(mappedBox);
        mappedBox->refreshLayout();
        mappedBox->update();
        removedAny = true;
    }

    if (removedAny) {
        rebuildSpatialIndex();
        update();
        requestSaveBoxes();
    }
    return removedAny;
}

bool ListView::handleDroppedPathsOnItemTarget(const QStringList& sourcePaths,
                                              const DesktopRootItemModel& targetModel,
                                              const DropInteractionOptions& options)
{
    if (sourcePaths.isEmpty()) {
        return false;
    }

    const QString clsid = targetModel.itemData.getCLSID();
    if (clsid == APP_MY_COMPUTER_CLS || clsid == APP_NETWORK_CLS || clsid == APP_CONTROL_PANEL_CLS) {
        return false;
    }

    const bool targetConsumesSources =
        !options.copyMode &&
        !options.preferShortcut &&
        (clsid == APP_RECYCLE_BIN_CLS ||
         targetModel.objectType == ObjectType::Folder_ObjectType ||
         targetModel.objectType == ObjectType::SpecialFolder ||
         targetModel.objectType == ObjectType::DiskLetter);

    const QStringList orderedPaths = options.preHideDesktopSources
        ? orderedDraggedPaths(sourcePaths)
        : QStringList();

    if (clsid == APP_RECYCLE_BIN_CLS) {
        if (options.preHideDesktopSources && !orderedPaths.isEmpty()) {
            setItemPathsTransientHidden(orderedPaths, true);
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }

        const bool ok = handleDroppedPathsOnModel(sourcePaths, targetModel, options.copyMode, options.preferShortcut);
        if (!ok) {
            if (options.preHideDesktopSources && !orderedPaths.isEmpty()) {
                setItemPathsTransientHidden(orderedPaths, false);
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            }
            return false;
        }

        if (options.preHideDesktopSources && !orderedPaths.isEmpty()) {
            trackPendingExternalRemovalPaths(orderedPaths);
            removeItemPathsBatch(orderedPaths);
            requestSaveBoxes();
        } else if (options.finalizeMovedSourcesAfterSuccess && targetConsumesSources) {
            finalizeMovedModelSourcesAfterFileOp(sourcePaths);
        }
        return true;
    }

    if (targetModel.objectType == ObjectType::Folder_ObjectType ||
        targetModel.objectType == ObjectType::SpecialFolder ||
        targetModel.objectType == ObjectType::DiskLetter) {
        if (targetConsumesSources && options.preHideDesktopSources && !orderedPaths.isEmpty()) {
            setItemPathsTransientHidden(orderedPaths, true);
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }

        const bool ok = handleDroppedPathsOnModel(sourcePaths, targetModel, options.copyMode, options.preferShortcut);
        if (!ok) {
            if (targetConsumesSources && options.preHideDesktopSources && !orderedPaths.isEmpty()) {
                setItemPathsTransientHidden(orderedPaths, false);
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            }
            return false;
        }

        if (targetConsumesSources && options.preHideDesktopSources && !orderedPaths.isEmpty()) {
            trackPendingExternalRemovalPaths(orderedPaths);
            removeItemPathsBatch(orderedPaths);
            requestSaveBoxes();
        } else if (options.finalizeMovedSourcesAfterSuccess && targetConsumesSources) {
            finalizeMovedModelSourcesAfterFileOp(sourcePaths);
        }
        return true;
    }

    return handleDroppedPathsOnModel(sourcePaths, targetModel, options.copyMode, options.preferShortcut);
}

bool ListView::handleDroppedPathsOnDesktopTarget(const QStringList& sourcePaths,
                                                 const DesktopRootItemModel& targetModel,
                                                 bool copyMode,
                                                 bool preferShortcut,
                                                 bool preHideDesktopSources)
{
    DropInteractionOptions options;
    options.copyMode = copyMode;
    options.preferShortcut = preferShortcut;
    options.preHideDesktopSources = preHideDesktopSources;
    return handleDroppedPathsOnItemTarget(sourcePaths, targetModel, options);
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
    return m_viewModel->createShortcutsInFolder(sourcePaths, targetFolder);
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
