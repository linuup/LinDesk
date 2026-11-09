def patch_html_loading():
    path = 'src/ui/hub/WallpaperStudioWindow.cpp'
    with open(path, 'r', encoding='utf-8') as f:
        c = f.read().replace('\r\n', '\n')

    target = """    if (container()) {
        container()->setAttribute(Qt::WA_StyledBackground, true);
        container()->setObjectName(QStringLiteral("hubCentralWidget"));
        container()->setStyleSheet(QStringLiteral(
            "QWidget#hubCentralWidget {"
            "  background-color: transparent;"
            "}"
        ));
    }

    setupHeader();
    setupNavPills();

    m_stack = new QStackedWidget(container());
    m_stack->setObjectName(QStringLiteral("hubContentStack"));
    m_stack->setStyleSheet(QStringLiteral(
        "QStackedWidget#hubContentStack { background: transparent; border: none; }"
    ));
    m_stack->addWidget(createWallpaperHubPage()); // Tab 0: 动态壁纸工坊 (Master-Detail)
    m_stack->addWidget(createWidgetsPage());      // Tab 1: 桌面组件管理
    m_stack->addWidget(createDevAdminPage());     // Tab 2: 开发者后台
    
    centralLayout()->addWidget(m_stack, 1);
    switchToTab(0);
}"""

    replacement = """    if (container()) {
        container()->setAttribute(Qt::WA_StyledBackground, true);
        container()->setObjectName(QStringLiteral("hubCentralWidget"));
        container()->setStyleSheet(QStringLiteral(
            "QWidget#hubCentralWidget {"
            "  background-color: transparent;"
            "}"
        ));
    }

    // 优先加载用户完整的 HTML5 / Web 版 LinDesk Hub 交互页面！
    QString targetPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/LinWidgets/LinDeskHub/index.html"));
    if (!QFileInfo::exists(targetPath)) {
        targetPath = QStringLiteral(LINDESK_SOURCE_DIR) + QStringLiteral("/src/resources/LinWidgets/LinDeskHub/index.html");
    }
    if (!QFileInfo::exists(targetPath)) {
        targetPath = QDir(AppPaths::userLinWidgetsDir()).filePath(QStringLiteral("LinDeskHub/index.html"));
    }

    auto& bridge = LinWidgetRuntimeBridge::instance();
    if (!bridge.isLoaded()) {
        bridge.loadRuntime();
    }

    if (QFileInfo::exists(targetPath)) {
        const QString targetUrl = QUrl::fromLocalFile(targetPath).toString();
        m_hubWebWidget = bridge.createHubWebWidget(targetUrl, container());
        if (m_hubWebWidget) {
            centralLayout()->addWidget(m_hubWebWidget, 1);
            qInfo() << "[WallpaperStudio] Successfully loaded HTML Hub page from:" << targetPath;
            return;
        }
    }

    // 若 CEF 未就绪，降级到 Qt 原生后备界面
    setupHeader();
    setupNavPills();

    m_stack = new QStackedWidget(container());
    m_stack->setObjectName(QStringLiteral("hubContentStack"));
    m_stack->setStyleSheet(QStringLiteral(
        "QStackedWidget#hubContentStack { background: transparent; border: none; }"
    ));
    m_stack->addWidget(createWallpaperHubPage()); // Tab 0: 动态壁纸工坊 (Master-Detail)
    m_stack->addWidget(createWidgetsPage());      // Tab 1: 桌面组件管理
    m_stack->addWidget(createDevAdminPage());     // Tab 2: 开发者后台
    
    centralLayout()->addWidget(m_stack, 1);
    switchToTab(0);
}"""

    if target in c:
        c = c.replace(target, replacement, 1)
        print("HTML WebWidget loading restored successfully!")
    else:
        print("Target block NOT found!")

    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(c)

if __name__ == '__main__':
    patch_html_loading()
