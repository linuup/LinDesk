#pragma once

#include "desktop/widgets/linwidget/LinWidgetTypes.h"

#include <QDialog>
#include <QJsonObject>
#include <QMap>
#include <QPointer>
#include <QString>
#include <QStringList>

namespace LinWidget {

class WidgetSettingsDialog : public QDialog {
    Q_OBJECT
    friend class LinWidgetWebViewRuntime;

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

private:
    static QString dialogKey(const QString& packagePath,
                             const WidgetManifest& manifest,
                             bool isDevelopment,
                             const QString& instanceId);

    void renderPlaceholder(const QString& message);
    void renderWebView(const QString& settingsPath);
    QString settingsHostEntryPath() const;
    QString virtualHostName() const;
    bool mapVirtualHost(const QString& hostName);
    QString webViewErrorString(long result) const;
    void installConsoleLogging();
    void installSettingsBridge();
    void installPermissionGuards();
    void appendLog(const QString& level, const QString& message) const;
    void updateWebViewBounds();
    void handleSettingsWebMessage(const QJsonObject& object);
    bool event(QEvent* event) override;

    QString m_packagePath;
    WidgetManifest m_manifest;
    QStringList m_grantedPermissions;
    bool m_isDevelopment = false;
    QString m_instanceId;
    void* m_webViewController = nullptr;
    void* m_webView = nullptr;
    bool m_comInitialized = false;
    QMap<QString, QString> m_virtualHostMappings;
};

}
