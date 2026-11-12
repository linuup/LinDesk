#pragma once
#include <QDir>
#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QIntValidator>
#include <QPushButton>
#include <QCheckBox>
#include <QFrame>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QSettings>
#include <QCoreApplication>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <functional>
#include "services/AppPaths.h"
#include "../../src/ui/Fluent/FluentDialog.h"
#include "../../src/ui/Fluent/FluentButton.h"
#include "../../src/ui/Fluent/FluentInput.h"
#include "../../src/ui/Fluent/FluentComboBox.h"
#include "OcrLocalEngine.h"

// Specialized QLineEdit that captures global hotkey combinations with live continuous switching
class HotkeyRecorder : public QLineEdit {
public:
    explicit HotkeyRecorder(QWidget* parent = nullptr) : QLineEdit(parent) {
        setReadOnly(true);
        setAlignment(Qt::AlignCenter);
        setFixedHeight(34);
        updateStyle(false);
    }

    void setOnHotkeyChanged(std::function<void(QString)> cb) { m_callback = std::move(cb); }
    bool isRecording() const { return m_recording; }

    void startRecording() {
        m_recording = true;
        m_originalKey = text().trimmed();
        setFocus();
        setText(tr("请按下快捷键组合..."));
        updateStyle(true);
    }

    void finishRecording() {
        if (!m_recording) return;
        m_recording = false;
        if (m_stagedKey.isEmpty()) {
            m_stagedKey = m_originalKey;
        }
        setText(m_stagedKey);
        updateStyle(false);
        if (m_callback) {
            m_callback(m_stagedKey);
        }
    }

    void cancelRecording() {
        if (!m_recording) return;
        m_recording = false;
        m_stagedKey = m_originalKey;
        setText(m_originalKey);
        updateStyle(false);
        if (m_callback) {
            m_callback(m_originalKey);
        }
    }

    void setHotkey(const QString& key) {
        m_stagedKey = key;
        m_originalKey = key;
        setText(key);
    }

    QString currentHotkey() const {
        return m_stagedKey.isEmpty() ? m_originalKey : m_stagedKey;
    }

protected:
    void focusOutEvent(QFocusEvent* event) override {
        QLineEdit::focusOutEvent(event);
        if (m_recording) {
            finishRecording();
        }
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (!m_recording) {
            QLineEdit::keyPressEvent(event);
            return;
        }

        int key = event->key();

        if (key == Qt::Key_Escape) {
            cancelRecording();
            event->accept();
            return;
        }

        if (key == Qt::Key_Backspace || key == Qt::Key_Delete) {
            m_stagedKey = QStringLiteral("None");
            setText(m_stagedKey);
            event->accept();
            return;
        }

        if (key == Qt::Key_Control || key == Qt::Key_Shift || 
            key == Qt::Key_Alt || key == Qt::Key_Meta) {
            event->accept();
            return;
        }

        QStringList parts;
        Qt::KeyboardModifiers mods = event->modifiers();
        if (mods & Qt::ControlModifier) parts << QStringLiteral("Ctrl");
        if (mods & Qt::AltModifier) parts << QStringLiteral("Alt");
        if (mods & Qt::ShiftModifier) parts << QStringLiteral("Shift");
        if (mods & Qt::MetaModifier) parts << QStringLiteral("Win");

        QString keyText;
        if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
            keyText = QStringLiteral("F%1").arg(key - Qt::Key_F1 + 1);
        } else if (key >= Qt::Key_A && key <= Qt::Key_Z) {
            keyText = QString(QChar('A' + (key - Qt::Key_A)));
        } else if (key >= Qt::Key_0 && key <= Qt::Key_9) {
            keyText = QString(QChar('0' + (key - Qt::Key_0)));
        } else if (key == Qt::Key_Print) {
            keyText = QStringLiteral("PrintScreen");
        } else if (key == Qt::Key_Space) {
            keyText = QStringLiteral("Space");
        } else if (key == Qt::Key_Tab) {
            keyText = QStringLiteral("Tab");
        } else {
            keyText = QKeySequence(key).toString();
        }

        if (keyText.isEmpty()) {
            event->accept();
            return;
        }

        parts << keyText;
        m_stagedKey = parts.join(QStringLiteral("+"));
        setText(m_stagedKey);
        event->accept();
    }

private:
    void updateStyle(bool active) {
        if (active) {
            setStyleSheet(QStringLiteral(
                "QLineEdit {"
                "  background-color: #eff6fc;"
                "  border: 1.5px solid #0067c0;"
                "  border-radius: 6px;"
                "  color: #0067c0;"
                "  font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI';"
                "  font-size: 13px;"
                "  font-weight: 600;"
                "  padding: 4px 8px;"
                "}"
            ));
        } else {
            setStyleSheet(QStringLiteral(
                "QLineEdit {"
                "  background-color: #f9f9f9;"
                "  border: 1px solid #e5e5e5;"
                "  border-radius: 6px;"
                "  color: #1b1b1b;"
                "  font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI';"
                "  font-size: 13px;"
                "  font-weight: 500;"
                "  padding: 4px 8px;"
                "}"
            ));
        }
    }

    bool m_recording = false;
    QString m_originalKey;
    QString m_stagedKey;
    std::function<void(QString)> m_callback;
};

class OcrSettingsDialog : public FluentDialog {
    Q_OBJECT
public:
    explicit OcrSettingsDialog(QWidget* parent = nullptr) : FluentDialog(parent) {
        setDialogTitle(tr("截图与文字识别配置"));
        setMessage(tr("配置全局快捷键、端侧离线高精引擎与云端在线识别服务。"));

        QWidget* formContainer = new QWidget(this);
        formContainer->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
        QVBoxLayout* formLayout = new QVBoxLayout(formContainer);
        formLayout->setContentsMargins(0, 4, 0, 4);
        formLayout->setSpacing(14);

        // 1. Hotkeys Configuration Group
        QWidget* hkCard = new QWidget(formContainer);
        hkCard->setStyleSheet(QStringLiteral(
            "background-color: #ffffff; border: 1px solid #e5e5e5; border-radius: 8px;"));
        QVBoxLayout* hkCardLayout = new QVBoxLayout(hkCard);
        hkCardLayout->setContentsMargins(14, 12, 14, 12);
        hkCardLayout->setSpacing(10);

        QLabel* hkCardTitle = new QLabel(tr("全局快捷键"), hkCard);
        hkCardTitle->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 600; color: #1b1b1b; border: none;"));
        hkCardLayout->addWidget(hkCardTitle);

        // OCR Hotkey Row
        QHBoxLayout* ocrHkRow = new QHBoxLayout();
        ocrHkRow->setSpacing(8);
        QLabel* ocrHkLabel = new QLabel(tr("截屏文字识别:"), hkCard);
        ocrHkLabel->setStyleSheet(QStringLiteral("font-size: 13px; color: #1b1b1b; border: none;"));
        ocrHkLabel->setFixedWidth(120);

        m_ocrHotkeyInput = new HotkeyRecorder(hkCard);
        m_modifyOcrBtn = new FluentButton(tr("修改按键"), hkCard);
        m_modifyOcrBtn->setAccent(false);
        m_modifyOcrBtn->setFixedHeight(34);
        m_modifyOcrBtn->setFixedWidth(90);

        ocrHkRow->addWidget(ocrHkLabel);
        ocrHkRow->addWidget(m_ocrHotkeyInput, 1);
        ocrHkRow->addWidget(m_modifyOcrBtn, 0);
        hkCardLayout->addLayout(ocrHkRow);

        // Screenshot Only Hotkey Row
        QHBoxLayout* shotHkRow = new QHBoxLayout();
        shotHkRow->setSpacing(8);
        QLabel* shotHkLabel = new QLabel(tr("快捷截图取图:"), hkCard);
        shotHkLabel->setStyleSheet(QStringLiteral("font-size: 13px; color: #1b1b1b; border: none;"));
        shotHkLabel->setFixedWidth(120);

        m_shotHotkeyInput = new HotkeyRecorder(hkCard);
        m_modifyShotBtn = new FluentButton(tr("修改按键"), hkCard);
        m_modifyShotBtn->setAccent(false);
        m_modifyShotBtn->setFixedHeight(34);
        m_modifyShotBtn->setFixedWidth(90);

        shotHkRow->addWidget(shotHkLabel);
        shotHkRow->addWidget(m_shotHotkeyInput, 1);
        shotHkRow->addWidget(m_modifyShotBtn, 0);
        hkCardLayout->addLayout(shotHkRow);

        formLayout->addWidget(hkCard);

        // 2. Engine Type Selector
        QVBoxLayout* engineLayout = new QVBoxLayout();
        engineLayout->setSpacing(4);
        QLabel* engineLabel = new QLabel(tr("识别服务引擎"), formContainer);
        engineLabel->setStyleSheet(QStringLiteral(
            "font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI';"
            "font-size: 13px; font-weight: 600; color: #1b1b1b;"));

        m_engineCombo = new FluentComboBox(formContainer);
        m_engineCombo->setFixedWidth(340);
        m_engineCombo->addItem(tr("本地离线识别"), QStringLiteral("offline"));
        m_engineCombo->addItem(tr("百度智能云 AIP"), QStringLiteral("online"));
        engineLayout->addWidget(engineLabel);
        engineLayout->addWidget(m_engineCombo);
        formLayout->addLayout(engineLayout);

        // 3. Offline Settings Group
        auto createCardDivider = [](QWidget* parent) -> QFrame* {
            QFrame* line = new QFrame(parent);
            line->setFrameShape(QFrame::HLine);
            line->setFrameShadow(QFrame::Plain);
            line->setStyleSheet(QStringLiteral("background-color: #f0f0f0; max-height: 1px; border: none;"));
            return line;
        };

        m_offlineGroup = new QWidget(formContainer);
        m_offlineGroup->setStyleSheet(QStringLiteral(
            "background-color: #ffffff; border: 1px solid #e5e5e5; border-radius: 8px;"));
        QVBoxLayout* offlineLayout = new QVBoxLayout(m_offlineGroup);
        offlineLayout->setContentsMargins(16, 14, 16, 16);
        offlineLayout->setSpacing(12);

        QLabel* offlineCardTitle = new QLabel(tr("本地离线推理配置 (PP-OCRv6 原生加速)"), m_offlineGroup);
        offlineCardTitle->setStyleSheet(QStringLiteral(
            "font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI';"
            "font-size: 13px; font-weight: 600; color: #1b1b1b; border: none;"));
        offlineLayout->addWidget(offlineCardTitle);

        // Row 1: Inference Thread Count
        QHBoxLayout* threadRow = new QHBoxLayout();
        threadRow->setSpacing(16);
        QVBoxLayout* threadTextCol = new QVBoxLayout();
        threadTextCol->setSpacing(2);
        QLabel* threadTitle = new QLabel(tr("推理线程数"), m_offlineGroup);
        threadTitle->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 500; color: #1b1b1b; border: none;"));
        QLabel* threadSubtitle = new QLabel(tr("CPU 多线程并行加速，范围 1 - 16，默认推荐 4 线程"), m_offlineGroup);
        threadSubtitle->setStyleSheet(QStringLiteral("font-size: 12px; color: #71717a; border: none;"));
        threadTextCol->addWidget(threadTitle);
        threadTextCol->addWidget(threadSubtitle);

        m_threadsInput = new FluentInput(m_offlineGroup);
        m_threadsInput->setFixedWidth(64);
        m_threadsInput->setAlignment(Qt::AlignCenter);
        m_threadsInput->setValidator(new QIntValidator(OcrLocalEngine::MinThreads, OcrLocalEngine::MaxThreads, m_threadsInput));
        m_threadsInput->setPlaceholderText(QStringLiteral("1-16"));
        m_threadsInput->setText(QString::number(OcrLocalEngine::DefaultThreads));
        m_threadsInput->setToolTip(tr("CPU 推理线程数 (范围 1 - 16，推荐 4 线程)"));

        threadRow->addLayout(threadTextCol, 1);
        threadRow->addWidget(m_threadsInput, 0, Qt::AlignVCenter);
        offlineLayout->addLayout(threadRow);

        offlineLayout->addWidget(createCardDivider(m_offlineGroup));

        // Row 2: Direction Classifier Toggle
        QHBoxLayout* clsRow = new QHBoxLayout();
        clsRow->setSpacing(16);
        QVBoxLayout* clsTextCol = new QVBoxLayout();
        clsTextCol->setSpacing(2);
        QLabel* clsTitle = new QLabel(tr("文本方向分类器"), m_offlineGroup);
        clsTitle->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 500; color: #1b1b1b; border: none;"));
        QLabel* clsSubtitle = new QLabel(tr("自动纠正 180° 倒置文本，按需开启以避免增加单次耗时"), m_offlineGroup);
        clsSubtitle->setStyleSheet(QStringLiteral("font-size: 12px; color: #71717a; border: none;"));
        clsTextCol->addWidget(clsTitle);
        clsTextCol->addWidget(clsSubtitle);

        m_useClsCheckBox = new QCheckBox(tr("开启"), m_offlineGroup);
        m_useClsCheckBox->setStyleSheet(QStringLiteral(
            "QCheckBox { font-size: 13px; color: #1b1b1b; border: none; } "
            "QCheckBox::indicator { width: 16px; height: 16px; }"
        ));

        clsRow->addLayout(clsTextCol, 1);
        clsRow->addWidget(m_useClsCheckBox, 0, Qt::AlignVCenter);
        offlineLayout->addLayout(clsRow);

        offlineLayout->addWidget(createCardDivider(m_offlineGroup));

        // Row 3: Model Health & Diagnostic
        QHBoxLayout* testOfflineRow = new QHBoxLayout();
        testOfflineRow->setSpacing(16);
        QVBoxLayout* statusTextCol = new QVBoxLayout();
        statusTextCol->setSpacing(2);
        QLabel* statusTitle = new QLabel(tr("端侧模型状态"), m_offlineGroup);
        statusTitle->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 500; color: #1b1b1b; border: none;"));
        m_offlineStatusLabel = new QLabel(m_offlineGroup);
        m_offlineStatusLabel->setStyleSheet(QStringLiteral(
            "font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI';"
            "font-size: 12px; color: #5c5c5c; border: none;"));
        m_offlineStatusLabel->setWordWrap(true);
        statusTextCol->addWidget(statusTitle);
        statusTextCol->addWidget(m_offlineStatusLabel);

        m_testOfflineBtn = new FluentButton(tr("测试引擎"), m_offlineGroup);
        m_testOfflineBtn->setAccent(false);
        m_testOfflineBtn->setFixedHeight(32);
        m_testOfflineBtn->setFixedWidth(88);

        testOfflineRow->addLayout(statusTextCol, 1);
        testOfflineRow->addWidget(m_testOfflineBtn, 0, Qt::AlignVCenter);
        offlineLayout->addLayout(testOfflineRow);

        connect(m_testOfflineBtn, &QPushButton::clicked, this, [this]() {
            if (OcrLocalEngine::isModelDirValid(m_modelsDir)) {
                m_offlineStatusLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #16a34a; font-weight: 600; border: none;"));
                m_offlineStatusLabel->setText(tr("推理引擎已就绪"));
            } else {
                m_offlineStatusLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #c42b1c; border: none;"));
                m_offlineStatusLabel->setText(tr("未检测到内置模型文件，请检查安装完整性"));
            }
        });

        formLayout->addWidget(m_offlineGroup);

        // 4. Online Settings Group (Baidu AIP Card)
        m_onlineGroup = new QWidget(formContainer);
        m_onlineGroup->setStyleSheet(QStringLiteral(
            "background-color: #ffffff; border: 1px solid #e5e5e5; border-radius: 8px;"));
        QVBoxLayout* onlineLayout = new QVBoxLayout(m_onlineGroup);
        onlineLayout->setContentsMargins(16, 14, 16, 16);
        onlineLayout->setSpacing(12);

        QLabel* onlineCardTitle = new QLabel(tr("百度智能云配置 (在线高精识别)"), m_onlineGroup);
        onlineCardTitle->setStyleSheet(QStringLiteral(
            "font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI';"
            "font-size: 13px; font-weight: 600; color: #1b1b1b; border: none;"));
        onlineLayout->addWidget(onlineCardTitle);

        QVBoxLayout* appIdLayout = new QVBoxLayout();
        appIdLayout->setSpacing(4);
        QLabel* appIdLabel = new QLabel(tr("AppID (可选)"), m_onlineGroup);
        appIdLabel->setStyleSheet(QStringLiteral(
            "font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI';"
            "font-size: 13px; font-weight: 600; color: #1b1b1b;"));
        m_appIdInput = new FluentInput(m_onlineGroup);
        m_appIdInput->setPlaceholderText(tr("输入百度智能云 AppID"));
        appIdLayout->addWidget(appIdLabel);
        appIdLayout->addWidget(m_appIdInput);
        onlineLayout->addLayout(appIdLayout);

        QVBoxLayout* keyLayout = new QVBoxLayout();
        keyLayout->setSpacing(4);
        QLabel* keyLabel = new QLabel(tr("API Key (Client ID)"), m_onlineGroup);
        keyLabel->setStyleSheet(QStringLiteral(
            "font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI';"
            "font-size: 13px; font-weight: 600; color: #1b1b1b;"));
        m_keyInput = new FluentInput(m_onlineGroup);
        m_keyInput->setPlaceholderText(tr("输入百度智能云 API Key"));
        keyLayout->addWidget(keyLabel);
        keyLayout->addWidget(m_keyInput);
        onlineLayout->addLayout(keyLayout);

        QVBoxLayout* secretLayout = new QVBoxLayout();
        secretLayout->setSpacing(4);
        QLabel* secretLabel = new QLabel(tr("Secret Key (Client Secret)"), m_onlineGroup);
        secretLabel->setStyleSheet(QStringLiteral(
            "font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI';"
            "font-size: 13px; font-weight: 600; color: #1b1b1b;"));
        m_secretInput = new FluentInput(m_onlineGroup);
        m_secretInput->setPlaceholderText(tr("输入百度智能云 Secret Key"));
        secretLayout->addWidget(secretLabel);
        secretLayout->addWidget(m_secretInput);
        onlineLayout->addLayout(secretLayout);

        QHBoxLayout* testOnlineRow = new QHBoxLayout();
        testOnlineRow->setSpacing(10);
        m_testOnlineBtn = new FluentButton(tr("测试连接"), m_onlineGroup);
        m_testOnlineBtn->setAccent(false);
        m_testOnlineBtn->setFixedHeight(32);
        m_testOnlineBtn->setFixedWidth(90);

        m_onlineStatusLabel = new QLabel(tr("点击测试 API 凭证有效性"), m_onlineGroup);
        m_onlineStatusLabel->setStyleSheet(QStringLiteral(
            "font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI';"
            "font-size: 12px; color: #5c5c5c;"));
        m_onlineStatusLabel->setWordWrap(true);

        testOnlineRow->addWidget(m_testOnlineBtn, 0, Qt::AlignVCenter);
        testOnlineRow->addWidget(m_onlineStatusLabel, 1, Qt::AlignVCenter);
        onlineLayout->addLayout(testOnlineRow);

        connect(m_testOnlineBtn, &QPushButton::clicked, this, [this]() {
            const QString apiKey = m_keyInput->text().trimmed();
            const QString secretKey = m_secretInput->text().trimmed();

            auto setStatus = [this](const QString& text, const QString& color) {
                m_onlineStatusLabel->setStyleSheet(QString(
                    "font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI'; "
                    "font-size: 12px; color: %1;").arg(color));
                m_onlineStatusLabel->setText(text);
            };

            if (apiKey.isEmpty() || secretKey.isEmpty() || apiKey == "YOUR_API_KEY_HERE" || secretKey == "YOUR_SECRET_KEY_HERE") {
                setStatus(tr("请先填写有效的 API Key 和 Secret Key"), "#c42b1c");
                return;
            }

            m_testOnlineBtn->setEnabled(false);
            m_testOnlineBtn->setText(tr("测试中..."));
            setStatus(tr("正在连接百度云鉴权服务..."), "#0067c0");
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
                if (reply && reply->isRunning()) reply->abort();
                loop.quit();
            });
            timeoutTimer.start(10000);
            loop.exec();

            if (timedOut) {
                setStatus(tr("网络请求超时，请检查网络"), "#c42b1c");
            } else if (reply->error() != QNetworkReply::NoError) {
                setStatus(tr("网络错误: %1").arg(reply->errorString()), "#c42b1c");
            } else {
                const QByteArray responseData = reply->readAll();
                const QJsonDocument doc = QJsonDocument::fromJson(responseData);
                const QJsonObject root = doc.object();
                const QString token = root.value("access_token").toString();

                if (!token.isEmpty()) {
                    setStatus(tr("凭证有效，连接成功"), "#16a34a");
                } else {
                    const QString errDesc = root.value("error_description").toString();
                    const QString fallback = root.value("error").toString();
                    const QString message = !errDesc.isEmpty() ? errDesc : (!fallback.isEmpty() ? fallback : tr("未知错误"));
                    setStatus(tr("验证失败: %1").arg(message), "#c42b1c");
                }
            }

            reply->deleteLater();
            m_testOnlineBtn->setEnabled(true);
            m_testOnlineBtn->setText(tr("测试连接"));
        });

        formLayout->addWidget(m_onlineGroup);

        // 5. Engine Toggle Switch Event
        connect(m_engineCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
            bool isOffline = (index == 0);
            m_offlineGroup->setVisible(isOffline);
            m_onlineGroup->setVisible(!isOffline);
        });

        // Hotkey recording handlers
        connect(m_modifyOcrBtn, &QPushButton::clicked, this, [this]() {
            if (m_ocrHotkeyInput->isRecording()) {
                m_ocrHotkeyInput->finishRecording();
                m_modifyOcrBtn->setText(tr("修改按键"));
            } else {
                m_ocrHotkeyInput->startRecording();
                m_modifyOcrBtn->setText(tr("完成按键"));
            }
        });

        connect(m_modifyShotBtn, &QPushButton::clicked, this, [this]() {
            if (m_shotHotkeyInput->isRecording()) {
                m_shotHotkeyInput->finishRecording();
                m_modifyShotBtn->setText(tr("修改按键"));
            } else {
                m_shotHotkeyInput->startRecording();
                m_modifyShotBtn->setText(tr("完成按键"));
            }
        });

        contentLayout()->insertWidget(contentLayout()->count() - 1, formContainer);

        // Load existing configuration
        const QString iniPath = AppPaths::ocrConfigFilePath();
        QSettings settings(iniPath, QSettings::IniFormat);

        const QString defaultModelsDir = OcrLocalEngine::defaultModelsDir();
        m_modelsDir = settings.value("Offline/ModelsDir", defaultModelsDir).toString().trimmed();
        if (m_modelsDir.isEmpty() || !OcrLocalEngine::isModelDirValid(m_modelsDir)) {
            m_modelsDir = defaultModelsDir;
        }
        const int loadedThreads = qBound(OcrLocalEngine::MinThreads,
                                         settings.value("Offline/NumThreads", OcrLocalEngine::DefaultThreads).toInt(),
                                         OcrLocalEngine::MaxThreads);
        m_threadsInput->setText(QString::number(loadedThreads));
        m_useClsCheckBox->setChecked(settings.value("Offline/UseCls", false).toBool());

        QString currentAppId = settings.value("Baidu/AppId").toString().trimmed();
        QString currentKey = settings.value("Baidu/ApiKey").toString().trimmed();
        QString currentSecret = settings.value("Baidu/SecretKey").toString().trimmed();
        if (currentKey == "YOUR_API_KEY_HERE") currentKey.clear();
        if (currentSecret == "YOUR_SECRET_KEY_HERE") currentSecret.clear();
        m_appIdInput->setText(currentAppId);
        m_keyInput->setText(currentKey);
        m_secretInput->setText(currentSecret);

        QString ocrHk = settings.value("General/Hotkey", settings.value("Baidu/Hotkey", "F4")).toString().trimmed();
        if (ocrHk.isEmpty()) ocrHk = "F4";
        m_ocrHotkeyInput->setHotkey(ocrHk);

        QString shotHk = settings.value("General/ScreenshotHotkey", "Alt+A").toString().trimmed();
        if (shotHk.isEmpty()) shotHk = "Alt+A";
        m_shotHotkeyInput->setHotkey(shotHk);

        QString engineStr = settings.value("General/Engine", "offline").toString().trimmed().toLower();
        if (engineStr == "offline") {
            m_engineCombo->setCurrentIndex(0);
            m_offlineGroup->setVisible(true);
            m_onlineGroup->setVisible(false);
        } else {
            m_engineCombo->setCurrentIndex(1);
            m_offlineGroup->setVisible(false);
            m_onlineGroup->setVisible(true);
        }

        checkOfflineModelStatus();

        // 6. Footer action buttons
        addActionButton(tr("保存"), FluentDialog::ActionType::Primary, [this, iniPath]() {
            if (m_ocrHotkeyInput->isRecording()) m_ocrHotkeyInput->finishRecording();
            if (m_shotHotkeyInput->isRecording()) m_shotHotkeyInput->finishRecording();

            QSettings s(iniPath, QSettings::IniFormat);
            bool isOffline = (m_engineCombo->currentIndex() == 0);
            s.setValue("General/Engine", isOffline ? "offline" : "online");
            s.setValue("General/Hotkey", m_ocrHotkeyInput->currentHotkey());
            s.setValue("General/ScreenshotHotkey", m_shotHotkeyInput->currentHotkey());
            s.setValue("Baidu/Hotkey", m_ocrHotkeyInput->currentHotkey());

            s.setValue("Offline/ModelsDir", m_modelsDir.isEmpty() ? OcrLocalEngine::defaultModelsDir() : m_modelsDir);
            const int saveThreads = qBound(OcrLocalEngine::MinThreads,
                                           m_threadsInput->text().trimmed().toInt(),
                                           OcrLocalEngine::MaxThreads);
            s.setValue("Offline/NumThreads", saveThreads);
            s.setValue("Offline/UseCls", m_useClsCheckBox->isChecked());

            s.setValue("Baidu/AppId", m_appIdInput->text().trimmed());
            s.setValue("Baidu/ApiKey", m_keyInput->text().trimmed());
            s.setValue("Baidu/SecretKey", m_secretInput->text().trimmed());
            s.sync();
        });

        addActionButton(tr("取消"), FluentDialog::ActionType::Close);
    }

private:
    void checkOfflineModelStatus() {
        if (OcrLocalEngine::isModelDirValid(m_modelsDir)) {
            m_offlineStatusLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #16a34a; font-weight: 500;"));
            //C++ 原生深度学习模型已就绪 (PP-OCRv6)"
            m_offlineStatusLabel->setText(tr("模型已就绪，可进行离线识别"));
        } else {
            m_offlineStatusLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #b45309;"));
            m_offlineStatusLabel->setText(tr("未检测到内置离线模型，请检查组件安装完整性"));
        }
    }

    FluentComboBox* m_engineCombo = nullptr;
    QWidget* m_offlineGroup = nullptr;
    QString m_modelsDir;
    FluentInput* m_threadsInput = nullptr;
    QCheckBox* m_useClsCheckBox = nullptr;
    FluentButton* m_testOfflineBtn = nullptr;
    QLabel* m_offlineStatusLabel = nullptr;

    QWidget* m_onlineGroup = nullptr;
    FluentInput* m_appIdInput = nullptr;
    FluentInput* m_keyInput = nullptr;
    FluentInput* m_secretInput = nullptr;
    FluentButton* m_testOnlineBtn = nullptr;
    QLabel* m_onlineStatusLabel = nullptr;

    HotkeyRecorder* m_ocrHotkeyInput = nullptr;
    FluentButton* m_modifyOcrBtn = nullptr;

    HotkeyRecorder* m_shotHotkeyInput = nullptr;
    FluentButton* m_modifyShotBtn = nullptr;
};
