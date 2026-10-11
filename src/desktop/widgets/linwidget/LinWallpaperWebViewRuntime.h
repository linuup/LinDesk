#pragma once

#include "desktop/widgets/linwidget/LinWidgetTypes.h"
#include <QWidget>
#include <QJsonObject>
#include <QString>
#include <QRect>
#include <QVector>

namespace LinWidget {

class LinWallpaperWebViewRuntime : public QWidget {
    Q_OBJECT

public:
    explicit LinWallpaperWebViewRuntime(QString wallpaperId,
                                       QString packagePath,
                                       QWidget* parent = nullptr);
    ~LinWallpaperWebViewRuntime() override;

    bool initialize(QString* errorMessage = nullptr);
    
    void setInteractive(bool interactive);
    bool isInteractive() const { return m_interactive; }

    void suspend();
    void resume();

    void mirrorMouseMove(int x, int y);
    void mirrorMouseDown(int x, int y, const QString& button);
    void mirrorMouseUp(int x, int y, const QString& button);

    void pushAudioData(const QVector<float>& frequencies);
    void pushSystemInfo(const QJsonObject& info);

private:
    bool nativeEvent(const QByteArray& eventType, void* message, long* result) override;
    void resizeEvent(QResizeEvent* event) override;

    void setupWindow();
    void renderHtmlWallpaper();
    void updateWebViewBounds();
    void postWebMessage(const QJsonObject& payload);

    QString m_wallpaperId;
    QString m_packagePath;
    WidgetManifest m_manifest;
    bool m_interactive = false;
    bool m_suspended = false;

    void* m_webViewController = nullptr;
    void* m_compositionController = nullptr;
    void* m_webView = nullptr;
    void* m_d3d11Device = nullptr;
    void* m_dcompDevice = nullptr;
    void* m_dcompTarget = nullptr;
    void* m_rootVisual = nullptr;
    void* m_webViewVisual = nullptr;
    bool m_comInitialized = false;
    bool m_webViewReady = false;
};

}
