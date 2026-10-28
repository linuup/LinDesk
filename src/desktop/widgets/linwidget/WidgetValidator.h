#pragma once

#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>

namespace LinWidget {

struct ValidationIssue {
    enum class Severity {
        Warning,
        Error
    };

    Severity severity = Severity::Error;
    QString file;
    int line = -1;
    QString code;
    QString message;
    QString suggestion;
};

struct ValidationResult {
    bool ok = true;
    QVector<ValidationIssue> issues;

    bool hasErrors() const {
        for (const auto& issue : issues) {
            if (issue.severity == ValidationIssue::Severity::Error) {
                return true;
            }
        }
        return false;
    }

    QString toFormattedReport() const;
    QJsonObject toJson() const;
};

class WidgetValidator {
public:
    static ValidationResult validatePackage(const QString& packagePath);
};

} // namespace LinWidget
