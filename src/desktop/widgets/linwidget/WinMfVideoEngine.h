#pragma once

#include <QByteArray>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QRect>
#include <QSize>
#include <QString>
#include <QHash>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace LinWidget {

struct VideoRenderConfig {
    QString scaling = QStringLiteral("cover"); // cover | contain | fill
    int dimming = 15;                          // 0..85
    int blur = 0;                              // 0..30
    int volume = 80;                           // 0..100
    bool muted = false;
    double playbackRate = 1.0;                 // 0.25..3.0
};

class WinMfVideoEngine {
public:
    static WinMfVideoEngine& instance();

    // Extract a single representative frame (at ~1.0s or 0.0s) for preview.png
    static QImage extractThumbnail(const QString& videoFilePath, const QSize& targetSize = QSize(640, 360));

    // Serve the next sequential JPEG frame for Hub live preview (http://linwidget.bridge/video-frame)
    QByteArray getPreviewFrameJpeg(const QString& videoFilePath, double playbackRate = 1.0, const QSize& maxSize = QSize(640, 360));

    // Preview video decoding thread for Hub Studio canvas and card hover
    void startPreviewVideo(const QString& videoFilePath, int targetFps = 30);
    void stopPreviewVideo();

    // Start/stop desktop wallpaper native video + audio playback
    void startDesktopVideo(const QString& instanceId,
                           const QString& videoFilePath,
                           const QRect& viewportRect,
                           const VideoRenderConfig& initialConfig,
                           int targetFps,
                           bool suspended,
                           std::function<void(const QString&, const QRect&, const QImage&)> onFrameCallback);

    void stopDesktopVideo();
    bool isDesktopVideoRunning() const;
    QString activeDesktopInstanceId() const;
    QString activeDesktopVideoPath() const;

    void updateDesktopViewport(const QRect& viewportRect);
    void updateDesktopProperty(const QString& key, const QVariant& value);
    void setDesktopSuspended(bool suspended);
    void setDesktopFramerate(int fps);

private:
    WinMfVideoEngine();
    ~WinMfVideoEngine();
    WinMfVideoEngine(const WinMfVideoEngine&) = delete;
    WinMfVideoEngine& operator=(const WinMfVideoEngine&) = delete;

    struct PreviewSession;
    mutable QMutex m_previewMutex;
    std::atomic<bool> m_previewRunning{false};
    std::atomic<bool> m_previewStopRequested{false};
    std::atomic<qint64> m_lastPreviewRequestTimeMs{0};
    QString m_previewVideoPath;
    QImage m_latestPreviewRawFrame;
    QByteArray m_latestPreviewJpeg;
    std::thread m_previewThread;
    QHash<QString, std::shared_ptr<PreviewSession>> m_previewSessions;

    mutable QMutex m_desktopMutex;
    std::atomic<bool> m_desktopRunning{false};
    std::atomic<bool> m_desktopStopRequested{false};
    std::atomic<bool> m_desktopSuspended{false};
    std::atomic<int> m_desktopFps{60};
    std::atomic<int> m_desktopDimming{15};
    std::atomic<int> m_desktopBlur{0};
    std::atomic<int> m_desktopVolume{80};
    std::atomic<bool> m_desktopMuted{false};
    std::atomic<double> m_desktopPlaybackRate{1.0};
    QString m_desktopScaling = QStringLiteral("cover");
    QString m_desktopInstanceId;
    QString m_desktopVideoPath;
    QRect m_desktopViewport;
    QImage m_latestDesktopRawFrame;
    std::thread m_desktopThread;
};

} // namespace LinWidget
