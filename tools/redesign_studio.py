import re

def update_studio_cpp():
    path = 'src/ui/hub/WallpaperStudioWindow.cpp'
    with open(path, 'r', encoding='utf-8') as f:
        content = f.read()

    # 1. 替换 paintEvent
    old_paint = """void WallpaperStudioWindow::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(226, 232, 240, 255), 1));
    painter.setBrush(QColor(248, 250, 252, 255));
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 12, 12);
}"""

    new_paint = """void WallpaperStudioWindow::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    // 高级深空一体化背景（消灭突兀白边与割裂感）
    painter.setPen(QPen(QColor(255, 255, 255, 22), 1));
    painter.setBrush(QColor(11, 15, 25, 255)); // #0b0f19
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 12, 12);
}"""

    # 2. 替换 initUI
    old_init = """void WallpaperStudioWindow::initUI() {
    setWindowBtnMode(All);

    if (m_btnContainer) {
        m_btnContainer->setFixedHeight(36);
        m_btnContainer->setAttribute(Qt::WA_StyledBackground, true);
        m_btnContainer->setObjectName(QStringLiteral("hubTitleBar"));
        m_btnContainer->setStyleSheet(QStringLiteral(
            "QWidget#hubTitleBar {"
            "  background-color: #ffffff;"
            "  border-bottom: 1px solid #e2e8f0;"
            "}"
        ));
    }

    if (container()) {
        container()->setAttribute(Qt::WA_StyledBackground, true);
        container()->setObjectName(QStringLiteral("hubCentralWidget"));
        container()->setStyleSheet(QStringLiteral(
            "QWidget#hubCentralWidget {"
            "  background-color: #0f172a;"
            "}"
        ));
    }"""

    new_init = """void WallpaperStudioWindow::initUI() {
    setWindowBtnMode(All);

    // 全局注入 Authentic UI 现代深色控件与极简隐形滚动条，彻底消灭 Windows 粗白滚动条
    setStyleSheet(QStringLiteral(
        "WallpaperStudioWindow, QWidget#fluentBaseContainer {"
        "  background: transparent;"
        "  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', 'Microsoft YaHei', sans-serif;"
        "}"
        "/* 全局隐形无感细滚动条 */"
        "QScrollBar:vertical {"
        "  border: none; background: transparent; width: 6px; margin: 4px 2px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: rgba(255, 255, 255, 0.16); border-radius: 3px; min-height: 24px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "  background: rgba(255, 255, 255, 0.32);"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
        "QScrollBar:horizontal {"
        "  border: none; background: transparent; height: 6px; margin: 2px 4px;"
        "}"
        "QScrollBar::handle:horizontal {"
        "  background: rgba(255, 255, 255, 0.16); border-radius: 3px; min-width: 24px;"
        "}"
        "QScrollBar::handle:horizontal:hover {"
        "  background: rgba(255, 255, 255, 0.32);"
        "}"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: none; }"
        "/* 科技感纯净 Slider 控件 */"
        "QSlider::groove:horizontal {"
        "  height: 4px; background: rgba(255, 255, 255, 0.12); border-radius: 2px;"
        "}"
        "QSlider::sub-page:horizontal {"
        "  background: #3b82f6; border-radius: 2px;"
        "}"
        "QSlider::handle:horizontal {"
        "  background: #ffffff; border: 1px solid rgba(0,0,0,0.2); width: 14px; height: 14px; margin: -5px 0; border-radius: 7px;"
        "}"
        "QSlider::handle:horizontal:hover { background: #f8fafc; border-color: #3b82f6; }"
    ));

    if (m_btnContainer) {
        m_btnContainer->setFixedHeight(32);
        m_btnContainer->setAttribute(Qt::WA_StyledBackground, true);
        m_btnContainer->setObjectName(QStringLiteral("hubTitleBar"));
        m_btnContainer->setStyleSheet(QStringLiteral(
            "QWidget#hubTitleBar { background: transparent; border: none; }"
            "QPushButton { background: transparent; border: none; font-family: 'Segoe MDL2 Assets'; font-size: 10px; color: #94a3b8; }"
            "QPushButton:hover { background: rgba(255, 255, 255, 0.08); color: #ffffff; }"
            "QPushButton#closeBtn:hover { background: #e11d48; color: #ffffff; border-top-right-radius: 12px; }"
        ));
    }

    if (container()) {
        container()->setAttribute(Qt::WA_StyledBackground, true);
        container()->setObjectName(QStringLiteral("hubCentralWidget"));
        container()->setStyleSheet(QStringLiteral(
            "QWidget#hubCentralWidget {"
            "  background-color: transparent;"
            "}"
        ));
    }"""

    # 3. 替换 setupHeader
    old_header = """void WallpaperStudioWindow::setupHeader() {
    auto* header = new QWidget(container());
    header->setFixedHeight(52);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 8, 20, 0);
    headerLayout->setSpacing(14);

    // Brand icon
    auto* iconLabel = new QLabel(header);
    iconLabel->setFixedSize(34, 34);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setStyleSheet(QStringLiteral(
        "background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #6366f1, stop:1 #4f46e5);"
        "border-radius: 9px;"
        "font-size: 16px;"
    ));
    iconLabel->setText(QStringLiteral("🌌"));
    headerLayout->addWidget(iconLabel);

    // Brand titles
    auto* titlesBox = new QVBoxLayout();
    titlesBox->setSpacing(1);
    auto* titleLabel = new QLabel(tr("LinDesk Hub · 动态壁纸与桌面组件"), header);
    titleLabel->setStyleSheet(QStringLiteral(
        "font-size: 14px;"
        "font-weight: 700;"
        "color: #f8fafc;"
        "font-family: 'Segoe UI', 'Microsoft YaHei', sans-serif;"
    ));
    auto* subtitleLabel = new QLabel(tr("极致单屏沉浸 · 0.0% 智能休眠 · 拖拽即壁纸"), header);
    subtitleLabel->setStyleSheet(QStringLiteral(
        "font-size: 11px;"
        "color: #94a3b8;"
        "font-family: 'Segoe UI', 'Microsoft YaHei', sans-serif;"
    ));
    titlesBox->addWidget(titleLabel);
    titlesBox->addWidget(subtitleLabel);
    headerLayout->addLayout(titlesBox);

    headerLayout->addStretch(1);

    // Engine Badge
    auto* verTag = new QLabel(tr("CEF OSR · 60 FPS 无感运行"), header);
    verTag->setStyleSheet(QStringLiteral(
        "color: #34d399;"
        "background: rgba(16, 185, 129, 0.12);"
        "border: 1px solid rgba(16, 185, 129, 0.35);"
        "border-radius: 8px;"
        "padding: 4px 10px;"
        "font-size: 11px;"
        "font-weight: 700;"
    ));
    headerLayout->addWidget(verTag, 0, Qt::AlignVCenter);

    centralLayout()->addWidget(header);
}"""

    new_header = """void WallpaperStudioWindow::setupHeader() {
    auto* header = new QWidget(container());
    header->setFixedHeight(48);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 4, 20, 0);
    headerLayout->setSpacing(12);

    // Brand icon (精致微光深色方块，去除低端荧光紫渐变)
    auto* iconLabel = new QLabel(header);
    iconLabel->setFixedSize(30, 30);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setStyleSheet(QStringLiteral(
        "background: rgba(255, 255, 255, 0.08);"
        "border: 1px solid rgba(255, 255, 255, 0.12);"
        "border-radius: 7px;"
        "font-size: 14px;"
    ));
    iconLabel->setText(QStringLiteral("🌌"));
    headerLayout->addWidget(iconLabel);

    // Brand titles (符合专业生产力软件层次)
    auto* titlesBox = new QVBoxLayout();
    titlesBox->setSpacing(1);
    auto* titleLabel = new QLabel(tr("LinDesk Hub"), header);
    titleLabel->setStyleSheet(QStringLiteral(
        "font-size: 15px; font-weight: 600; color: #f8fafc; letter-spacing: -0.01em;"
    ));
    auto* subtitleLabel = new QLabel(tr("动态壁纸工坊与桌面组件管理"), header);
    subtitleLabel->setStyleSheet(QStringLiteral(
        "font-size: 11px; color: #64748b; font-weight: 400;"
    ));
    titlesBox->addWidget(titleLabel);
    titlesBox->addWidget(subtitleLabel);
    headerLayout->addLayout(titlesBox);

    headerLayout->addStretch(1);

    // 硬件加速指示芯片 (Linear 质感，6px 纯净翠绿呼吸微灯)
    auto* chipContainer = new QWidget(header);
    chipContainer->setObjectName(QStringLiteral("hwChip"));
    chipContainer->setStyleSheet(QStringLiteral(
        "QWidget#hwChip {"
        "  background: rgba(16, 185, 129, 0.08);"
        "  border: 1px solid rgba(16, 185, 129, 0.25);"
        "  border-radius: 12px;"
        "}"
    ));
    auto* chipLayout = new QHBoxLayout(chipContainer);
    chipLayout->setContentsMargins(9, 3, 9, 3);
    chipLayout->setSpacing(6);

    auto* chipDot = new QLabel(chipContainer);
    chipDot->setFixedSize(6, 6);
    chipDot->setStyleSheet(QStringLiteral("background: #10b981; border-radius: 3px;"));
    chipLayout->addWidget(chipDot);

    auto* chipText = new QLabel(tr("CEF 硬件加速 · 60 FPS"), chipContainer);
    chipText->setStyleSheet(QStringLiteral("color: #34d399; font-size: 11px; font-weight: 500; font-family: monospace, sans-serif;"));
    chipLayout->addWidget(chipText);

    headerLayout->addWidget(chipContainer, 0, Qt::AlignVCenter);
    centralLayout()->addWidget(header);
}"""

    # 4. 替换 setupNavPills 与 switchToTab
    old_pills = """void WallpaperStudioWindow::setupNavPills() {
    auto* navWidget = new QWidget(container());
    navWidget->setFixedHeight(46);
    auto* navLayout = new QHBoxLayout(navWidget);
    navLayout->setContentsMargins(20, 2, 20, 6);
    navLayout->setSpacing(8);

    auto createPill = [this, navWidget](const QString& text, int tabIndex) {
        auto* btn = new QPushButton(text, navWidget);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(32);
        btn->setMinimumWidth(115);
        connect(btn, &QPushButton::clicked, this, [this, tabIndex]() {
            switchToTab(tabIndex);
        });
        return btn;
    };

    m_tabWallpaperHubBtn = createPill(tr("🖼️ 动态壁纸工坊"), 0);
    m_tabWidgetsBtn = createPill(tr("🧩 桌面组件管理"), 1);
    m_tabDevBtn = createPill(tr("🛠️ 开发者后台 (Dev Inspector)"), 2);

    navLayout->addWidget(m_tabWallpaperHubBtn);
    navLayout->addWidget(m_tabWidgetsBtn);
    navLayout->addWidget(m_tabDevBtn);
    navLayout->addStretch(1);

    centralLayout()->addWidget(navWidget);
}

void WallpaperStudioWindow::switchToTab(int tabIndex) {
    if (!m_stack) return;
    m_stack->setCurrentIndex(tabIndex);

    const QString activeStyle = QStringLiteral(
        "QPushButton {"
        "  background: #4f46e5;"
        "  color: #ffffff;"
        "  border: 1px solid #6366f1;"
        "  border-radius: 8px;"
        "  font-size: 12px;"
        "  font-weight: 700;"
        "  padding: 0 14px;"
        "}"
    );

    const QString inactiveStyle = QStringLiteral(
        "QPushButton {"
        "  background: rgba(30, 41, 59, 0.6);"
        "  color: #94a3b8;"
        "  border: 1px solid rgba(255, 255, 255, 0.08);"
        "  border-radius: 8px;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "  padding: 0 14px;"
        "}"
        "QPushButton:hover {"
        "  background: rgba(51, 65, 85, 0.8);"
        "  color: #f1f5f9;"
        "  border-color: rgba(99, 102, 241, 0.4);"
        "}"
    );"""

    new_pills = """void WallpaperStudioWindow::setupNavPills() {
    auto* navWidget = new QWidget(container());
    navWidget->setFixedHeight(42);
    auto* navLayout = new QHBoxLayout(navWidget);
    navLayout->setContentsMargins(20, 2, 20, 6);
    navLayout->setSpacing(6);

    auto createPill = [this, navWidget](const QString& text, int tabIndex) {
        auto* btn = new QPushButton(text, navWidget);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(28);
        btn->setMinimumWidth(110);
        connect(btn, &QPushButton::clicked, this, [this, tabIndex]() {
            switchToTab(tabIndex);
        });
        return btn;
    };

    m_tabWallpaperHubBtn = createPill(tr("动态壁纸工坊"), 0);
    m_tabWidgetsBtn = createPill(tr("桌面小组件"), 1);
    m_tabDevBtn = createPill(tr("开发者控制台"), 2);

    navLayout->addWidget(m_tabWallpaperHubBtn);
    navLayout->addWidget(m_tabWidgetsBtn);
    navLayout->addWidget(m_tabDevBtn);
    navLayout->addStretch(1);

    centralLayout()->addWidget(navWidget);
}

void WallpaperStudioWindow::switchToTab(int tabIndex) {
    if (!m_stack) return;
    m_stack->setCurrentIndex(tabIndex);

    // Segmented Pill 现代高质感切换（避免低俗亮紫色，采用毛玻璃顶光按键）
    const QString activeStyle = QStringLiteral(
        "QPushButton {"
        "  background: rgba(255, 255, 255, 0.12);"
        "  color: #ffffff;"
        "  border: 1px solid rgba(255, 255, 255, 0.16);"
        "  border-radius: 6px;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "  padding: 0 12px;"
        "}"
    );

    const QString inactiveStyle = QStringLiteral(
        "QPushButton {"
        "  background: transparent;"
        "  color: #94a3b8;"
        "  border: 1px solid transparent;"
        "  border-radius: 6px;"
        "  font-size: 12px;"
        "  font-weight: 500;"
        "  padding: 0 12px;"
        "}"
        "QPushButton:hover {"
        "  background: rgba(255, 255, 255, 0.05);"
        "  color: #f1f5f9;"
        "}"
    );"""

    # 5. 替换 QScrollArea 策略，禁用一切横向滚动，重构卡片布局
    # 查找 galleryScroll
    old_gallery_scroll = """    // 3. 画廊网格卡片滚动区
    auto* galleryScroll = new QScrollArea(leftContainer);
    galleryScroll->setWidgetResizable(true);
    galleryScroll->setStyleSheet(QStringLiteral(
        "QScrollArea { border: none; background: transparent; }"
        "QScrollBar:vertical { width: 6px; background: transparent; }"
        "QScrollBar::handle:vertical { background: rgba(255, 255, 255, 0.15); border-radius: 3px; min-height: 24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    ));"""

    new_gallery_scroll = """    // 3. 画廊网格卡片滚动区（彻底禁用横向滚动条，杜绝截断与爆屏）
    auto* galleryScroll = new QScrollArea(leftContainer);
    galleryScroll->setWidgetResizable(true);
    galleryScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    galleryScroll->setStyleSheet(QStringLiteral(
        "QScrollArea { border: none; background: transparent; }"
    ));"""

    # 查找 heroCard 样式
    old_hero_style = """    // 1. 活动壁纸 Hero 条
    auto* heroCard = new QWidget(leftContainer);
    heroCard->setFixedHeight(68);
    heroCard->setStyleSheet(QStringLiteral(
        "QWidget {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 rgba(30, 41, 59, 0.9), stop:1 rgba(15, 23, 42, 0.95));"
        "  border: 1px solid rgba(255, 255, 255, 0.1);"
        "  border-radius: 12px;"
        "}"
    ));"""

    new_hero_style = """    // 1. 活动壁纸 Hero 条（收紧为 52px 现代微光条）
    auto* heroCard = new QWidget(leftContainer);
    heroCard->setFixedHeight(52);
    heroCard->setStyleSheet(QStringLiteral(
        "QWidget {"
        "  background: rgba(255, 255, 255, 0.03);"
        "  border: 1px solid rgba(255, 255, 255, 0.07);"
        "  border-radius: 10px;"
        "}"
    ));"""

    # 查找 dropCard 样式
    old_drop_style = """    // 2. 拖拽极速生成壁纸提示卡片
    auto* dropCard = new QWidget(leftContainer);
    dropCard->setFixedHeight(54);
    dropCard->setStyleSheet(QStringLiteral(
        "QWidget {"
        "  background: rgba(30, 41, 59, 0.4);"
        "  border: 1px dashed rgba(99, 102, 241, 0.5);"
        "  border-radius: 10px;"
        "}"
        "QWidget:hover {"
        "  border-color: rgba(129, 140, 248, 0.9);"
        "  background: rgba(49, 46, 129, 0.2);"
        "}"
    ));"""

    new_drop_style = """    // 2. 拖拽极速生成壁纸提示卡片（微光虚线框）
    auto* dropCard = new QWidget(leftContainer);
    dropCard->setFixedHeight(44);
    dropCard->setStyleSheet(QStringLiteral(
        "QWidget {"
        "  background: rgba(255, 255, 255, 0.02);"
        "  border: 1px dashed rgba(255, 255, 255, 0.15);"
        "  border-radius: 8px;"
        "}"
        "QWidget:hover {"
        "  border-color: rgba(59, 130, 246, 0.6);"
        "  background: rgba(59, 130, 246, 0.04);"
        "}"
    ));"""

    # 查找右侧应用大按钮样式
    old_detail_apply = """    m_detailApplyBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: #4f46e5;"
        "  color: #ffffff;"
        "  border: 1px solid #6366f1;"
        "  border-radius: 8px;"
        "  font-size: 13px;"
        "  font-weight: 700;"
        "  padding: 6px 16px;"
        "}"
        "QPushButton:hover { background: #4338ca; }"
        "QPushButton:disabled { background: #334155; color: #64748b; border: none; }"
    ));"""

    new_detail_apply = """    m_detailApplyBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: #2563eb;"
        "  color: #ffffff;"
        "  border: 1px solid #3b82f6;"
        "  border-radius: 8px;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "  padding: 6px 16px;"
        "}"
        "QPushButton:hover { background: #1d4ed8; }"
        "QPushButton:pressed { background: #1e40af; }"
        "QPushButton:disabled { background: rgba(255, 255, 255, 0.08); color: #64748b; border: none; }"
    ));"""

    # 查找 propScroll 滚动条
    old_prop_scroll = """    auto* propScroll = new QScrollArea(rightContainer);
    propScroll->setWidgetResizable(true);
    propScroll->setStyleSheet(QStringLiteral("QScrollArea { border: none; background: transparent; }"));"""

    new_prop_scroll = """    auto* propScroll = new QScrollArea(rightContainer);
    propScroll->setWidgetResizable(true);
    propScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    propScroll->setStyleSheet(QStringLiteral("QScrollArea { border: none; background: transparent; }"));"""

    # 查找宿主通用外挂标题与盒子
    old_env_box = """    // 下层：宿主通用外挂四件套
    auto* envHeader = new QLabel(tr("🛠️ 宿主通用外挂三件套 (Host Environment & Audio)"), propScrollContent);
    envHeader->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: 700; color: #e2e8f0; border: none; margin-top: 6px;"));
    propScrollLayout->addWidget(envHeader);

    auto* envBox = new QWidget(propScrollContent);
    envBox->setStyleSheet(QStringLiteral(
        "QWidget {"
        "  background: rgba(15, 23, 42, 0.6);"
        "  border: 1px solid rgba(255, 255, 255, 0.06);"
        "  border-radius: 10px;"
        "  padding: 8px;"
        "}"
    ));"""

    new_env_box = """    // 下层：桌面视听与图层环境配置
    auto* envHeader = new QLabel(tr("桌面环境与视听配置"), propScrollContent);
    envHeader->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: 600; color: #cbd5e1; border: none; margin-top: 6px;"));
    propScrollLayout->addWidget(envHeader);

    auto* envBox = new QWidget(propScrollContent);
    envBox->setStyleSheet(QStringLiteral(
        "QWidget {"
        "  background: rgba(255, 255, 255, 0.025);"
        "  border: 1px solid rgba(255, 255, 255, 0.06);"
        "  border-radius: 10px;"
        "  padding: 8px;"
        "}"
    ));"""

    # 替换卡片生成样式
    old_card_style = """        auto* card = new QWidget(m_galleryGridContainer);
        card->setCursor(Qt::PointingHandCursor);
        card->setStyleSheet(QStringLiteral(
            "QWidget {"
            "  background: rgba(30, 41, 59, 0.7);"
            "  border: 1px solid %1;"
            "  border-radius: 12px;"
            "}"
            "QWidget:hover {"
            "  border-color: rgba(99, 102, 241, 0.85);"
            "}"
        ).arg(isActive ? QStringLiteral("rgba(16, 185, 129, 0.8)") : (isSelected ? QStringLiteral("rgba(99, 102, 241, 0.7)") : QStringLiteral("rgba(255, 255, 255, 0.08)"))));"""

    new_card_style = """        auto* card = new QWidget(m_galleryGridContainer);
        card->setCursor(Qt::PointingHandCursor);
        card->setStyleSheet(QStringLiteral(
            "QWidget {"
            "  background: rgba(255, 255, 255, 0.03);"
            "  border: 1px solid %1;"
            "  border-radius: 10px;"
            "}"
            "QWidget:hover {"
            "  border-color: rgba(255, 255, 255, 0.25);"
            "  background: rgba(255, 255, 255, 0.05);"
            "}"
        ).arg(isActive ? QStringLiteral("rgba(16, 185, 129, 0.8)") : (isSelected ? QStringLiteral("rgba(59, 130, 246, 0.7)") : QStringLiteral("rgba(255, 255, 255, 0.08)"))));"""

    # 替换卡片按钮样式（应用按钮使用科技蓝）
    old_apply_btn = """        auto* applyBtn = new QPushButton(isActive ? tr("当前应用中") : tr("应用到桌面"), card);
        applyBtn->setCursor(Qt::PointingHandCursor);
        applyBtn->setFixedHeight(28);
        applyBtn->setEnabled(!isActive);
        applyBtn->setStyleSheet(isActive ? QStringLiteral(
            "QPushButton { background: rgba(16, 185, 129, 0.15); color: #34d399; border: 1px solid rgba(16, 185, 129, 0.35); border-radius: 6px; font-size: 11px; font-weight: 700; }"
        ) : QStringLiteral(
            "QPushButton { background: #4f46e5; color: #ffffff; border: none; border-radius: 6px; font-size: 11px; font-weight: 700; }"
            "QPushButton:hover { background: #4338ca; }"
        ));"""

    new_apply_btn = """        auto* applyBtn = new QPushButton(isActive ? tr("当前应用中") : tr("应用到桌面"), card);
        applyBtn->setCursor(Qt::PointingHandCursor);
        applyBtn->setFixedHeight(28);
        applyBtn->setEnabled(!isActive);
        applyBtn->setStyleSheet(isActive ? QStringLiteral(
            "QPushButton { background: rgba(16, 185, 129, 0.12); color: #34d399; border: 1px solid rgba(16, 185, 129, 0.3); border-radius: 6px; font-size: 11px; font-weight: 600; }"
        ) : QStringLiteral(
            "QPushButton { background: #2563eb; color: #ffffff; border: none; border-radius: 6px; font-size: 11px; font-weight: 600; }"
            "QPushButton:hover { background: #1d4ed8; }"
        ));"""

    # 执行全部标准化替换
    subs = [
        (old_paint, new_paint),
        (old_init, new_init),
        (old_header, new_header),
        (old_pills, new_pills),
        (old_gallery_scroll, new_gallery_scroll),
        (old_hero_style, new_hero_style),
        (old_drop_style, new_drop_style),
        (old_detail_apply, new_detail_apply),
        (old_prop_scroll, new_prop_scroll),
        (old_env_box, new_env_box),
        (old_card_style, new_card_style),
        (old_apply_btn, new_apply_btn)
    ]

    content_norm = content.replace('\r\n', '\n')
    replaced_count = 0
    for old_s, new_s in subs:
        old_norm = old_s.replace('\r\n', '\n')
        new_norm = new_s.replace('\r\n', '\n')
        if old_norm in content_norm:
            content_norm = content_norm.replace(old_norm, new_norm, 1)
            replaced_count += 1
        else:
            print("FAILED TO MATCH BLOCK:", old_norm[:50], "...")

    print(f"Total blocks matched & replaced: {replaced_count}/{len(subs)}")
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(content_norm)

if __name__ == '__main__':
    update_studio_cpp()
