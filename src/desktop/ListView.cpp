#include "desktop/ListView_Internal.h"

// ============================================================================
// ListView Core Lifecycle, Geometry & Selection State Implementation
// ============================================================================

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




