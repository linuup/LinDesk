#include "desktop/widgets/linwidget/WidgetValidator.h"
#include "desktop/widgets/linwidget/WidgetPackage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTextStream>

namespace LinWidget {

QString ValidationResult::toFormattedReport() const
{
    QString report;
    QTextStream ts(&report);
    ts << (ok && !hasErrors() ? "[PASS] Widget Package Validation Passed\n" : "[FAIL] Widget Package Validation Failed\n");
    if (issues.isEmpty()) {
        ts << "No issues found.\n";
        return report;
    }

    ts << QStringLiteral("Found %1 issue(s):\n").arg(issues.size());
    for (int i = 0; i < issues.size(); ++i) {
        const auto& issue = issues[i];
        ts << QStringLiteral("[%1] %2 in %3")
                  .arg(issue.severity == ValidationIssue::Severity::Error ? "ERROR" : "WARNING")
                  .arg(issue.code)
                  .arg(issue.file);
        if (issue.line > 0) {
            ts << QStringLiteral(" (line %1)").arg(issue.line);
        }
        ts << "\n";
        ts << QStringLiteral("  Message: %1\n").arg(issue.message);
        if (!issue.suggestion.isEmpty()) {
            ts << QStringLiteral("  Fix: %1\n").arg(issue.suggestion);
        }
    }
    return report;
}

QJsonObject ValidationResult::toJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("ok"), ok && !hasErrors());
    QJsonArray issueArray;
    for (const auto& issue : issues) {
        QJsonObject obj;
        obj.insert(QStringLiteral("severity"), issue.severity == ValidationIssue::Severity::Error ? QStringLiteral("error") : QStringLiteral("warning"));
        obj.insert(QStringLiteral("file"), issue.file);
        obj.insert(QStringLiteral("line"), issue.line);
        obj.insert(QStringLiteral("code"), issue.code);
        obj.insert(QStringLiteral("message"), issue.message);
        obj.insert(QStringLiteral("suggestion"), issue.suggestion);
        issueArray.append(obj);
    }
    root.insert(QStringLiteral("issues"), issueArray);
    return root;
}

ValidationResult WidgetValidator::validatePackage(const QString& packagePath)
{
    ValidationResult result;
    result.ok = true;

    QFileInfo pkgInfo(packagePath);
    if (!pkgInfo.exists() || !pkgInfo.isDir()) {
        ValidationIssue issue;
        issue.severity = ValidationIssue::Severity::Error;
        issue.file = packagePath;
        issue.code = QStringLiteral("ERR_PACKAGE_NOT_FOUND");
        issue.message = QStringLiteral("Widget package directory does not exist or is not a directory.");
        issue.suggestion = QStringLiteral("Provide a valid directory path containing manifest.json.");
        result.issues.append(issue);
        result.ok = false;
        return result;
    }

    const QDir pkgDir(packagePath);
    const QString manifestPath = pkgDir.filePath(QStringLiteral("manifest.json"));
    QFile manifestFile(manifestPath);
    if (!manifestFile.exists()) {
        ValidationIssue issue;
        issue.severity = ValidationIssue::Severity::Error;
        issue.file = QStringLiteral("manifest.json");
        issue.code = QStringLiteral("ERR_MANIFEST_MISSING");
        issue.message = QStringLiteral("Missing required manifest.json in package root.");
        issue.suggestion = QStringLiteral("Create a manifest.json file with id, name, version, and entry fields.");
        result.issues.append(issue);
        result.ok = false;
        return result;
    }

    if (!manifestFile.open(QIODevice::ReadOnly)) {
        ValidationIssue issue;
        issue.severity = ValidationIssue::Severity::Error;
        issue.file = QStringLiteral("manifest.json");
        issue.code = QStringLiteral("ERR_MANIFEST_READ");
        issue.message = QStringLiteral("Cannot open manifest.json for reading.");
        result.issues.append(issue);
        result.ok = false;
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument manifestDoc = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
    manifestFile.close();

    if (parseError.error != QJsonParseError::NoError || !manifestDoc.isObject()) {
        ValidationIssue issue;
        issue.severity = ValidationIssue::Severity::Error;
        issue.file = QStringLiteral("manifest.json");
        issue.code = QStringLiteral("ERR_MANIFEST_SYNTAX");
        issue.message = QStringLiteral("Syntax error in manifest.json: %1").arg(parseError.errorString());
        issue.suggestion = QStringLiteral("Ensure manifest.json is formatted as valid JSON.");
        result.issues.append(issue);
        result.ok = false;
        return result;
    }

    const QJsonObject manifest = manifestDoc.object();
    const QString id = manifest.value(QStringLiteral("id")).toString().trimmed();
    if (id.isEmpty()) {
        ValidationIssue issue;
        issue.severity = ValidationIssue::Severity::Error;
        issue.file = QStringLiteral("manifest.json");
        issue.code = QStringLiteral("ERR_MISSING_ID");
        issue.message = QStringLiteral("Field 'id' is required.");
        issue.suggestion = QStringLiteral("Add a unique package identifier like 'com.author.widget'.");
        result.issues.append(issue);
    } else {
        static const QRegularExpression idRegex(QStringLiteral("^[a-zA-Z0-9._-]+$"));
        if (!idRegex.match(id).hasMatch()) {
            ValidationIssue issue;
            issue.severity = ValidationIssue::Severity::Error;
            issue.file = QStringLiteral("manifest.json");
            issue.code = QStringLiteral("ERR_INVALID_ID");
            issue.message = QStringLiteral("Field 'id' contains invalid characters.");
            issue.suggestion = QStringLiteral("Only use alphanumeric characters, dots, underscores, and dashes.");
            result.issues.append(issue);
        }
    }

    const QString name = manifest.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) {
        ValidationIssue issue;
        issue.severity = ValidationIssue::Severity::Error;
        issue.file = QStringLiteral("manifest.json");
        issue.code = QStringLiteral("ERR_MISSING_NAME");
        issue.message = QStringLiteral("Field 'name' is required.");
        issue.suggestion = QStringLiteral("Specify a user-friendly display name.");
        result.issues.append(issue);
    }

    const QString version = manifest.value(QStringLiteral("version")).toString().trimmed();
    if (version.isEmpty()) {
        ValidationIssue issue;
        issue.severity = ValidationIssue::Severity::Warning;
        issue.file = QStringLiteral("manifest.json");
        issue.code = QStringLiteral("WARN_MISSING_VERSION");
        issue.message = QStringLiteral("Field 'version' is empty, defaulting to 1.0.0.");
        issue.suggestion = QStringLiteral("Specify a semver string like '1.0.0'.");
        result.issues.append(issue);
    }

    const QString entry = manifest.value(QStringLiteral("entry")).toString(QStringLiteral("index.html")).trimmed();
    if (!pkgDir.exists(entry)) {
        ValidationIssue issue;
        issue.severity = ValidationIssue::Severity::Error;
        issue.file = entry;
        issue.code = QStringLiteral("ERR_ENTRY_NOT_FOUND");
        issue.message = QStringLiteral("Specified entry point '%1' does not exist in package.").arg(entry);
        issue.suggestion = QStringLiteral("Create '%1' or update the 'entry' field in manifest.json.").arg(entry);
        result.issues.append(issue);
    }

    // 检查权限声明
    QSet<QString> declaredPermissions;
    if (manifest.contains(QStringLiteral("permissions"))) {
        const QJsonArray permArray = manifest.value(QStringLiteral("permissions")).toArray();
        for (const auto& val : permArray) {
            const QString p = val.toString().trimmed().toLower();
            if (p == QStringLiteral("storage") || p == QStringLiteral("desktop") || p == QStringLiteral("network")) {
                declaredPermissions.insert(p);
            } else {
                ValidationIssue issue;
                issue.severity = ValidationIssue::Severity::Warning;
                issue.file = QStringLiteral("manifest.json");
                issue.code = QStringLiteral("WARN_UNKNOWN_PERMISSION");
                issue.message = QStringLiteral("Unknown permission '%1' requested.").arg(p);
                issue.suggestion = QStringLiteral("Supported permissions are: storage, desktop, network.");
                result.issues.append(issue);
            }
        }
    }

    // 扫描代码文件：危险模式与权限联动扫描
    qint64 totalBytes = 0;
    QDirIterator it(packagePath, QStringList() << QStringLiteral("*.html") << QStringLiteral("*.js") << QStringLiteral("*.json"),
                    QDir::Files, QDirIterator::Subdirectories);

    static const QRegularExpression evalRegex(QStringLiteral("\\beval\\s*\\("));
    static const QRegularExpression remoteScriptRegex(QStringLiteral("<script[^>]+src\\s*=\\s*['\"]https?://"));
    static const QRegularExpression externalUrlRegex(QStringLiteral("https?://[a-zA-Z0-9.-]+"));

    while (it.hasNext()) {
        const QString filePath = it.next();
        const QFileInfo fi(filePath);
        totalBytes += fi.size();

        const QString relativePath = pkgDir.relativeFilePath(filePath);
        if (relativePath == QStringLiteral("manifest.json")) {
            continue;
        }

        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        int lineNumber = 0;
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine();
            lineNumber++;

            // 1. eval 动态执行检测 (Error)
            if (evalRegex.match(line).hasMatch()) {
                ValidationIssue issue;
                issue.severity = ValidationIssue::Severity::Error;
                issue.file = relativePath;
                issue.line = lineNumber;
                issue.code = QStringLiteral("ERR_DISALLOWED_EVAL");
                issue.message = QStringLiteral("Direct use of eval() detected, which violates security policy.");
                issue.suggestion = QStringLiteral("Refactor to parse JSON using JSON.parse() or use safe alternatives.");
                result.issues.append(issue);
            }

            // 2. 远程脚本注入检测 (Error)
            if (remoteScriptRegex.match(line).hasMatch()) {
                ValidationIssue issue;
                issue.severity = ValidationIssue::Severity::Error;
                issue.file = relativePath;
                issue.line = lineNumber;
                issue.code = QStringLiteral("ERR_REMOTE_SCRIPT_INJECTION");
                issue.message = QStringLiteral("Loading remote executable script via <script src='http...'> is not allowed.");
                issue.suggestion = QStringLiteral("Bundle all necessary JavaScript libraries locally within the widget package.");
                result.issues.append(issue);
            }

            // 3. 外部网络 URL 扫描与权限模型联动 (Warning if network not declared)
            if (!declaredPermissions.contains(QStringLiteral("network")) && externalUrlRegex.match(line).hasMatch()) {
                // 排除常见的命名空间与 schema 声明 (如 w3.org)
                if (!line.contains(QStringLiteral("w3.org")) && !line.contains(QStringLiteral("schema.org"))) {
                    ValidationIssue issue;
                    issue.severity = ValidationIssue::Severity::Warning;
                    issue.file = relativePath;
                    issue.line = lineNumber;
                    issue.code = QStringLiteral("WARN_UNDECLARED_NETWORK");
                    issue.message = QStringLiteral("External URL found but 'network' permission is not declared in manifest.json.");
                    issue.suggestion = QStringLiteral("Add 'network' to manifest.json permissions array if external API access is required.");
                    result.issues.append(issue);
                }
            }
        }
        file.close();
    }

    // 包体积检查（超过 50MB 预警）
    if (totalBytes > 50 * 1024 * 1024) {
        ValidationIssue issue;
        issue.severity = ValidationIssue::Severity::Warning;
        issue.file = packagePath;
        issue.code = QStringLiteral("WARN_OVERSIZED_PACKAGE");
        issue.message = QStringLiteral("Package total size is %1 MB, exceeding recommended limit (50 MB).").arg(totalBytes / (1024 * 1024));
        issue.suggestion = QStringLiteral("Optimize media assets and remove unused files.");
        result.issues.append(issue);
    }

    result.ok = !result.hasErrors();
    return result;
}

} // namespace LinWidget
