#include "desktop/widgets/linwidget/WidgetManager_Internal.h"
#include "desktop/widgets/linwidget/WinMfVideoEngine.h"
#include <QThreadPool>

namespace LinWidget {

// ============================================================================
// WidgetManager JSON-RPC Protocol Dispatcher Implementation
// ============================================================================

QJsonObject WidgetManager::handleWidgetManagerRequest(const QJsonObject& request)
{
    const QString action = request.value(QStringLiteral("action")).toString(QStringLiteral("list")).trimmed().toLower();
    const QString requestId = request.value(QStringLiteral("requestId")).toString();
    QJsonObject response;
    response.insert(QStringLiteral("type"), QString::fromLatin1(WidgetMessageType::WidgetManagerResponse));
    response.insert(QStringLiteral("action"), action);
    if (!requestId.isEmpty()) {
        response.insert(QStringLiteral("requestId"), requestId);
    }

    auto& registry = WidgetRegistryManager::instance();
    registry.load();

    auto buildWidgetDetailJson = [&](const WidgetRegistryEntry& entry) -> QJsonObject {
        WidgetManifest manifest;
        const bool manifestLoaded = WidgetPackage::loadManifest(entry.packagePath, &manifest, nullptr);
        const QString regId = entry.id.trimmed().toLower();
        const QString safeId = safeStorageSegment(regId.isEmpty() ? manifest.id : regId);
        const QString mode = entry.isDevelopment ? QStringLiteral("dev") : QStringLiteral("installed");
        const QString dataDirPath = QDir(AppPaths::linWidgetDataDir()).filePath(mode + QLatin1Char('/') + safeId);
        const QString altMode = entry.isDevelopment ? QStringLiteral("installed") : QStringLiteral("dev");
        const QString altDataDirPath = QDir(AppPaths::linWidgetDataDir()).filePath(altMode + QLatin1Char('/') + safeId);
        const qint64 storageUsedBytes = calculateDirectorySize(dataDirPath) + calculateDirectorySize(altDataDirPath);
        const qint64 packageSizeBytes = calculateDirectorySize(entry.packagePath);

        QJsonArray runningInstanceIds;
        QJsonArray instancesArray;
        int totalCrashes = m_totalCrashCounts.value(regId, 0);
        bool isCrashed = m_crashTrippedWidgets.contains(regId);

        for (const WidgetLayoutState& state : m_layout.states()) {
            if (stateRegistryId(state) == regId) {
                const bool instRunning = m_runningWidgets.contains(state.instanceId);
                if (instRunning) {
                    runningInstanceIds.append(state.instanceId);
                }
                totalCrashes += m_totalCrashCounts.value(state.instanceId, 0);
                if (m_crashTrippedWidgets.contains(state.instanceId)) {
                    isCrashed = true;
                }
                QJsonObject instObj;
                instObj.insert(QStringLiteral("instanceId"), state.instanceId);
                instObj.insert(QStringLiteral("running"), instRunning);
                instObj.insert(QStringLiteral("visible"), state.visible);
                QJsonObject geomObj;
                geomObj.insert(QStringLiteral("x"), state.geometry.x());
                geomObj.insert(QStringLiteral("y"), state.geometry.y());
                geomObj.insert(QStringLiteral("width"), state.geometry.width());
                geomObj.insert(QStringLiteral("height"), state.geometry.height());
                instObj.insert(QStringLiteral("geometry"), geomObj);
                instancesArray.append(instObj);
            }
        }
        const QString wallpaperInstId = QStringLiteral("wallpaper:") + regId;
        if (!m_activeWallpaperId.isEmpty() && m_activeWallpaperId.compare(wallpaperInstId, Qt::CaseInsensitive) == 0) {
            runningInstanceIds.append(m_activeWallpaperId);
        }

        const bool isRunning = !runningInstanceIds.isEmpty();
        QString statusText = QStringLiteral("stopped");
        if (isCrashed && !isRunning) {
            statusText = QStringLiteral("crashed");
        } else if (isRunning) {
            statusText = QStringLiteral("running");
        } else if (!entry.enabled) {
            statusText = QStringLiteral("disabled");
        }

        QString sourceLabel = QStringLiteral("组件包安装");
        if (entry.isDevelopment) {
            sourceLabel = QStringLiteral("开发目录");
        } else if (entry.trusted || entry.packagePath.contains(QStringLiteral("resources/LinWidgets"), Qt::CaseInsensitive)) {
            sourceLabel = QStringLiteral("内置组件");
        }

        QJsonArray declaredPerms;
        if (manifestLoaded) {
            for (const QString& p : manifest.permissions) {
                declaredPerms.append(p);
            }
        }
        const QStringList normalizedGranted = entry.isDevelopment
            ? m_permissions.normalizePermissions(manifestLoaded ? manifest.permissions : entry.permissionsGranted)
            : m_permissions.normalizePermissions(entry.permissionsGranted);
        QJsonArray grantedPerms;
        for (const QString& p : normalizedGranted) {
            grantedPerms.append(p);
        }

        QJsonObject item;
        item.insert(QStringLiteral("id"), entry.id);
        item.insert(QStringLiteral("name"), entry.name.isEmpty() ? (manifestLoaded ? manifest.name : entry.id) : entry.name);
        item.insert(QStringLiteral("version"), entry.version.isEmpty() ? (manifestLoaded ? manifest.version : QStringLiteral("1.0.0")) : entry.version);
        item.insert(QStringLiteral("author"), entry.author.isEmpty() ? manifest.author : entry.author);
        item.insert(QStringLiteral("description"), entry.description.isEmpty() ? manifest.description : entry.description);
        item.insert(QStringLiteral("role"), entry.role.isEmpty() ? manifest.role : entry.role);
        item.insert(QStringLiteral("packagePath"), entry.packagePath);
        item.insert(QStringLiteral("source"), sourceLabel);
        item.insert(QStringLiteral("isDevelopment"), entry.isDevelopment);
        item.insert(QStringLiteral("trusted"), entry.trusted);
        item.insert(QStringLiteral("enabled"), entry.enabled);
        item.insert(QStringLiteral("running"), isRunning);
        item.insert(QStringLiteral("status"), statusText);
        item.insert(QStringLiteral("crashCount"), totalCrashes);
        item.insert(QStringLiteral("packageSizeBytes"), packageSizeBytes);
        item.insert(QStringLiteral("storageUsedBytes"), storageUsedBytes);
        item.insert(QStringLiteral("storageQuotaBytes"), kWidgetStorageQuotaBytes);
        item.insert(QStringLiteral("declaredPermissions"), declaredPerms);
        item.insert(QStringLiteral("grantedPermissions"), grantedPerms);
        item.insert(QStringLiteral("hasSettings"), manifestLoaded && (!manifest.settings.isEmpty() || !manifest.settingsSchema.isEmpty()));
        if (manifestLoaded && !manifest.settingsSchema.isEmpty()) {
            QJsonObject schemaObj;
            for (const WidgetSettingField& field : manifest.settingsSchema) {
                QJsonObject fieldObj;
                fieldObj.insert(QStringLiteral("type"), field.type);
                fieldObj.insert(QStringLiteral("label"), field.title.isEmpty() ? field.key : field.title);
                fieldObj.insert(QStringLiteral("default"), field.defaultValue);
                if (field.hasMin) fieldObj.insert(QStringLiteral("min"), field.minimum);
                if (field.hasMax) fieldObj.insert(QStringLiteral("max"), field.maximum);
                if (!field.options.isEmpty()) {
                    QJsonArray optsArr;
                    for (const WidgetSettingOption& opt : field.options) {
                        QJsonObject optObj;
                        optObj.insert(QStringLiteral("label"), opt.label);
                        optObj.insert(QStringLiteral("value"), opt.value);
                        optsArr.append(optObj);
                    }
                    fieldObj.insert(QStringLiteral("options"), optsArr);
                }
                schemaObj.insert(field.key, fieldObj);
            }
            item.insert(QStringLiteral("settingsSchema"), schemaObj);
        }
        {
            QJsonObject savedSettings;
            const QString modeSeg = entry.isDevelopment ? QStringLiteral("dev") : QStringLiteral("installed");
            const QString settingsFile = QDir(AppPaths::linWidgetDataDir()).filePath(modeSeg + QLatin1Char('/') + safeId + QStringLiteral("/widget.json"));
            if (loadStorageObject(settingsFile, &savedSettings, nullptr) && !savedSettings.isEmpty()) {
                item.insert(QStringLiteral("settingsValues"), savedSettings);
            }
        }
        item.insert(QStringLiteral("installedAt"), entry.installedAt);
        item.insert(QStringLiteral("updatedAt"), entry.updatedAt);
        item.insert(QStringLiteral("dataDirPath"), dataDirPath);
        item.insert(QStringLiteral("logFilePath"), sharedLogFilePath());
        item.insert(QStringLiteral("runningInstanceIds"), runningInstanceIds);
        item.insert(QStringLiteral("instances"), instancesArray);
        return item;
    };

    auto resolveWidgetId = [this](const QString& rawId) -> QString {
        return this->resolveWidgetId(rawId);
    };

    if (action == QStringLiteral("list")) {
        QJsonArray widgetsArray;
        QSet<QString> seenIds;
        for (const WidgetRegistryEntry& entry : registry.entries()) {
            seenIds.insert(entry.id.trimmed().toLower());
            widgetsArray.append(buildWidgetDetailJson(entry));
        }
        for (const WidgetInstance& inst : m_instances) {
            const QString regId = registryIdForManifest(inst.manifest);
            if (!regId.isEmpty() && !seenIds.contains(regId)) {
                seenIds.insert(regId);
                WidgetRegistryEntry synth;
                synth.id = inst.manifest.id;
                synth.name = inst.manifest.name;
                synth.version = inst.manifest.version;
                synth.author = inst.manifest.author;
                synth.description = inst.manifest.description;
                synth.packagePath = inst.packagePath;
                synth.role = inst.manifest.role;
                synth.isDevelopment = inst.isDevelopment;
                synth.enabled = true;
                synth.permissionsGranted = inst.grantedPermissions;
                widgetsArray.append(buildWidgetDetailJson(synth));
            }
        }
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("widgets"), widgetsArray);

        // --- Scan Wallpapers for Hub Gallery & Studio ---
        QJsonArray wallpapersArray;
        QSet<QString> seenWallIds;

        // 1. Scan dedicated wallpaper directories first (includes full manifest properties)
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
                if (seenWallIds.contains(normId) || normId.contains(QStringLiteral("gargantua")) || normId.contains(QStringLiteral("blackhole"))) {
                    continue;
                }
                seenWallIds.insert(normId);

                QJsonObject wallItem;
                wallItem.insert(QStringLiteral("id"), id);
                wallItem.insert(QStringLiteral("name"), mObj.value(QStringLiteral("name")).toString(fi.fileName()));
                wallItem.insert(QStringLiteral("version"), mObj.value(QStringLiteral("version")).toString(QStringLiteral("1.0.0")));
                wallItem.insert(QStringLiteral("author"), mObj.value(QStringLiteral("author")).toString(QStringLiteral("LinDesk")));
                wallItem.insert(QStringLiteral("description"), mObj.value(QStringLiteral("description")).toString());
                wallItem.insert(QStringLiteral("packagePath"), fi.absoluteFilePath());
                wallItem.insert(QStringLiteral("role"), QStringLiteral("wallpaper"));
                wallItem.insert(QStringLiteral("wallpaperType"), mObj.value(QStringLiteral("wallpaperType")).toString(QStringLiteral("html")));
                const bool isCustomWall = mObj.value(QStringLiteral("isCustom")).toBool(
                    id.startsWith(QStringLiteral("custom."), Qt::CaseInsensitive) ||
                    fi.absoluteFilePath().startsWith(QDir::cleanPath(AppPaths::userWallpapersDir()), Qt::CaseInsensitive));
                wallItem.insert(QStringLiteral("isCustom"), isCustomWall);
                if (mObj.contains(QStringLiteral("mediaFile"))) {
                    const QString mfName = mObj.value(QStringLiteral("mediaFile")).toString();
                    wallItem.insert(QStringLiteral("mediaUrl"), QUrl::fromLocalFile(QDir(fi.absoluteFilePath()).filePath(mfName)).toString());
                }

                // Resolve preview image (preview.png / preview.jpg / preview.webp)
                QString previewFile = mObj.value(QStringLiteral("preview")).toString();
                if (previewFile.isEmpty() || !QFileInfo::exists(QDir(fi.absoluteFilePath()).filePath(previewFile))) {
                    if (QFileInfo::exists(QDir(fi.absoluteFilePath()).filePath(QStringLiteral("preview.png")))) {
                        previewFile = QStringLiteral("preview.png");
                    } else if (QFileInfo::exists(QDir(fi.absoluteFilePath()).filePath(QStringLiteral("preview.jpg")))) {
                        previewFile = QStringLiteral("preview.jpg");
                    } else if (QFileInfo::exists(QDir(fi.absoluteFilePath()).filePath(QStringLiteral("preview.webp")))) {
                        previewFile = QStringLiteral("preview.webp");
                    }
                }
                if (!previewFile.isEmpty() && QFileInfo::exists(QDir(fi.absoluteFilePath()).filePath(previewFile))) {
                    wallItem.insert(QStringLiteral("previewUrl"), QUrl::fromLocalFile(QDir(fi.absoluteFilePath()).filePath(previewFile)).toString());
                }
                QSettings wallPropSettings(AppPaths::configFilePath(), QSettings::IniFormat);
                const QString savedPropsJson = wallPropSettings.value(QStringLiteral("LinWallpapers/%1/Properties").arg(id)).toString();
                QJsonObject savedPropsObj;
                if (!savedPropsJson.isEmpty()) {
                    savedPropsObj = QJsonDocument::fromJson(savedPropsJson.toUtf8()).object();
                }

                if (mObj.contains(QStringLiteral("customConfig")) || !savedPropsObj.isEmpty()) {
                    QJsonObject mergedCustomCfg = mObj.value(QStringLiteral("customConfig")).toObject();
                    const QStringList uniKeys = {
                        QStringLiteral("scaling"), QStringLiteral("dimming"), QStringLiteral("blur"),
                        QStringLiteral("volume"), QStringLiteral("muted"), QStringLiteral("playbackRate"),
                        QStringLiteral("audioDucking")
                    };
                    for (const QString& uk : uniKeys) {
                        if (savedPropsObj.contains(uk)) {
                            mergedCustomCfg.insert(uk, savedPropsObj.value(uk));
                        }
                    }
                    if (!mergedCustomCfg.isEmpty()) {
                        wallItem.insert(QStringLiteral("customConfig"), mergedCustomCfg);
                    }
                }
                const QVariant perWallInter = wallPropSettings.value(QStringLiteral("LinWallpapers/%1/Interactive").arg(id));
                wallItem.insert(QStringLiteral("interactive"), perWallInter.isValid() ? perWallInter.toBool() : mObj.value(QStringLiteral("interactive")).toBool(true));
                if (mObj.contains(QStringLiteral("properties"))) {
                    QJsonObject propsObj = mObj.value(QStringLiteral("properties")).toObject();
                    for (auto pit = propsObj.begin(); pit != propsObj.end(); ++pit) {
                        if (savedPropsObj.contains(pit.key()) && pit.value().isObject()) {
                            QJsonObject schemaObj = pit.value().toObject();
                            schemaObj.insert(QStringLiteral("value"), savedPropsObj.value(pit.key()));
                            pit.value() = schemaObj;
                        }
                    }
                    wallItem.insert(QStringLiteral("properties"), propsObj);
                }

                const QString cleanActiveId = m_activeWallpaperId.startsWith(QStringLiteral("wallpaper:"), Qt::CaseInsensitive)
                    ? m_activeWallpaperId.mid(10).trimmed()
                    : m_activeWallpaperId.trimmed();
                const bool isRunningWall = (!cleanActiveId.isEmpty() &&
                    (cleanActiveId.compare(id, Qt::CaseInsensitive) == 0 ||
                     cleanActiveId.compare(fi.fileName(), Qt::CaseInsensitive) == 0 ||
                     QDir::cleanPath(m_activeWallpaperPackagePath).compare(QDir::cleanPath(fi.absoluteFilePath()), Qt::CaseInsensitive) == 0));
                wallItem.insert(QStringLiteral("running"), isRunningWall);

                wallpapersArray.append(wallItem);
            }
        }

        // 2. Scan from registry entries (only if directory still exists on disk)
        for (const WidgetRegistryEntry& entry : registry.entries()) {
            if (entry.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0) {
                const QString normId = entry.id.trimmed().toLower();
                if (seenWallIds.contains(normId) || normId.contains(QStringLiteral("gargantua")) || normId.contains(QStringLiteral("blackhole"))) {
                    continue;
                }
                if (entry.packagePath.isEmpty() || !QFileInfo::exists(entry.packagePath + QStringLiteral("/manifest.json"))) {
                    continue;
                }
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
                     QDir::cleanPath(m_activeWallpaperPackagePath).compare(QDir::cleanPath(entry.packagePath), Qt::CaseInsensitive) == 0));
                wallObj.insert(QStringLiteral("running"), isAct);

                QString regPreview = QDir(entry.packagePath).filePath(QStringLiteral("preview.png"));
                if (!QFileInfo::exists(regPreview)) {
                    regPreview = QDir(entry.packagePath).filePath(QStringLiteral("preview.jpg"));
                }
                if (QFileInfo::exists(regPreview)) {
                    wallObj.insert(QStringLiteral("previewUrl"), QUrl::fromLocalFile(regPreview).toString());
                }

                wallpapersArray.append(wallObj);
            }
        }

        // 3. Load custom wallpapers from custom_wallpapers.json metadata registry
        const QJsonArray customWalls = loadCustomWallpapersArray();
        for (const QJsonValue& cv : customWalls) {
            QJsonObject cObj = cv.toObject();
            const QString id = cObj.value(QStringLiteral("id")).toString();
            const QString normId = id.trimmed().toLower();
            if (id.isEmpty() || seenWallIds.contains(normId)) {
                continue;
            }
            seenWallIds.insert(normId);

            const QString srcPath = cObj.value(QStringLiteral("sourcePath")).toString();
            const bool fileExists = !srcPath.isEmpty() && QFileInfo::exists(srcPath);
            cObj.insert(QStringLiteral("isMissing"), !fileExists);

            const QString cleanActiveId = m_activeWallpaperId.startsWith(QStringLiteral("wallpaper:"), Qt::CaseInsensitive)
                ? m_activeWallpaperId.mid(10).trimmed()
                : m_activeWallpaperId.trimmed();
            const bool isRunningWall = (!cleanActiveId.isEmpty() &&
                (cleanActiveId.compare(id, Qt::CaseInsensitive) == 0 ||
                 QDir::cleanPath(m_activeWallpaperPackagePath).compare(QDir::cleanPath(srcPath), Qt::CaseInsensitive) == 0));
            cObj.insert(QStringLiteral("running"), isRunningWall);

            wallpapersArray.append(cObj);
        }

        response.insert(QStringLiteral("wallpapers"), wallpapersArray);
        response.insert(QStringLiteral("runtimeEnabled"), m_enabled);
        response.insert(QStringLiteral("showBounds"), showWidgetBounds());
        response.insert(QStringLiteral("activeWallpaperId"), m_activeWallpaperId);
        response.insert(QStringLiteral("activeWallpaperPackagePath"), m_activeWallpaperPackagePath);
        response.insert(QStringLiteral("activeWallpaperInteractive"), m_activeWallpaperInteractive);
        response.insert(QStringLiteral("userWidgetsDir"), AppPaths::userLinWidgetsDir());
        response.insert(QStringLiteral("logDir"), AppPaths::userLogsDir());
        return response;
    }

    // --- Global Actions (do not require widgetId) ---
    if (action == QStringLiteral("open_widgets_dir") || action == QStringLiteral("open_widgets_folder")) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::userLinWidgetsDir()));
        response.insert(QStringLiteral("ok"), true);
        return response;
    }

    if (action == QStringLiteral("open_log_folder") || action == QStringLiteral("open_logs_folder")) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::userLogsDir()));
        response.insert(QStringLiteral("ok"), true);
        return response;
    }

    if (action == QStringLiteral("set_master_enabled")) {
        const bool enabled = request.value(QStringLiteral("enabled")).toBool(true);
        setEnabled(enabled);
        QSettings s(AppPaths::configFilePath(), QSettings::IniFormat);
        s.setValue(QStringLiteral("LinWidgets/Enabled"), enabled);
        s.sync();
        if (enabled) {
            QVector<WidgetLayoutState> currentStates = filterWallpapers(m_layout.states());
            if (currentStates.isEmpty()) {
                ConfigManager::loadLayout(ConfigManager::defaultConfigPath(), &currentStates, nullptr);
                currentStates = filterWallpapers(currentStates);
            }
            bool anyVisible = false;
            for (const WidgetLayoutState& st : currentStates) {
                if (st.visible) {
                    anyVisible = true;
                    break;
                }
            }
            if (!anyVisible && !currentStates.isEmpty()) {
                for (WidgetLayoutState& st : currentStates) {
                    st.visible = true;
                }
                ConfigManager::saveLayout(ConfigManager::defaultConfigPath(), deduplicatedLayoutStates(currentStates), nullptr);
            }
            restoreFromConfig();

            bool hasRunningSmallWidget = false;
            for (const QString& runId : m_runningWidgets) {
                if (!runId.startsWith(QStringLiteral("wallpaper:"))) {
                    hasRunningSmallWidget = true;
                    break;
                }
            }
            if (!hasRunningSmallWidget) {
                for (const WidgetRegistryEntry& entry : registry.entries()) {
                    if (entry.enabled &&
                        entry.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) != 0 &&
                        QFileInfo::exists(entry.packagePath)) {
                        const QSize defSize = (entry.defaultSize.isValid() && entry.defaultSize.width() > 0)
                            ? entry.defaultSize
                            : QSize(340, 240);
                        const QPoint launchPos(240 + (m_runningWidgets.size() * 32) % 320,
                                               180 + (m_runningWidgets.size() * 32) % 240);
                        launchWidgetPackage(entry.packagePath, QRect(launchPos, defSize), entry.isDevelopment, entry.autoReload, nullptr, nullptr);
                    }
                }
            }
        }
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("enabled"), enabled);
        return response;
    }

    if (action == QStringLiteral("set_show_bounds")) {
        const bool showBounds = request.value(QStringLiteral("showBounds")).toBool(false);
        setShowWidgetBounds(showBounds);
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("showBounds"), showBounds);
        return response;
    }

    if (action == QStringLiteral("stop_wallpaper")) {
        stopWallpaper(true);
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("activeWallpaperId"), QString());
        return response;
    }

    if (action == QStringLiteral("create_project")) {
        const QString rawName = request.value(QStringLiteral("name")).toString(QStringLiteral("MyWidget")).trimmed();
        QString safeName = rawName;
        safeName.replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_-]")), QStringLiteral("_"));
        if (safeName.isEmpty()) safeName = QStringLiteral("MyWidget");
        const QString targetDir = QDir(AppPaths::userLinWidgetsDir()).filePath(safeName);
        QDir().mkpath(targetDir);

        QJsonObject manifestObj;
        manifestObj.insert(QStringLiteral("id"), safeName.toLower());
        manifestObj.insert(QStringLiteral("name"), rawName);
        manifestObj.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
        manifestObj.insert(QStringLiteral("description"), QStringLiteral("用户自定义 LinWidget 桌面组件"));
        manifestObj.insert(QStringLiteral("entry"), QStringLiteral("index.html"));
        manifestObj.insert(QStringLiteral("width"), 320);
        manifestObj.insert(QStringLiteral("height"), 220);
        manifestObj.insert(QStringLiteral("role"), QStringLiteral("widget"));
        QJsonArray perms; perms.append(QStringLiteral("storage")); perms.append(QStringLiteral("systemInfo"));
        manifestObj.insert(QStringLiteral("permissions"), perms);

        QFile mFile(QDir(targetDir).filePath(QStringLiteral("manifest.json")));
        if (mFile.open(QIODevice::WriteOnly)) {
            mFile.write(QJsonDocument(manifestObj).toJson(QJsonDocument::Indented));
            mFile.close();
        }

        QFile hFile(QDir(targetDir).filePath(QStringLiteral("index.html")));
        if (hFile.open(QIODevice::WriteOnly)) {
            const QString htmlContent = QStringLiteral(
                "<!DOCTYPE html>\n<html>\n<head>\n"
                "  <meta charset=\"utf-8\">\n"
                "  <style>\n"
                "    body { margin: 0; padding: 16px; font-family: system-ui; background: rgba(15,23,42,0.88); color: #fff; border-radius: 12px; border: 1px solid rgba(255,255,255,0.12); backdrop-filter: blur(16px); box-sizing: border-box; height: 100vh; display: flex; flex-direction: column; justify-content: center; align-items: center; text-align: center; }\n"
                "    h2 { margin: 0 0 6px 0; font-size: 16px; font-weight: 600; }\n"
                "    p { margin: 0; font-size: 11px; color: #94a3b8; }\n"
                "    .time { font-size: 26px; font-weight: 700; margin: 10px 0; color: #38bdf8; font-variant-numeric: tabular-nums; }\n"
                "  </style>\n</head>\n<body>\n"
                "  <h2>%1</h2>\n"
                "  <div class=\"time\" id=\"clock\">00:00:00</div>\n"
                "  <p>LinWidget 桌面小组件已就绪</p>\n"
                "  <script>\n"
                "    function update() { document.getElementById('clock').textContent = new Date().toLocaleTimeString(); }\n"
                "    setInterval(update, 1000); update();\n"
                "  </script>\n</body>\n</html>\n"
            ).arg(rawName);
            hFile.write(htmlContent.toUtf8());
            hFile.close();
        }

        registry.addDevelopmentProject(targetDir);
        registry.save();
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("packagePath"), targetDir);
        response.insert(QStringLiteral("id"), safeName.toLower());
        return response;
    }

    if (action == QStringLiteral("add_development")) {
        const QString path = request.value(QStringLiteral("path")).toString().trimmed();
        if (path.isEmpty() || !QDir(path).exists()) {
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), QStringLiteral("Directory does not exist."));
            return response;
        }
        WidgetManifest devManifest;
        QString manifestErr;
        if (!WidgetPackage::loadManifest(path, &devManifest, &manifestErr)) {
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), manifestErr);
            return response;
        }
        registry.addDevelopmentProject(path);
        registry.save();
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("id"), devManifest.id);
        return response;
    }

    if (action == QStringLiteral("stage_wallpaper_media") ||
        action == QStringLiteral("import_wallpaper_media") ||
        action == QStringLiteral("import_external_paths")) {
        QStringList candidatePaths;
        if (request.contains(QStringLiteral("paths")) && request.value(QStringLiteral("paths")).isArray()) {
            const QJsonArray arr = request.value(QStringLiteral("paths")).toArray();
            for (const QJsonValue& val : arr) {
                const QString p = val.toString().trimmed();
                if (!p.isEmpty()) candidatePaths.append(p);
            }
        }
        const QString singlePath = request.value(QStringLiteral("filePath")).toString(
            request.value(QStringLiteral("path")).toString()).trimmed();
        if (!singlePath.isEmpty() && !candidatePaths.contains(singlePath)) {
            candidatePaths.append(singlePath);
        }

        if (candidatePaths.isEmpty()) {
            const QString filter = QStringLiteral("壁纸媒体文件 (*.mp4 *.webm *.mov *.mkv *.avi *.png *.jpg *.jpeg *.webp *.gif *.bmp *.svg);;视频文件 (*.mp4 *.webm *.mov *.mkv *.avi);;图片文件 (*.png *.jpg *.jpeg *.webp *.gif *.bmp *.svg)");
            const QString chosen = QFileDialog::getOpenFileName(
                QApplication::activeWindow(),
                QStringLiteral("选择要导入的壁纸视频或图片媒体"),
                QString(),
                filter);
            if (chosen.isEmpty()) {
                response.insert(QStringLiteral("action"), QStringLiteral("stage_wallpaper_media"));
                response.insert(QStringLiteral("ok"), false);
                response.insert(QStringLiteral("cancelled"), true);
                return response;
            }
            candidatePaths.append(chosen);
        }

        QJsonArray customWalls = loadCustomWallpapersArray();
        QJsonObject lastImportedObj;

        for (QString candidate : candidatePaths) {
            QFileInfo fi(candidate);
            if (fi.isSymLink()) {
                const QString symTarget = fi.symLinkTarget();
                if (!symTarget.isEmpty() && QFileInfo::exists(symTarget)) {
                    candidate = symTarget;
                    fi = QFileInfo(candidate);
                }
            }
            if (!fi.exists() || !fi.isFile()) {
                continue;
            }

            const QString ext = fi.suffix().toLower();
            const bool isVideo = (ext == QStringLiteral("mp4") || ext == QStringLiteral("webm") ||
                                  ext == QStringLiteral("mov") || ext == QStringLiteral("mkv") ||
                                  ext == QStringLiteral("avi"));
            const bool isImage = (ext == QStringLiteral("png") || ext == QStringLiteral("jpg") ||
                                  ext == QStringLiteral("jpeg") || ext == QStringLiteral("webp") ||
                                  ext == QStringLiteral("gif") || ext == QStringLiteral("bmp") ||
                                  ext == QStringLiteral("svg"));
            if (!isVideo && !isImage) {
                continue;
            }

            const QString wallUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
            const QString generatedId = QStringLiteral("custom.") + wallUuid;
            const QString previewPath = QDir(customWallpapersPreviewsDir()).filePath(generatedId + QStringLiteral(".png"));

            QString finalSourcePath = fi.absoluteFilePath();
            bool isArchived = false;

            if (isImage) {
                // 小图片自动归档到 LinWallpapers/images
                const QString destImage = QDir(customWallpapersImagesDir()).filePath(generatedId + QStringLiteral(".") + ext);
                if (QFile::copy(finalSourcePath, destImage)) {
                    finalSourcePath = destImage;
                    isArchived = true;
                }
                QImage img(finalSourcePath);
                if (!img.isNull()) {
                    QImage thumb = img.scaled(480, 270, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                    QRect cropRect((thumb.width() - 480) / 2, (thumb.height() - 270) / 2, 480, 270);
                    thumb.copy(cropRect).save(previewPath, "PNG");
                }
            } else {
                // 大视频默认零拷贝就地引用，秒级返回即时海报，后台异步提取真实代表帧
                isArchived = false;

                // 1. 瞬时生成并保存高质感暗色占位海报（0ms 阻塞）
                QImage poster(480, 270, QImage::Format_ARGB32_Premultiplied);
                poster.fill(QColor(15, 23, 42));
                QPainter p(&poster);
                p.setRenderHint(QPainter::Antialiasing);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(99, 102, 241, 220));
                p.drawEllipse(QPoint(240, 120), 28, 28);
                p.setPen(QColor(241, 245, 249));
                p.drawText(QRect(20, 165, 440, 40), Qt::AlignCenter | Qt::TextWordWrap, fi.completeBaseName());
                p.end();
                poster.save(previewPath, "PNG");

                // 2. 线程池后台异步提取视频原画帧，覆盖预览海报，绝不卡死主窗口
                const QString asyncSrc = finalSourcePath;
                const QString asyncPreview = previewPath;
                QThreadPool::globalInstance()->start([asyncSrc, asyncPreview]() {
                    const QImage realThumb = WinMfVideoEngine::extractThumbnail(asyncSrc, QSize(480, 270));
                    if (!realThumb.isNull()) {
                        realThumb.save(asyncPreview, "PNG");
                    }
                });
            }

            QJsonObject defaultCfg;
            defaultCfg.insert(QStringLiteral("scaling"), QStringLiteral("cover"));
            defaultCfg.insert(QStringLiteral("dimming"), 15);
            defaultCfg.insert(QStringLiteral("blur"), 0);
            defaultCfg.insert(QStringLiteral("volume"), 80);
            defaultCfg.insert(QStringLiteral("muted"), false);
            defaultCfg.insert(QStringLiteral("playbackRate"), 1.0);
            defaultCfg.insert(QStringLiteral("audioDucking"), true);

            QJsonObject wallObj;
            wallObj.insert(QStringLiteral("id"), generatedId);
            wallObj.insert(QStringLiteral("name"), fi.completeBaseName());
            wallObj.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
            wallObj.insert(QStringLiteral("author"), QStringLiteral("User Custom"));
            wallObj.insert(QStringLiteral("description"), isVideo ? QStringLiteral("自定义视频壁纸") : QStringLiteral("自定义高清静态壁纸"));
            wallObj.insert(QStringLiteral("role"), QStringLiteral("wallpaper"));
            wallObj.insert(QStringLiteral("wallpaperType"), isVideo ? QStringLiteral("video") : QStringLiteral("image"));
            wallObj.insert(QStringLiteral("isCustom"), true);
            wallObj.insert(QStringLiteral("sourcePath"), finalSourcePath);
            wallObj.insert(QStringLiteral("packagePath"), finalSourcePath);
            wallObj.insert(QStringLiteral("mediaUrl"), QUrl::fromLocalFile(finalSourcePath).toString());
            wallObj.insert(QStringLiteral("previewUrl"), QUrl::fromLocalFile(previewPath).toString());
            wallObj.insert(QStringLiteral("isArchived"), isArchived);
            wallObj.insert(QStringLiteral("fileSizeBytes"), fi.size());
            wallObj.insert(QStringLiteral("createdAt"), QDateTime::currentMSecsSinceEpoch());
            wallObj.insert(QStringLiteral("customConfig"), defaultCfg);
            wallObj.insert(QStringLiteral("running"), false);
            wallObj.insert(QStringLiteral("isMissing"), false);

            customWalls.prepend(wallObj);
            lastImportedObj = wallObj;
        }

        if (!lastImportedObj.isEmpty()) {
            saveCustomWallpapersArray(customWalls);
            response.insert(QStringLiteral("action"), QStringLiteral("stage_wallpaper_media"));
            response.insert(QStringLiteral("ok"), true);
            response.insert(QStringLiteral("wallpaper"), lastImportedObj);
            response.insert(QStringLiteral("id"), lastImportedObj.value(QStringLiteral("id")).toString());
            response.insert(QStringLiteral("name"), lastImportedObj.value(QStringLiteral("name")).toString());
            response.insert(QStringLiteral("filePath"), lastImportedObj.value(QStringLiteral("sourcePath")).toString());
            response.insert(QStringLiteral("fileUrl"), lastImportedObj.value(QStringLiteral("mediaUrl")).toString());
            response.insert(QStringLiteral("wallpaperType"), lastImportedObj.value(QStringLiteral("wallpaperType")).toString());
            return response;
        }

        response.insert(QStringLiteral("action"), QStringLiteral("stage_wallpaper_media"));
        response.insert(QStringLiteral("ok"), false);
        response.insert(QStringLiteral("error"), QStringLiteral("仅支持拖入图片 (PNG/JPG/WebP/GIF) 或视频 (MP4/WebM/MOV) 文件"));
        return response;
    }

    if (action == QStringLiteral("archive_wallpaper_media")) {
        const QString targetId = request.value(QStringLiteral("id")).toString(
            request.value(QStringLiteral("widgetId")).toString()).trimmed();
        QJsonArray customWalls = loadCustomWallpapersArray();
        bool found = false;
        QString newPath;
        for (int i = 0; i < customWalls.size(); ++i) {
            QJsonObject wObj = customWalls[i].toObject();
            if (wObj.value(QStringLiteral("id")).toString().compare(targetId, Qt::CaseInsensitive) == 0) {
                const QString src = wObj.value(QStringLiteral("sourcePath")).toString();
                if (QFileInfo::exists(src)) {
                    QFileInfo fi(src);
                    const QString dest = QDir(customWallpapersVideosDir()).filePath(targetId + QStringLiteral(".") + fi.suffix().toLower());
                    if (QFile::copy(src, dest) || (src.compare(dest, Qt::CaseInsensitive) == 0)) {
                        newPath = dest;
                        wObj.insert(QStringLiteral("sourcePath"), dest);
                        wObj.insert(QStringLiteral("packagePath"), dest);
                        wObj.insert(QStringLiteral("mediaUrl"), QUrl::fromLocalFile(dest).toString());
                        wObj.insert(QStringLiteral("isArchived"), true);
                        customWalls[i] = wObj;
                        found = true;
                        if (m_activeWallpaperId.contains(targetId, Qt::CaseInsensitive)) {
                            m_activeWallpaperPackagePath = dest;
                        }
                    }
                }
                break;
            }
        }
        if (found) {
            saveCustomWallpapersArray(customWalls);
            response.insert(QStringLiteral("action"), QStringLiteral("archive_wallpaper_media"));
            response.insert(QStringLiteral("ok"), true);
            response.insert(QStringLiteral("id"), targetId);
            response.insert(QStringLiteral("isArchived"), true);
            response.insert(QStringLiteral("sourcePath"), newPath);
            return response;
        }
        response.insert(QStringLiteral("action"), QStringLiteral("archive_wallpaper_media"));
        response.insert(QStringLiteral("ok"), false);
        response.insert(QStringLiteral("error"), QStringLiteral("未找到该壁纸或源文件无法复制"));
        return response;
    }

    if (action == QStringLiteral("relocate_wallpaper_media")) {
        const QString targetId = request.value(QStringLiteral("id")).toString(
            request.value(QStringLiteral("widgetId")).toString()).trimmed();
        QJsonArray customWalls = loadCustomWallpapersArray();
        int targetIdx = -1;
        QJsonObject targetObj;
        for (int i = 0; i < customWalls.size(); ++i) {
            const QJsonObject wObj = customWalls[i].toObject();
            if (wObj.value(QStringLiteral("id")).toString().compare(targetId, Qt::CaseInsensitive) == 0) {
                targetIdx = i;
                targetObj = wObj;
                break;
            }
        }
        if (targetIdx < 0) {
            response.insert(QStringLiteral("action"), QStringLiteral("relocate_wallpaper_media"));
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), QStringLiteral("未找到该自定义壁纸"));
            return response;
        }

        QString chosenPath = request.value(QStringLiteral("filePath")).toString().trimmed();
        const QString wallType = targetObj.value(QStringLiteral("wallpaperType")).toString().toLower();
        const bool isVideo = (wallType == QStringLiteral("video"));

        if (chosenPath.isEmpty()) {
            QWidget* parentWin = QApplication::activeWindow();
            const QString filter = isVideo
                ? QStringLiteral("视频文件 (*.mp4 *.webm *.mov *.avi *.mkv *.flv);;所有文件 (*.*)")
                : QStringLiteral("图片文件 (*.png *.jpg *.jpeg *.webp *.bmp *.gif);;所有文件 (*.*)");
            const QString title = isVideo
                ? QStringLiteral("重新选择视频壁纸源文件")
                : QStringLiteral("重新选择图片壁纸源文件");

            chosenPath = QFileDialog::getOpenFileName(parentWin, title, QString(), filter);
        }

        if (chosenPath.isEmpty()) {
            response.insert(QStringLiteral("action"), QStringLiteral("relocate_wallpaper_media"));
            response.insert(QStringLiteral("cancelled"), true);
            return response;
        }

        if (!QFileInfo::exists(chosenPath)) {
            response.insert(QStringLiteral("action"), QStringLiteral("relocate_wallpaper_media"));
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), QStringLiteral("选择的文件不存在"));
            return response;
        }

        QFileInfo fi(chosenPath);
        const QString ext = fi.suffix().toLower();
        static const QSet<QString> videoExts = { QStringLiteral("mp4"), QStringLiteral("webm"), QStringLiteral("mov"),
                                                 QStringLiteral("avi"), QStringLiteral("mkv"), QStringLiteral("flv") };
        static const QSet<QString> imageExts = { QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                                 QStringLiteral("webp"), QStringLiteral("bmp"), QStringLiteral("gif") };

        if (isVideo && !videoExts.contains(ext)) {
            response.insert(QStringLiteral("action"), QStringLiteral("relocate_wallpaper_media"));
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), QStringLiteral("选择的文件不是有效的视频格式"));
            return response;
        }
        if (!isVideo && !imageExts.contains(ext)) {
            response.insert(QStringLiteral("action"), QStringLiteral("relocate_wallpaper_media"));
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), QStringLiteral("选择的文件不是有效的图片格式"));
            return response;
        }

        const QString nativePath = QDir::toNativeSeparators(fi.absoluteFilePath());
        targetObj.insert(QStringLiteral("sourcePath"), nativePath);
        targetObj.insert(QStringLiteral("packagePath"), nativePath);
        targetObj.insert(QStringLiteral("mediaUrl"), QUrl::fromLocalFile(nativePath).toString());
        targetObj.insert(QStringLiteral("fileSizeBytes"), static_cast<double>(fi.size()));
        targetObj.insert(QStringLiteral("isMissing"), false);

        const QString normPath = nativePath.toLower();
        const QString archiveVideoDir = customWallpapersVideosDir().toLower();
        const QString archiveImageDir = customWallpapersImagesDir().toLower();
        const bool isInArchive = normPath.startsWith(archiveVideoDir) || normPath.startsWith(archiveImageDir);
        targetObj.insert(QStringLiteral("isArchived"), isInArchive);

        if (isVideo) {
            const QString previewPath = QDir(customWallpapersPreviewsDir()).filePath(targetId + QStringLiteral(".jpg"));
            const QImage realThumb = WinMfVideoEngine::extractThumbnail(nativePath, QSize(480, 270));
            if (!realThumb.isNull()) {
                realThumb.save(previewPath, "JPG", 85);
                targetObj.insert(QStringLiteral("previewUrl"), QUrl::fromLocalFile(previewPath).toString());
                targetObj.insert(QStringLiteral("thumbnail"), QUrl::fromLocalFile(previewPath).toString());
            }
        } else {
            targetObj.insert(QStringLiteral("previewUrl"), QUrl::fromLocalFile(nativePath).toString());
            targetObj.insert(QStringLiteral("thumbnail"), QUrl::fromLocalFile(nativePath).toString());
        }

        customWalls[targetIdx] = targetObj;
        saveCustomWallpapersArray(customWalls);

        if (m_activeWallpaperId.contains(targetId, Qt::CaseInsensitive)) {
            m_activeWallpaperPackagePath = nativePath;
            startWallpaper(nativePath, false, m_activeWallpaperInteractive, nullptr);
        }

        response.insert(QStringLiteral("action"), QStringLiteral("relocate_wallpaper_media"));
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("id"), targetId);
        response.insert(QStringLiteral("sourcePath"), nativePath);
        response.insert(QStringLiteral("name"), targetObj.value(QStringLiteral("name")).toString());
        response.insert(QStringLiteral("fileSizeBytes"), static_cast<double>(fi.size()));
        response.insert(QStringLiteral("previewUrl"), targetObj.value(QStringLiteral("previewUrl")).toString());
        return response;
    }

    if (action == QStringLiteral("confirm_wallpaper_import") || action == QStringLiteral("update_wallpaper_config")) {
        const QString targetId = request.value(QStringLiteral("editWallpaperId")).toString(
            request.value(QStringLiteral("id")).toString(request.value(QStringLiteral("widgetId")).toString())).trimmed();

        QJsonObject cfg;
        cfg.insert(QStringLiteral("scaling"), request.value(QStringLiteral("scaling")).toString(QStringLiteral("cover")));
        cfg.insert(QStringLiteral("dimming"), request.value(QStringLiteral("dimming")).toInt(15));
        cfg.insert(QStringLiteral("blur"), request.value(QStringLiteral("blur")).toInt(0));
        cfg.insert(QStringLiteral("volume"), request.value(QStringLiteral("volume")).toInt(80));
        cfg.insert(QStringLiteral("muted"), request.value(QStringLiteral("muted")).toBool(false));
        cfg.insert(QStringLiteral("playbackRate"), request.value(QStringLiteral("playbackRate")).toDouble(1.0));
        cfg.insert(QStringLiteral("audioDucking"), request.value(QStringLiteral("audioDucking")).toBool(true));

        QJsonArray customWalls = loadCustomWallpapersArray();
        bool found = false;
        for (int i = 0; i < customWalls.size(); ++i) {
            QJsonObject wObj = customWalls[i].toObject();
            if (wObj.value(QStringLiteral("id")).toString().compare(targetId, Qt::CaseInsensitive) == 0) {
                wObj.insert(QStringLiteral("customConfig"), cfg);
                if (request.contains(QStringLiteral("name"))) {
                    const QString n = request.value(QStringLiteral("name")).toString().trimmed();
                    if (!n.isEmpty()) wObj.insert(QStringLiteral("name"), n);
                }
                customWalls[i] = wObj;
                found = true;
                break;
            }
        }
        if (found) {
            saveCustomWallpapersArray(customWalls);
            const bool applyImmediately = request.value(QStringLiteral("applyImmediately")).toBool(false);
            if (applyImmediately) {
                QString wallErr;
                startWallpaper(targetId, false, true, &wallErr);
            } else if (m_activeWallpaperId.contains(targetId, Qt::CaseInsensitive)) {
                if (m_activeWallpaperType == QStringLiteral("image")) {
                    m_activeImageConfig.scaling = cfg.value(QStringLiteral("scaling")).toString();
                    m_activeImageConfig.dimming = cfg.value(QStringLiteral("dimming")).toInt();
                    m_activeImageConfig.blur = cfg.value(QStringLiteral("blur")).toInt();
                    renderActiveImageWallpaper();
                } else if (WinMfVideoEngine::instance().isDesktopVideoRunning()) {
                    WinMfVideoEngine::instance().updateDesktopProperty(QStringLiteral("scaling"), cfg.value(QStringLiteral("scaling")).toString());
                    WinMfVideoEngine::instance().updateDesktopProperty(QStringLiteral("dimming"), cfg.value(QStringLiteral("dimming")).toInt());
                    WinMfVideoEngine::instance().updateDesktopProperty(QStringLiteral("blur"), cfg.value(QStringLiteral("blur")).toInt());
                    WinMfVideoEngine::instance().updateDesktopProperty(QStringLiteral("volume"), cfg.value(QStringLiteral("volume")).toInt());
                    WinMfVideoEngine::instance().updateDesktopProperty(QStringLiteral("muted"), cfg.value(QStringLiteral("muted")).toBool());
                    WinMfVideoEngine::instance().updateDesktopProperty(QStringLiteral("playbackRate"), cfg.value(QStringLiteral("playbackRate")).toDouble());
                }
            }
            response.insert(QStringLiteral("action"), QStringLiteral("confirm_wallpaper_import"));
            response.insert(QStringLiteral("ok"), true);
            response.insert(QStringLiteral("id"), targetId);
            response.insert(QStringLiteral("applied"), applyImmediately);
            response.insert(QStringLiteral("activeWallpaperId"), m_activeWallpaperId);
            return response;
        }

        response.insert(QStringLiteral("action"), QStringLiteral("confirm_wallpaper_import"));
        response.insert(QStringLiteral("ok"), true);
        return response;
    }

    if (action == QStringLiteral("delete_wallpaper")) {
        const QString targetId = request.value(QStringLiteral("widgetId")).toString(
            request.value(QStringLiteral("id")).toString()).trimmed();

        QJsonArray customWalls = loadCustomWallpapersArray();
        bool found = false;
        QJsonArray nextArr;
        for (int i = 0; i < customWalls.size(); ++i) {
            const QJsonObject wObj = customWalls[i].toObject();
            const QString id = wObj.value(QStringLiteral("id")).toString();
            if (id.compare(targetId, Qt::CaseInsensitive) == 0) {
                found = true;
                const bool isArchived = wObj.value(QStringLiteral("isArchived")).toBool();
                const QString src = wObj.value(QStringLiteral("sourcePath")).toString();
                if (isArchived && !src.isEmpty() && QFileInfo::exists(src)) {
                    QFile::remove(src);
                }
                const QString previewPath = QDir(customWallpapersPreviewsDir()).filePath(id + QStringLiteral(".png"));
                if (QFileInfo::exists(previewPath)) {
                    QFile::remove(previewPath);
                }
            } else {
                nextArr.append(wObj);
            }
        }

        if (found) {
            saveCustomWallpapersArray(nextArr);
            if (!m_activeWallpaperId.isEmpty() && m_activeWallpaperId.contains(targetId, Qt::CaseInsensitive)) {
                stopWallpaper(true);
            }
            response.insert(QStringLiteral("action"), QStringLiteral("delete_wallpaper"));
            response.insert(QStringLiteral("ok"), true);
            response.insert(QStringLiteral("id"), targetId);
            response.insert(QStringLiteral("activeWallpaperId"), m_activeWallpaperId);
            return response;
        }

        response.insert(QStringLiteral("action"), QStringLiteral("delete_wallpaper"));
        response.insert(QStringLiteral("ok"), false);
        response.insert(QStringLiteral("error"), QStringLiteral("未找到该自定义壁纸"));
        return response;
    }

    if (action == QStringLiteral("import_dev_project")) {
        QString dirPath = request.value(QStringLiteral("path")).toString().trimmed();
        if (dirPath.isEmpty()) {
            dirPath = QFileDialog::getExistingDirectory(
                QApplication::activeWindow(),
                QStringLiteral("选择桌面组件开发工程目录 (包含 manifest.json)"),
                AppPaths::userLinWidgetsDir());
        }
        if (dirPath.isEmpty()) {
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("cancelled"), true);
            return response;
        }
        WidgetManifest devManifest;
        QString manifestErr;
        if (!WidgetPackage::loadManifest(dirPath, &devManifest, &manifestErr)) {
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), manifestErr.isEmpty() ? QStringLiteral("工程目录缺少有效的 manifest.json") : manifestErr);
            return response;
        }
        registry.addDevelopmentProject(dirPath);
        registry.save();
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("id"), devManifest.id);
        response.insert(QStringLiteral("name"), devManifest.name);
        return response;
    }

    if (action == QStringLiteral("set_wallpaper_prop") || action == QStringLiteral("set_wallpaper_property")) {
        const QString key = request.contains(QStringLiteral("key"))
            ? request.value(QStringLiteral("key")).toString().trimmed()
            : request.value(QStringLiteral("property")).toString().trimmed();
        const QJsonValue val = request.value(QStringLiteral("value"));

        QString targetWallId = request.value(QStringLiteral("widgetId")).toString(
            request.value(QStringLiteral("id")).toString()).trimmed();
        if (targetWallId.isEmpty() && !m_activeWallpaperId.isEmpty()) {
            const WidgetInstance inst = m_instances.value(m_activeWallpaperId);
            targetWallId = !inst.manifest.id.isEmpty() ? inst.manifest.id : m_activeWallpaperId;
        }
        if (targetWallId.startsWith(QStringLiteral("wallpaper:"), Qt::CaseInsensitive)) {
            targetWallId = targetWallId.mid(10).trimmed();
        }

        if (key == QStringLiteral("interactive")) {
            setWallpaperInteractive(val.toBool(true));
            if (!targetWallId.isEmpty()) {
                QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
                settings.setValue(QStringLiteral("LinWallpapers/%1/Interactive").arg(targetWallId), val.toBool(true));
                settings.sync();
            }
        } else if (key == QStringLiteral("reset_defaults")) {
            if (!targetWallId.isEmpty()) {
                QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
                settings.remove(QStringLiteral("LinWallpapers/%1/Properties").arg(targetWallId));
                settings.remove(QStringLiteral("LinWallpapers/%1/Interactive").arg(targetWallId));
                settings.sync();
            }
        } else if (!key.isEmpty() && !targetWallId.isEmpty()) {
            QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
            const QString propSettingKey = QStringLiteral("LinWallpapers/%1/Properties").arg(targetWallId);
            QJsonObject savedProps = QJsonDocument::fromJson(settings.value(propSettingKey).toString().toUtf8()).object();
            savedProps.insert(key, val);
            settings.setValue(propSettingKey, QString::fromUtf8(QJsonDocument(savedProps).toJson(QJsonDocument::Compact)));
            settings.sync();

            // 若是用户导入的自定义壁纸，同步更新 custom_wallpapers.json
            QJsonArray customWalls = loadCustomWallpapersArray();
            bool cFound = false;
            for (int i = 0; i < customWalls.size(); ++i) {
                QJsonObject wObj = customWalls[i].toObject();
                if (wObj.value(QStringLiteral("id")).toString().compare(targetWallId, Qt::CaseInsensitive) == 0) {
                    QJsonObject cfg = wObj.value(QStringLiteral("customConfig")).toObject();
                    cfg.insert(key, val);
                    wObj.insert(QStringLiteral("customConfig"), cfg);
                    customWalls[i] = wObj;
                    cFound = true;
                    break;
                }
            }
            if (cFound) {
                saveCustomWallpapersArray(customWalls);
            }
        }

        const QString cleanActive = m_activeWallpaperId.startsWith(QStringLiteral("wallpaper:"), Qt::CaseInsensitive)
            ? m_activeWallpaperId.mid(10).trimmed()
            : m_activeWallpaperId.trimmed();
        if (!m_activeWallpaperId.isEmpty() &&
            (targetWallId.isEmpty() || cleanActive.compare(targetWallId, Qt::CaseInsensitive) == 0)) {
            if (m_activeWallpaperType == QStringLiteral("image")) {
                if (key == QStringLiteral("scaling") || key == QStringLiteral("fitMode")) m_activeImageConfig.scaling = val.toString();
                else if (key == QStringLiteral("dimming")) m_activeImageConfig.dimming = val.toInt();
                else if (key == QStringLiteral("blur")) m_activeImageConfig.blur = val.toInt();
                renderActiveImageWallpaper();
            } else if (WinMfVideoEngine::instance().isDesktopVideoRunning()) {
                WinMfVideoEngine::instance().updateDesktopProperty(key, val.toVariant());
            } else {
                QJsonObject propMsg;
                propMsg.insert(QStringLiteral("type"), QStringLiteral("wallpaper_property_change"));
                propMsg.insert(QStringLiteral("key"), key);
                propMsg.insert(QStringLiteral("property"), key);
                propMsg.insert(QStringLiteral("value"), val);
                sendMessageToWidget(m_activeWallpaperId, propMsg);
            }
        }
        response.insert(QStringLiteral("ok"), true);
        return response;
    }

    const QString rawTargetId = request.value(QStringLiteral("widgetId")).toString(
        request.value(QStringLiteral("id")).toString(
            request.value(QStringLiteral("instanceId")).toString()));
    const QString resolvedId = resolveWidgetId(rawTargetId);
    if (resolvedId.isEmpty()) {
        response.insert(QStringLiteral("ok"), false);
        response.insert(QStringLiteral("error"), QStringLiteral("Missing widgetId."));
        return response;
    }

    if (action == QStringLiteral("info")) {
        WidgetRegistryEntry entry = registry.entryById(resolvedId);
        if (entry.id.isEmpty()) {
            for (const WidgetInstance& inst : m_instances) {
                if (registryIdForManifest(inst.manifest) == resolvedId) {
                    entry.id = inst.manifest.id;
                    entry.name = inst.manifest.name;
                    entry.version = inst.manifest.version;
                    entry.author = inst.manifest.author;
                    entry.description = inst.manifest.description;
                    entry.packagePath = inst.packagePath;
                    entry.role = inst.manifest.role;
                    entry.isDevelopment = inst.isDevelopment;
                    entry.enabled = true;
                    entry.permissionsGranted = inst.grantedPermissions;
                    break;
                }
            }
        }
        if (entry.id.isEmpty()) {
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), QStringLiteral("Widget not found: %1").arg(resolvedId));
            return response;
        }
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("widget"), buildWidgetDetailJson(entry));
        return response;
    }

    if (action == QStringLiteral("set_permission")) {
        const QString perm = request.value(QStringLiteral("permission")).toString().trimmed().toLower();
        const bool granted = request.value(QStringLiteral("granted")).toBool(false);
        if (!m_permissions.isPermissionSupported(perm)) {
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), QStringLiteral("Unsupported permission: %1").arg(perm));
            return response;
        }
        WidgetRegistryEntry entry = registry.entryById(resolvedId);
        if (entry.id.isEmpty()) {
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), QStringLiteral("Widget not found in registry: %1").arg(resolvedId));
            return response;
        }
        QStringList nextPerms = m_permissions.normalizePermissions(entry.permissionsGranted);
        if (granted) {
            if (!nextPerms.contains(perm)) {
                nextPerms.append(perm);
            }
        } else {
            nextPerms.removeAll(perm);
        }
        registry.setPermissionsGranted(entry.id, nextPerms);
        registry.save();

        // Update all running instances immediately
        for (auto it = m_instances.begin(); it != m_instances.end(); ++it) {
            if (registryIdForManifest(it.value().manifest) == resolvedId) {
                it.value().grantedPermissions = nextPerms;
                if (perm == QStringLiteral("network")) {
                    if (auto* osr = m_osrBrowsers.value(it.key(), nullptr)) {
                        osr->setAllowNetwork(granted && it.value().manifest.permissions.contains(QStringLiteral("network")));
                    }
                }
            }
        }
        qInfo() << "[LinWidget][WidgetManager] Updated permission for widget:" << resolvedId
                << "permission:" << perm << "granted:" << granted;
        response.insert(QStringLiteral("ok"), true);
        QJsonArray grantedArr;
        for (const QString& p : nextPerms) {
            grantedArr.append(p);
        }
        response.insert(QStringLiteral("grantedPermissions"), grantedArr);
        return response;
    }

    if (action == QStringLiteral("clear_storage")) {
        const QString safeId = safeStorageSegment(resolvedId);
        const QStringList modes { QStringLiteral("installed"), QStringLiteral("dev") };
        for (const QString& mode : modes) {
            QDir widgetDataDir(QDir(AppPaths::linWidgetDataDir()).filePath(mode + QLatin1Char('/') + safeId));
            if (widgetDataDir.exists()) {
                widgetDataDir.removeRecursively();
                QDir(AppPaths::linWidgetDataDir()).mkpath(mode + QLatin1Char('/') + safeId);
            }
        }
        QJsonObject notifyMsg = makeWidgetMessage(QStringLiteral("storage_cleared"), resolvedId);
        broadcastToWidgetInstances(resolvedId, notifyMsg);
        qInfo() << "[LinWidget][WidgetManager] Cleared isolated storage data for widget:" << resolvedId;
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("storageUsedBytes"), 0);
        return response;
    }

    if (action == QStringLiteral("export_data")) {
        const QString safeId = safeStorageSegment(resolvedId);
        QJsonObject exportBundle;
        exportBundle.insert(QStringLiteral("widgetId"), resolvedId);
        exportBundle.insert(QStringLiteral("exportedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

        QJsonObject modesBundle;
        const QStringList modes { QStringLiteral("installed"), QStringLiteral("dev") };
        for (const QString& mode : modes) {
            const QDir modeDir(QDir(AppPaths::linWidgetDataDir()).filePath(mode + QLatin1Char('/') + safeId));
            if (!modeDir.exists()) {
                continue;
            }
            QJsonObject modeObj;
            QJsonObject widgetJson;
            if (loadStorageObject(modeDir.filePath(QStringLiteral("widget.json")), &widgetJson, nullptr) && !widgetJson.isEmpty()) {
                modeObj.insert(QStringLiteral("widget"), widgetJson);
            }
            const QDir instDir(modeDir.filePath(QStringLiteral("instances")));
            if (instDir.exists()) {
                QJsonObject instancesObj;
                const QFileInfoList files = instDir.entryInfoList({ QStringLiteral("*.json") }, QDir::Files);
                for (const QFileInfo& fi : files) {
                    QJsonObject instData;
                    if (loadStorageObject(fi.absoluteFilePath(), &instData, nullptr)) {
                        instancesObj.insert(fi.completeBaseName(), instData);
                    }
                }
                if (!instancesObj.isEmpty()) {
                    modeObj.insert(QStringLiteral("instances"), instancesObj);
                }
            }
            if (!modeObj.isEmpty()) {
                modesBundle.insert(mode, modeObj);
            }
        }
        exportBundle.insert(QStringLiteral("storage"), modesBundle);

        const QString targetPath = request.value(QStringLiteral("targetPath")).toString().trimmed();
        if (!targetPath.isEmpty()) {
            QDir().mkpath(QFileInfo(targetPath).absolutePath());
            QSaveFile saveFile(targetPath);
            if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                response.insert(QStringLiteral("ok"), false);
                response.insert(QStringLiteral("error"), saveFile.errorString());
                return response;
            }
            saveFile.write(QJsonDocument(exportBundle).toJson(QJsonDocument::Indented));
            if (!saveFile.commit()) {
                response.insert(QStringLiteral("ok"), false);
                response.insert(QStringLiteral("error"), saveFile.errorString());
                return response;
            }
            response.insert(QStringLiteral("exportPath"), targetPath);
        }
        qInfo() << "[LinWidget][WidgetManager] Exported widget storage data for:" << resolvedId << "targetPath:" << targetPath;
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("data"), exportBundle);
        return response;
    }

    if (action == QStringLiteral("restart")) {
        m_crashTimestamps.remove(resolvedId);
        m_crashTrippedWidgets.remove(resolvedId);
        int restartedCount = 0;

        if (!m_activeWallpaperId.isEmpty() &&
            m_activeWallpaperId.compare(QStringLiteral("wallpaper:") + resolvedId, Qt::CaseInsensitive) == 0) {
            if (restartWidget(m_activeWallpaperId)) {
                ++restartedCount;
            }
        }

        for (const WidgetLayoutState& state : m_layout.states()) {
            if (stateRegistryId(state) == resolvedId) {
                m_crashTimestamps.remove(state.instanceId);
                m_crashTrippedWidgets.remove(state.instanceId);
                if (m_runningWidgets.contains(state.instanceId) || state.visible) {
                    if (restartWidget(state.instanceId)) {
                        ++restartedCount;
                    }
                }
            }
        }

        if (restartedCount == 0 && m_enabled) {
            const WidgetRegistryEntry entry = registry.entryById(resolvedId);
            if (!entry.id.isEmpty() && QFileInfo::exists(entry.packagePath)) {
                QString launchedId;
                if (launchWidgetPackage(entry.packagePath, QRect(QPoint(260, 220), entry.defaultSize), entry.isDevelopment, entry.autoReload, &launchedId, nullptr)) {
                    ++restartedCount;
                }
            }
        }
        qInfo() << "[LinWidget][WidgetManager] Restarted widget:" << resolvedId << "restartedCount:" << restartedCount;
        response.insert(QStringLiteral("ok"), restartedCount > 0);
        response.insert(QStringLiteral("restartedCount"), restartedCount);
        if (restartedCount == 0) {
            response.insert(QStringLiteral("error"), QStringLiteral("Failed to restart widget %1.").arg(resolvedId));
        }
        return response;
    }

    if (action == QStringLiteral("set_enabled")) {
        const bool targetEnabled = request.value(QStringLiteral("enabled")).toBool(true);
        WidgetRegistryEntry entry = registry.entryById(resolvedId);
        if (entry.id.isEmpty()) {
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), QStringLiteral("Widget not found in registry: %1").arg(resolvedId));
            return response;
        }
        registry.setEnabled(entry.id, targetEnabled);
        registry.save();

        if (!targetEnabled) {
            if (!m_activeWallpaperId.isEmpty() &&
                m_activeWallpaperId.compare(QStringLiteral("wallpaper:") + resolvedId, Qt::CaseInsensitive) == 0) {
                stopWallpaper(true);
            }
            const QVector<WidgetLayoutState> states = m_layout.states();
            for (const WidgetLayoutState& state : states) {
                if (stateRegistryId(state) == resolvedId && m_runningWidgets.contains(state.instanceId)) {
                    stopWidget(state.instanceId);
                }
            }
        } else if (m_enabled) {
            m_crashTimestamps.remove(resolvedId);
            m_crashTrippedWidgets.remove(resolvedId);
            if (entry.role.compare(QStringLiteral("wallpaper"), Qt::CaseInsensitive) == 0) {
                startWallpaper(entry.packagePath, entry.isDevelopment, false, nullptr);
            } else {
                bool launchedAny = false;
                for (const WidgetLayoutState& state : m_layout.states()) {
                    if (stateRegistryId(state) == resolvedId) {
                        if (restartWidget(state.instanceId)) {
                            launchedAny = true;
                        }
                    }
                }
                if (!launchedAny && QFileInfo::exists(entry.packagePath)) {
                    launchWidgetPackage(entry.packagePath, QRect(QPoint(260, 220), entry.defaultSize), entry.isDevelopment, entry.autoReload, nullptr, nullptr);
                }
            }
        }

        qInfo() << "[LinWidget][WidgetManager] Set widget enabled state:" << resolvedId << "enabled:" << targetEnabled;
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("enabled"), targetEnabled);
        return response;
    }

    if (action == QStringLiteral("launch") || action == QStringLiteral("load") || action == QStringLiteral("start")) {
        if (!m_enabled) {
            setEnabled(true);
            QSettings s(AppPaths::configFilePath(), QSettings::IniFormat);
            s.setValue(QStringLiteral("LinWidgets/Enabled"), true);
        }
        m_crashTimestamps.remove(resolvedId);
        m_crashTrippedWidgets.remove(resolvedId);

        WidgetRegistryEntry entry = registry.entryById(resolvedId);
        if (entry.id.isEmpty()) {
            for (const WidgetRegistryEntry& e : registry.entries()) {
                if (e.id.compare(resolvedId, Qt::CaseInsensitive) == 0) {
                    entry = e;
                    break;
                }
            }
        }

        QString pkgPath = entry.packagePath;
        if (pkgPath.isEmpty()) {
            pkgPath = request.value(QStringLiteral("packagePath")).toString();
        }
        if (pkgPath.isEmpty() || !QDir(pkgPath).exists()) {
            pkgPath = QDir(AppPaths::userLinWidgetsDir()).filePath(resolvedId);
            if (!QDir(pkgPath).exists()) {
                const QString rootWidget = QStringLiteral(LINDESK_SOURCE_DIR) + QStringLiteral("/widgets/") + resolvedId;
                if (QDir(rootWidget).exists()) {
                    pkgPath = rootWidget;
                } else {
                    pkgPath = QStringLiteral(LINDESK_SOURCE_DIR) + QStringLiteral("/src/resources/LinWidgets/") + resolvedId;
                }
            }
        }

        QString newInstId;
        QString launchErr;
        const QSize defSize = (entry.defaultSize.isValid() && entry.defaultSize.width() > 0) ? entry.defaultSize : QSize(340, 240);
        const QPoint launchPos(240 + (m_runningWidgets.size() * 32) % 320, 180 + (m_runningWidgets.size() * 32) % 240);
        const bool ok = launchWidgetPackage(pkgPath, QRect(launchPos, defSize), entry.isDevelopment, entry.autoReload, &newInstId, &launchErr);
        if (ok) {
            registry.setLastRunState(entry.id.isEmpty() ? resolvedId : entry.id, entry.isDevelopment, QStringLiteral("running"));
            registry.save();
        }
        response.insert(QStringLiteral("ok"), ok);
        response.insert(QStringLiteral("instanceId"), newInstId);
        if (!ok) response.insert(QStringLiteral("error"), launchErr.isEmpty() ? QStringLiteral("Failed to launch widget.") : launchErr);
        return response;
    }

    if (action == QStringLiteral("stop") || action == QStringLiteral("unload")) {
        int stoppedCount = 0;
        const QString targetInstId = request.value(QStringLiteral("instanceId")).toString().trimmed();
        if (!targetInstId.isEmpty()) {
            stopWidget(targetInstId);
            ++stoppedCount;
        } else {
            for (const WidgetLayoutState& state : m_layout.states()) {
                if (stateRegistryId(state) == resolvedId && m_runningWidgets.contains(state.instanceId)) {
                    stopWidget(state.instanceId);
                    ++stoppedCount;
                }
            }
            if (!m_activeWallpaperId.isEmpty() && m_activeWallpaperId.contains(resolvedId, Qt::CaseInsensitive)) {
                stopWallpaper(true);
                ++stoppedCount;
            }
        }
        WidgetRegistryEntry entry = registry.entryById(resolvedId);
        registry.setLastRunState(resolvedId, entry.isDevelopment, QStringLiteral("stopped"));
        registry.save();
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("stoppedCount"), stoppedCount);
        return response;
    }

    if (action == QStringLiteral("show_devtools") || action == QStringLiteral("devtools")) {
        QString targetInst = request.value(QStringLiteral("instanceId")).toString().trimmed();
        if (targetInst.isEmpty()) {
            for (const WidgetLayoutState& state : m_layout.states()) {
                if (stateRegistryId(state) == resolvedId && m_runningWidgets.contains(state.instanceId)) {
                    targetInst = state.instanceId;
                    break;
                }
            }
            if (targetInst.isEmpty() && !m_activeWallpaperId.isEmpty() && m_activeWallpaperId.contains(resolvedId, Qt::CaseInsensitive)) {
                targetInst = m_activeWallpaperId;
            }
        }
        bool devOk = false;
        if (!targetInst.isEmpty()) {
            devOk = showDevTools(targetInst);
        }
        response.insert(QStringLiteral("ok"), devOk);
        if (!devOk) response.insert(QStringLiteral("error"), QStringLiteral("Widget instance is not running or DevTools unavailable."));
        return response;
    }

    if (action == QStringLiteral("open_settings") || action == QStringLiteral("settings")) {
        QString targetInst = request.value(QStringLiteral("instanceId")).toString().trimmed();
        if (targetInst.isEmpty()) {
            for (const WidgetLayoutState& state : m_layout.states()) {
                if (stateRegistryId(state) == resolvedId) {
                    targetInst = state.instanceId;
                    break;
                }
            }
        }
        emit widgetSettingsRequested(targetInst.isEmpty() ? resolvedId : targetInst);
        response.insert(QStringLiteral("ok"), true);
        return response;
    }

    if (action == QStringLiteral("open_folder")) {
        QString folderPath = request.value(QStringLiteral("path")).toString().trimmed();
        if (folderPath.isEmpty()) {
            WidgetRegistryEntry entry = registry.entryById(resolvedId);
            folderPath = entry.packagePath;
        }
        if (folderPath.isEmpty() || !QDir(folderPath).exists()) {
            folderPath = AppPaths::userLinWidgetsDir();
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath));
        response.insert(QStringLiteral("ok"), true);
        return response;
    }

    if (action == QStringLiteral("apply_wallpaper")) {
        QString wallPath = request.value(QStringLiteral("packagePath")).toString().trimmed();
        const QString customId = request.value(QStringLiteral("widgetId")).toString(
            request.value(QStringLiteral("id")).toString()).trimmed();
        if (!customId.isEmpty()) {
            const QJsonObject cObj = findCustomWallpaper(customId);
            if (!cObj.isEmpty()) {
                wallPath = customId;
            }
        }
        if (wallPath.isEmpty()) {
            WidgetRegistryEntry entry = registry.entryById(resolvedId);
            wallPath = entry.packagePath;
        }
        if (wallPath.isEmpty()) {
            const QString cleanId = resolvedId.startsWith(QStringLiteral("wallpaper:")) ? resolvedId.mid(10) : resolvedId;
            const QString lastPart = cleanId.section(QLatin1Char('.'), -1);
            const QStringList candidateDirs = {
                QDir(AppPaths::runtimeWallpapersDir()).filePath(cleanId),
                QDir(QStringLiteral(LINDESK_SOURCE_DIR) + QStringLiteral("/src/resources/wallpapers")).filePath(cleanId),
                QDir(AppPaths::userWallpapersDir()).filePath(cleanId),
                QDir(AppPaths::runtimeWallpapersDir()).filePath(lastPart),
                QDir(QStringLiteral(LINDESK_SOURCE_DIR) + QStringLiteral("/src/resources/wallpapers")).filePath(lastPart),
                QDir(AppPaths::userWallpapersDir()).filePath(lastPart),
                QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/LinWidgets/") + cleanId),
                QDir(QStringLiteral(LINDESK_SOURCE_DIR) + QStringLiteral("/widgets")).filePath(cleanId),
                QDir(QStringLiteral(LINDESK_SOURCE_DIR) + QStringLiteral("/src/resources/LinWidgets")).filePath(cleanId),
                QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/LinWidgets/") + lastPart),
                QDir(QStringLiteral(LINDESK_SOURCE_DIR) + QStringLiteral("/widgets")).filePath(lastPart),
                QDir(QStringLiteral(LINDESK_SOURCE_DIR) + QStringLiteral("/src/resources/LinWidgets")).filePath(lastPart)
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

    if (action == QStringLiteral("save_widget_settings")) {
        const QJsonObject settingsObj = request.value(QStringLiteral("settings")).toObject();
        const WidgetRegistryEntry entry = registry.entryById(resolvedId);
        const QString safeId = safeStorageSegment(resolvedId);
        const QString modeSeg = entry.isDevelopment ? QStringLiteral("dev") : QStringLiteral("installed");
        const QString modeDir = QDir(AppPaths::linWidgetDataDir()).filePath(modeSeg + QLatin1Char('/') + safeId);
        QDir().mkpath(modeDir);
        const QString settingsFile = QDir(modeDir).filePath(QStringLiteral("widget.json"));
        QJsonObject existing;
        loadStorageObject(settingsFile, &existing, nullptr);
        for (auto it = settingsObj.begin(); it != settingsObj.end(); ++it) {
            existing.insert(it.key(), it.value());
        }
        saveStorageObject(settingsFile, existing, nullptr);

        QJsonObject notifyMsg = makeWidgetMessage(QStringLiteral("settings_updated"), resolvedId);
        notifyMsg.insert(QStringLiteral("settings"), existing);
        broadcastToWidgetInstances(resolvedId, notifyMsg);

        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("settings"), existing);
        return response;
    }

    if (action == QStringLiteral("apply_studio_preset")) {
        response.insert(QStringLiteral("ok"), true);
        return response;
    }

    if (action == QStringLiteral("uninstall") || action == QStringLiteral("delete")) {
        for (const WidgetLayoutState& state : m_layout.states()) {
            if (stateRegistryId(state) == resolvedId) {
                stopWidget(state.instanceId);
            }
        }
        WidgetRegistryEntry entry = registry.entryById(resolvedId);
        if (entry.isDevelopment) {
            registry.removeDevelopmentProject(resolvedId);
        } else {
            registry.removeById(resolvedId);
        }
        registry.save();
        response.insert(QStringLiteral("ok"), true);
        return response;
    }

    response.insert(QStringLiteral("ok"), false);
    response.insert(QStringLiteral("error"), QStringLiteral("Unsupported widget_manager action: %1").arg(action));
    return response;
}

void WidgetManager::handleRuntimeMessage(const QString& widgetId, const QJsonObject& message)
{
    const QString type = message.value(QStringLiteral("type")).toString();
    const QString effectiveInstanceId = message.value(QStringLiteral("instanceId")).toString(
        message.value(QStringLiteral("widgetId")).toString(widgetId)).trimmed();

    if (type == QString::fromLatin1(WidgetMessageType::WidgetManagerRequest)) {
        const QJsonObject response = handleWidgetManagerRequest(message);
        if (!effectiveInstanceId.isEmpty()) {
            sendMessageToWidget(effectiveInstanceId, response);
        }
        return;
    }

    if (type == QString::fromLatin1(WidgetMessageType::WidgetGeometryChanged)) {
        WidgetLayoutState state = m_layout.state(effectiveInstanceId);
        if (state.instanceId.isEmpty()) {
            return;
        }
        QRect nextGeometry(message.value(QStringLiteral("x")).toInt(state.geometry.x()),
                           message.value(QStringLiteral("y")).toInt(state.geometry.y()),
                           message.value(QStringLiteral("width")).toInt(state.geometry.width()),
                           message.value(QStringLiteral("height")).toInt(state.geometry.height()));
        WidgetManifest manifest;
        if (WidgetPackage::loadManifest(state.packagePath, &manifest, nullptr)) {
            nextGeometry.setSize(clampedWidgetSize(manifest, nextGeometry.size()));
        } else if (m_instances.contains(effectiveInstanceId)) {
            nextGeometry.setSize(clampedWidgetSize(m_instances.value(effectiveInstanceId).manifest, nextGeometry.size()));
        }
        state.geometry = nextGeometry;
        m_layout.setGeometry(effectiveInstanceId, state.geometry);
        if (m_instances.contains(effectiveInstanceId)) {
            WidgetInstance instance = m_instances.value(effectiveInstanceId);
            instance.geometry = state.geometry;
            m_instances.insert(effectiveInstanceId, instance);
        }
        if (auto* osr = m_osrBrowsers.value(effectiveInstanceId)) {
            osr->setGeometry(state.geometry);
        }
        scheduleSaveToConfig();
        return;
    }
    if (type == QString::fromLatin1(WidgetMessageType::WidgetError)) {
        emit widgetError(effectiveInstanceId, message.value(QStringLiteral("message")).toString());
        return;
    }
    if (type == QString::fromLatin1(WidgetMessageType::OpenWidgetSettings)) {
        if (!effectiveInstanceId.isEmpty() && m_instances.contains(effectiveInstanceId)) {
            emit widgetSettingsRequested(effectiveInstanceId);
        }
        return;
    }
    if (type == QString::fromLatin1(WidgetMessageType::SystemInfoRequest)) {
        handleSystemInfoRequest(message);
        return;
    }
    if (type == QStringLiteral("set_resize_ui_visible")) {
        // The host draws resize affordances itself (ListView edge hit-test); the flag is accepted for API compatibility.
        qDebug() << "[LinWidget][WidgetManager] set_resize_ui_visible from" << effectiveInstanceId
                 << "visible:" << message.value(QStringLiteral("visible")).toBool(true);
        return;
    }
    if (type == QString::fromLatin1(WidgetMessageType::StorageRequest)) {
        handleStorageRequest(message);
        return;
    }
    if (type == QString::fromLatin1(WidgetMessageType::CreateWidgetInstanceRequest)) {
        const QString sourceInstanceId = effectiveInstanceId;
        QRect requestedGeometry;
        const QJsonObject geometry = message.value(QStringLiteral("geometry")).toObject();
        if (!geometry.isEmpty()) {
            requestedGeometry = QRect(geometry.value(QStringLiteral("x")).toInt(),
                                      geometry.value(QStringLiteral("y")).toInt(),
                                      geometry.value(QStringLiteral("width")).toInt(),
                                      geometry.value(QStringLiteral("height")).toInt());
        }
        QString newInstanceId;
        QString errorMessage;
        const bool ok = createWidgetInstance(sourceInstanceId, requestedGeometry, &newInstanceId, &errorMessage);
        QJsonObject response = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::CreateWidgetInstanceResponse), sourceInstanceId);
        response.insert(QStringLiteral("targetInstanceId"), sourceInstanceId);
        response.insert(QStringLiteral("requestId"), message.value(QStringLiteral("requestId")).toString());
        response.insert(QStringLiteral("ok"), ok);
        response.insert(QStringLiteral("instanceId"), newInstanceId);
        response.insert(QStringLiteral("error"), errorMessage);
        sendMessageToWidget(sourceInstanceId, response);
        return;
    }
    if (type == QString::fromLatin1(WidgetMessageType::ListWidgetInstancesRequest)) {
        const QString sourceInstanceId = effectiveInstanceId;
        const QString requestId = message.value(QStringLiteral("requestId")).toString();
        QJsonObject response = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::ListWidgetInstancesResponse), sourceInstanceId);
        response.insert(QStringLiteral("targetInstanceId"), sourceInstanceId);
        response.insert(QStringLiteral("requestId"), requestId);
        QJsonArray instances;
        const WidgetInstance source = m_instances.value(sourceInstanceId);
        if (!source.instanceId.isEmpty()) {
            for (const WidgetLayoutState& state : m_layout.states()) {
                if (!sameWidgetFamily(source, state)) {
                    continue;
                }
                QJsonObject item;
                item.insert(QStringLiteral("instanceId"), state.instanceId);
                item.insert(QStringLiteral("id"), state.instanceId);
                item.insert(QStringLiteral("visible"), state.visible);
                item.insert(QStringLiteral("running"), m_runningWidgets.contains(state.instanceId));
                item.insert(QStringLiteral("parentInstanceId"), state.parentInstanceId);
                item.insert(QStringLiteral("createdAt"), state.createdAt);
                QJsonObject geometry;
                geometry.insert(QStringLiteral("x"), state.geometry.x());
                geometry.insert(QStringLiteral("y"), state.geometry.y());
                geometry.insert(QStringLiteral("width"), state.geometry.width());
                geometry.insert(QStringLiteral("height"), state.geometry.height());
                item.insert(QStringLiteral("geometry"), geometry);
                instances.append(item);
            }
            response.insert(QStringLiteral("ok"), true);
        } else {
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), QStringLiteral("Source widget instance is not running."));
        }
        response.insert(QStringLiteral("instances"), instances);
        sendMessageToWidget(sourceInstanceId, response);
        return;
    }
    if (type == QString::fromLatin1(WidgetMessageType::CloseWidgetInstanceRequest)) {
        const QString sourceInstanceId = effectiveInstanceId;
        QString targetInstanceId = message.value(QStringLiteral("targetInstanceId")).toString().trimmed();
        if (targetInstanceId.isEmpty()) {
            targetInstanceId = sourceInstanceId;
        }
        const QString requestId = message.value(QStringLiteral("requestId")).toString();
        const WidgetInstance source = m_instances.value(sourceInstanceId);
        const WidgetLayoutState targetState = m_layout.state(targetInstanceId);
        const bool allowed = sameWidgetFamily(source, targetState);
        if (allowed && m_runningWidgets.contains(targetInstanceId)) {
            stopWidget(targetInstanceId);
        }
        QJsonObject response = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::CloseWidgetInstanceResponse), sourceInstanceId);
        response.insert(QStringLiteral("targetInstanceId"), sourceInstanceId);
        response.insert(QStringLiteral("requestId"), requestId);
        response.insert(QStringLiteral("ok"), allowed);
        response.insert(QStringLiteral("instanceId"), targetInstanceId);
        if (!allowed) {
            response.insert(QStringLiteral("error"), QStringLiteral("Target widget instance is outside the caller scope."));
        }
        sendMessageToWidget(sourceInstanceId, response);
        return;
    }
    if (type == QString::fromLatin1(WidgetMessageType::RemoveWidgetInstanceRequest)) {
        const QString sourceInstanceId = effectiveInstanceId;
        QString targetInstanceId = message.value(QStringLiteral("targetInstanceId")).toString().trimmed();
        if (targetInstanceId.isEmpty()) {
            targetInstanceId = sourceInstanceId;
        }
        const QString requestId = message.value(QStringLiteral("requestId")).toString();
        const WidgetInstance source = m_instances.value(sourceInstanceId);
        const WidgetLayoutState targetState = m_layout.state(targetInstanceId);
        const bool allowed = sameWidgetFamily(source, targetState);
        if (allowed && (m_layout.contains(targetInstanceId) || m_runningWidgets.contains(targetInstanceId))) {
            removeWidgetState(targetInstanceId);
        }
        QJsonObject response = makeWidgetMessage(QString::fromLatin1(WidgetMessageType::RemoveWidgetInstanceResponse), sourceInstanceId);
        response.insert(QStringLiteral("targetInstanceId"), sourceInstanceId);
        response.insert(QStringLiteral("requestId"), requestId);
        response.insert(QStringLiteral("ok"), allowed);
        response.insert(QStringLiteral("instanceId"), targetInstanceId);
        if (!allowed) {
            response.insert(QStringLiteral("error"), QStringLiteral("Target widget instance is outside the caller scope."));
        }
        sendMessageToWidget(sourceInstanceId, response);
        return;
    }

    if (!m_runningWidgets.contains(effectiveInstanceId)) {
        return;
    }

    if (type == QString::fromLatin1(WidgetMessageType::MoveWidget)) {
        const WidgetInstance instance = m_instances.value(effectiveInstanceId);
        const QPoint pos(
            message.value(QStringLiteral("x")).toInt(instance.geometry.x()),
            message.value(QStringLiteral("y")).toInt(instance.geometry.y()));
        moveWidget(effectiveInstanceId, pos);
    } else if (type == QString::fromLatin1(WidgetMessageType::ResizeWidget)) {
        const WidgetInstance instance = m_instances.value(effectiveInstanceId);
        const QSize size(
            message.value(QStringLiteral("width")).toInt(instance.geometry.width()),
            message.value(QStringLiteral("height")).toInt(instance.geometry.height()));
        resizeWidget(effectiveInstanceId, size);
    } else if (type == QStringLiteral("wallpaper_set_interactive")) {
        const QString requestId = message.value(QStringLiteral("requestId")).toString();
        const bool interactive = message.value(QStringLiteral("interactive")).toBool(true);
        setWallpaperInteractive(interactive);

        QJsonObject response = makeWidgetMessage(QStringLiteral("wallpaper_interactive_changed"), effectiveInstanceId);
        response.insert(QStringLiteral("requestId"), requestId);
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("interactive"), interactive);
        sendMessageToWidget(effectiveInstanceId, response);
        return;
    } else if (type == QStringLiteral("wallpaper_get_interactive")) {
        const QString requestId = message.value(QStringLiteral("requestId")).toString();
        QJsonObject response = makeWidgetMessage(QStringLiteral("wallpaper_interactive_response"), effectiveInstanceId);
        response.insert(QStringLiteral("requestId"), requestId);
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("interactive"), m_activeWallpaperInteractive);
        sendMessageToWidget(effectiveInstanceId, response);

        // 同时向已就绪的壁纸页面推送已保存的专属与通用参数
        const WidgetInstance inst = m_instances.value(effectiveInstanceId);
        const QString manifestId = !inst.manifest.id.isEmpty() ? inst.manifest.id : effectiveInstanceId;
        const QString cleanId = manifestId.startsWith(QStringLiteral("wallpaper:"), Qt::CaseInsensitive)
            ? manifestId.mid(10).trimmed()
            : manifestId.trimmed();
        QSettings s(AppPaths::configFilePath(), QSettings::IniFormat);
        const QString savedJson = s.value(QStringLiteral("LinWallpapers/%1/Properties").arg(cleanId)).toString();
        if (!savedJson.isEmpty()) {
            const QJsonObject savedProps = QJsonDocument::fromJson(savedJson.toUtf8()).object();
            for (auto it = savedProps.begin(); it != savedProps.end(); ++it) {
                QJsonObject propMsg;
                propMsg.insert(QStringLiteral("type"), QStringLiteral("wallpaper_property_change"));
                propMsg.insert(QStringLiteral("key"), it.key());
                propMsg.insert(QStringLiteral("property"), it.key());
                propMsg.insert(QStringLiteral("value"), it.value());
                sendMessageToWidget(effectiveInstanceId, propMsg);
            }
        }
        return;
    } else if (type == QStringLiteral("wallpaper_set_source")) {
        const QString requestId = message.value(QStringLiteral("requestId")).toString();
        const bool isClear = message.value(QStringLiteral("clear")).toBool(false) ||
                             (message.contains(QStringLiteral("filePath")) &&
                              message.value(QStringLiteral("filePath")).toString().trimmed().isEmpty() &&
                              !message.value(QStringLiteral("dialog")).toBool(false));
        if (isClear) {
            QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
            settings.remove(QStringLiteral("LinWallpapers/%1/SourcePath").arg(effectiveInstanceId));
            settings.sync();

            QJsonObject response = makeWidgetMessage(QStringLiteral("wallpaper_source_changed"), effectiveInstanceId);
            response.insert(QStringLiteral("requestId"), requestId);
            response.insert(QStringLiteral("ok"), true);
            response.insert(QStringLiteral("sourcePath"), QString());
            sendMessageToWidget(effectiveInstanceId, response);
            return;
        }

        QString selectedPath = message.value(QStringLiteral("filePath")).toString().trimmed();
        if (selectedPath.isEmpty() && message.value(QStringLiteral("dialog")).toBool(false)) {
            QWidget* parentWidget = LinCef::LinWidgetCanvasWidget::hostWidget();
            selectedPath = QFileDialog::getOpenFileName(
                parentWidget,
                QStringLiteral("Select Wallpaper Media"),
                QString(),
                QStringLiteral("Media Files (*.mp4 *.webm *.png *.jpg *.jpeg *.bmp *.webp *.gif);;All Files (*.*)"));
        }
        if (!selectedPath.isEmpty()) {
            QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
            settings.setValue(QStringLiteral("LinWallpapers/%1/SourcePath").arg(effectiveInstanceId), selectedPath);
            settings.sync();

            QJsonObject response = makeWidgetMessage(QStringLiteral("wallpaper_source_changed"), effectiveInstanceId);
            response.insert(QStringLiteral("requestId"), requestId);
            response.insert(QStringLiteral("ok"), true);
            response.insert(QStringLiteral("sourcePath"), selectedPath);
            sendMessageToWidget(effectiveInstanceId, response);
        } else {
            QJsonObject response = makeWidgetMessage(QStringLiteral("wallpaper_source_changed"), effectiveInstanceId);
            response.insert(QStringLiteral("requestId"), requestId);
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), QStringLiteral("No file selected."));
            sendMessageToWidget(effectiveInstanceId, response);
        }
        return;
    } else if (type == QStringLiteral("wallpaper_get_source")) {
        const QString requestId = message.value(QStringLiteral("requestId")).toString();
        QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
        const QString savedPath = settings.value(QStringLiteral("LinWallpapers/%1/SourcePath").arg(effectiveInstanceId)).toString();
        QJsonObject response = makeWidgetMessage(QStringLiteral("wallpaper_source_response"), effectiveInstanceId);
        response.insert(QStringLiteral("requestId"), requestId);
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("sourcePath"), savedPath);
        sendMessageToWidget(effectiveInstanceId, response);
    } else if (type == QStringLiteral("wallpaper_set_preset")) {
        const QString requestId = message.value(QStringLiteral("requestId")).toString();
        const QString preset = message.value(QStringLiteral("preset")).toString().trimmed();
        QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("LinWallpapers/%1/Preset").arg(effectiveInstanceId), preset);
        settings.sync();
        QJsonObject response = makeWidgetMessage(QStringLiteral("wallpaper_preset_changed"), effectiveInstanceId);
        response.insert(QStringLiteral("requestId"), requestId);
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("preset"), preset);
        sendMessageToWidget(effectiveInstanceId, response);
    } else if (type == QStringLiteral("wallpaper_get_preset")) {
        const QString requestId = message.value(QStringLiteral("requestId")).toString();
        QSettings settings(AppPaths::configFilePath(), QSettings::IniFormat);
        const QString saved = settings.value(QStringLiteral("LinWallpapers/%1/Preset").arg(effectiveInstanceId), QStringLiteral("aurora")).toString();
        QJsonObject response = makeWidgetMessage(QStringLiteral("wallpaper_preset_response"), effectiveInstanceId);
        response.insert(QStringLiteral("requestId"), requestId);
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("preset"), saved);
        sendMessageToWidget(effectiveInstanceId, response);
    }
}

bool WidgetManager::sendMessageToWidget(const QString& instanceId, const QJsonObject& message)
{
    if (instanceId.isEmpty()) {
        return false;
    }
    if (auto* osr = m_osrBrowsers.value(instanceId)) {
        osr->sendMessageToWeb(message);
        return true;
    }
    qWarning() << "[LinWidget][WidgetManager] sendMessageToWidget dropped: no OSR browser for" << instanceId
               << "type:" << message.value(QStringLiteral("type")).toString();
    return false;
}

void WidgetManager::broadcastToWidgetInstances(const QString& widgetRegistryId, const QJsonObject& message)
{
    const QString targetId = widgetRegistryId.trimmed().toLower();
    for (const WidgetLayoutState& state : m_layout.states()) {
        if (state.registryId.trimmed().toLower() == targetId && m_runningWidgets.contains(state.instanceId)) {
            QJsonObject instanceMessage = message;
            instanceMessage.insert(QStringLiteral("instanceId"), state.instanceId);
            instanceMessage.insert(QStringLiteral("widgetId"), state.instanceId);
            sendMessageToWidget(state.instanceId, instanceMessage);
        }
    }
}


} // namespace LinWidget
