#pragma once
#include <QObject>
#include <QImage>
#include <QBuffer>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QDebug>
#include <QPointer>
#include <QSettings>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QTimer>
#include <functional>
#include "../../src/plugins/ILinPlugin.h"
#include "services/AppPaths.h"
#include "ScreenShot.h"
#include "OcrResultDialog.h"
#include "OcrSettingsDialog.h"

/**
 * @brief Baidu OCR Implementation (Ported from Assistant)
 */
class OCR_Baidu : public QObject, public IOCRPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID ILinPlugin_IID FILE "ocr_plugin.json")
    Q_INTERFACES(ILinPlugin IOCRPlugin)

public:
    explicit OCR_Baidu(QObject* parent = nullptr) : QObject(parent) {
        m_manager = new QNetworkAccessManager(this);
        
        m_screenShot = new ScreenShot();
        m_resultDialog = new OcrResultDialog();
        
        connect(m_screenShot, &ScreenShot::imageCaptured, this, [this](const QImage& img) {
            beginRecognitionSession(img);
        });
    }

    // ILinPlugin Implementation
    QString pluginId() const override { return "com.lindesk.ocr.baidu"; }
    QString pluginName() const override { return "Baidu OCR Engine"; }
    QString pluginDescription() const override { return "Connects to Baidu AIP for high-precision text recognition"; }
    QString customHotkey() const override { return m_hotkey; }
    
    void onInit() override { 
        m_unloading = false;
        if (!m_screenShot) {
            m_screenShot = new ScreenShot();
            connect(m_screenShot, &ScreenShot::imageCaptured, this, [this](const QImage& img) {
                beginRecognitionSession(img);
            });
        }
        if (!m_resultDialog) {
            m_resultDialog = new OcrResultDialog();
        }

        const QString iniPath = AppPaths::ocrConfigFilePath();

        QSettings settings(iniPath, QSettings::IniFormat);
        
        // If the file was empty, initialize it with clear placeholders
        if (settings.allKeys().isEmpty()) {
            settings.setValue("Baidu/ApiKey", "YOUR_API_KEY_HERE");
            settings.setValue("Baidu/SecretKey", "YOUR_SECRET_KEY_HERE");
            settings.setValue("Baidu/Hotkey", "F4");
            settings.sync(); // Force write to disk
            qDebug() << "[OCR_Baidu] Template config created at:" << iniPath;
            m_apiKey = "";
            m_secretKey = "";
            m_hotkey = "F4";
        } else {
            m_apiKey = settings.value("Baidu/ApiKey").toString().trimmed();
            m_secretKey = settings.value("Baidu/SecretKey").toString().trimmed();
            m_hotkey = settings.value("Baidu/Hotkey", "F4").toString().trimmed();
            qDebug() << "[OCR_Baidu] Configuration loaded from:" << iniPath;
            qDebug() << "[OCR_Baidu]   -> Hotkey configured:" << m_hotkey;
        }

        qDebug() << "[OCR_Baidu] Initialized. Ready for API calls."; 
        
        // Only attempt to refresh if keys look valid (not placeholders)
        if (!m_apiKey.isEmpty() && m_apiKey != "YOUR_API_KEY_HERE") {
            refreshToken();
        } else {
            qWarning() << "[OCR_Baidu] OCR is NOT ready. Please fill your keys in:" << iniPath;
        }
    }
    void onUnload() override { 
        qDebug() << "[OCR_Baidu] Unloaded."; 
        m_unloading = true;
        m_recognitionInProgress = false;
        m_pendingImage = QImage();
        if (m_screenShot) {
            m_screenShot->disconnect(this);
            m_screenShot->close();
            m_screenShot->deleteLater();
            m_screenShot = nullptr;
        }
        if (m_resultDialog) {
            m_resultDialog->close();
            m_resultDialog->deleteLater();
            m_resultDialog = nullptr;
        }
    }

    void onSettingsRequested() override {
        m_isConfiguring = true;
        OcrSettingsDialog dlg;
        if (dlg.exec() == QDialog::Accepted) {
            onInit(); // Refresh keys and internal state
            qDebug() << "[OCR_Baidu] Settings applied. (Note: Hotkey updates require app reload if changed)";
        }
        m_isConfiguring = false;
    }

    void onTrigger() override {
        if (m_isConfiguring) return; // Prevent triggering while recording
        if (m_unloading || !m_screenShot) return;
        
        qDebug() << "[OCR_Baidu] Global Hotkey/Trigger received! Starting ScreenShot...";
        m_screenShot->start();
    }

    // IOCRPlugin Implementation
    QString recognizeText(const QImage& source) override {
        const RecognitionResult result = recognizeWithBoxes(source);
        if (!result.ok) {
            return result.errorMessage.trimmed().isEmpty()
                ? QStringLiteral("Image text recognition failed.")
                : result.errorMessage;
        }
        return result.text.trimmed().isEmpty() ? QStringLiteral("No text detected.") : result.text;
    }

private:
    struct RecognitionResult {
        QString text;
        QVector<OcrTextBox> boxes;
        QString errorMessage;
        bool ok = false;
    };

    void beginRecognitionSession(const QImage& source)
    {
        if (m_unloading || source.isNull() || !m_resultDialog) {
            return;
        }

        if (m_recognitionInProgress) {
            m_pendingImage = source;
            return;
        }

        m_recognitionInProgress = true;
        m_pendingImage = source;
        const QImage sessionImage = source;

        auto runOcrRequest = [this, sessionImage]() {
            requestRecognitionAsync(sessionImage, [this, sessionImage](const RecognitionResult& result) {
                finishRecognitionSession(sessionImage, result);
            });
        };

        if (m_token.isEmpty()) {
            ensureTokenAsync([this, runOcrRequest, sessionImage](bool ok) {
                if (!ok) {
                    RecognitionResult failed;
                    failed.ok = false;
                    failed.errorMessage = QStringLiteral("Failed to obtain Baidu API token. ") + m_lastError;
                    finishRecognitionSession(sessionImage, failed);
                    return;
                }
                runOcrRequest();
            });
            return;
        }

        runOcrRequest();
    }

    void finishRecognitionSession(const QImage& source, const RecognitionResult& result)
    {
        m_recognitionInProgress = false;

        if (m_unloading) {
            m_pendingImage = QImage();
            return;
        }

        if (m_resultDialog) {
            if (result.ok) {
                m_resultDialog->setResult(source, result.text, result.boxes);
            } else {
                m_resultDialog->setError(source, result.errorMessage);
            }
        }

        if (!m_pendingImage.isNull() && m_pendingImage.cacheKey() != source.cacheKey()) {
            const QImage nextImage = m_pendingImage;
            m_pendingImage = QImage();
            beginRecognitionSession(nextImage);
            return;
        }

        m_pendingImage = QImage();
    }

    void ensureTokenAsync(const std::function<void(bool)>& onDone)
    {
        if (m_apiKey.isEmpty() || m_secretKey.isEmpty()) {
            m_lastError = "API Key or Secret Key is empty! Please check OCR_Baidu.h";
            onDone(false);
            return;
        }

        if (!m_manager->supportedSchemes().contains("https")) {
            m_lastError = "OpenSSL not found! Please ensure libssl-1_1-x64.dll and libcrypto-1_1-x64.dll (v1.1.1) are in the application folder.";
            qWarning() << "[OCR_Baidu]" << m_lastError;
            onDone(false);
            return;
        }

        QUrl url(QString("https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials&client_id=%1&client_secret=%2")
                 .arg(m_apiKey).arg(m_secretKey));

        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

        QNetworkReply* reply = m_manager->post(request, QByteArray());
        QTimer* timeoutTimer = new QTimer(reply);
        timeoutTimer->setSingleShot(true);
        connect(timeoutTimer, &QTimer::timeout, reply, [reply]() {
            reply->setProperty("ocrTimedOut", true);
            if (reply->isRunning()) {
                reply->abort();
            }
        });
        timeoutTimer->start(kNetworkTimeoutMs);
        connect(reply, &QNetworkReply::finished, this, [this, reply, onDone]() {
            if (reply->property("ocrTimedOut").toBool()) {
                m_lastError = "Baidu token request timed out.";
                reply->deleteLater();
                onDone(false);
                return;
            }
            if (reply->error() == QNetworkReply::NoError) {
                const QByteArray data = reply->readAll();
                m_token = QJsonDocument::fromJson(data).object().value("access_token").toString();
                if (m_token.isEmpty()) {
                    m_lastError = "Baidu response did not contain access_token. Check your credentials.";
                    reply->deleteLater();
                    onDone(false);
                    return;
                }
                reply->deleteLater();
                onDone(true);
                return;
            }

            m_lastError = reply->errorString();
            reply->deleteLater();
            onDone(false);
        });
    }

    void requestRecognitionAsync(const QImage& source, const std::function<void(const RecognitionResult&)>& onDone)
    {
        RecognitionResult output;
        if (source.isNull()) {
            output.errorMessage = QStringLiteral("Failed to capture OCR area.");
            onDone(output);
            return;
        }

        if (m_token.isEmpty()) {
            output.errorMessage = QStringLiteral("Failed to obtain Baidu API token.");
            onDone(output);
            return;
        }

        QByteArray ba;
        QBuffer buffer(&ba);
        buffer.open(QIODevice::WriteOnly);
        source.save(&buffer, "JPG", 88);
        const QString base64 = ba.toBase64();

        QUrl url(QStringLiteral("https://aip.baidubce.com/rest/2.0/ocr/v1/general?access_token=") + m_token);
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

        QByteArray postData;
        postData += "image=" + QUrl::toPercentEncoding(base64);
        postData += "&detect_direction=false";
        postData += "&paragraph=false";
        postData += "&probability=false";

        QNetworkReply* reply = m_manager->post(request, postData);
        QTimer* timeoutTimer = new QTimer(reply);
        timeoutTimer->setSingleShot(true);
        connect(timeoutTimer, &QTimer::timeout, reply, [reply]() {
            reply->setProperty("ocrTimedOut", true);
            if (reply->isRunning()) {
                reply->abort();
            }
        });
        timeoutTimer->start(kNetworkTimeoutMs);
        connect(reply, &QNetworkReply::finished, this, [this, reply, onDone]() {
            RecognitionResult result;
            if (reply->property("ocrTimedOut").toBool()) {
                result.errorMessage = QStringLiteral("OCR network request timed out.");
                reply->deleteLater();
                onDone(result);
                return;
            }
            if (reply->error() != QNetworkReply::NoError) {
                result.errorMessage = QStringLiteral("OCR network error: ") + reply->errorString();
                reply->deleteLater();
                onDone(result);
                return;
            }

            const QByteArray responseData = reply->readAll();
            reply->deleteLater();

            const QJsonDocument doc = QJsonDocument::fromJson(responseData);
            if (!doc.isObject()) {
                result.errorMessage = QStringLiteral("OCR response parsing failed.");
                onDone(result);
                return;
            }

            const QJsonObject root = doc.object();
            if (root.contains("error_code")) {
                const int code = root.value("error_code").toInt();
                const QString message = root.value("error_msg").toString();
                result.errorMessage = QStringLiteral("Baidu API error %1: %2").arg(code).arg(message);
                if (code == 110 || code == 111 || code == 1101) {
                    m_token.clear();
                }
                onDone(result);
                return;
            }

            const QJsonArray wordsResult = root.value("words_result").toArray();
            QStringList lines;
            QVector<OcrTextBox> boxes;
            boxes.reserve(wordsResult.size());

            for (const QJsonValue& value : wordsResult) {
                const QJsonObject item = value.toObject();
                const QString words = item.value("words").toString().trimmed();
                if (!words.isEmpty()) {
                    lines.append(words);
                }

                QRectF rect;
                const QJsonObject location = item.value("location").toObject();
                if (!location.isEmpty()) {
                    rect = QRectF(location.value("left").toDouble(),
                                  location.value("top").toDouble(),
                                  location.value("width").toDouble(),
                                  location.value("height").toDouble());
                } else {
                    const QJsonArray vertices = item.value("vertexes_location").toArray();
                    if (!vertices.isEmpty()) {
                        bool firstPoint = true;
                        double minX = 0.0;
                        double minY = 0.0;
                        double maxX = 0.0;
                        double maxY = 0.0;
                        for (const QJsonValue& vertexValue : vertices) {
                            const QJsonObject vertex = vertexValue.toObject();
                            const double x = vertex.value("x").toDouble();
                            const double y = vertex.value("y").toDouble();
                            if (firstPoint) {
                                minX = maxX = x;
                                minY = maxY = y;
                                firstPoint = false;
                            } else {
                                minX = qMin(minX, x);
                                minY = qMin(minY, y);
                                maxX = qMax(maxX, x);
                                maxY = qMax(maxY, y);
                            }
                        }
                        rect = QRectF(minX, minY, maxX - minX, maxY - minY);
                    }
                }

                if (rect.isValid() && rect.width() > 0.0 && rect.height() > 0.0) {
                    OcrTextBox box;
                    box.text = words;
                    box.imageRect = rect;
                    boxes.append(box);
                }
            }

            result.text = lines.join('\n');
            result.boxes = boxes;
            result.ok = true;
            onDone(result);
        });
    }

    RecognitionResult recognizeWithBoxes(const QImage& source)
    {
        RecognitionResult output;

        if (source.isNull()) {
            output.errorMessage = QStringLiteral("Failed to capture OCR area.");
            return output;
        }

        if (m_token.isEmpty() && !refreshToken()) {
            output.errorMessage = QStringLiteral("Failed to obtain Baidu API token. ") + m_lastError;
            return output;
        }

        QByteArray ba;
        QBuffer buffer(&ba);
        buffer.open(QIODevice::WriteOnly);
        source.save(&buffer, "JPG", 88);
        const QString base64 = ba.toBase64();

        QUrl url(QStringLiteral("https://aip.baidubce.com/rest/2.0/ocr/v1/general?access_token=") + m_token);
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

        QByteArray postData;
        postData += "image=" + QUrl::toPercentEncoding(base64);
        postData += "&detect_direction=false";
        postData += "&paragraph=false";
        postData += "&probability=false";

        QNetworkReply* reply = m_manager->post(request, postData);
        QEventLoop loop;
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        bool timedOut = false;
        QTimer timeoutTimer;
        timeoutTimer.setSingleShot(true);
        connect(&timeoutTimer, &QTimer::timeout, &loop, [&]() {
            timedOut = true;
            if (reply && reply->isRunning()) {
                reply->abort();
            }
            loop.quit();
        });
        timeoutTimer.start(kNetworkTimeoutMs);
        loop.exec();

        if (timedOut) {
            output.errorMessage = QStringLiteral("OCR network request timed out.");
            reply->deleteLater();
            return output;
        }

        if (reply->error() != QNetworkReply::NoError) {
            output.errorMessage = QStringLiteral("OCR network error: ") + reply->errorString();
            reply->deleteLater();
            return output;
        }

        const QByteArray responseData = reply->readAll();
        reply->deleteLater();

        const QJsonDocument doc = QJsonDocument::fromJson(responseData);
        if (!doc.isObject()) {
            output.errorMessage = QStringLiteral("OCR response parsing failed.");
            return output;
        }

        const QJsonObject root = doc.object();
        if (root.contains("error_code")) {
            const int code = root.value("error_code").toInt();
            const QString message = root.value("error_msg").toString();
            output.errorMessage = QStringLiteral("Baidu API error %1: %2").arg(code).arg(message);
            if (code == 110 || code == 111 || code == 1101) {
                m_token.clear();
            }
            return output;
        }

        const QJsonArray wordsResult = root.value("words_result").toArray();
        QStringList lines;
        QVector<OcrTextBox> boxes;
        boxes.reserve(wordsResult.size());

        for (const QJsonValue& value : wordsResult) {
            const QJsonObject item = value.toObject();
            const QString words = item.value("words").toString().trimmed();
            if (!words.isEmpty()) {
                lines.append(words);
            }

            QRectF rect;
            const QJsonObject location = item.value("location").toObject();
            if (!location.isEmpty()) {
                rect = QRectF(location.value("left").toDouble(),
                              location.value("top").toDouble(),
                              location.value("width").toDouble(),
                              location.value("height").toDouble());
            } else {
                const QJsonArray vertices = item.value("vertexes_location").toArray();
                if (!vertices.isEmpty()) {
                    bool firstPoint = true;
                    double minX = 0.0;
                    double minY = 0.0;
                    double maxX = 0.0;
                    double maxY = 0.0;
                    for (const QJsonValue& vertexValue : vertices) {
                        const QJsonObject vertex = vertexValue.toObject();
                        const double x = vertex.value("x").toDouble();
                        const double y = vertex.value("y").toDouble();
                        if (firstPoint) {
                            minX = maxX = x;
                            minY = maxY = y;
                            firstPoint = false;
                        } else {
                            minX = qMin(minX, x);
                            minY = qMin(minY, y);
                            maxX = qMax(maxX, x);
                            maxY = qMax(maxY, y);
                        }
                    }
                    rect = QRectF(minX, minY, maxX - minX, maxY - minY);
                }
            }

            if (rect.isValid() && rect.width() > 0.0 && rect.height() > 0.0) {
                OcrTextBox box;
                box.text = words;
                box.imageRect = rect;
                boxes.append(box);
            }
        }

        output.text = lines.join('\n');
        output.boxes = boxes;
        output.ok = true;
        return output;
    }

    bool refreshToken() {
        if (m_apiKey.isEmpty() || m_secretKey.isEmpty()) {
            m_lastError = "API Key or Secret Key is empty! Please check OCR_Baidu.h";
            return false;
        }

        // Check SSL Support
        if (!m_manager->supportedSchemes().contains("https")) {
            m_lastError = "OpenSSL not found! Please ensure libssl-1_1-x64.dll and libcrypto-1_1-x64.dll (v1.1.1) are in the application folder.";
            qWarning() << "[OCR_Baidu]" << m_lastError;
            return false;
        }

        QUrl url(QString("https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials&client_id=%1&client_secret=%2")
                 .arg(m_apiKey).arg(m_secretKey));
        
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
        
        QNetworkReply* reply = m_manager->post(request, QByteArray());
        
        QEventLoop loop;
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        bool timedOut = false;
        QTimer timeoutTimer;
        timeoutTimer.setSingleShot(true);
        connect(&timeoutTimer, &QTimer::timeout, &loop, [&]() {
            timedOut = true;
            if (reply && reply->isRunning()) {
                reply->abort();
            }
            loop.quit();
        });
        timeoutTimer.start(kNetworkTimeoutMs);
        loop.exec();

        if (timedOut) {
            m_lastError = "Baidu token request timed out.";
            reply->deleteLater();
            return false;
        }

        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            m_token = QJsonDocument::fromJson(data).object().value("access_token").toString();
            reply->deleteLater();
            if (m_token.isEmpty()) {
                m_lastError = "Baidu response did not contain access_token. Check your credentials.";
                return false;
            }
            return true;
        } else {
            m_lastError = reply->errorString();
            reply->deleteLater();
            return false;
        }
    }

    QNetworkAccessManager* m_manager = nullptr;
    QString m_token;
    QString m_lastError;
    QString m_hotkey;
    bool m_isConfiguring = false;
    bool m_unloading = false;
    bool m_recognitionInProgress = false;
    QImage m_pendingImage;
    QPointer<ScreenShot> m_screenShot = nullptr;
    QPointer<OcrResultDialog> m_resultDialog = nullptr;
    static constexpr int kNetworkTimeoutMs = 15000;
    
    // IMPORTANT: Users must fill these to use the plugin
    QString m_apiKey = ""; 
    QString m_secretKey = "";
};
