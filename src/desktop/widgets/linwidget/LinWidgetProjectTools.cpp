#include "desktop/widgets/linwidget/LinWidgetProjectTools.h"

#include "desktop/widgets/linwidget/LinWidgetTypes.h"
#include "desktop/widgets/linwidget/WidgetPackage.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>

namespace LinWidget {

namespace {
bool writeTextFile(const QString& path, const QString& text, QString* errorMessage)
{
    QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot create directory: %1").arg(info.absolutePath());
        }
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot write file: %1").arg(file.errorString());
        }
        return false;
    }
    file.write(text.toUtf8());
    return true;
}

bool shouldSkipPackageEntry(const QString& name)
{
    return name == QStringLiteral(".git") ||
        name == QStringLiteral(".webview2") ||
        name == QStringLiteral("node_modules") ||
        name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive) ||
        name.compare(QStringLiteral("cache"), Qt::CaseInsensitive) == 0 ||
        name.compare(QStringLiteral("tmp"), Qt::CaseInsensitive) == 0 ||
        name.compare(QStringLiteral("temp"), Qt::CaseInsensitive) == 0;
}

bool copyDirectoryFiltered(const QString& sourcePath, const QString& targetPath, QString* errorMessage)
{
    QDir source(sourcePath);
    if (!source.exists()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Package directory does not exist: %1").arg(sourcePath);
        }
        return false;
    }
    if (!QDir().mkpath(targetPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot create export staging directory: %1").arg(targetPath);
        }
        return false;
    }

    const QFileInfoList entries = source.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries, QDir::Name);
    for (const QFileInfo& entry : entries) {
        if (shouldSkipPackageEntry(entry.fileName())) {
            continue;
        }
        const QString targetEntryPath = QDir(targetPath).filePath(entry.fileName());
        if (entry.isDir()) {
            if (!copyDirectoryFiltered(entry.absoluteFilePath(), targetEntryPath, errorMessage)) {
                return false;
            }
        } else if (!QFile::copy(entry.absoluteFilePath(), targetEntryPath)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Cannot copy export file: %1").arg(entry.absoluteFilePath());
            }
            return false;
        }
    }
    return true;
}
}

bool LinWidgetProjectTools::createProject(const QString& targetDir,
                                          const WidgetProjectMetadata& metadata,
                                          QString* errorMessage)
{
    const QString cleanDir = QDir::cleanPath(targetDir);
    if (cleanDir.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Project directory is empty.");
        }
        return false;
    }
    if (metadata.id.trimmed().isEmpty() || metadata.name.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget id and name are required.");
        }
        return false;
    }
    if (!QDir().mkpath(cleanDir)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot create project directory: %1").arg(cleanDir);
        }
        return false;
    }

    WidgetManifest manifest;
    manifest.id = metadata.id.trimmed();
    manifest.name = metadata.name.trimmed();
    manifest.version = QStringLiteral("0.1.0");
    manifest.author = metadata.author.trimmed();
    manifest.description = metadata.description.trimmed();
    manifest.width = qMax(1, metadata.width);
    manifest.height = qMax(1, metadata.height);
    manifest.minWidth = qMin(160, manifest.width);
    manifest.minHeight = qMin(120, manifest.height);
    manifest.maxWidth = 0;
    manifest.maxHeight = 0;
    manifest.transparent = metadata.transparent;
    manifest.resizable = metadata.resizable;
    manifest.autoLoad = false;
    manifest.entry = QStringLiteral("index.html");

    const QString manifestText = QString::fromUtf8(QJsonDocument(manifestToJson(manifest)).toJson(QJsonDocument::Indented));
    if (!writeTextFile(QDir(cleanDir).filePath(QStringLiteral("manifest.json")), manifestText, errorMessage)) {
        return false;
    }

    const QString indexHtml = QStringLiteral(
        "<!doctype html>\n"
        "<html>\n"
        "<head>\n"
        "  <meta charset=\"utf-8\">\n"
        "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
        "  <title>%1</title>\n"
        "  <link rel=\"stylesheet\" href=\"style.css\">\n"
        "</head>\n"
        "<body>\n"
        "  <main class=\"widget\">\n"
        "    <div class=\"title\">%1</div>\n"
        "    <div class=\"value\" id=\"clock\">Ready</div>\n"
        "    <button id=\"resizeButton\" type=\"button\">Resize</button>\n"
        "  </main>\n"
        "  <script src=\"script.js\"></script>\n"
        "</body>\n"
        "</html>\n").arg(manifest.name.toHtmlEscaped());
    if (!writeTextFile(QDir(cleanDir).filePath(QStringLiteral("index.html")), indexHtml, errorMessage)) {
        return false;
    }

    const QString styleCss = QStringLiteral(
        "html, body {\n"
        "  width: 100%;\n"
        "  height: 100%;\n"
        "  margin: 0;\n"
        "  overflow: hidden;\n"
        "  font-family: \"Segoe UI\", sans-serif;\n"
        "}\n"
        ".widget {\n"
        "  box-sizing: border-box;\n"
        "  width: 100%;\n"
        "  height: 100%;\n"
        "  padding: 14px;\n"
        "  color: #111827;\n"
        "  background: rgba(255, 255, 255, 0.86);\n"
        "  border: 1px solid rgba(15, 23, 42, 0.12);\n"
        "  border-radius: 10px;\n"
        "}\n"
        ".title {\n"
        "  font-size: 13px;\n"
        "  font-weight: 700;\n"
        "  color: #475569;\n"
        "}\n"
        ".value {\n"
        "  margin-top: 8px;\n"
        "  font-size: 26px;\n"
        "  font-weight: 800;\n"
        "}\n"
        "button {\n"
        "  margin-top: 12px;\n"
        "  padding: 6px 10px;\n"
        "}\n");
    if (!writeTextFile(QDir(cleanDir).filePath(QStringLiteral("style.css")), styleCss, errorMessage)) {
        return false;
    }

    const QString scriptJs = QStringLiteral(
        "const clock = document.getElementById('clock');\n"
        "const update = () => {\n"
        "  clock.textContent = new Date().toLocaleTimeString();\n"
        "};\n"
        "update();\n"
        "setInterval(update, 1000);\n"
        "document.getElementById('resizeButton')?.addEventListener('click', () => {\n"
        "  window.desktop?.resizeWidget?.({ width: 360, height: 180 });\n"
        "});\n");
    if (!writeTextFile(QDir(cleanDir).filePath(QStringLiteral("script.js")), scriptJs, errorMessage)) {
        return false;
    }

    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool LinWidgetProjectTools::validatePackage(const QString& packagePath, QString* errorMessage)
{
    WidgetManifest manifest;
    if (!WidgetPackage::loadManifest(packagePath, &manifest, errorMessage)) {
        return false;
    }
    const QString entryPath = WidgetPackage::entryPath(packagePath, manifest);
    if (!QFileInfo::exists(entryPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Widget entry file does not exist: %1").arg(entryPath);
        }
        return false;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool LinWidgetProjectTools::exportProjectZip(const QString& packagePath,
                                             const QString& outputZipPath,
                                             QString* errorMessage)
{
    if (!validatePackage(packagePath, errorMessage)) {
        return false;
    }
    if (outputZipPath.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Output zip path is empty.");
        }
        return false;
    }

    QTemporaryDir staging;
    if (!staging.isValid()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot create export staging directory.");
        }
        return false;
    }
    const QString stagingRoot = QDir(staging.path()).filePath(QFileInfo(packagePath).fileName());
    if (!copyDirectoryFiltered(packagePath, stagingRoot, errorMessage)) {
        return false;
    }

    QFile::remove(outputZipPath);
    const QString program = QStringLiteral("powershell");
    QString archiveSource = QDir::toNativeSeparators(QDir(staging.path()).filePath(QStringLiteral("*")));
    QString archiveTarget = QDir::toNativeSeparators(outputZipPath);
    archiveSource.replace(QStringLiteral("'"), QStringLiteral("''"));
    archiveTarget.replace(QStringLiteral("'"), QStringLiteral("''"));
    QStringList args;
    args << QStringLiteral("-NoProfile")
         << QStringLiteral("-ExecutionPolicy")
         << QStringLiteral("Bypass")
         << QStringLiteral("-Command")
         << QStringLiteral("Compress-Archive -Path '%1' -DestinationPath '%2' -Force")
                .arg(archiveSource, archiveTarget);

    QProcess process;
    process.start(program, args);
    if (!process.waitForFinished(30000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Zip export failed: %1").arg(QString::fromLocal8Bit(process.readAllStandardError()));
        }
        return false;
    }

    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

}
