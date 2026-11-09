with open('src/ui/hub/WallpaperStudioWindow.cpp', 'r', encoding='utf-8') as f:
    c = f.read().replace('\r\n', '\n')

old_btn = """    m_detailApplyBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: #4f46e5;"
        "  color: #ffffff;"
        "  border: 1px solid #6366f1;"
        "  border-radius: 8px;"
        "  font-size: 12px;"
        "  font-weight: 700;"
        "  padding: 0 14px;"
        "}"
        "QPushButton:hover { background: #4338ca; border-color: #818cf8; }"
    ));"""

new_btn = """    m_detailApplyBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: #2563eb;"
        "  color: #ffffff;"
        "  border: 1px solid #3b82f6;"
        "  border-radius: 6px;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "  padding: 5px 14px;"
        "}"
        "QPushButton:hover { background: #1d4ed8; }"
        "QPushButton:pressed { background: #1e40af; }"
    ));"""

old_prop = """    auto* propScroll = new QScrollArea(rightContainer);
    propScroll->setWidgetResizable(true);
    propScroll->setStyleSheet(QStringLiteral(
        "QScrollArea { border: none; background: transparent; }"
        "QScrollBar:vertical { width: 6px; background: transparent; }"
        "QScrollBar::handle:vertical { background: rgba(255, 255, 255, 0.15); border-radius: 3px; min-height: 24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    ));"""

new_prop = """    auto* propScroll = new QScrollArea(rightContainer);
    propScroll->setWidgetResizable(true);
    propScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    propScroll->setStyleSheet(QStringLiteral("QScrollArea { border: none; background: transparent; }"));"""

if old_btn in c:
    c = c.replace(old_btn, new_btn, 1)
    print("btn replaced!")
else:
    print("btn NOT found")

if old_prop in c:
    c = c.replace(old_prop, new_prop, 1)
    print("prop replaced!")
else:
    print("prop NOT found")

with open('src/ui/hub/WallpaperStudioWindow.cpp', 'w', encoding='utf-8', newline='\n') as f:
    f.write(c)
