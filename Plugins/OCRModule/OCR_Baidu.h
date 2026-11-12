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
#include <QSet>
#include <QDate>
#include <functional>
#include "../../src/plugins/ILinPlugin.h"
#include "services/AppPaths.h"
#include "ScreenShot.h"
#include "OcrResultDialog.h"
#include "OcrSettingsDialog.h"
#include "OcrLocalEngine.h"

/**
 * @brief Baidu & Local Offline OCR Implementation
 */
class OCR_Baidu : public QObject, public IOCRPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID ILinPlugin_IID FILE "ocr_plugin.json")
    Q_INTERFACES(ILinPlugin IOCRPlugin)

public:
    enum class EngineType {
        Offline,
        Online
    };

    explicit OCR_Baidu(QObject* parent = nullptr) : QObject(parent) {
        m_manager = new QNetworkAccessManager(this);
        
        m_screenShot = new ScreenShot();
        m_resultDialog = new OcrResultDialog();
        m_localEngine.reset(new OcrLocalEngine(this));
        
        connect(m_screenShot, &ScreenShot::imageCaptured, this, [this](const QImage& img) {
            beginRecognitionSession(img);
        });
    }

    // ILinPlugin Implementation
    QString pluginId() const override { return "com.lindesk.ocr.baidu"; }
    QString pluginName() const override { return QStringLiteral("智能截图与文字识别"); }
    QString pluginDescription() const override {
        return QStringLiteral("支持屏幕截图、支持快捷键独立截图与文字提取。");
    }
    QString customHotkey() const override { return isConfigured() ? m_hotkey : QString(); }
    QString customSecondaryHotkey() const override { return isConfigured() ? m_screenshotHotkey : QString(); }
    bool isConfigured() const override {
        if (m_engineType == EngineType::Offline) {
            return OcrLocalEngine::isModelDirValid(m_offlineModelsDir);
        } else {
            return !m_apiKey.trimmed().isEmpty() &&
                   !m_secretKey.trimmed().isEmpty() &&
                   m_apiKey != QStringLiteral("YOUR_API_KEY_HERE") &&
                   m_secretKey != QStringLiteral("YOUR_SECRET_KEY_HERE");
        }
    }
    
    void onInit() override { 
        m_unloading = false;
        if (!m_screenShot) {
            m_screenShot = new ScreenShot();
            connect(m_screenShot, &ScreenShot::imageCaptured, this, [this](const QImage& img) {
                beginRecognitionSession(img);
            });
            connect(m_screenShot, &ScreenShot::screenshotCopied, this, [this](const QImage&) {
                qDebug() << "[OCR] Screenshot captured and copied to clipboard.";
            });
        }
        if (!m_resultDialog) {
            m_resultDialog = new OcrResultDialog();
        }
        if (!m_localEngine) {
            m_localEngine.reset(new OcrLocalEngine(this));
        }

        const QString iniPath = AppPaths::ocrConfigFilePath();
        QSettings settings(iniPath, QSettings::IniFormat);
        const QString defaultModelsDir = OcrLocalEngine::defaultModelsDir();

        if (settings.allKeys().isEmpty()) {
            settings.setValue("General/Engine", "offline");
            settings.setValue("General/Hotkey", "F4");
            settings.setValue("General/ScreenshotHotkey", "Alt+A");
            settings.setValue("Offline/ModelsDir", defaultModelsDir);
            settings.setValue("Offline/NumThreads", 4);
            settings.setValue("Offline/UseCls", false);
            settings.setValue("Baidu/AppId", "");
            settings.setValue("Baidu/ApiKey", "");
            settings.setValue("Baidu/SecretKey", "");
            settings.sync();
            qDebug() << "[OCR] Template config created at:" << iniPath;
            m_engineType = EngineType::Offline;
            m_appId.clear();
            m_apiKey.clear();
            m_secretKey.clear();
            m_hotkey = "F4";
            m_screenshotHotkey = "Alt+A";
            m_offlineModelsDir = defaultModelsDir;
            m_offlineThreads = 4;
            m_useCls = false;
        } else {
            QString engineStr = settings.value("General/Engine", "offline").toString().trimmed().toLower();
            m_engineType = (engineStr == QStringLiteral("online")) ? EngineType::Online : EngineType::Offline;
            m_hotkey = settings.value("General/Hotkey", settings.value("Baidu/Hotkey", "F4")).toString().trimmed();
            if (m_hotkey.isEmpty()) m_hotkey = QStringLiteral("F4");

            m_screenshotHotkey = settings.value("General/ScreenshotHotkey", "Alt+A").toString().trimmed();
            if (m_screenshotHotkey.isEmpty()) m_screenshotHotkey = QStringLiteral("Alt+A");

            m_offlineModelsDir = settings.value("Offline/ModelsDir", defaultModelsDir).toString().trimmed();
            if (m_offlineModelsDir.isEmpty() || !OcrLocalEngine::isModelDirValid(m_offlineModelsDir)) {
                m_offlineModelsDir = defaultModelsDir;
            }
            m_offlineThreads = qBound(OcrLocalEngine::MinThreads,
                                      settings.value("Offline/NumThreads", OcrLocalEngine::DefaultThreads).toInt(),
                                      OcrLocalEngine::MaxThreads);

            m_useCls = settings.value("Offline/UseCls", false).toBool();

            m_appId = settings.value("Baidu/AppId", "").toString().trimmed();
            m_apiKey = settings.value("Baidu/ApiKey", "").toString().trimmed();
            m_secretKey = settings.value("Baidu/SecretKey", "").toString().trimmed();
            if (m_apiKey == QStringLiteral("YOUR_API_KEY_HERE")) m_apiKey.clear();
            if (m_secretKey == QStringLiteral("YOUR_SECRET_KEY_HERE")) m_secretKey.clear();
            qDebug() << "[OCR] Configuration loaded from:" << iniPath;
            qDebug() << "[OCR] Engine:" << (m_engineType == EngineType::Offline ? "Offline" : "Online");
            qDebug() << "[OCR] OCR Hotkey:" << m_hotkey << "Screenshot Hotkey:" << m_screenshotHotkey;
        }

        m_localEngine->configure(m_offlineModelsDir, m_offlineThreads, m_useCls);

        qDebug() << "[OCR] Initialized. Configured:" << isConfigured(); 
        
        if (m_engineType == EngineType::Online) {
            if (isConfigured()) {
                refreshToken();
            } else {
                m_token.clear();
                qDebug() << "[OCR] Online API keys not configured. Standing by silently.";
            }
        }
    }
    void onUnload() override { 
        qDebug() << "[OCR] Unloaded."; 
        m_unloading = true;
        m_recognitionInProgress = false;
        m_pendingImage = QImage();
        if (m_localEngine) {
            m_localEngine.reset();
        }
        if (m_screenShot) {
            ScreenShot* screenShot = m_screenShot.data();
            screenShot->disconnect(this);
            screenShot->hide();
            m_screenShot = nullptr;
            delete screenShot;
        }
        if (m_resultDialog) {
            OcrResultDialog* resultDialog = m_resultDialog.data();
            resultDialog->disconnect(this);
            resultDialog->hide();
            m_resultDialog = nullptr;
            delete resultDialog;
        }
    }

    void onSettingsRequested(QWidget* parent = nullptr) override {
        m_isConfiguring = true;
        OcrSettingsDialog dlg(parent);
        if (dlg.exec() == QDialog::Accepted) {
            onInit(); // Refresh keys and internal state
            qDebug() << "[OCR] Settings applied. Configured:" << isConfigured();
        }
        m_isConfiguring = false;
    }

    void onTrigger() override {
        onTriggerAction(QStringLiteral("ocr"));
    }

    void onTriggerAction(const QString& action) override {
        if (m_isConfiguring) return; // Prevent triggering while recording
        if (m_unloading || !m_screenShot) return;
        if (!isConfigured()) {
            qWarning() << "[OCR] Global Hotkey/Trigger ignored: OCR engine is not configured.";
            return;
        }

        if (action == QStringLiteral("screenshot")) {
            qDebug() << "[OCR] Screenshot hotkey triggered! Starting ScreenShot in ScreenshotOnly mode...";
            m_screenShot->start(ScreenShot::CaptureMode::ScreenshotOnly);
        } else {
            qDebug() << "[OCR] OCR hotkey triggered! Starting ScreenShot in OCR mode...";
            m_screenShot->start(ScreenShot::CaptureMode::Ocr);
        }
    }

    // IOCRPlugin Implementation
    QString recognizeText(const QImage& source) override {
        if (!isConfigured()) {
            return QStringLiteral("OCR engine is not configured. Please open settings to configure.");
        }
        if (m_engineType == EngineType::Offline) {
            if (!m_localEngine) {
                m_localEngine.reset(new OcrLocalEngine(this));
                m_localEngine->configure(m_offlineModelsDir, m_offlineThreads, m_useCls);
            }
            auto res = m_localEngine->recognizeSync(source);
            if (!res.ok) {
                return res.errorMessage.isEmpty() ? QStringLiteral("Local OCR failed.") : res.errorMessage;
            }
            return res.text.isEmpty() ? QStringLiteral("No text detected.") : res.text;
        }

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

        if (m_engineType == EngineType::Offline) {
            if (!m_localEngine) {
                m_localEngine.reset(new OcrLocalEngine(this));
                m_localEngine->configure(m_offlineModelsDir, m_offlineThreads, m_useCls);
            }
            m_localEngine->recognizeAsync(sessionImage, [this, sessionImage](const OcrLocalEngine::LocalResult& localRes) {
                RecognitionResult res;
                res.ok = localRes.ok;
                res.text = localRes.text;
                res.boxes = localRes.boxes;
                res.errorMessage = localRes.errorMessage;
                finishRecognitionSession(sessionImage, res);
            });
            return;
        }

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

    struct BaiduOcrTier {
        QString id;
        QString name;
        QString endpoint;
        bool hasLocation;
    };

    static const QVector<BaiduOcrTier>& baiduTiers() {
        static const QVector<BaiduOcrTier> s_tiers = {
            {
                QStringLiteral("accurate"),
                QStringLiteral("高精度含位置版"),
                QStringLiteral("https://aip.baidubce.com/rest/2.0/ocr/v1/accurate"),
                true
            },
            {
                QStringLiteral("general"),
                QStringLiteral("标准含位置版"),
                QStringLiteral("https://aip.baidubce.com/rest/2.0/ocr/v1/general"),
                true
            },
            {
                QStringLiteral("general_basic"),
                QStringLiteral("标准版"),
                QStringLiteral("https://aip.baidubce.com/rest/2.0/ocr/v1/general_basic"),
                false
            },
            {
                QStringLiteral("accurate_basic"),
                QStringLiteral("高精度版"),
                QStringLiteral("https://aip.baidubce.com/rest/2.0/ocr/v1/accurate_basic"),
                false
            }
        };
        return s_tiers;
    }

    static bool isQuotaExhaustedError(int code, const QString& msg) {
        if (code == 17 || code == 18 || code == 19 || code == 1004 ||
            code == 282114 || code == 282115 || code == 282808) {
            return true;
        }
        const QString lower = msg.toLower();
        if (lower.contains(QStringLiteral("limit reached")) ||
            lower.contains(QStringLiteral("daily request limit")) ||
            lower.contains(QStringLiteral("total request limit")) ||
            lower.contains(QStringLiteral("qps request limit")) ||
            lower.contains(QStringLiteral("quota")) ||
            lower.contains(QStringLiteral("out of date")) ||
            lower.contains(QStringLiteral("no permission")) ||
            msg.contains(QStringLiteral("额度")) ||
            msg.contains(QStringLiteral("超限")) ||
            msg.contains(QStringLiteral("欠费"))) {
            return true;
        }
        return false;
    }

    void checkAndResetDailyQuotaTracker() {
        const QDate today = QDate::currentDate();
        if (m_exhaustedDate != today) {
            m_exhaustedDate = today;
            m_exhaustedTiers.clear();
        }
    }

    static RecognitionResult parseBaiduResponse(const QJsonObject& root, bool hasLocation) {
        RecognitionResult result;
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

            if (hasLocation) {
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
                        double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
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
        }

        result.text = lines.join('\n');
        result.boxes = boxes;
        result.ok = true;
        return result;
    }

    void ensureTokenAsync(const std::function<void(bool)>& onDone)
    {
        if (m_apiKey.isEmpty() || m_secretKey.isEmpty()) {
            m_lastError = "API Key or Secret Key is empty! Please check OCR settings.";
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
                reply->deleteLater();
                if (m_token.isEmpty()) {
                    m_lastError = "Baidu response did not contain access_token. Check your credentials.";
                    onDone(false);
                    return;
                }
                onDone(true);
                return;
            }

            m_lastError = reply->errorString();
            reply->deleteLater();
            onDone(false);
        });
    }

    void requestRecognitionTierAsync(int tierIndex,
                                     const QByteArray& postData,
                                     const QImage& sourceImage,
                                     const std::function<void(const RecognitionResult&)>& onDone)
    {
        const auto& tiers = baiduTiers();
        if (tierIndex < 0 || tierIndex >= tiers.size()) {
            qWarning() << "[OCR_Baidu] All 4 Baidu OCR tiers exhausted for today!";
            RecognitionResult allExhaustedResult;
            allExhaustedResult.ok = false;
            allExhaustedResult.errorMessage = QStringLiteral("百度在线识别额度已耗尽，请检查账户或切换为本地离线引擎。");
            onDone(allExhaustedResult);
            return;
        }

        if (m_exhaustedTiers.contains(tierIndex)) {
            requestRecognitionTierAsync(tierIndex + 1, postData, sourceImage, onDone);
            return;
        }

        const BaiduOcrTier& tier = tiers[tierIndex];
        const QUrl url(tier.endpoint + QStringLiteral("?access_token=") + m_token);
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

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

        connect(reply, &QNetworkReply::finished, this, [this, reply, tierIndex, postData, sourceImage, onDone]() {
            const auto& tiers = baiduTiers();
            const BaiduOcrTier& tier = tiers[tierIndex];

            if (reply->property("ocrTimedOut").toBool()) {
                reply->deleteLater();
                qWarning() << "[OCR_Baidu] Tier" << tier.name << "timed out, falling back to next tier...";
                requestRecognitionTierAsync(tierIndex + 1, postData, sourceImage, onDone);
                return;
            }

            if (reply->error() != QNetworkReply::NoError) {
                const QString errStr = reply->errorString();
                reply->deleteLater();
                qWarning() << "[OCR_Baidu] Tier" << tier.name << "network error:" << errStr << ", falling back to next tier...";
                requestRecognitionTierAsync(tierIndex + 1, postData, sourceImage, onDone);
                return;
            }

            const QByteArray responseData = reply->readAll();
            reply->deleteLater();

            const QJsonDocument doc = QJsonDocument::fromJson(responseData);
            if (!doc.isObject()) {
                qWarning() << "[OCR_Baidu] Tier" << tier.name << "JSON parse failed, falling back to next tier...";
                requestRecognitionTierAsync(tierIndex + 1, postData, sourceImage, onDone);
                return;
            }

            const QJsonObject root = doc.object();
            if (root.contains("error_code")) {
                const int code = root.value("error_code").toInt();
                const QString message = root.value("error_msg").toString();
                qWarning() << "[OCR_Baidu] Tier" << tier.name << "returned error" << code << ":" << message;

                if (code == 110 || code == 111 || code == 1101) {
                    m_token.clear();
                    ensureTokenAsync([this, tierIndex, postData, sourceImage, onDone](bool tokenOk) {
                        if (tokenOk) {
                            requestRecognitionTierAsync(tierIndex, postData, sourceImage, onDone);
                        } else {
                            RecognitionResult res;
                            res.ok = false;
                            res.errorMessage = QStringLiteral("百度鉴权 Token 刷新失败，请检查 API Key 和 Secret Key。");
                            onDone(res);
                        }
                    });
                    return;
                }

                if (isQuotaExhaustedError(code, message)) {
                    m_exhaustedTiers.insert(tierIndex);
                    qInfo() << "[OCR_Baidu] Tier" << tier.name << "free quota exhausted! Auto switching to next tier...";
                    requestRecognitionTierAsync(tierIndex + 1, postData, sourceImage, onDone);
                    return;
                }

                RecognitionResult result;
                result.ok = false;
                result.errorMessage = QStringLiteral("百度 API 错误 (%1): %2").arg(code).arg(message);
                onDone(result);
                return;
            }

            RecognitionResult result = parseBaiduResponse(root, tier.hasLocation);
            qInfo() << "[OCR_Baidu] Successfully recognized with tier:" << tier.name;
            onDone(result);
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

        checkAndResetDailyQuotaTracker();

        if (m_token.isEmpty()) {
            ensureTokenAsync([this, source, onDone](bool ok) {
                if (!ok) {
                    RecognitionResult failed;
                    failed.ok = false;
                    failed.errorMessage = QStringLiteral("获取百度鉴权 Token 失败: ") + m_lastError;
                    onDone(failed);
                    return;
                }
                requestRecognitionAsync(source, onDone);
            });
            return;
        }

        QByteArray ba;
        QBuffer buffer(&ba);
        buffer.open(QIODevice::WriteOnly);
        source.save(&buffer, "JPG", 88);
        const QString base64 = ba.toBase64();

        QByteArray postData;
        postData += "image=" + QUrl::toPercentEncoding(base64);
        postData += "&detect_direction=false";
        postData += "&paragraph=false";
        postData += "&probability=false";

        requestRecognitionTierAsync(0, postData, source, onDone);
    }

    RecognitionResult recognizeWithBoxes(const QImage& source)
    {
        RecognitionResult output;
        if (source.isNull()) {
            output.errorMessage = QStringLiteral("Failed to capture OCR area.");
            return output;
        }

        checkAndResetDailyQuotaTracker();

        if (m_token.isEmpty() && !refreshToken()) {
            output.errorMessage = QStringLiteral("Failed to obtain Baidu API token. ") + m_lastError;
            return output;
        }

        QByteArray ba;
        QBuffer buffer(&ba);
        buffer.open(QIODevice::WriteOnly);
        source.save(&buffer, "JPG", 88);
        const QString base64 = ba.toBase64();

        QByteArray postData;
        postData += "image=" + QUrl::toPercentEncoding(base64);
        postData += "&detect_direction=false";
        postData += "&paragraph=false";
        postData += "&probability=false";

        const auto& tiers = baiduTiers();
        for (int tierIndex = 0; tierIndex < tiers.size(); ++tierIndex) {
            if (m_exhaustedTiers.contains(tierIndex)) continue;

            const BaiduOcrTier& tier = tiers[tierIndex];
            QUrl url(tier.endpoint + QStringLiteral("?access_token=") + m_token);
            QNetworkRequest request(url);
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

            QNetworkReply* reply = m_manager->post(request, postData);
            QEventLoop loop;
            connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            bool timedOut = false;
            QTimer timeoutTimer;
            timeoutTimer.setSingleShot(true);
            connect(&timeoutTimer, &QTimer::timeout, &loop, [&]() {
                timedOut = true;
                if (reply && reply->isRunning()) reply->abort();
                loop.quit();
            });
            timeoutTimer.start(kNetworkTimeoutMs);
            loop.exec();

            if (timedOut || reply->error() != QNetworkReply::NoError) {
                reply->deleteLater();
                continue;
            }

            const QByteArray responseData = reply->readAll();
            reply->deleteLater();

            const QJsonDocument doc = QJsonDocument::fromJson(responseData);
            if (!doc.isObject()) continue;

            const QJsonObject root = doc.object();
            if (root.contains("error_code")) {
                const int code = root.value("error_code").toInt();
                const QString message = root.value("error_msg").toString();
                if (code == 110 || code == 111 || code == 1101) {
                    m_token.clear();
                    if (refreshToken()) {
                        tierIndex--; // retry
                    }
                    continue;
                }
                if (isQuotaExhaustedError(code, message)) {
                    m_exhaustedTiers.insert(tierIndex);
                    continue;
                }
                output.errorMessage = QStringLiteral("Baidu API error %1: %2").arg(code).arg(message);
                return output;
            }

            return parseBaiduResponse(root, tier.hasLocation);
        }

        output.ok = false;
        output.errorMessage = QStringLiteral("百度在线识别额度已耗尽，请检查账户或切换为本地离线引擎。");
        return output;
    }

    bool refreshToken() {
        if (m_apiKey.isEmpty() || m_secretKey.isEmpty()) {
            m_lastError = "API Key or Secret Key is empty! Please check OCR settings.";
            return false;
        }

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
    QString m_hotkey = "F4";
    QString m_screenshotHotkey = "Alt+A";
    bool m_useCls = false;
    bool m_isConfiguring = false;
    bool m_unloading = false;
    bool m_recognitionInProgress = false;
    QImage m_pendingImage;
    QPointer<ScreenShot> m_screenShot = nullptr;
    QPointer<OcrResultDialog> m_resultDialog = nullptr;
    static constexpr int kNetworkTimeoutMs = 15000;
    
    // User-configured credentials (stored in ocr_config.ini, never hardcoded)
    QString m_appId = "";
    QString m_apiKey = ""; 
    QString m_secretKey = "";
    QSet<int> m_exhaustedTiers;
    QDate m_exhaustedDate;

    EngineType m_engineType = EngineType::Offline;
    QString m_offlineModelsDir;
    int m_offlineThreads = 4;
    QScopedPointer<OcrLocalEngine> m_localEngine;
};
