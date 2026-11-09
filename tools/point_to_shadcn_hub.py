def update_target_path():
    path = 'src/ui/hub/WallpaperStudioWindow.cpp'
    with open(path, 'r', encoding='utf-8') as f:
        c = f.read().replace('\r\n', '\n')

    old_res = """    // 优先加载用户完整的 HTML5 / Web 版 LinDesk Hub 交互页面！
    QString targetPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/LinWidgets/LinDeskHub/index.html"));
    if (!QFileInfo::exists(targetPath)) {
        targetPath = QStringLiteral(LINDESK_SOURCE_DIR) + QStringLiteral("/src/resources/LinWidgets/LinDeskHub/index.html");
    }
    if (!QFileInfo::exists(targetPath)) {
        targetPath = QDir(AppPaths::userLinWidgetsDir()).filePath(QStringLiteral("LinDeskHub/index.html"));
    }"""

    new_res = """    // 优先加载基于 shadcn/ui 打造的高质感 React 现代前端工程 (web/hub -> resources/hub/index.html)
    QString targetPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/hub/index.html"));
    if (!QFileInfo::exists(targetPath)) {
        targetPath = QStringLiteral(LINDESK_SOURCE_DIR) + QStringLiteral("/src/resources/hub/index.html");
    }
    if (!QFileInfo::exists(targetPath)) {
        targetPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/LinWidgets/LinDeskHub/index.html"));
    }
    if (!QFileInfo::exists(targetPath)) {
        targetPath = QStringLiteral(LINDESK_SOURCE_DIR) + QStringLiteral("/src/resources/LinWidgets/LinDeskHub/index.html");
    }"""

    if old_res in c:
        c = c.replace(old_res, new_res, 1)
        print("Updated targetPath to resources/hub/index.html successfully!")
    else:
        print("old_res NOT found")

    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(c)

if __name__ == '__main__':
    update_target_path()
