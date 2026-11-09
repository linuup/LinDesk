with open('src/ui/hub/WallpaperStudioWindow.cpp', 'rb') as f:
    raw = f.read()

is_crlf = b'\r\n' in raw
text = raw.decode('utf-8').replace('\r\n', '\n')

target_paint = """void WallpaperStudioWindow::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    // 高级深空一体化背景（消灭突兀白边与割裂感）
    painter.setPen(QPen(QColor(255, 255, 255, 22), 1));
    painter.setBrush(QColor(11, 15, 25, 255)); // #0b0f19
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 12, 12);
}"""

replacement_paint = """void WallpaperStudioWindow::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    // 现代浅色一体化背景（与 shadcn/ui #f8fafc 无缝融合，消除标题栏黑条）
    painter.setPen(QPen(QColor(226, 232, 240), 1)); // #e2e8f0 柔和微边框
    painter.setBrush(QColor(248, 250, 252)); // #f8fafc
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 12, 12);
}"""

target_btn = """    if (m_btnContainer) {
        m_btnContainer->setFixedHeight(32);
        m_btnContainer->setAttribute(Qt::WA_StyledBackground, true);
        m_btnContainer->setObjectName(QStringLiteral("hubTitleBar"));
        m_btnContainer->setStyleSheet(QStringLiteral(
            "QWidget#hubTitleBar { background: transparent; border: none; }"
            "QPushButton { background: transparent; border: none; font-family: 'Segoe MDL2 Assets'; font-size: 10px; color: #94a3b8; }"
            "QPushButton:hover { background: rgba(255, 255, 255, 0.08); color: #ffffff; }"
            "QPushButton#closeBtn:hover { background: #e11d48; color: #ffffff; border-top-right-radius: 12px; }"
        ));
    }"""

replacement_btn = """    if (m_btnContainer) {
        m_btnContainer->setFixedHeight(32);
        m_btnContainer->setAttribute(Qt::WA_StyledBackground, true);
        m_btnContainer->setObjectName(QStringLiteral("hubTitleBar"));
        m_btnContainer->setStyleSheet(QStringLiteral(
            "QWidget#hubTitleBar { background: #f8fafc; border: none; border-top-left-radius: 12px; border-top-right-radius: 12px; }"
            "QPushButton { background: transparent; border: none; font-family: 'Segoe MDL2 Assets'; font-size: 10px; color: #64748b; }"
            "QPushButton:hover { background: rgba(0, 0, 0, 0.06); color: #0f172a; }"
            "QPushButton#closeBtn:hover { background: #ef4444; color: #ffffff; border-top-right-radius: 12px; }"
        ));
    }"""

target_container = """    if (container()) {
        container()->setAttribute(Qt::WA_StyledBackground, true);
        container()->setObjectName(QStringLiteral("hubCentralWidget"));
        container()->setStyleSheet(QStringLiteral(
            "QWidget#hubCentralWidget {"
            "  background-color: transparent;"
            "}"
        ));
    }"""

replacement_container = """    if (container()) {
        container()->setAttribute(Qt::WA_StyledBackground, true);
        container()->setObjectName(QStringLiteral("hubCentralWidget"));
        container()->setStyleSheet(QStringLiteral(
            "QWidget#fluentBaseContainer, QWidget#hubCentralWidget {"
            "  background-color: #f8fafc;"
            "  border: none;"
            "}"
        ));
    }"""

assert target_paint in text, 'target_paint not found'
text = text.replace(target_paint, replacement_paint, 1)

assert target_btn in text, 'target_btn not found'
text = text.replace(target_btn, replacement_btn, 1)

assert target_container in text, 'target_container not found'
text = text.replace(target_container, replacement_container, 1)

if is_crlf:
    text = text.replace('\n', '\r\n')

with open('src/ui/hub/WallpaperStudioWindow.cpp', 'wb') as f:
    f.write(text.encode('utf-8'))

print("SUCCESS: Fixed title bar color in WallpaperStudioWindow.cpp!")
