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
#include "../../src/Plugins/ILinPlugin.h"
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
            QString result = recognizeText(img);
            m_resultDialog->setResultText(result);
        });
    }

    // ILinPlugin Implementation
    QString pluginId() const override { return "com.lindesk.ocr.baidu"; }
    QString pluginName() const override { return "Baidu OCR Engine"; }
    QString pluginDescription() const override { return "Connects to Baidu AIP for high-precision text recognition"; }
    QString customHotkey() const override { return m_hotkey; }
    
    void onInit() override { 
        // Load configuration from INI file in the plugins folder
        QString iniDir = QCoreApplication::applicationDirPath() + "/plugins";
        QString iniPath = iniDir + "/ocr_config.ini";
        
        // Force directory creation to ensure file can be written
        QDir().mkpath(iniDir);

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
        if (m_screenShot) m_screenShot->deleteLater();
        if (m_resultDialog) m_resultDialog->deleteLater();
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
        
        qDebug() << "[OCR_Baidu] Global Hotkey/Trigger received! Starting ScreenShot...";
        m_screenShot->start();
    }

    // IOCRPlugin Implementation
    QString recognizeText(const QImage& source) override {
        if (m_token.isEmpty()) {
            if (!refreshToken()) return "Error: Failed to obtain Baidu API Token. \nDetails: " + m_lastError;
        }

        // Rest of the logic remains same, but let's make sure m_lastError is updated
        // 1. Convert QImage to Base64 (JPG format)
        // ... (preserving content)

        // 1. Convert QImage to Base64 (JPG format)
        QByteArray ba;
        QBuffer buffer(&ba);
        buffer.open(QIODevice::WriteOnly);
        source.save(&buffer, "JPG", 80);
        QString base64 = ba.toBase64();

        // 2. Prepare request to Baidu AIP General Basic OCR
        QUrl url("https://aip.baidubce.com/rest/2.0/ocr/v1/general_basic?access_token=" + m_token);
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

        // Baidu needs the base64 string to be URL-encoded.
        QByteArray postData = "image=" + QUrl::toPercentEncoding(base64);

        // 3. Perform Sync Network Request
        QNetworkReply* reply = m_manager->post(request, postData);
        QEventLoop loop;
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        if (reply->error() != QNetworkReply::NoError) {
            QString err = reply->errorString();
            reply->deleteLater();
            return "OCR Network Error: " + err;
        }

        QByteArray responseData = reply->readAll();
        reply->deleteLater();

        QJsonDocument doc = QJsonDocument::fromJson(responseData);
        QJsonObject root = doc.object();
        
        if (root.contains("error_code")) {
            return "Baidu API Error " + QString::number(root.value("error_code").toInt()) + ": " + root.value("error_msg").toString();
        }

        QJsonArray wordsResult = root.value("words_result").toArray();
        QString result;
        for (int i = 0; i < wordsResult.size(); ++i) {
            result += wordsResult.at(i).toObject().value("words").toString() + "\n";
        }

        qDebug() << "[OCR_Baidu] Recognition Result:\n" << result;
        return result.isEmpty() ? "No text detected." : result.trimmed();
    }

private:
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
        loop.exec();

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
    ScreenShot* m_screenShot = nullptr;
    OcrResultDialog* m_resultDialog = nullptr;
    
    // IMPORTANT: Users must fill these to use the plugin
    QString m_apiKey = ""; 
    QString m_secretKey = "";
};
