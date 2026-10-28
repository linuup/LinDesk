#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDataStream>
#include <QFile>
#include <QSet>
#include <QCoreApplication>
#include <QSettings>
#include "desktop/ListView.h"
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
            htmlText = QString("<b style='color:%1'>闂?/b> <span>%2</span> <b style='color:%1'>闂傚倷鐒﹁ぐ鍐嚐椤栫偛鍨傞柛妤冨剱閸ゅ牊绻涘顔荤凹闁瑰磭濞€閺?/b>")
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
                // 闂備礁鎼粔鐑斤綖婢跺﹦鏆ゅ〒姘ｅ亾闁哄被鍔戦、娆撴倷椤掑偊绱┑鐑囩到濞村倿宕抽敐澶娢ч柟娈垮枤缁犳棃鏌ㄩ弴鐐测偓鐢稿磻閹炬潙绶為悗锝庝憾娴犲瓨绻濋姀锝嗙【缂佸鎸冲畷鍝勎旈崨顓狀吅闂佸搫绋侀崐锝夊煛閸涱厾顦?
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
        // --- 濠电姰鍨奸崺鏍偋閻樿纾块柟缁㈠枛缁狀垶鏌″畵顔煎暙濮规煡姊绘笟鍥т簼婵″弶甯炲▎?---
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

void ListView::onMenuShowDesktopIcons() {
    qDebug() << "ListView: Show Desktop Icons triggered";
    if(m_iconShowRule.showDesktopIcons) {
        m_iconShowRule.showDesktopIcons = false;
        this->rightMenu->setShowDesktopIconsChecked(false);
    } else {
        m_iconShowRule.showDesktopIcons = true;
        this->rightMenu->setShowDesktopIconsChecked(true);
    }
    syncBoxVisibilityWithDesktopIcons();
    updateAllItemRenderData();
    rebuildSpatialIndex();
    update();
    if (m_snakeGameController) {
        m_snakeGameController->onDesktopIconsVisibilityChanged();
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
            box->hide();
        }
    }

    QTimer::singleShot(80, this, [this, flashBoxes]() {
        m_hideDesktopItemsForRefresh = false;
        for (const auto& box : flashBoxes) {
            if (box) {
                box->show();
            }
        }
        syncBoxVisibilityWithDesktopIcons();
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

            qDebug() << "Comprehensive State Sync (Mirroring Initialize)...";
            
            if (monitor) {
                monitor->refreshIconsState();
                sysIconRegisterInfo = monitor->getCurrentIcons();
            }

            DesktopItemInitContext context = currentDesktopItemInitContext();
            for (auto& state : m_itemStates) {
                if (state.model.filePath.isEmpty()) {
                    continue;
                }

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
            }

            updateAllItemRenderData();
            if (m_iconShowRule.autoArrange) {
                applyAutoArrangeAfterDesktopMutation(false, false);
            } else {
                rebuildSpatialIndex();
                update();
            }

            // Refresh icons for items in boxes and keep box visibility consistent with desktop icon visibility.
            for (const auto& box : allBoxes) {
                if (box) {
                    box->refreshLayout();
                }
            }
            syncBoxVisibilityWithDesktopIcons();

            for (const auto& box : allBoxes) {
                if (box) {
                    box->setUpdatesEnabled(true);
                    box->update();
                }
            }
            this->setUpdatesEnabled(true);
            syncBoxVisibilityWithDesktopIcons();
            this->update();
            QTimer::singleShot(0, this, [this]() {
                syncBoxVisibilityWithDesktopIcons();
                syncLinWidgetLayer();
            });
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
    // 闂佽崵鍠愰悷銉р偓姘煎灣閳ь剙鐏氶悧鐘茬暦閻樿鍐€妞ゆ帒鍊婚崬鎾倵閸偅绶查悗姘憸濡叉劕鈻庨幋鐘碉紲闂佺粯鏌ㄦ晶搴敂闁秵鐓熸い顐墮婵¤偐鈧鍠曠划娆忣嚕椤掆偓椤劑宕熼浣恒偖闂備焦瀵х粙鎴︽嚐椤栫偟宓侀柟鎹愵嚙缁犲啿顭跨捄渚剱婵湱鍋ら幃妯跨疀閺囩偘澹曢梺鍝勵儌閺呯姴顕ｉ崼鏇熷亼闁告侗鍨抽ˇ?
    const QString newPath = normalizeDesktopPath(eventInfo.newPath);
    const QString oldPath = normalizeDesktopPath(eventInfo.oldPath);

    qDebug() << "[onFileSystemChanged]"
             << "type:" << (int)eventInfo.eventType
             << "isDir:" << eventInfo.isDirectory
             << "old:" << oldPath
             << "new:" << newPath;

    // 闂傚倷鐒﹁ぐ鍐矓閻㈢钃熷┑鐘插閺嗘粓鏌涢幇鈺佸婵犫偓閻楀牜娈介柣鎰煐绾箖鏌涢埡鍌滄创鐎?
    auto relayout = [this]() {
        if (m_iconShowRule.autoArrange) {
            applyAutoArrangeLayout(false, false);
        } else {
            loadIconLayer();
            update();
            repaint();
        }
    };

    // 闂備礁婀遍…鍫ニ囬婊呯閻庯綆鍠栫粈鍫⑩偓骞垮劚濡绮幘璇茬闁哄鍨圭粻鐑樸亜椤愩埄妯€鐎规洘鑹鹃埢搴ㄥ箻閾忣偒浠ч梻?
    auto resort = [this]() {
        this->performSort(m_iconShowRule.sortType);
    };

    auto removeTrackedPath = [this](const QString& path) -> bool {
        if (path.isEmpty()) {
            return false;
        }

        if (findDesktopStateByPath(path)) {
            removeDesktopStateByPath(path);
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
        if (newPath.isEmpty()) break;
        if (findAnyItemStateByPath(newPath)) break;
        
        qDebug() << "[onFileSystemChanged] Creating item:" << newPath;
        QPointer<LinBox> pendingTargetBox = m_pendingCreatedItemBoxes.value(newPath);
        if (pendingTargetBox.isNull()) {
            pendingTargetBox = m_pendingShellNewTargetBox;
        }
        addDesktopModelItemFromPath(eventInfo.newPath);

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
                    update();
                }
            }
        }
        else {
            handlePendingShellCreatedItem(eventInfo.newPath);
        }
        
        // 濠电儑绲藉ú锔炬崲閸岀偞鍋ら柕濞炬櫅缁犳澘霉閿濆妫戦柣锝勭矙閺屾盯寮介妸褍顫ч梺鍝勵儏閸燁垳绮嬮幒鏃€濯奸柛锔诲幘椤︻噣鏌ｉ悩杈╁妽婵犮垺顭囩槐鐐差吋婢跺﹥顥濋梺缁橆殔閻楀﹪鎮楅娴庡綊鎮╅搹顐ｇ€紓鍌氱Т閹虫劗妲?
        requestSaveBoxes();
        break;
    }
    case FileSystemEvent::FileDeleted:
    case FileSystemEvent::DirectoryDeleted:
    {
        const QString& deletePath = !oldPath.isEmpty() ? oldPath : newPath;
        if (deletePath.isEmpty()) break;
        if (consumePendingExternalRemoval(deletePath)) break;
        
            // 濠电儑绲藉ú锔炬崲閸岀偞鍋ら柕濞炬櫅缁犳澘霉閿濆妫戦柣锝勭矙閺屾盯寮介妸褍顫ч梺鍝勵儏閸燁垳绮嬮幒鏃€濯奸柛锔诲幘椤︻喚绱撴担鍓叉Ц妞ゎ厼鍢查埢宥嗘償閳藉棗鏅犻梺鍦帛鐢帡鎮橀埡鍛拻闁告洍鏅涢埀顒佺墵瀵悂宕橀埡鍐炬祫闂佽鍨堕崑濠囧船閸撲焦鍠?
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
                if (eventInfo.eventType == FileSystemEvent::DirectoryRenamed) {
                    invalidateFolderItemIconCache(oldPath);
                    invalidateFolderItemIconCache(newPath);
                }
                refreshBoxModelItemPath(sourceBox, oldPath, eventInfo.newPath);
            } else {
                sourceBox->removeItemState(oldPath, true);
            }
        } else if (!findAnyItemStateByPath(newPath)) {
            addDesktopModelItemFromPath(eventInfo.newPath);
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
                    update();
                }
            }
        }
        else {
            handlePendingShellCreatedItem(eventInfo.newPath);
        }
        
        // 濠电儑绲藉ú锔炬崲閸岀偞鍋ら柕濞炬櫅缁犳澘霉閿濆妫戦柣锝勭矙閺屾盯寮介妸褍顫ч梺鍝勵儏閸燁垳绮嬮幒鏃€濯奸柛锔诲幘椤︻噣姊洪崫鍕悙闁宦板姂瀵粯绻濋崶銊у帓閻庡箍鍎遍幊蹇涘吹閹烘鐓曟慨姗嗗墰鏍￠梺鍝勵儏閸燁垳绮嬮幒妤€绠氶梺顓ㄧ畱閺佸綊鎮?
        requestSaveBoxes();
        break;
    }
    case FileSystemEvent::FileUpdated:
    case FileSystemEvent::FileAttributesChanged:
    case FileSystemEvent::DirectoryUpdated:
    {
        const QString& updatePath = !newPath.isEmpty() ? newPath : oldPath;
        if (updatePath.isEmpty()) break;

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
        dragPixmap = QPixmap(ghostBounds.size());
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

    for (GhostInfo& ghost : m_ghosts) {
        if (ghost.widget) {
            ghost.widget->hide();
            ghost.widget->deleteLater();
        }
    }
    m_ghosts.clear();

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

            for (auto& ghost : m_ghosts) {
                if (ghost.widget) {
                    ghost.widget->hide();
                    ghost.widget->deleteLater();
                }
            }
            m_ghosts.clear();
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
                for (auto& ghost : m_ghosts) {
                    if (ghost.widget) {
                        ghost.widget->hide();
                        ghost.widget->deleteLater();
                    }
                }
                m_ghosts.clear();
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
			if (ghost.widget && draggedState) {
				if (!hasCollision) {
					QPoint finalPos = this->mapFromGlobal(ghost.widget->pos());
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
				ghost.widget->hide();
				ghost.widget->deleteLater();
			}
		}

		m_ghosts.clear();
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
		
		// 濠电儑绲藉ú锔炬崲閸岀偞鍋ら柕濞炬櫅閻愬﹪鏌ｉ幇闈涘闁搞劎鍘ч湁闁挎繂鎳愯倴闂佹眹鍊曞Λ婵嗙暦濮橆厼顕遍柡澶嬪灥鐠у绱撻崒姘灓闁哥姵顨婂鎼佸礃閳哄喚娴?
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

bool ListView::createNewTextDocumentInHost()
{
    if (RenameManager::instance()->isRenaming()) {
        return false;
    }

    const QString targetDirectory = normalizeDesktopPath(m_desktopDir);
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
