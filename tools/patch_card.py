with open('src/ui/hub/WallpaperStudioWindow.cpp', 'r', encoding='utf-8') as f:
    c = f.read().replace('\r\n', '\n')

old_part = """        auto* nameLabel = new QLabel(item.name, card);
        nameLabel->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 700; color: #f8fafc; border: none; background: transparent;"));
        titleRow->addWidget(nameLabel, 1);

        auto* tagLabel = new QLabel(item.tag, card);
        tagLabel->setStyleSheet(QStringLiteral(
            "color: #a5b4fc; background: rgba(99, 102, 241, 0.15); border-radius: 4px; padding: 2px 6px; font-size: 10px; font-weight: 600; border: none;"
        ));
        titleRow->addWidget(tagLabel);
        vLayout->addLayout(titleRow);

        // 描述
        auto* descLabel = new QLabel(item.description, card);
        descLabel->setWordWrap(true);
        descLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #94a3b8; border: none; background: transparent;"));
        vLayout->addWidget(descLabel);"""

new_part = """        auto* nameLabel = new QLabel(item.name, card);
        nameLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        nameLabel->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 600; color: #f8fafc; border: none; background: transparent;"));
        titleRow->addWidget(nameLabel, 1);

        auto* tagLabel = new QLabel(item.tag, card);
        tagLabel->setStyleSheet(QStringLiteral(
            "color: #94a3b8; background: rgba(255, 255, 255, 0.06); border: 1px solid rgba(255, 255, 255, 0.08); border-radius: 4px; padding: 1px 6px; font-size: 10px; font-weight: 500; border: none;"
        ));
        titleRow->addWidget(tagLabel);
        vLayout->addLayout(titleRow);

        // 描述 (限高两行，保持规整高度一致)
        auto* descLabel = new QLabel(item.description, card);
        descLabel->setWordWrap(true);
        descLabel->setFixedHeight(32);
        descLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #64748b; line-height: 1.3; border: none; background: transparent;"));
        vLayout->addWidget(descLabel);"""

if old_part in c:
    c = c.replace(old_part, new_part, 1)
    print("card labels patched!")
else:
    print("card labels NOT found")

with open('src/ui/hub/WallpaperStudioWindow.cpp', 'w', encoding='utf-8', newline='\n') as f:
    f.write(c)
