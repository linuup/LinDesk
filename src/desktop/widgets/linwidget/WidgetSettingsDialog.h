#pragma once

#include "desktop/widgets/linwidget/LinWidgetTypes.h"

#include <QDialog>
#include <QString>
#include <QStringList>

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

    bool initialize(QString* errorMessage = nullptr);

private:
    void renderPlaceholder(const QString& message);
    void renderWebView(const QString& settingsPath);
    QString settingsHostEntryPath() const;
    QString virtualHostName() const;
    bool mapVirtualHost(const QString& hostName);
    void installConsoleLogging();
    void installSettingsBridge();
    void installPermissionGuards();
    void appendLog(const QString& level, const QString& message) const;
    void updateWebViewBounds();
    bool event(QEvent* event) override;

    QString m_packagePath;
    WidgetManifest m_manifest;
    QStringList m_grantedPermissions;
    bool m_isDevelopment = false;
    QString m_instanceId;
    void* m_webViewController = nullptr;
    void* m_webView = nullptr;
    bool m_comInitialized = false;
};

}
