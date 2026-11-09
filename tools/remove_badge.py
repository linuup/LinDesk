with open('src/ui/hub/WallpaperStudioWindow.cpp', 'rb') as f:
    raw = f.read()

is_crlf = b'\r\n' in raw
text = raw.decode('utf-8').replace('\r\n', '\n')

target = """        QLabel* titleLabel = new QLabel(tr("LinDesk Hub"), brandWidget);
        titleLabel->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: 700; color: #1e293b; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;"));
        brandLayout->addWidget(titleLabel);

        QLabel* badgeLabel = new QLabel(tr("动态壁纸工坊"), brandWidget);
        badgeLabel->setStyleSheet(QStringLiteral("font-size: 10px; font-weight: 600; color: #4f46e5; background: #eef2ff; border: 1px solid #e0e7ff; border-radius: 4px; padding: 1px 5px; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;"));
        brandLayout->addWidget(badgeLabel);"""

replacement = """        QLabel* titleLabel = new QLabel(tr("LinDesk Hub"), brandWidget);
        titleLabel->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: 700; color: #1e293b; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;"));
        brandLayout->addWidget(titleLabel);"""

assert target in text, "target not found!"
text = text.replace(target, replacement, 1)

if is_crlf:
    text = text.replace('\n', '\r\n')

with open('src/ui/hub/WallpaperStudioWindow.cpp', 'wb') as f:
    f.write(text.encode('utf-8'))

print("SUCCESS: Removed badgeLabel from titlebar.")
