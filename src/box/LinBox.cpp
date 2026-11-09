#include "box/LinBox_Internal.h"

// ============================================================================
// LinBox Core Lifecycle, Layout & Window State Implementation
// ============================================================================

LinBox::LinBox(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::Widget);
    setFocusPolicy(Qt::StrongFocus);
    setStyleSheet("LinBox { background: transparent; }");
    setMouseTracking(true);
    
    updateDpiMetrics();
    setMinimumSize(m_minBoxWidth, m_minBoxHeight);
    resize(scaledPx(this, kBaseBoxDefaultWidth), scaledPx(this, kBaseBoxDefaultHeight));
    setAcceptDrops(true);

    QSettings boxSettings(AppPaths::configFilePath(), QSettings::IniFormat);
    m_autoPeekEnabled = boxSettings.value(QStringLiteral("Box/AutoPeekOnHover"), true).toBool();
    m_autoPeekInstant = boxSettings.value(QStringLiteral("Box/AutoPeekInstant"), false).toBool();

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(m_borderWidth, m_borderWidth, m_borderWidth, m_borderWidth);
    m_mainLayout->setSpacing(0);

    this->m_title = tr("New Box");
    m_titleBar = new QWidget(this);
    m_titleBar->setFixedHeight(m_titleHeight);
    m_titleBar->setObjectName("titleBar");
    m_titleBar->setAttribute(Qt::WA_StyledBackground); 
    m_titleBar->setMouseTracking(true);
    
    QHBoxLayout* titleLayout = new QHBoxLayout(m_titleBar);
    titleLayout->setContentsMargins(scaledPx(this, kBaseTitleLayoutLeft), 0, scaledPx(this, kBaseTitleLayoutRight), 0);

    m_titleEdit = new QLineEdit(m_titleBar);
    m_titleEdit->setMouseTracking(true);
    m_titleEdit->setVisible(false);
    m_titleEdit->setText(m_title);
    m_titleEdit->setAlignment(Qt::AlignCenter);
    m_titleEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_titleEdit->setFixedHeight(scaledPx(this, 22));
    m_titleEdit->setStyleSheet(QString("background: white; color: black; border-radius: %1px; padding: %2px;")
        .arg(scaledPx(this, 4))
        .arg(scaledPx(this, 2)));

    m_viewSwitchBtn = new QPushButton(m_titleBar);
    m_viewSwitchBtn->setMouseTracking(true);
    m_viewSwitchBtn->setFixedSize(scaledPx(this, 24), scaledPx(this, 24));
    m_viewSwitchBtn->setIconSize(QSize(scaledPx(this, 16), scaledPx(this, 16)));
    m_viewSwitchBtn->setText(QString());
    m_viewSwitchBtn->setIcon(buildWhiteSvgIcon(":/Assets/list.svg", m_viewSwitchBtn->iconSize()));
    
    QFont iconFont("Segoe Fluent Icons");
    bool fontLoaded = QFontInfo(iconFont).exactMatch();
    if (!fontLoaded) {
        iconFont.setFamily("Segoe MDL2 Assets");
        fontLoaded = QFontInfo(iconFont).exactMatch();
    }
    if (!fontLoaded) {
        iconFont.setFamily("Segoe UI Symbol");
        fontLoaded = QFontInfo(iconFont).exactMatch();
    }
    if (!fontLoaded) {
        iconFont.setFamily("Wingdings");
        fontLoaded = QFontInfo(iconFont).exactMatch();
    }
    iconFont.setPixelSize(scaledPx(this, 14));
    m_viewSwitchBtn->setStyleSheet("QPushButton { background: transparent; color: white; border: none; border-radius: 4px; } "
                                   "QPushButton:hover { background: rgba(255, 255, 255, 40); } "
                                   "QPushButton:pressed { background: rgba(255, 255, 255, 20); }");

    m_collapseBtn = new QPushButton(m_titleBar);
    m_collapseBtn->setMouseTracking(true);
    m_collapseBtn->setFixedSize(scaledPx(this, 24), scaledPx(this, 24));
    m_collapseBtn->setFont(iconFont);
    // Use text icons as fallback if font doesn't support Fluent Icons
    QString collapseIcon = fontLoaded ? u8"\uE70E" : "v";
    m_collapseBtn->setText(collapseIcon);
    m_collapseBtn->setStyleSheet("QPushButton { background: transparent; color: white; border: none; border-radius: 4px; } "
                                 "QPushButton:hover { background: rgba(255, 255, 255, 40); } ");

    titleLayout->insertWidget(0, m_collapseBtn);
    titleLayout->addStretch();
    titleLayout->addWidget(m_titleEdit, 1);
    titleLayout->addStretch();
    titleLayout->addWidget(m_viewSwitchBtn);

    // Content Area inside Scroll Area
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName("scrollArea");
    m_scrollArea->setMouseTracking(true);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setStyleSheet(boxScrollAreaStyle(this));

    m_contentArea = new QWidget(m_scrollArea);
    m_contentArea->setObjectName("contentArea");
    m_contentArea->setAttribute(Qt::WA_StyledBackground);
    m_contentArea->setMouseTracking(true);
    m_scrollArea->setWidget(m_contentArea);
    if (m_scrollArea->viewport()) {
        m_scrollArea->viewport()->setMouseTracking(true);
    }

    m_mainLayout->addWidget(m_titleBar);
    m_mainLayout->addWidget(m_scrollArea);

    m_iconFadeTimer = new QTimer(this);
    m_iconFadeTimer->setInterval(16);
    connect(m_iconFadeTimer, &QTimer::timeout, this, &LinBox::onIconFadeTimerTimeout);
    connect(&IconCacheManager::instance(), &IconCacheManager::iconLoaded, this, &LinBox::onIconLoaded);

    m_blurRefreshTimer = new QTimer(this);
    m_blurRefreshTimer->setSingleShot(true);
    m_blurRefreshTimer->setInterval(16);
    connect(m_blurRefreshTimer, &QTimer::timeout, this, &LinBox::updateBlurCache);
    m_mappedFolderStateTimer = new QTimer(this);
    m_mappedFolderStateTimer->setInterval(1200);
    connect(m_mappedFolderStateTimer, &QTimer::timeout, this, &LinBox::checkMappedFolderStateAndRefresh);
    m_mappedFolderStateTimer->start();

    updateDpiMetrics();

    refreshStyles();
    loadSelectionColors();
    loadVisualEffectSettings();

    // Signals
    connect(m_viewSwitchBtn, &QPushButton::clicked, this, [this]() {
        if (m_showType == BoxShowType::Icon_ShowType) {
            setShowType(BoxShowType::List_ShowType);
        } else {
            setShowType(BoxShowType::Icon_ShowType);
        }
    });

    connect(m_collapseBtn, &QPushButton::clicked, this, [this]() {
        toggleCollapse();
    });

    connect(m_titleEdit, &QLineEdit::returnPressed, this, [this]() {
        commitTitleEditing();
    });

    connect(m_titleEdit, &QLineEdit::editingFinished, this, [this]() {
        if (m_titleEdit->isVisible()) {
            commitTitleEditing();
        }
    });

    m_titleBar->installEventFilter(this);
    m_titleEdit->installEventFilter(this);
    m_viewSwitchBtn->installEventFilter(this);
    m_collapseBtn->installEventFilter(this);
    m_scrollArea->installEventFilter(this);
    if (m_scrollArea->viewport()) {
        m_scrollArea->viewport()->installEventFilter(this);
    }
    m_contentArea->installEventFilter(this);
    raise(); 
}

void LinBox::updateDpiMetrics()
{
    m_dpiScale = DpiScaleHelper::uiScaleForWidget(this);
    m_borderWidth = scaledPx(this, kBaseBoxBorderWidth);
    m_titleHeight = scaledPx(this, kBaseBoxTitleHeight);
    m_minBoxWidth = scaledPx(this, kBaseBoxMinWidth);
    m_minBoxHeight = scaledPx(this, kBaseBoxMinHeight);

    if (m_mainLayout) {
        m_mainLayout->setContentsMargins(m_borderWidth, m_borderWidth, m_borderWidth, m_borderWidth);
    }
    if (m_titleBar) {
        m_titleBar->setFixedHeight(m_titleHeight);
        if (m_titleBar->layout()) {
            m_titleBar->layout()->setContentsMargins(scaledPx(this, kBaseTitleLayoutLeft), 0, scaledPx(this, kBaseTitleLayoutRight), 0);
        }
    }
    if (m_titleEdit) {
        m_titleEdit->setFixedHeight(scaledPx(this, kBaseTitleEditHeight));
    }
    if (m_viewSwitchBtn) {
        m_viewSwitchBtn->setFixedSize(scaledPx(this, kBaseTitleButtonSize), scaledPx(this, kBaseTitleButtonSize));
        m_viewSwitchBtn->setIconSize(QSize(scaledPx(this, kBaseTitleIconSize), scaledPx(this, kBaseTitleIconSize)));
    }
    if (m_collapseBtn) {
        m_collapseBtn->setFixedSize(scaledPx(this, kBaseTitleButtonSize), scaledPx(this, kBaseTitleButtonSize));
    }
    if (m_scrollArea) {
        m_scrollArea->setStyleSheet(boxScrollAreaStyle(this));
    }
}

LinBox::~LinBox() {
    // Clean up drag tip
    if (m_dragTip) {
        delete m_dragTip;
        m_dragTip = nullptr;
    }
}

void LinBox::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    updateDpiMetrics();
    raise();
    refreshLayout();
    if (QWidget* parent = parentWidget()) {
        parent->update(geometry().adjusted(-16, -16, 16, 16));
    }
    QTimer::singleShot(0, this, [this]() {
        m_lastLayoutContentWidth = -1;
        refreshLayout();
        if (QWidget* parent = parentWidget()) {
            parent->update(geometry().adjusted(-16, -16, 16, 16));
        }
    });
    if (!m_isCapturingBackground) {
        scheduleBlurRefresh();
    }
}

void LinBox::setShowType(BoxShowType type) {
    if (m_showType == type) return;
    m_showType = type;
    if (m_showType == BoxShowType::Icon_ShowType) {
        m_viewSwitchBtn->setIcon(buildWhiteSvgIcon(":/Assets/list.svg", m_viewSwitchBtn->iconSize()));
    } else {
        m_viewSwitchBtn->setIcon(buildWhiteSvgIcon(":/Assets/grid.svg", m_viewSwitchBtn->iconSize()));
    }
    markAllItemRenderDataDirty();
    refreshLayout();
    QTimer::singleShot(0, this, [this]() {
        m_lastLayoutContentWidth = -1;
        refreshLayout();
    });
    emit geometryChanged(this);
}

void LinBox::addItemState(const DesktopRootItemState& state, bool refresh)
{
    insertItemState(state, m_itemStates.size(), refresh);
}

void LinBox::insertItemState(const DesktopRootItemState& state, int index, bool refresh)
{
    if (state.model.filePath.isEmpty()) {
        return;
    }

    DesktopRootItemState stateCopy = state;
    stateCopy.model.boxId = m_id;
    stateCopy.model.visible = true;
    stateCopy.model.layoutMode = (m_showType == BoxShowType::List_ShowType)
        ? DesktopLayoutMode::ListMode
        : DesktopLayoutMode::IconMode;
    stateCopy.model.position = QPoint();
    stateCopy.model.baseSize = QSize();
    stateCopy.model.effectiveSize = QSize();
    stateCopy.model.visibleTextHeight = 0;
    stateCopy.model.renderData = DesktopRootItemRenderData();
    stateCopy.model.isHovered = false;
    stateCopy.renderDataDirty = true;

    removeItemState(stateCopy.model.filePath, false);
    const int clampedIndex = qBound(0, index, m_itemStates.size());
    m_itemStates.insert(clampedIndex, stateCopy);
    m_lastLayoutItemCount = -1;
    if (refresh) {
        refreshLayout();
        emit geometryChanged(this);
    }
}

bool LinBox::removeItemState(const QString& filePath, bool refresh)
{
    const QString normalizedPath = normalizeBoxItemPath(filePath);
    for (int i = 0; i < m_itemStates.size(); ++i) {
        if (normalizeBoxItemPath(m_itemStates[i].model.filePath) != normalizedPath) {
            continue;
        }
        IconCacheManager::instance().invalidateItemCache(m_itemStates[i].model.iconCacheKey, normalizedPath);
        m_itemStates.removeAt(i);
        if (refresh) {
            refreshLayout();
            emit geometryChanged(this);
        } else {
            m_lastLayoutItemCount = -1;
        }
        return true;
    }
    return false;
}

bool LinBox::takeItemState(const QString& filePath, DesktopRootItemState& stateOut, bool refresh)
{
    const QString normalizedPath = normalizeBoxItemPath(filePath);
    for (int i = 0; i < m_itemStates.size(); ++i) {
        if (normalizeBoxItemPath(m_itemStates[i].model.filePath) != normalizedPath) {
            continue;
        }
        stateOut = m_itemStates.takeAt(i);
        if (refresh) {
            refreshLayout();
            emit geometryChanged(this);
        } else {
            m_lastLayoutItemCount = -1;
        }
        return true;
    }
    return false;
}

QVector<QString> LinBox::itemPaths() const
{
    QVector<QString> paths;
    paths.reserve(m_itemStates.size());
    for (const DesktopRootItemState& state : m_itemStates) {
        if (!state.model.filePath.isEmpty()) {
            paths.append(state.model.filePath);
        }
    }
    return paths;
}

bool LinBox::containsItemPath(const QString& filePath) const
{
    return findItemStateByPath(filePath) != nullptr;
}

QString LinBox::hoveredItemPath() const
{
    return m_hoveredItemPath;
}

void LinBox::markItemRenderDataDirty(DesktopRootItemState* state)
{
    if (!state) {
        return;
    }

    state->renderDataDirty = true;
}

void LinBox::markAllItemRenderDataDirty()
{
    for (DesktopRootItemState& state : m_itemStates) {
        state.renderDataDirty = true;
    }
}

bool LinBox::hasDirtyItemRenderData() const
{
    for (const DesktopRootItemState& state : m_itemStates) {
        if (state.renderDataDirty) {
            return true;
        }
    }
    return false;
}

quint64 LinBox::currentLayoutVisualSignature() const
{
    quint64 visualSignature = 1469598103934665603ULL;
    for (const DesktopRootItemState& state : m_itemStates) {
        const quint64 textHeight = static_cast<quint64>(qMax(0, state.model.visibleTextHeight));
        const quint64 expanded = state.model.isFullNameExpanded ? 0x9e3779b97f4a7c15ULL : 0ULL;
        const quint64 renaming = state.model.isRenaming ? 0xbf58476d1ce4e5b9ULL : 0ULL;
        const quint64 previewHash = static_cast<quint64>(qHash(state.model.expandedPreviewText));
        visualSignature ^= textHeight + expanded + renaming + previewHash + 0x9e3779b97f4a7c15ULL + (visualSignature << 6) + (visualSignature >> 2);
    }
    return visualSignature;
}

void LinBox::rebuildItemRenderData(DesktopRootItemState& state, const DesktopRenderParams& params)
{
    state.model.renderData = buildRenderDataFromModel(state.model, params);
    if (state.model.isRenaming) {
        const QString editorText = state.model.expandedPreviewText.isEmpty()
            ? state.model.displayName
            : state.model.expandedPreviewText;
        QRect editorRect = state.model.renderData.renameEditorRect;
        editorRect.setHeight(FilenameEditor::heightForText(
            editorText,
            state.model.renderData.labelFont,
            editorRect.width(),
            0));
        state.model.renderData.renameEditorRect = editorRect;
        state.model.renderData.selectionContentRect = editorRect;
        const qreal dpiScale = devicePixelRatioF();
        auto sp = [dpiScale](int value) {
            return qMax(1, qRound(value * dpiScale));
        };
        state.model.renderData.outerHighlightRect = editorRect.adjusted(-sp(3), -sp(3), sp(3), sp(3));
        state.model.renderData.innerHighlightRect = editorRect.adjusted(sp(1), sp(1), -sp(1), -sp(1));
        state.model.visibleTextHeight = QFontMetrics(state.model.renderData.labelFont).lineSpacing();
    } else {
        state.model.visibleTextHeight = qMax(1, state.model.renderData.selectionContentRect.height());
    }
    state.renderDataDirty = false;
}

void LinBox::ensureItemVisible(const QString& filePath) {
    if (!m_scrollArea || !m_contentArea) return;
    const DesktopRootItemState* state = findItemStateByPath(filePath);
    if (!state) return;
    QRect itemRect = state->model.renderData.selectionContentRect;
    if (itemRect.isNull() || !itemRect.isValid()) {
        itemRect = state->model.renderData.staticContentRect;
    }
    if (!itemRect.isNull() && itemRect.isValid()) {
        m_scrollArea->ensureVisible(itemRect.center().x(), itemRect.center().y(), itemRect.width() / 2 + 10, itemRect.height() / 2 + 10);
    }
}

void LinBox::refreshLayout() {
    if (m_isRefreshingLayout) return;
    if (m_isMappedFolderLoading) return;
    const int itemW = scaledPx(this, kBaseItemWidth);
    const int itemH = scaledPx(this, kBaseItemHeight);
    const int colSpacing = scaledPx(this, kBaseItemGap);
    const int rowSpacing = scaledPx(this, kBaseItemGap);
    const int marginLeft = scaledPx(this, kBaseItemMarginLeft);
    const int marginTop = scaledPx(this, kBaseItemMarginTop);
    if (m_mainLayout) {
        m_mainLayout->activate();
    }
    if (m_scrollArea) {
        const bool renameActive = hasActiveItemRename();
        m_scrollArea->setVerticalScrollBarPolicy(renameActive ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
        if (QScrollBar* bar = m_scrollArea->verticalScrollBar()) {
            bar->setEnabled(!renameActive);
        }
        m_scrollArea->updateGeometry();
    }
    const int viewportW = (m_scrollArea && m_scrollArea->viewport()) ? m_scrollArea->viewport()->width() : 0;
    const int fallbackContentW = qMax(1, width() - (m_borderWidth * 2));
    const int contentW = qMax(1, viewportW > 0 ? viewportW : fallbackContentW);
    const quint64 visualSignature = currentLayoutVisualSignature();
    const bool layoutUnchanged =
        m_lastLayoutContentWidth == contentW &&
        m_lastLayoutItemCount == m_itemStates.size() &&
        m_lastLayoutShowType == m_showType &&
        m_lastLayoutVisualSignature == visualSignature;
    const bool hasDirtyRenderData = hasDirtyItemRenderData();

    if (m_itemStates.isEmpty()) {
        if (m_contentArea) {
            m_contentArea->setMinimumWidth(contentW);
            m_contentArea->setMinimumHeight(1);
            m_contentArea->resize(contentW, 1);
            m_contentArea->update();
        }
        m_lastLayoutContentWidth = contentW;
        m_lastLayoutItemCount = 0;
        m_lastLayoutShowType = m_showType;
        m_lastLayoutVisualSignature = visualSignature;
        return;
    }

    if (layoutUnchanged && !hasDirtyRenderData) {
        return;
    }

    m_isRefreshingLayout = true;
    bool needsFullLayout = !layoutUnchanged;

    if (m_showType == BoxShowType::Icon_ShowType) {
        DesktopRenderParams params = buildBoxRenderParams(this, m_showType, itemW, itemH);
        if (!needsFullLayout && hasDirtyRenderData) {
            for (DesktopRootItemState& state : m_itemStates) {
                if (!state.renderDataDirty) {
                    continue;
                }

                const int oldHeight = state.model.renderData.itemRect.height();
                const int oldVisibleTextHeight = state.model.visibleTextHeight;
                state.model.layoutMode = DesktopLayoutMode::IconMode;
                if (!state.model.effectiveSize.isValid()) {
                    state.model.effectiveSize = QSize(itemW, itemH);
                }
                rebuildItemRenderData(state, params);
                if (state.model.renderData.itemRect.height() != oldHeight ||
                    state.model.visibleTextHeight != oldVisibleTextHeight) {
                    needsFullLayout = true;
                    break;
                }
            }
        }

        const int cols = qMax(1, (contentW - 2 * marginLeft + colSpacing) / (itemW + colSpacing));
        const int rows = (m_itemStates.size() + cols - 1) / cols;
        QVector<int> rowHeights(rows, itemH);

        if (needsFullLayout) {
            for (int i = 0; i < m_itemStates.size(); ++i) {
                const int row = i / cols;
                DesktopRootItemState& state = m_itemStates[i];
                state.model.layoutMode = DesktopLayoutMode::IconMode;
                state.model.position = QPoint(0, 0);
                state.model.effectiveSize = QSize(itemW, itemH);
                rebuildItemRenderData(state, params);
                rowHeights[row] = qMax(rowHeights[row], state.model.renderData.itemRect.height());
            }

            QVector<int> rowTops(rows, marginTop);
            for (int row = 1; row < rows; ++row) {
                rowTops[row] = rowTops[row - 1] + rowHeights[row - 1] + rowSpacing;
            }

            for (int i = 0; i < m_itemStates.size(); ++i) {
                const int row = i / cols;
                const int col = i % cols;
                const int x = marginLeft + col * (itemW + colSpacing);
                const int y = rowTops[row];
                DesktopRootItemState& state = m_itemStates[i];
                const QPoint targetPos(x, y);
                state.model.position = targetPos;
                state.model.effectiveSize = QSize(itemW, qMax(itemH, rowHeights[row]));
                rebuildItemRenderData(state, params);
            }
        }
    } else {
        const int listItemH = scaledPx(this, kBaseItemListHeight);
        const int maxListItemW = qMax(1, contentW - 2 * marginLeft);
        DesktopRenderParams baseParams = buildBoxRenderParams(this, m_showType, maxListItemW, listItemH);
        const QFontMetrics listMetrics(baseParams.labelFont);
        const int leftPadding = scaledPx(this, kBaseItemListPadding);
        const int rightPadding = scaledPx(this, kBaseItemListRightPadding);
        const int iconSide = qMax(1, listItemH - scaledPx(this, 6));
        const int gapAfterIcon = qMax(1, baseParams.iconTextGap + 1);
        const int minListItemW = leftPadding + iconSide + gapAfterIcon + rightPadding + scaledPx(this, 24);
        for (int i = 0; i < m_itemStates.size(); ++i) {
            DesktopRootItemState& state = m_itemStates[i];
            if (!needsFullLayout && !state.renderDataDirty) {
                continue;
            }

            const QString renderText = state.model.expandedPreviewText.isEmpty()
                ? state.model.displayName
                : state.model.expandedPreviewText;
            const int textWidth = qMax(scaledPx(this, 10), listMetrics.horizontalAdvance(renderText));
            const int listItemW = qBound(
                minListItemW,
                leftPadding + iconSide + gapAfterIcon + textWidth + rightPadding,
                maxListItemW);
            const int x = marginLeft;
            const int y = marginTop + i * (listItemH + scaledPx(this, 2));
            const QPoint targetPos(x, y);
            DesktopRenderParams params = baseParams;
            params.iconWidth = listItemW;
            state.model.layoutMode = DesktopLayoutMode::ListMode;
            state.model.position = targetPos;
            state.model.effectiveSize = QSize(listItemW, listItemH);
            rebuildItemRenderData(state, params);
        }
    }
    
    // Update content area height to enable scroll area
    int totalHeight = 0;
    if (m_showType == BoxShowType::Icon_ShowType) {
        const int cols = qMax(1, (contentW - 2 * marginLeft + colSpacing) / (itemW + colSpacing));
        int rowHeightSum = 0;
        for (int rowStart = 0; rowStart < m_itemStates.size(); rowStart += cols) {
            int rowMaxHeight = itemH;
            const int rowEnd = qMin(rowStart + cols, m_itemStates.size());
            for (int i = rowStart; i < rowEnd; ++i) {
                rowMaxHeight = qMax(rowMaxHeight, m_itemStates[i].model.renderData.itemRect.height());
            }
            rowHeightSum += rowMaxHeight;
        }
        const int rows = (m_itemStates.size() + cols - 1) / cols;
        totalHeight = marginTop + rowHeightSum + qMax(0, rows - 1) * rowSpacing + scaledPx(this, 10);
    } else {
        totalHeight = marginTop + m_itemStates.size() * (scaledPx(this, kBaseItemListHeight) + scaledPx(this, 2)) + scaledPx(this, 10);
    }
    if (m_contentArea) {
        m_contentArea->setMinimumWidth(contentW);
        m_contentArea->setMinimumHeight(totalHeight);
        m_contentArea->resize(contentW, totalHeight);
        m_contentArea->update();
    }

    m_lastLayoutContentWidth = contentW;
    m_lastLayoutItemCount = m_itemStates.size();
    m_lastLayoutShowType = m_showType;
    m_lastLayoutVisualSignature = currentLayoutVisualSignature();
    m_isRefreshingLayout = false;
    update();
}

QRect LinBox::globalRenameEditorRectForPath(const QString& filePath) const
{
    if (!m_contentArea) {
        return QRect();
    }

    const DesktopRootItemState* state = findItemStateByPath(filePath);
    if (!state) {
        return QRect();
    }

    const QPoint globalTopLeft = m_contentArea->mapToGlobal(state->model.renderData.renameEditorRect.topLeft());
    return QRect(globalTopLeft, state->model.renderData.renameEditorRect.size());
}

void LinBox::moveEvent(QMoveEvent* event) {
    if (QWidget* parent = parentWidget()) {
        if (!event->oldPos().isNull() && event->oldPos() != event->pos()) {
            const QRect oldRect(event->oldPos(), size());
            const QRect newRect(pos(), size());
            const QRect dirty = oldRect.united(newRect).adjusted(-12, -12, 12, 12);
            parent->update(dirty);
            if (QWidget* host = parent->window(); host && host != parent) {
                host->update(QRect(parent->mapTo(host, dirty.topLeft()), dirty.size()));
            }

            // 追加：移动停止后强制同步刷新父窗口，消除半透明残影
            if (!m_moveRefreshTimer) {
                m_moveRefreshTimer = new QTimer(this);
                m_moveRefreshTimer->setSingleShot(true);
                m_moveRefreshTimer->setInterval(30);   // 30ms 无移动后再刷
                connect(m_moveRefreshTimer, &QTimer::timeout, this, [this]() {
                    if (QWidget* p = parentWidget()) {
                        const QRect d = geometry().adjusted(-16, -16, 16, 16);
                        p->update(d);
                        if (QWidget* host = p->window(); host && host != p) {
                            host->update(QRect(p->mapTo(host, d.topLeft()), d.size()));
                        }
                    }
                    update();
                });
            }
            m_moveRefreshTimer->start();
        }
    }

    QWidget::moveEvent(event);
    if (!m_isCapturingBackground && m_glassEffect && event->oldPos() != pos()) {
        scheduleBlurRefresh();
    }
}

void LinBox::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_collapseAnim && m_collapseAnim->state() == QAbstractAnimation::Running) {
        return;
    }
    if (m_isCollapsed) {
        return;
    }
    refreshLayout();
    if (!m_isCapturingBackground && m_glassEffect && event->oldSize() != event->size()) {
        scheduleBlurRefresh();
    }
}

void LinBox::setBoxTitle(const QString& title) {
    m_title = sanitizedTitleText(title);
    m_titleEdit->setText(m_title);
    m_titleBar->update();
    emit geometryChanged(this);
}


void LinBox::loadSelectionColors()
{
    QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
    const QColor baseColor(settings.value(QStringLiteral("Desktop/SelectionRectangleColor"), QStringLiteral("#64A0F0")).toString());
    const int opacity = qBound(20, settings.value(QStringLiteral("Desktop/SelectionRectangleOpacity"), 75).toInt(), 100);
    const int contrast = qBound(0, settings.value(QStringLiteral("Desktop/SelectionRectangleContrast"), 60).toInt(), 100);
    m_fluentSelectionEnabled = settings.value(QStringLiteral("Desktop/FluentSelectionMarquee"), true).toBool();
    m_fluentSelectionCornerRadius = qBound(0, settings.value(QStringLiteral("Desktop/SelectionCornerRadius"), 5).toInt(), 16);
    m_selectionBorderWidth = qBound(0.0, settings.value(QStringLiteral("Desktop/SelectionBorderWidth"), 1.0).toReal(), 3.0);
    QColor fillColor = baseColor.isValid() ? baseColor : QColor(100, 160, 240);
    QColor borderColor = fillColor.darker(110);
    fillColor.setAlpha(qBound(35, opacity * 140 / 100, 180));
    borderColor.setAlpha(qBound(95, contrast * 230 / 100, 230));
    m_selectionFillColor = fillColor;
    m_selectionBorderColor = borderColor;
}

void LinBox::refreshSelectionOverlayColor()
{
    loadSelectionColors();
    update();
}

void LinBox::loadVisualEffectSettings()
{
    QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
    m_layeredZDepth = settings.value(QStringLiteral("Box/LayeredZDepth"), true).toBool();
    m_activeDragPulse = settings.value(QStringLiteral("Box/ActiveDragPulse"), true).toBool();
}

void LinBox::setLayeredZDepth(bool enabled)
{
    if (m_layeredZDepth == enabled) return;
    m_layeredZDepth = enabled;
    update();
}

void LinBox::setActiveDragPulse(bool enabled)
{
    if (m_activeDragPulse == enabled) return;
    m_activeDragPulse = enabled;
    update();
}

QColor LinBox::effectiveBackgroundColor() const
{
    return m_bgColor.isValid() ? m_bgColor : QColor(60, 60, 60);
}

void LinBox::setAutoPeekEnabled(bool enabled)
{
    m_autoPeekEnabled = enabled;
    QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("Box/AutoPeekOnHover"), enabled);
    settings.sync();
}

void LinBox::setAutoPeekInstant(bool instant)
{
    m_autoPeekInstant = instant;
    QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("Box/AutoPeekInstant"), instant);
    settings.sync();
}

void LinBox::handleBoxMouseEnter()
{
    if (m_isMoving || m_isResizing) return;
    if (m_collapseAnim && m_collapseAnim->state() == QAbstractAnimation::Running) return;
    if (m_manualCollapseHoverSuppressed) return;
    if (m_hoverCollapseTimer && m_hoverCollapseTimer->isActive()) {
        m_hoverCollapseTimer->stop();
    }
    if (m_isCollapsed && m_autoPeekEnabled) {
        if (!m_hoverExpandTimer) {
            m_hoverExpandTimer = new QTimer(this);
            m_hoverExpandTimer->setSingleShot(true);
            connect(m_hoverExpandTimer, &QTimer::timeout, this, [this]() {
                if (m_isCollapsed && m_autoPeekEnabled && !m_isMoving && !m_isResizing) {
                    const QPoint curGlobalPos = QCursor::pos();
                    const QRect globalRect(mapToGlobal(QPoint(0, 0)), size());
                    if (globalRect.contains(curGlobalPos)) {
                        setCollapsed(false, true, true);
                    }
                }
            });
        }
        if (!m_hoverExpandTimer->isActive()) {
            m_hoverExpandTimer->start(m_autoPeekInstant ? 0 : 150);
        }
    }
}

void LinBox::handleBoxMouseLeave()
{
    if (m_isMoving || m_isResizing) return;
    m_manualCollapseHoverSuppressed = false;
    if (m_hoverExpandTimer && m_hoverExpandTimer->isActive()) {
        m_hoverExpandTimer->stop();
    }
    if (m_isAutoPeeking && !m_isCollapsed) {
        if (!m_hoverCollapseTimer) {
            m_hoverCollapseTimer = new QTimer(this);
            m_hoverCollapseTimer->setSingleShot(true);
            connect(m_hoverCollapseTimer, &QTimer::timeout, this, [this]() {
                if (m_isAutoPeeking && !m_isCollapsed && !m_isMoving && !m_isResizing) {
                    const QPoint curGlobalPos = QCursor::pos();
                    const QRect globalRect(mapToGlobal(QPoint(0, 0)), size());
                    if (!globalRect.contains(curGlobalPos)) {
                        setCollapsed(true, true, true);
                    }
                }
            });
        }
        const QPoint curGlobalPos = QCursor::pos();
        const QRect globalRect(mapToGlobal(QPoint(0, 0)), size());
        if (!globalRect.contains(curGlobalPos)) {
            if (!m_hoverCollapseTimer->isActive()) {
                m_hoverCollapseTimer->start(m_autoPeekInstant ? 0 : 300);
            }
        }
    }
}

void LinBox::beginTitleEditing() {
    if (isFolderMapped() && isDriveRootPath(m_mappedFolderPath)) {
        return;
    }
    m_titleEdit->setMaxLength(m_maxCustomTitleLength);
    m_titleEdit->setText(isFolderMapped() ? mappedFolderDisplayText() : m_title);
    m_titleEdit->show();
    m_titleEdit->setFocus();
    m_titleEdit->selectAll();
    m_titleBar->update();
}

void LinBox::commitTitleEditing() {
    const QString newTitle = sanitizedTitleText(m_titleEdit->text());
    if (isFolderMapped()) {
        m_title = newTitle;
        m_titleEdit->hide();
        m_titleBar->update();
        emit geometryChanged(this);
        return;
    }
    setBoxTitle(newTitle.isEmpty() ? m_title : newTitle);
    m_titleEdit->hide();
    m_titleBar->update();
}


DesktopRootItemState* LinBox::hitItemStateAt(const QPoint& boxPos)
{
    return const_cast<DesktopRootItemState*>(
        static_cast<const LinBox*>(this)->hitItemStateAt(boxPos));
}

const DesktopRootItemState* LinBox::hitItemStateAt(const QPoint& boxPos) const
{
    if (!m_contentArea) {
        return nullptr;
    }

    const QPoint contentPos = m_contentArea->mapFromGlobal(mapToGlobal(boxPos));
    for (int i = m_itemStates.size() - 1; i >= 0; --i) {
        const DesktopRootItemState& state = m_itemStates[i];
        const QRect& selRect = state.model.renderData.selectionContentRect;
        if (!selRect.isNull() && selRect.isValid() && selRect.contains(contentPos)) {
            return &state;
        }
        const QRect& staticRect = state.model.renderData.staticContentRect;
        if (!staticRect.isNull() && staticRect.isValid() && staticRect.contains(contentPos)) {
            return &state;
        }
        const QRect& itemRect = state.model.renderData.itemRect;
        if (!itemRect.isNull() && itemRect.isValid()) {
            const int insetX = qMax(4, itemRect.width() / 8);
            const int insetY = qMax(4, itemRect.height() / 8);
            const QRect coreRect = itemRect.adjusted(insetX, insetY, -insetX, -insetY);
            if (coreRect.contains(contentPos)) {
                return &state;
            }
        }
    }
    return nullptr;
}

const DesktopRootItemState* LinBox::dropTargetItemStateAt(const QPoint& boxPos) const
{
    if (!m_contentArea) {
        return nullptr;
    }

    const QPoint contentPos = m_contentArea->mapFromGlobal(mapToGlobal(boxPos));
    for (int i = m_itemStates.size() - 1; i >= 0; --i) {
        const DesktopRootItemState& state = m_itemStates[i];
        QRect targetRect = state.model.renderData.selectionContentRect;
        if (targetRect.isNull() || !targetRect.isValid()) {
            targetRect = state.model.renderData.staticContentRect;
        } else if (!state.model.renderData.staticContentRect.isNull() &&
                   state.model.renderData.staticContentRect.isValid()) {
            targetRect = targetRect.united(state.model.renderData.staticContentRect);
        }
        if (targetRect.isNull() || !targetRect.isValid()) {
            targetRect = state.model.renderData.itemRect;
        }
        if (targetRect.contains(contentPos)) {
            return &state;
        }
    }
    return nullptr;
}

bool LinBox::hasActiveItemRename() const
{
    for (const DesktopRootItemState& state : m_itemStates) {
        if (state.model.isRenaming) {
            return true;
        }
    }
    return false;
}

DesktopRootItemState* LinBox::findItemStateByPath(const QString& filePath)
{
    return const_cast<DesktopRootItemState*>(
        static_cast<const LinBox*>(this)->findItemStateByPath(filePath));
}

const DesktopRootItemState* LinBox::findItemStateByPath(const QString& filePath) const
{
    const QString normalized = normalizeBoxItemPath(filePath);
    for (const DesktopRootItemState& state : m_itemStates) {
        if (normalizeBoxItemPath(state.model.filePath) == normalized) {
            return &state;
        }
    }
    return nullptr;
}


void LinBox::toggleCollapse() {
    toggleCollapse(false);
}

void LinBox::toggleCollapse(bool isAutoPeek) {
    setCollapsed(!m_isCollapsed, true, isAutoPeek);
}

void LinBox::setCollapsed(bool collapsed, bool animated, bool isAutoPeek) {
    if (m_isCollapsed == collapsed) {
        return;
    }

    if (!isAutoPeek) {
        m_isAutoPeeking = false;
        if (collapsed) {
            m_manualCollapseHoverSuppressed = true;
        } else {
            m_manualCollapseHoverSuppressed = false;
        }
    } else {
        m_isAutoPeeking = !collapsed;
    }

    if (m_hoverCollapseTimer && m_hoverCollapseTimer->isActive()) {
        m_hoverCollapseTimer->stop();
    }
    if (m_hoverExpandTimer && m_hoverExpandTimer->isActive()) {
        m_hoverExpandTimer->stop();
    }

    QFont iconFont("Segoe Fluent Icons");
    bool fontLoaded = QFontInfo(iconFont).exactMatch();
    if (!fontLoaded) {
        iconFont.setFamily("Segoe MDL2 Assets");
        fontLoaded = QFontInfo(iconFont).exactMatch();
    }
    if (!fontLoaded) {
        iconFont.setFamily("Segoe UI Symbol");
        fontLoaded = QFontInfo(iconFont).exactMatch();
    }
    if (!fontLoaded) {
        iconFont.setFamily("Wingdings");
        fontLoaded = QFontInfo(iconFont).exactMatch();
    }
    iconFont.setPixelSize(scaledPx(this, 14));
    if (m_collapseBtn) {
        m_collapseBtn->setFont(iconFont);
    }

    const int collapsedHeight = m_titleHeight + 2 * m_borderWidth;

    if (!collapsed) {
        if (m_originalHeight < m_minBoxHeight) {
            m_originalHeight = qMax(m_minBoxHeight, scaledPx(this, 300));
        }
    } else {
        if (!m_isCollapsed && height() > collapsedHeight) {
            m_originalHeight = height();
        }
    }

    m_isCollapsed = collapsed;
    if (m_collapseBtn) {
        m_collapseBtn->setText(m_isCollapsed ? (fontLoaded ? u8"\uE70D" : "+") : (fontLoaded ? u8"\uE70E" : "v"));
    }

    const int targetHeight = m_isCollapsed ? collapsedHeight : m_originalHeight;

    if (animated) {
        if (!m_collapseAnim) {
            m_collapseAnim = new QPropertyAnimation(this, "geometry", this);
            m_collapseAnim->setEasingCurve(QEasingCurve::OutCubic);
            m_collapseAnim->setDuration(200);

            connect(m_collapseAnim, &QPropertyAnimation::valueChanged, this, [this]() {
                const int curCollapsedH = m_titleHeight + 2 * m_borderWidth;
                if (m_scrollArea) {
                    const int heightRange = qMax(1, m_originalHeight - curCollapsedH);
                    const qreal progress = qBound(0.0, static_cast<qreal>(height() - curCollapsedH) / heightRange, 1.0);
                    QGraphicsOpacityEffect* eff = qobject_cast<QGraphicsOpacityEffect*>(m_scrollArea->graphicsEffect());
                    if (!eff) {
                        eff = new QGraphicsOpacityEffect(m_scrollArea);
                        m_scrollArea->setGraphicsEffect(eff);
                    }
                    eff->setOpacity(progress);
                }
                update();
            });

            connect(m_collapseAnim, &QPropertyAnimation::finished, this, [this]() {
                const int curCollapsedH = m_titleHeight + 2 * m_borderWidth;
                if (m_isCollapsed) {
                    if (m_scrollArea) {
                        if (m_scrollArea->verticalScrollBar()) {
                            m_scrollArea->verticalScrollBar()->hide();
                        }
                        m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                        m_scrollArea->hide();
                    }
                    if (m_contentArea) {
                        m_contentArea->setFixedHeight(0);
                    }
                    setFixedHeight(curCollapsedH);
                    QRect g = geometry();
                    g.setHeight(curCollapsedH);
                    setGeometry(g);
                    resize(width(), curCollapsedH);
                    update();
                } else {
                    if (m_contentArea) {
                        m_contentArea->setMinimumHeight(0);
                        m_contentArea->setMaximumHeight(16777215);
                    }
                    if (m_scrollArea) {
                        m_scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
                        m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
                        m_scrollArea->show();
                        if (QGraphicsOpacityEffect* eff = qobject_cast<QGraphicsOpacityEffect*>(m_scrollArea->graphicsEffect())) {
                            eff->setOpacity(1.0);
                        }
                    }
                    setMinimumSize(m_minBoxWidth, m_minBoxHeight);
                    setMaximumHeight(16777215);
                    QRect g = geometry();
                    g.setHeight(m_originalHeight);
                    setGeometry(g);
                    resize(width(), m_originalHeight);
                    refreshLayout();
                    if (!m_isCapturingBackground && m_glassEffect) {
                        scheduleBlurRefresh();
                    }
                    update();
                }
                emit geometryChanged(this);
            });
        }

        if (m_collapseAnim->state() == QAbstractAnimation::Running) {
            m_collapseAnim->stop();
        }

        setMinimumHeight(0);
        setMaximumHeight(16777215);

        if (m_scrollArea) {
            m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            if (m_isCollapsed) {
                m_scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
                m_scrollArea->setMinimumHeight(0);
                m_scrollArea->setMinimumSize(0, 0);
                if (m_contentArea) {
                    m_contentArea->setMinimumHeight(0);
                    m_contentArea->setMinimumSize(0, 0);
                }
                if (m_mainLayout) {
                    m_mainLayout->setSizeConstraint(QLayout::SetNoConstraint);
                }
            } else {
                if (m_contentArea) {
                    m_contentArea->setMinimumHeight(0);
                    m_contentArea->setMaximumHeight(16777215);
                }
                m_scrollArea->show();
                if (QGraphicsOpacityEffect* eff = qobject_cast<QGraphicsOpacityEffect*>(m_scrollArea->graphicsEffect())) {
                    eff->setOpacity(0.0);
                }
            }
        }

        QRect startGeom = geometry();
        QRect endGeom = startGeom;
        endGeom.setHeight(targetHeight);

        // 边缘保护：如果在屏幕底边缘展开且超出底边，则向上展开
        QScreen* screen = QGuiApplication::screenAt(mapToGlobal(QPoint(width() / 2, height() / 2)));
        if (!screen) {
            screen = windowHandle() ? windowHandle()->screen() : QGuiApplication::primaryScreen();
        }
        const QRect screenArea = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
        if (!m_isCollapsed && endGeom.bottom() - m_borderWidth > screenArea.bottom()) {
            endGeom.moveBottom(screenArea.bottom() + m_borderWidth);
        }

        m_collapseAnim->setStartValue(startGeom);
        m_collapseAnim->setEndValue(endGeom);
        m_collapseAnim->start();
    } else {
        if (m_isCollapsed) {
            if (m_scrollArea) {
                if (m_scrollArea->verticalScrollBar()) {
                    m_scrollArea->verticalScrollBar()->hide();
                }
                m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                m_scrollArea->hide();
            }
            if (m_contentArea) {
                m_contentArea->setFixedHeight(0);
            }
            setFixedHeight(collapsedHeight);
            QRect g = geometry();
            g.setHeight(collapsedHeight);
            setGeometry(g);
            resize(width(), collapsedHeight);
            update();
        } else {
            if (m_contentArea) {
                m_contentArea->setMinimumHeight(0);
                m_contentArea->setMaximumHeight(16777215);
            }
            if (m_scrollArea) {
                m_scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
                m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
                m_scrollArea->show();
                if (QGraphicsOpacityEffect* eff = qobject_cast<QGraphicsOpacityEffect*>(m_scrollArea->graphicsEffect())) {
                    eff->setOpacity(1.0);
                }
            }
            setMinimumSize(m_minBoxWidth, m_minBoxHeight);
            setMaximumHeight(16777215);
            QRect g = geometry();
            g.setHeight(m_originalHeight);
            setGeometry(g);
            resize(width(), m_originalHeight);
            refreshLayout();
            update();
        }
        emit geometryChanged(this);
    }
}


void LinBox::showCustomContextMenu(const QPoint& globalPos, bool extendedVerbs) {
    if (ListView* lv = ListView::instance()) {
        lv->armPendingShellNewTargetBox(this);
        lv->setLastRightClickPos(lv->mapFromGlobal(globalPos));
    }

    ShellContextMenu scm;
    logBoxDpiTrace("showCustomContextMenu", this, globalPos);
    const QPoint nativeGlobalPos = DpiCoordinateHelper::qtGlobalPointToNativePoint(this, globalPos);
    POINT p = { nativeGlobalPos.x(), nativeGlobalPos.y() };
    const POINT* position = &p;
    HWND ownerHwnd = parentWidget() ? (HWND)parentWidget()->winId() : (HWND)this->winId();

    RightMenu::getInstance()->preparePasteActionsForMenu();
    const QString contextPath = isFolderMapped()
        ? normalizeMappedFolderPath(mappedFolderPath())
        : defaultDesktopPath();
    if (isFolderMapped() && !contextPath.isEmpty()) {
        scm.DisplayBoxBackgroundContextMenu(contextPath.toStdWString());
    } else {
        scm.DisplayBoxBackgroundContextMenu();
    }
    const bool filterEnabled = RightMenu::isShellMenuExclusionEnabled();
    QElapsedTimer filterTimer;
    filterTimer.start();
    if (filterEnabled) {
        const auto exclusionKeys = ShellContextMenu::ExclusionKeysForPath(ShellContextMenu::OperationType::boxBackground, nullptr);
        scm.setExclusions(ShellMenuExclusionStore::loadForKeys(exclusionKeys));
    }
    const qint64 filterLoadMs = filterTimer.elapsed();
    if (!contextPath.isEmpty()) {
        scm.setFilesystemDirectory(contextPath.toStdString());
    }

    std::vector<ShellContextMenu::CustomMenuItem> items;
    std::vector<ShellContextMenu::CustomMenuItem> boxItems;
    const QPointer<LinBox> boxGuard(this);

    // 1. Add Box-specific context actions
    // Rename Box
    ShellContextMenu::CustomMenuItem renameItem;
    QIcon renameIcon(":/Assets/rename.svg");
    renameItem.hBitmap = cachedMenuBitmap(QStringLiteral("box.rename"), renameIcon, this);
    renameItem.text = tr("Rename Box").toStdWString();
    renameItem.callback = [boxGuard]() {
        if (boxGuard) {
            boxGuard->beginTitleEditing();
        }
    };
    boxItems.push_back(renameItem);

    if (isFolderMapped()) {
        ShellContextMenu::CustomMenuItem remapItem;
        QIcon remapIcon(":/Assets/change_Mapped_Folder.svg");
        remapItem.hBitmap = cachedMenuBitmap(QStringLiteral("box.remap"), remapIcon, this);
        remapItem.text = tr("Change Mapped Folder").toStdWString();
        remapItem.callback = [boxGuard]() {
            if (boxGuard) {
                boxGuard->remapMappedFolderInteractively();
            }
        };
        boxItems.push_back(remapItem);
    }


    // Toggle View mode
    ShellContextMenu::CustomMenuItem viewItem;
    QIcon viewIcon;
    if(m_showType == BoxShowType::List_ShowType){
        viewIcon = QIcon(":/Assets/list.svg");
    }else{
        viewIcon = QIcon(":/Assets/grid.svg");
    }
    viewItem.hBitmap = cachedMenuBitmap(
        m_showType == BoxShowType::List_ShowType ? QStringLiteral("box.view.list") : QStringLiteral("box.view.grid"),
        viewIcon,
        this);
    viewItem.text = (m_showType == BoxShowType::Icon_ShowType ? tr("List View") : tr("Icon View")).toStdWString();
    viewItem.callback = [boxGuard]() {
        if (boxGuard) {
            boxGuard->setShowType(boxGuard->m_showType == BoxShowType::Icon_ShowType
                        ? BoxShowType::List_ShowType
                        : BoxShowType::Icon_ShowType);
        }
    };
    boxItems.push_back(viewItem);

    // Settings
    ShellContextMenu::CustomMenuItem settingsItem;
    QIcon settingsIcon(":/Assets/settings.svg");
    settingsItem.hBitmap = cachedMenuBitmap(QStringLiteral("box.settings"), settingsIcon, this);
    settingsItem.text = tr("Box Settings").toStdWString();
    settingsItem.callback = [boxGuard]() {
        if (boxGuard && ListView::instance()) {
            ListView::instance()->showSettings(boxGuard.data());
        }
    };
    boxItems.push_back(settingsItem);

    // Destroy Box
    ShellContextMenu::CustomMenuItem destroyItem;
    destroyItem.text = tr("Destroy Box").toStdWString();
    QIcon icon(":/Assets/destroy.svg");
    destroyItem.hBitmap = cachedMenuBitmap(QStringLiteral("box.destroy"), icon, this);
    destroyItem.callback = [boxGuard]() {
        if (boxGuard) {
            emit boxGuard->destroyRequested(boxGuard.data());
        }
    };
    boxItems.push_back(destroyItem);

    // 2. Get shared actions from RightMenu and convert them
    const auto& sharedItems = RightMenu::getInstance()->sharedBoxCustomMenuItems();
    if (!sharedItems.empty()) {
        items.insert(items.end(), sharedItems.begin(), sharedItems.end());
    }

    if (!items.empty() && !boxItems.empty()) {
        ShellContextMenu::CustomMenuItem sep;
        sep.isSeparator = true;
        items.push_back(sep);
    }

    for (auto& boxItem : boxItems) {
        items.push_back(std::move(boxItem));
    }

    // Set custom items and show the native menu
    filterTimer.restart();
    scm.setCustomMenuItems(filterEnabled
        ? RightMenu::filterCustomItemsForExclusionKey(items, L"box_background")
        : items);
    const qint64 customFilterMs = filterTimer.elapsed();
    qInfo() << "[LinBox] showCustomContextMenu prepared:"
            << "contextPath=" << contextPath
            << "filterEnabled=" << filterEnabled
            << "filterLoadMs=" << filterLoadMs
            << "customFilterMs=" << customFilterMs;
    scm.ShowMenu(ownerHwnd, position, nullptr,
        extendedVerbs ? ShellContextMenu::Flags::ExtendedVerbs : ShellContextMenu::Flags::Standard);
}

