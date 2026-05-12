#pragma once
#include <QDir>
#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QSettings>
#include <QCoreApplication>
#include <QDialog>
#include <QTimer>
#include <QVBoxLayout>
#include <functional>
#include "../../src/ui/Fluent/FluentBaseDialog.h"
#include "../../src/ui/Fluent/FluentButton.h"

// A specialized QLineEdit that captures key combos (no Q_OBJECT to avoid moc redefinition)
class HotkeyRecorder : public QLineEdit {
public:
    explicit HotkeyRecorder(QWidget* parent = nullptr) : QLineEdit(parent) {
        setReadOnly(true);
        setAlignment(Qt::AlignCenter);
    }

    void setOnHotkeyRecorded(std::function<void(QString)> cb) { m_callback = cb; }

    bool isRecording() const { return m_recording; }

    void setRecording(bool r) {
        m_recording = r;
        if (m_recording) {
            setFocus();
            setText("Recording...");
            setStyleSheet("background: white; border: 1px solid #f44336; border-radius: 6px; color: #f44336; font-weight: 700;");
        } else {
            setStyleSheet("background: rgba(0,0,0,0.03); border: 1px solid rgba(0,0,0,0.05); border-radius: 6px; color: #0078d4; font-weight: 600; padding: 10px;");
        }
    }

protected:
    void focusOutEvent(QFocusEvent* event) override {
        QLineEdit::focusOutEvent(event);
        if (m_recording) {
            setRecording(false);
            if (m_callback) m_callback("");
        }
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (!m_recording) {
            QLineEdit::keyPressEvent(event);
            return;
        }

        int key = event->key();
        if (key == Qt::Key_Escape) {
            setRecording(false);
            if (m_callback) m_callback("");
            return;
        }

        if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta) {
            return;
        }

        QStringList mods;
        if (event->modifiers() & Qt::ControlModifier) mods << "Ctrl";
        if (event->modifiers() & Qt::AltModifier) mods << "Alt";
        if (event->modifiers() & Qt::ShiftModifier) mods << "Shift";
        if (event->modifiers() & Qt::MetaModifier) mods << "Win";

        QString keyStr = QKeySequence(key).toString();
        QString combo = mods.isEmpty() ? keyStr : mods.join("+") + "+" + keyStr;
        
        // Update live, but STAY in recording mode for repeated trials
        if (m_callback) m_callback(combo);
    }

private:
    bool m_recording = false;
    std::function<void(QString)> m_callback;
};

class OcrSettingsDialog : public FluentBaseDialog {
    Q_OBJECT
public:
    explicit OcrSettingsDialog(QWidget* parent = nullptr) : FluentBaseDialog(parent) {
        setWindowTitle("OCR Intelligence Settings");
        setFixedSize(400, 420);

        QVBoxLayout* layout = centralLayout();
        layout->setContentsMargins(20, 20, 20, 20);
        layout->setSpacing(15);

        // 1. Header
        QLabel* title = new QLabel("Baidu OCR Settings", container());
        title->setStyleSheet("font-size: 18px; font-weight: 600; color: #333;");
        layout->addWidget(title);

        // 2. Inputs
        auto createField = [&](const QString& label, const QString& placeholder) {
            auto* v = new QVBoxLayout();
            v->setSpacing(5);
            auto* l = new QLabel(label, container());
            l->setStyleSheet("font-size: 12px; font-weight: 600; color: #666;");
            auto* e = new QLineEdit(container());
            e->setPlaceholderText(placeholder);
            e->setStyleSheet(
                "QLineEdit { background: rgba(0,0,0,0.03); border: 1px solid rgba(0,0,0,0.05); border-radius: 6px; padding: 8px; font-size: 13px; } "
                "QLineEdit:focus { background: white; border: 1px solid #0078d4; } "
            );
            v->addWidget(l);
            v->addWidget(e);
            layout->addLayout(v);
            return e;
        };

        m_keyInput = createField("API Key", "Paste your Baidu API Key");
        m_secretInput = createField("Secret Key", "Paste your Secret Key");
        
        // --- Hotkey Configuration ---
        auto* vCol = new QVBoxLayout();
        vCol->setSpacing(5);
        auto* lHk = new QLabel("Trigger Hotkey", container());
        lHk->setStyleSheet("font-size: 12px; font-weight: 600; color: #666;");
        
        QHBoxLayout* hkLayout = new QHBoxLayout();
        hkLayout->setSpacing(8);
        
        m_hotkeyInput = new HotkeyRecorder(container());
        m_modifyBtn = new FluentButton("Modify", container());
        m_modifyBtn->setAccent(false);
        m_modifyBtn->setFixedSize(80, 32);
        
        connect(m_modifyBtn, &QPushButton::clicked, m_hotkeyInput, [this](){ 
            m_modifyBtn->hide();
            m_hotkeyInput->setRecording(true); 
        });

        m_hotkeyInput->setOnHotkeyRecorded([this](const QString& combo){
            m_modifyBtn->show();
            if (!combo.isEmpty()) {
                m_currentHotkey = combo;
                m_hotkeyInput->setText(m_currentHotkey);
            }
        });

        hkLayout->addWidget(m_hotkeyInput, 1);
        hkLayout->addWidget(m_modifyBtn);
        vCol->addWidget(lHk);
        vCol->addLayout(hkLayout);
        layout->addLayout(vCol);

        // Load existing
        QString iniPath = QCoreApplication::applicationDirPath() + "/plugins/ocr_config.ini";
        QSettings settings(iniPath, QSettings::IniFormat);
        m_keyInput->setText(settings.value("Baidu/ApiKey").toString());
        m_secretInput->setText(settings.value("Baidu/SecretKey").toString());
        m_currentHotkey = settings.value("Baidu/Hotkey", "F4").toString();
        m_hotkeyInput->setText(m_currentHotkey);
        m_hotkeyInput->setRecording(false); // Apply initial styles

        // --- Credential Test ---
        QHBoxLayout* testLayout = new QHBoxLayout();
        testLayout->setSpacing(8);
        m_testBtn = new FluentButton("Test API Key", container());
        m_testBtn->setAccent(false);
        m_testBtn->setFixedSize(110, 32);
        m_testStatus = new QLabel(container());
        m_testStatus->setStyleSheet("font-size: 12px; color: #666;");
        m_testStatus->setText("Click to verify credentials");

        connect(m_testBtn, &QPushButton::clicked, this, [this]() {
            const QString apiKey = m_keyInput->text().trimmed();
            const QString secretKey = m_secretInput->text().trimmed();

            auto setStatus = [this](const QString& text, const QString& color) {
                m_testStatus->setStyleSheet(QString("font-size: 12px; color: %1;").arg(color));
                m_testStatus->setText(text);
            };

            if (apiKey.isEmpty() || secretKey.isEmpty()) {
                setStatus("Please fill API Key and Secret Key first", "#d14343");
                return;
            }

            m_testBtn->setEnabled(false);
            m_testBtn->setText("Testing...");
            setStatus("Verifying credentials...", "#666");
            QApplication::processEvents();

            QNetworkAccessManager manager;
            const QUrl url(QString("https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials&client_id=%1&client_secret=%2")
                           .arg(apiKey, secretKey));
            QNetworkRequest request(url);
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

            QNetworkReply* reply = manager.post(request, QByteArray());
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
            timeoutTimer.start(15000);
            loop.exec();

            if (timedOut) {
                setStatus("Network request timed out", "#d14343");
                reply->deleteLater();
                m_testBtn->setEnabled(true);
                m_testBtn->setText("Test API Key");
                return;
            }

            if (reply->error() != QNetworkReply::NoError) {
                setStatus("Network error: " + reply->errorString(), "#d14343");
                reply->deleteLater();
                m_testBtn->setEnabled(true);
                m_testBtn->setText("Test API Key");
                return;
            }

            const QByteArray responseData = reply->readAll();
            reply->deleteLater();
            const QJsonDocument doc = QJsonDocument::fromJson(responseData);
            const QJsonObject root = doc.object();
            const QString token = root.value("access_token").toString();

            if (!token.isEmpty()) {
                setStatus("Credentials are valid", "#2e8b57");
            } else {
                const QString err = root.value("error_description").toString();
                const QString fallback = root.value("error").toString();
                const QString message = err.isEmpty() ? (fallback.isEmpty() ? "Invalid credentials" : fallback) : err;
                setStatus("Validation failed: " + message, "#d14343");
            }

            m_testBtn->setEnabled(true);
            m_testBtn->setText("Test API Key");
        });

        testLayout->addWidget(m_testBtn);
        testLayout->addWidget(m_testStatus, 1);
        layout->addLayout(testLayout);

        layout->addStretch();

        // 3. Actions
        QHBoxLayout* btnLayout = new QHBoxLayout();
        FluentButton* saveBtn = new FluentButton("Save && Restart Plugin", container());
        saveBtn->setFixedSize(160, 36);
        
        FluentButton* cancelBtn = new FluentButton("Cancel", container());
        cancelBtn->setAccent(false);
        cancelBtn->setFixedSize(80, 36);

        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
        connect(saveBtn, &QPushButton::clicked, this, [this, iniPath]() {
            QSettings s(iniPath, QSettings::IniFormat);
            s.setValue("Baidu/ApiKey", m_keyInput->text().trimmed());
            s.setValue("Baidu/SecretKey", m_secretInput->text().trimmed());
            s.setValue("Baidu/Hotkey", m_currentHotkey);
            s.sync();
            accept();
        });

        btnLayout->addStretch();
        btnLayout->addWidget(cancelBtn);
        btnLayout->addWidget(saveBtn);
        layout->addLayout(btnLayout);

        // Prevent auto-focus on first text box for a cleaner start
        m_keyInput->clearFocus();
    }

private:
    QLineEdit* m_keyInput = nullptr;
    QLineEdit* m_secretInput = nullptr;
    HotkeyRecorder* m_hotkeyInput = nullptr;
    FluentButton* m_modifyBtn = nullptr;
    FluentButton* m_testBtn = nullptr;
    QLabel* m_testStatus = nullptr;
    QString m_currentHotkey;
};
