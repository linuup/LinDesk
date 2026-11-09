with open('src/ui/hub/WallpaperStudioWindow.cpp', 'rb') as f:
    raw = f.read()

is_crlf = b'\r\n' in raw
text = raw.decode('utf-8').replace('\r\n', '\n')

# 1. Add #include <QLabel>
if '#include <QLabel>' not in text:
    text = text.replace('#include <QFormLayout>', '#include <QFormLayout>\n#include <QLabel>', 1)

# 2. Update m_btnContainer titlebar
target_btn = """    if (m_btnContainer) {
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

replacement_btn = """    if (m_btnContainer) {
        m_btnContainer->setFixedHeight(36);
        m_btnContainer->setAttribute(Qt::WA_StyledBackground, true);
        m_btnContainer->setObjectName(QStringLiteral("hubTitleBar"));
        m_btnContainer->setStyleSheet(QStringLiteral(
            "QWidget#hubTitleBar { background: #f8fafc; border: none; border-top-left-radius: 12px; border-top-right-radius: 12px; }"
            "QPushButton { background: transparent; border: none; font-family: 'Segoe MDL2 Assets'; font-size: 10px; color: #64748b; }"
            "QPushButton:hover { background: rgba(0, 0, 0, 0.06); color: #0f172a; }"
            "QPushButton#closeBtn:hover { background: #ef4444; color: #ffffff; border-top-right-radius: 12px; }"
        ));

        // 左上角 LinDesk 品牌标识与标题栏组件（消除左上角空白）
        QWidget* brandWidget = new QWidget(m_btnContainer);
        brandWidget->setAttribute(Qt::WA_TransparentForMouseEvents, true); // 鼠标穿透，点击即可直接拖动窗口
        QHBoxLayout* brandLayout = new QHBoxLayout(brandWidget);
        brandLayout->setContentsMargins(14, 0, 0, 0);
        brandLayout->setSpacing(7);

        QLabel* iconLabel = new QLabel(brandWidget);
        QPixmap iconPix(":/Assets/icon.png");
        if (!iconPix.isNull()) {
            iconLabel->setPixmap(iconPix.scaled(18, 18, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        iconLabel->setFixedSize(18, 18);
        brandLayout->addWidget(iconLabel);

        QLabel* titleLabel = new QLabel(tr("LinDesk Hub"), brandWidget);
        titleLabel->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: 700; color: #1e293b; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;"));
        brandLayout->addWidget(titleLabel);

        QLabel* badgeLabel = new QLabel(tr("动态壁纸工坊"), brandWidget);
        badgeLabel->setStyleSheet(QStringLiteral("font-size: 10px; font-weight: 600; color: #4f46e5; background: #eef2ff; border: 1px solid #e0e7ff; border-radius: 4px; padding: 1px 5px; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;"));
        brandLayout->addWidget(badgeLabel);

        if (m_btnLayout) {
            m_btnLayout->insertWidget(0, brandWidget);
        }
    }"""

assert target_btn in text, 'target_btn not found!'
text = text.replace(target_btn, replacement_btn, 1)

if is_crlf:
    text = text.replace('\n', '\r\n')

with open('src/ui/hub/WallpaperStudioWindow.cpp', 'wb') as f:
    f.write(text.encode('utf-8'))

print("SUCCESS: Added title bar brand widget in WallpaperStudioWindow.cpp!")
