import sys

with open('src/desktop/widgets/linwidget/WidgetManager.cpp', 'rb') as f:
    raw = f.read()

# Normalize newline for searching
is_crlf = b'\r\n' in raw
text = raw.decode('utf-8')

target1 = """        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("widgets"), widgetsArray);
        response.insert(QStringLiteral("runtimeEnabled"), m_enabled);
        response.insert(QStringLiteral("showBounds"), showWidgetBounds());
        response.insert(QStringLiteral("activeWallpaperId"), m_activeWallpaperId);
        response.insert(QStringLiteral("activeWallpaperPackagePath"), m_activeWallpaperPackagePath);
        response.insert(QStringLiteral("activeWallpaperInteractive"), m_activeWallpaperInteractive);
        response.insert(QStringLiteral("userWidgetsDir"), AppPaths::userLinWidgetsDir());
        response.insert(QStringLiteral("logDir"), AppPaths::userLogsDir());
        return response;"""

replacement1 = """        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("widgets"), widgetsArray);

        // --- Scan Wallpapers for Hub Gallery & Studio ---
        QJsonArray wallpapersArray;
        QSet<QString> seenWallIds;

        // 1. Scan from registry entries
        for (const WidgetRegistryEntry& entry : registry.entries()) {
            if (entry.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0) {
                const QString normId = entry.id.trimmed().toLower();
                if (!seenWallIds.contains(normId)) {
                    seenWallIds.insert(normId);
                    QJsonObject wallObj;
                    wallObj.insert(QStringLiteral("id"), entry.id);
                    wallObj.insert(QStringLiteral("name"), entry.name);
                    wallObj.insert(QStringLiteral("version"), entry.version);
                    wallObj.insert(QStringLiteral("author"), entry.author);
                    wallObj.insert(QStringLiteral("description"), entry.description);
                    wallObj.insert(QStringLiteral("packagePath"), entry.packagePath);
                    wallObj.insert(QStringLiteral("role"), QStringLiteral("wallpaper"));
                    const bool isAct = (!m_activeWallpaperId.isEmpty() &&
                        (m_activeWallpaperId.compare(entry.id, Qt::CaseInsensitive) == 0 ||
                         m_activeWallpaperId.compare(QStringLiteral("wallpaper:") + entry.id, Qt::CaseInsensitive) == 0 ||
                         m_activeWallpaperPackagePath == entry.packagePath));
                    wallObj.insert(QStringLiteral("running"), isAct);
                    wallpapersArray.append(wallObj);
                }
            }
        }

        // 2. Scan dedicated wallpaper directories
        QStringList wallpaperDirs;
        wallpaperDirs << AppPaths::userWallpapersDir();
        wallpaperDirs << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/wallpapers"));
        wallpaperDirs << QStringLiteral(LINDESK_SOURCE_DIR) + QStringLiteral("/src/resources/wallpapers");
        wallpaperDirs << QDir(AppPaths::userLinWidgetsDir()).filePath(QStringLiteral("../wallpapers"));

        for (const QString& baseDir : wallpaperDirs) {
            QDir dir(baseDir);
            if (!dir.exists()) continue;

            const QFileInfoList subDirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo& fi : subDirs) {
                const QString mPath = fi.filePath() + QStringLiteral("/manifest.json");
                if (!QFileInfo::exists(mPath)) continue;

                QFile f(mPath);
                if (!f.open(QIODevice::ReadOnly)) continue;
                const QByteArray mData = f.readAll();
                f.close();

                const QJsonDocument mDoc = QJsonDocument::fromJson(mData);
                if (!mDoc.isObject()) continue;
                const QJsonObject mObj = mDoc.object();

                const QString role = mObj.value(QStringLiteral("role")).toString();
                if (!role.isEmpty() && role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) != 0) {
                    continue;
                }

                const QString id = mObj.value(QStringLiteral("id")).toString(fi.fileName());
                const QString normId = id.trimmed().toLower();
                if (seenWallIds.contains(normId)) continue;
                seenWallIds.insert(normId);

                QJsonObject wallItem;
                wallItem.insert(QStringLiteral("id"), id);
                wallItem.insert(QStringLiteral("name"), mObj.value(QStringLiteral("name")).toString(fi.fileName()));
                wallItem.insert(QStringLiteral("version"), mObj.value(QStringLiteral("version")).toString(QStringLiteral("1.0.0")));
                wallItem.insert(QStringLiteral("author"), mObj.value(QStringLiteral("author")).toString(QStringLiteral("LinDesk")));
                wallItem.insert(QStringLiteral("description"), mObj.value(QStringLiteral("description")).toString());
                wallItem.insert(QStringLiteral("packagePath"), fi.absoluteFilePath());
                wallItem.insert(QStringLiteral("role"), QStringLiteral("wallpaper"));
                wallItem.insert(QStringLiteral("interactive"), mObj.value(QStringLiteral("interactive")).toBool(true));
                if (mObj.contains(QStringLiteral("properties"))) {
                    wallItem.insert(QStringLiteral("properties"), mObj.value(QStringLiteral("properties")).toObject());
                }

                const bool isRunningWall = (!m_activeWallpaperId.isEmpty() &&
                    (m_activeWallpaperId.contains(id, Qt::CaseInsensitive) ||
                     m_activeWallpaperId.contains(fi.fileName(), Qt::CaseInsensitive) ||
                     m_activeWallpaperPackagePath == fi.absoluteFilePath()));
                wallItem.insert(QStringLiteral("running"), isRunningWall);

                wallpapersArray.append(wallItem);
            }
        }

        response.insert(QStringLiteral("wallpapers"), wallpapersArray);
        response.insert(QStringLiteral("runtimeEnabled"), m_enabled);
        response.insert(QStringLiteral("showBounds"), showWidgetBounds());
        response.insert(QStringLiteral("activeWallpaperId"), m_activeWallpaperId);
        response.insert(QStringLiteral("activeWallpaperPackagePath"), m_activeWallpaperPackagePath);
        response.insert(QStringLiteral("activeWallpaperInteractive"), m_activeWallpaperInteractive);
        response.insert(QStringLiteral("userWidgetsDir"), AppPaths::userLinWidgetsDir());
        response.insert(QStringLiteral("logDir"), AppPaths::userLogsDir());
        return response;"""

target2 = """    if (action == QStringLiteral("open_widgets_dir")) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::userLinWidgetsDir()));
        response.insert(QStringLiteral("ok"), true);
        return response;
    }

    if (action == QStringLiteral("open_log_folder")) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::userLogsDir()));
        response.insert(QStringLiteral("ok"), true);
        return response;
    }"""

replacement2 = """    if (action == QStringLiteral("open_widgets_dir") || action == QStringLiteral("open_widgets_folder")) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::userLinWidgetsDir()));
        response.insert(QStringLiteral("ok"), true);
        return response;
    }

    if (action == QStringLiteral("open_log_folder") || action == QStringLiteral("open_logs_folder")) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::userLogsDir()));
        response.insert(QStringLiteral("ok"), true);
        return response;
    }"""

target3 = """    if (action == QStringLiteral("stop_wallpaper")) {
        stopWallpaper(true);
        response.insert(QStringLiteral("ok"), true);
        return response;
    }"""

replacement3 = """    if (action == QStringLiteral("stop_wallpaper")) {
        stopWallpaper(true);
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("activeWallpaperId"), QString());
        return response;
    }"""

target4 = """    if (action == QStringLiteral("apply_wallpaper")) {
        QString wallPath = request.value(QStringLiteral("packagePath")).toString().trimmed();
        if (wallPath.isEmpty()) {
            WidgetRegistryEntry entry = registry.entryById(resolvedId);
            wallPath = entry.packagePath;
        }
        if (wallPath.isEmpty()) {
            wallPath = QDir(AppPaths::userLinWidgetsDir()).filePath(resolvedId);
        }
        QString wallErr;
        const bool wOk = startWallpaper(wallPath, false, false, &wallErr);
        response.insert(QStringLiteral("ok"), wOk);
        if (!wOk) response.insert(QStringLiteral("error"), wallErr);
        return response;
    }

    if (action == QStringLiteral("set_wallpaper_prop")) {
        if (!m_activeWallpaperId.isEmpty()) {
            QJsonObject propMsg;
            propMsg.insert(QStringLiteral("type"), QStringLiteral("wallpaper_property_change"));
            const QString key = request.contains(QStringLiteral("key")) ? request.value(QStringLiteral("key")).toString() : request.value(QStringLiteral("property")).toString();
            propMsg.insert(QStringLiteral("key"), key);
            propMsg.insert(QStringLiteral("property"), key);
            propMsg.insert(QStringLiteral("value"), request.value(QStringLiteral("value")));
            sendMessageToWidget(m_activeWallpaperId, propMsg);
            response.insert(QStringLiteral("ok"), true);
        } else {
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), QStringLiteral("No active wallpaper running."));
        }
        return response;
    }"""

replacement4 = """    if (action == QStringLiteral("apply_wallpaper")) {
        QString wallPath = request.value(QStringLiteral("packagePath")).toString().trimmed();
        if (wallPath.isEmpty()) {
            WidgetRegistryEntry entry = registry.entryById(resolvedId);
            wallPath = entry.packagePath;
        }
        if (wallPath.isEmpty()) {
            const QString cleanId = resolvedId.startsWith(QStringLiteral("wallpaper:")) ? resolvedId.mid(10) : resolvedId;
            const QStringList candidateDirs = {
                QDir(AppPaths::runtimeWallpapersDir()).filePath(cleanId),
                QDir(QStringLiteral(LINDESK_SOURCE_DIR) + QStringLiteral("/src/resources/wallpapers")).filePath(cleanId),
                QDir(AppPaths::userWallpapersDir()).filePath(cleanId)
            };
            for (const QString& cand : candidateDirs) {
                if (QFileInfo::exists(cand + QStringLiteral("/manifest.json"))) {
                    wallPath = cand;
                    break;
                }
            }
        }
        if (wallPath.isEmpty()) {
            wallPath = QDir(AppPaths::userLinWidgetsDir()).filePath(resolvedId);
        }
        QString wallErr;
        const bool wOk = startWallpaper(wallPath, false, true, &wallErr);
        response.insert(QStringLiteral("ok"), wOk);
        response.insert(QStringLiteral("activeWallpaperId"), m_activeWallpaperId);
        if (!wOk) response.insert(QStringLiteral("error"), wallErr);
        return response;
    }

    if (action == QStringLiteral("set_wallpaper_prop") || action == QStringLiteral("set_wallpaper_property")) {
        if (!m_activeWallpaperId.isEmpty()) {
            QJsonObject propMsg;
            propMsg.insert(QStringLiteral("type"), QStringLiteral("wallpaper_property_change"));
            const QString key = request.contains(QStringLiteral("key")) ? request.value(QStringLiteral("key")).toString() : request.value(QStringLiteral("property")).toString();
            propMsg.insert(QStringLiteral("key"), key);
            propMsg.insert(QStringLiteral("property"), key);
            propMsg.insert(QStringLiteral("value"), request.value(QStringLiteral("value")));
            sendMessageToWidget(m_activeWallpaperId, propMsg);
            response.insert(QStringLiteral("ok"), true);
        } else {
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), QStringLiteral("No active wallpaper running."));
        }
        return response;
    }

    if (action == QStringLiteral("apply_studio_preset")) {
        response.insert(QStringLiteral("ok"), true);
        return response;
    }"""

# unify newlines
text = text.replace('\r\n', '\n')
target1 = target1.replace('\r\n', '\n')
replacement1 = replacement1.replace('\r\n', '\n')
target2 = target2.replace('\r\n', '\n')
replacement2 = replacement2.replace('\r\n', '\n')
target3 = target3.replace('\r\n', '\n')
replacement3 = replacement3.replace('\r\n', '\n')
target4 = target4.replace('\r\n', '\n')
replacement4 = replacement4.replace('\r\n', '\n')

if target1 not in text:
    print("ERROR: target1 not found!")
    sys.exit(1)
text = text.replace(target1, replacement1, 1)

if target2 not in text:
    print("ERROR: target2 not found!")
    sys.exit(1)
text = text.replace(target2, replacement2, 1)

if target3 not in text:
    print("ERROR: target3 not found!")
    sys.exit(1)
text = text.replace(target3, replacement3, 1)

if target4 not in text:
    print("ERROR: target4 not found!")
    sys.exit(1)
text = text.replace(target4, replacement4, 1)

if is_crlf:
    text = text.replace('\n', '\r\n')

with open('src/desktop/widgets/linwidget/WidgetManager.cpp', 'wb') as f:
    f.write(text.encode('utf-8'))

print("SUCCESS: WidgetManager.cpp patched successfully!")
