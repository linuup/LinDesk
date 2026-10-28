#pragma once

#include "desktop/widgets/linwidget/LinWidgetTypes.h"

#include <QDialog>
#include <QJsonObject>
#include <QMap>
#include <QMouseEvent>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWheelEvent>

namespace LinWidget {

class WidgetSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit WidgetSettingsDialog(QString packagePath,
                                  WidgetManifest manifest,
                                  QStringList grantedPermissions,
                                  bool isDevelopment,
                                  QString instanceId = QString(),
                                  QWidget* parent = nullptr);
    ~WidgetSettingsDialog() override;

    static WidgetSettingsDialog* openOrCreate(QString packagePath,
                                              WidgetManifest manifest,
                                              QStringList grantedPermissions,
                                              bool isDevelopment,
                                              QString instanceId = QString(),
                                              QWidget* parent = nullptr,
                                              QString* errorMessage = nullptr);

    static QMap<QString, QPointer<WidgetSettingsDialog>>& openSettingsDialogs();

    bool initialize(QString* errorMessage = nullptr);
    bool hasSchemaControls() const { return !m_schemaEditors.isEmpty(); }
    int schemaControlCount() const { return m_schemaEditors.size(); }
    QJsonObject currentSchemaValues() const;
    bool applySchemaValues(const QJsonObject& values, QString* errorMessage = nullptr);
    bool resetSchemaToDefaults(QString* errorMessage = nullptr);

private:
    static QString dialogKey(const QString& packagePath,
                             const WidgetManifest& manifest,
                             bool isDevelopment,
                             const QString& instanceId);

    void renderPlaceholder(const QString& message);
    void renderSettingsPage(const QString& settingsPath);
    QWidget* createSchemaSettingsPanel(QWidget* parentWidget);
    QWidget* createCustomHtmlSettingsPanel(const QString& settingsPath, QWidget* parentWidget);
    QString widgetStorageFilePath() const;
    QJsonObject loadStoredSettings() const;
    bool saveSchemaSettings(const QJsonObject& values, QString* errorMessage = nullptr);
    void appendLog(const QString& level, const QString& message) const;
    void handleSettingsWebMessage(const QJsonObject& object);

    QString m_packagePath;
    WidgetManifest m_manifest;
    QStringList m_grantedPermissions;
    bool m_isDevelopment = false;
    QString m_instanceId;
    QMap<QString, QPointer<QWidget>> m_schemaEditors;
};

}
