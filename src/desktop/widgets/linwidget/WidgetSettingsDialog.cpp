#include "desktop/widgets/linwidget/WidgetSettingsDialog.h"

#include "desktop/widgets/linwidget/PermissionManager.h"
#include "desktop/widgets/linwidget/WidgetManager.h"
#include "services/AppPaths.h"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace LinWidget {

namespace {
QString safeStorageSegmentForSettings(QString value)
{
    value = value.trimmed().toLower();
    QString safe;
    safe.reserve(value.size());
    for (const QChar ch : value) {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('.') || ch == QLatin1Char('_') || ch == QLatin1Char('-')) {
            safe.append(ch);
        } else if (!safe.endsWith(QLatin1Char('_'))) {
            safe.append(QLatin1Char('_'));
        }
    }
    while (safe.startsWith(QLatin1Char('.')) || safe.startsWith(QLatin1Char('_')) || safe.startsWith(QLatin1Char('-'))) {
        safe.remove(0, 1);
    }
    while (safe.endsWith(QLatin1Char('.')) || safe.endsWith(QLatin1Char('_')) || safe.endsWith(QLatin1Char('-'))) {
        safe.chop(1);
    }
    return safe.isEmpty() ? QStringLiteral("unknown") : safe.left(128);
}

void activateSettingsDialog(WidgetSettingsDialog* dialog)
{
    if (!dialog) {
        return;
    }
    if (dialog->isMinimized()) {
        dialog->setWindowState(dialog->windowState() & ~Qt::WindowMinimized);
    }
    if (!dialog->isVisible()) {
        dialog->show();
    }
    dialog->raise();
    dialog->activateWindow();
#ifdef Q_OS_WIN
    if (const HWND hwnd = reinterpret_cast<HWND>(dialog->winId())) {
        ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
        BringWindowToTop(hwnd);
    }
#endif
}
}

QMap<QString, QPointer<WidgetSettingsDialog>>& WidgetSettingsDialog::openSettingsDialogs()
{
    static QMap<QString, QPointer<WidgetSettingsDialog>> dialogs;
    return dialogs;
}

WidgetSettingsDialog::WidgetSettingsDialog(QString packagePath,
                                           WidgetManifest manifest,
                                           QStringList grantedPermissions,
                                           bool isDevelopment,
                                           QString instanceId,
                                           QWidget* parent)
    : QDialog(parent)
    , m_packagePath(std::move(packagePath))
    , m_manifest(std::move(manifest))
    , m_grantedPermissions(PermissionManager().normalizePermissions(grantedPermissions))
    , m_isDevelopment(isDevelopment)
    , m_instanceId(std::move(instanceId))
{
    setWindowTitle(tr("%1 - 组件设置").arg(m_manifest.name.isEmpty() ? m_manifest.id : m_manifest.name));
    resize(680, 520);
    setMinimumSize(480, 340);
}

WidgetSettingsDialog::~WidgetSettingsDialog() = default;

WidgetSettingsDialog* WidgetSettingsDialog::openOrCreate(QString packagePath,
                                                         WidgetManifest manifest,
                                                         QStringList grantedPermissions,
                                                         bool isDevelopment,
                                                         QString instanceId,
                                                         QWidget* parent,
                                                         QString* errorMessage)
{
    const QString key = dialogKey(packagePath, manifest, isDevelopment, instanceId);
    auto& dialogs = openSettingsDialogs();
    if (WidgetSettingsDialog* existing = dialogs.value(key).data()) {
        activateSettingsDialog(existing);
        if (errorMessage) {
            errorMessage->clear();
        }
        return existing;
    }

    auto* dialog = new WidgetSettingsDialog(std::move(packagePath),
                                            std::move(manifest),
                                            std::move(grantedPermissions),
                                            isDevelopment,
                                            std::move(instanceId),
                                            parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialogs.insert(key, dialog);
    QObject::connect(dialog, &QObject::destroyed, dialog, [key]() {
        openSettingsDialogs().remove(key);
    });
    dialog->initialize(errorMessage);
    activateSettingsDialog(dialog);
    return dialog;
}

QString WidgetSettingsDialog::dialogKey(const QString& packagePath,
                                        const WidgetManifest& manifest,
                                        bool isDevelopment,
                                        const QString& instanceId)
{
    const QString normalizedInstanceId = instanceId.trimmed().toLower();
    if (!normalizedInstanceId.isEmpty()) {
        return QStringLiteral("instance:%1").arg(normalizedInstanceId);
    }

    const QString normalizedId = manifest.id.trimmed().toLower();
    if (!normalizedId.isEmpty()) {
        return QStringLiteral("%1:%2").arg(isDevelopment ? QStringLiteral("dev") : QStringLiteral("installed"), normalizedId);
    }

    return QStringLiteral("package:%1").arg(QFileInfo(packagePath).absoluteFilePath().toLower());
}

QString WidgetSettingsDialog::widgetStorageFilePath() const
{
    const QString mode = m_isDevelopment ? QStringLiteral("dev") : QStringLiteral("installed");
    const QString safeId = safeStorageSegmentForSettings(m_manifest.id);
    QDir root(AppPaths::linWidgetDataDir());
    root.mkpath(mode + QLatin1Char('/') + safeId);
    return root.filePath(mode + QLatin1Char('/') + safeId + QStringLiteral("/widget.json"));
}

QJsonObject WidgetSettingsDialog::loadStoredSettings() const
{
    const QString path = widgetStorageFilePath();
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return QJsonObject();
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.isObject() ? doc.object() : QJsonObject();
}

bool WidgetSettingsDialog::saveSchemaSettings(const QJsonObject& values, QString* errorMessage)
{
    const QString path = widgetStorageFilePath();
    QJsonObject rootObj = loadStoredSettings();
    QJsonObject settingsSubObj = rootObj.value(QStringLiteral("settings")).toObject();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        settingsSubObj.insert(it.key(), it.value());
        rootObj.insert(it.key(), it.value());
    }
    rootObj.insert(QStringLiteral("settings"), settingsSubObj);

    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile saveFile(path);
    if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = saveFile.errorString();
        }
        return false;
    }
    saveFile.write(QJsonDocument(rootObj).toJson(QJsonDocument::Compact));
    if (!saveFile.commit()) {
        if (errorMessage) {
            *errorMessage = saveFile.errorString();
        }
        return false;
    }

    if (WidgetManager* manager = WidgetManager::instance()) {
        const QString targetRegId = m_manifest.id.trimmed().toLower();
        QJsonObject settingsChangedMsg;
        settingsChangedMsg.insert(QStringLiteral("type"), QStringLiteral("settings_changed"));
        settingsChangedMsg.insert(QStringLiteral("settings"), values);
        manager->broadcastToWidgetInstances(targetRegId, settingsChangedMsg);

        QJsonObject appearanceMsg;
        appearanceMsg.insert(QStringLiteral("type"), QStringLiteral("linwidget_appearance_changed_notification"));
        appearanceMsg.insert(QStringLiteral("settings"), values);
        manager->broadcastToWidgetInstances(targetRegId, appearanceMsg);
    }

    qInfo() << "[LinWidget][WidgetSettingsDialog] Saved schema settings for widget:" << m_manifest.id
            << "keys:" << values.keys();
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

QJsonObject WidgetSettingsDialog::currentSchemaValues() const
{
    QJsonObject result;
    for (const WidgetSettingField& field : m_manifest.settingsSchema) {
        QWidget* editor = m_schemaEditors.value(field.key).data();
        if (!editor) {
            if (!field.defaultValue.isUndefined()) {
                result.insert(field.key, field.defaultValue);
            }
            continue;
        }
        if (auto* checkBox = qobject_cast<QCheckBox*>(editor)) {
            result.insert(field.key, checkBox->isChecked());
        } else if (auto* spinBox = qobject_cast<QSpinBox*>(editor)) {
            result.insert(field.key, spinBox->value());
        } else if (auto* doubleSpinBox = qobject_cast<QDoubleSpinBox*>(editor)) {
            result.insert(field.key, doubleSpinBox->value());
        } else if (auto* comboBox = qobject_cast<QComboBox*>(editor)) {
            const QVariant data = comboBox->currentData();
            result.insert(field.key, data.isValid() ? data.toString() : comboBox->currentText());
        } else if (auto* lineEdit = qobject_cast<QLineEdit*>(editor)) {
            result.insert(field.key, lineEdit->text());
        }
    }
    return result;
}

bool WidgetSettingsDialog::applySchemaValues(const QJsonObject& values, QString* errorMessage)
{
    for (const WidgetSettingField& field : m_manifest.settingsSchema) {
        if (!values.contains(field.key)) {
            continue;
        }
        const QJsonValue val = values.value(field.key);
        QWidget* editor = m_schemaEditors.value(field.key).data();
        if (!editor) {
            continue;
        }
        if (auto* checkBox = qobject_cast<QCheckBox*>(editor)) {
            checkBox->setChecked(val.toBool(field.defaultValue.toBool()));
        } else if (auto* spinBox = qobject_cast<QSpinBox*>(editor)) {
            spinBox->setValue(val.toInt(field.defaultValue.toInt()));
        } else if (auto* doubleSpinBox = qobject_cast<QDoubleSpinBox*>(editor)) {
            doubleSpinBox->setValue(val.toDouble(field.defaultValue.toDouble()));
        } else if (auto* comboBox = qobject_cast<QComboBox*>(editor)) {
            const QString targetVal = val.toVariant().toString();
            const int idx = comboBox->findData(targetVal);
            if (idx >= 0) {
                comboBox->setCurrentIndex(idx);
            } else {
                const int textIdx = comboBox->findText(targetVal);
                if (textIdx >= 0) {
                    comboBox->setCurrentIndex(textIdx);
                }
            }
        } else if (auto* lineEdit = qobject_cast<QLineEdit*>(editor)) {
            lineEdit->setText(val.toVariant().toString());
        }
    }
    return saveSchemaSettings(currentSchemaValues(), errorMessage);
}

bool WidgetSettingsDialog::resetSchemaToDefaults(QString* errorMessage)
{
    QJsonObject defaults;
    for (const WidgetSettingField& field : m_manifest.settingsSchema) {
        if (!field.defaultValue.isUndefined()) {
            defaults.insert(field.key, field.defaultValue);
        }
    }
    return applySchemaValues(defaults, errorMessage);
}

QWidget* WidgetSettingsDialog::createSchemaSettingsPanel(QWidget* parentWidget)
{
    auto* container = new QWidget(parentWidget);
    auto* rootLayout = new QVBoxLayout(container);
    rootLayout->setContentsMargins(16, 16, 16, 16);
    rootLayout->setSpacing(12);

    auto* scrollArea = new QScrollArea(container);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* formHost = new QWidget(scrollArea);
    auto* formLayout = new QFormLayout(formHost);
    formLayout->setContentsMargins(8, 8, 8, 8);
    formLayout->setSpacing(14);
    formLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    const QJsonObject storedRoot = loadStoredSettings();
    const QJsonObject storedSettings = storedRoot.value(QStringLiteral("settings")).toObject();

    for (const WidgetSettingField& field : m_manifest.settingsSchema) {
        QJsonValue initialValue = field.defaultValue;
        if (storedSettings.contains(field.key)) {
            initialValue = storedSettings.value(field.key);
        } else if (storedRoot.contains(field.key)) {
            initialValue = storedRoot.value(field.key);
        }

        auto* rowWidget = new QWidget(formHost);
        auto* rowLayout = new QVBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(4);

        QWidget* control = nullptr;
        if (field.type == QStringLiteral("boolean")) {
            auto* cb = new QCheckBox(field.description.isEmpty() ? tr("启用") : field.description, rowWidget);
            cb->setChecked(initialValue.toBool(false));
            control = cb;
            rowLayout->addWidget(cb);
        } else if (field.type == QStringLiteral("integer")) {
            auto* sb = new QSpinBox(rowWidget);
            sb->setMinimum(field.hasMin ? static_cast<int>(field.minimum) : -999999);
            sb->setMaximum(field.hasMax ? static_cast<int>(field.maximum) : 999999);
            sb->setValue(initialValue.toInt(field.defaultValue.toInt(0)));
            control = sb;
            rowLayout->addWidget(sb);
        } else if (field.type == QStringLiteral("number")) {
            auto* dsb = new QDoubleSpinBox(rowWidget);
            dsb->setDecimals(2);
            dsb->setMinimum(field.hasMin ? field.minimum : -999999.0);
            dsb->setMaximum(field.hasMax ? field.maximum : 999999.0);
            dsb->setValue(initialValue.toDouble(field.defaultValue.toDouble(0.0)));
            control = dsb;
            rowLayout->addWidget(dsb);
        } else if (field.type == QStringLiteral("select")) {
            auto* combo = new QComboBox(rowWidget);
            for (const WidgetSettingOption& opt : field.options) {
                combo->addItem(opt.label.isEmpty() ? opt.value : opt.label, opt.value);
            }
            const QString initStr = initialValue.toVariant().toString();
            const int idx = combo->findData(initStr);
            if (idx >= 0) {
                combo->setCurrentIndex(idx);
            }
            control = combo;
            rowLayout->addWidget(combo);
        } else if (field.type == QStringLiteral("color")) {
            auto* colorRow = new QWidget(rowWidget);
            auto* colorLayout = new QHBoxLayout(colorRow);
            colorLayout->setContentsMargins(0, 0, 0, 0);
            colorLayout->setSpacing(8);
            auto* edit = new QLineEdit(initialValue.toVariant().toString(), colorRow);
            auto* pickBtn = new QPushButton(tr("选择颜色"), colorRow);
            auto updateSwatch = [pickBtn](const QString& colorStr) {
                const QColor c(colorStr);
                if (c.isValid()) {
                    pickBtn->setStyleSheet(QStringLiteral(
                        "QPushButton { background-color: %1; color: %2; border: 1px solid #cbd5e1; border-radius: 6px; padding: 4px 10px; }")
                        .arg(c.name(), c.lightness() > 140 ? QStringLiteral("#0f172a") : QStringLiteral("#ffffff")));
                } else {
                    pickBtn->setStyleSheet(QString());
                }
            };
            updateSwatch(edit->text());
            QObject::connect(edit, &QLineEdit::textChanged, colorRow, updateSwatch);
            QObject::connect(pickBtn, &QPushButton::clicked, this, [this, edit]() {
                const QColor initial(edit->text());
                const QColor chosen = QColorDialog::getColor(initial.isValid() ? initial : QColor("#3b82f6"), this, tr("选择颜色"));
                if (chosen.isValid()) {
                    edit->setText(chosen.name(QColor::HexRgb));
                }
            });
            colorLayout->addWidget(edit, 1);
            colorLayout->addWidget(pickBtn);
            control = edit;
            rowLayout->addWidget(colorRow);
        } else {
            auto* edit = new QLineEdit(initialValue.toVariant().toString(), rowWidget);
            control = edit;
            rowLayout->addWidget(edit);
        }

        if (!field.description.isEmpty() && field.type != QStringLiteral("boolean")) {
            auto* descLabel = new QLabel(field.description, rowWidget);
            descLabel->setWordWrap(true);
            descLabel->setStyleSheet(QStringLiteral("color: #64748b; font-size: 12px;"));
            rowLayout->addWidget(descLabel);
        }

        if (control) {
            m_schemaEditors.insert(field.key, control);
        }

        auto* titleLabel = new QLabel(field.title.isEmpty() ? field.key : field.title, formHost);
        titleLabel->setStyleSheet(QStringLiteral("font-weight: 600; color: #1e293b;"));
        formLayout->addRow(titleLabel, rowWidget);
    }

    scrollArea->setWidget(formHost);
    rootLayout->addWidget(scrollArea, 1);

    auto* statusLabel = new QLabel(container);
    statusLabel->setStyleSheet(QStringLiteral("color: #059669; font-size: 12px;"));
    rootLayout->addWidget(statusLabel);

    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(10);
    auto* resetBtn = new QPushButton(tr("恢复默认"), container);
    auto* saveBtn = new QPushButton(tr("保存并应用"), container);
    saveBtn->setDefault(true);
    saveBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: #2563eb; color: #ffffff; border: none; border-radius: 6px; padding: 6px 16px; font-weight: 600; }"
        "QPushButton:hover { background: #1d4ed8; }"));

    QObject::connect(resetBtn, &QPushButton::clicked, this, [this, statusLabel]() {
        QString err;
        if (resetSchemaToDefaults(&err)) {
            statusLabel->setText(tr("已恢复默认配置并实时应用。"));
        } else {
            statusLabel->setText(err);
        }
    });

    QObject::connect(saveBtn, &QPushButton::clicked, this, [this, statusLabel]() {
        QString err;
        if (saveSchemaSettings(currentSchemaValues(), &err)) {
            statusLabel->setText(tr("设置已保存并实时同步到组件。"));
        } else {
            statusLabel->setText(err);
        }
    });

    btnRow->addWidget(resetBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(saveBtn);
    rootLayout->addLayout(btnRow);

    return container;
}

QWidget* WidgetSettingsDialog::createCustomHtmlSettingsPanel(const QString& settingsPath, QWidget* parentWidget)
{
    auto* container = new QWidget(parentWidget);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(16, 16, 16, 16);
    auto* label = new QLabel(tr("自定义设置页 (%1)").arg(QFileInfo(settingsPath).fileName()), container);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(QStringLiteral(
        "QLabel {"
        "background: #ffffff;"
        "border: 1px solid rgba(15, 23, 42, 0.12);"
        "border-radius: 10px;"
        "color: #374151;"
        "font-size: 14px;"
        "padding: 20px;"
        "}"));
    layout->addWidget(label, 1);
    return container;
}

bool WidgetSettingsDialog::initialize(QString* errorMessage)
{
    const bool hasSchema = !m_manifest.settingsSchema.isEmpty();
    const bool hasCustomSettingPath = !m_manifest.settings.trimmed().isEmpty();
    const QString settingsPath = hasCustomSettingPath ? QDir(m_packagePath).filePath(m_manifest.settings) : QString();
    const bool customHtmlExists = hasCustomSettingPath && QFileInfo::exists(settingsPath);

    if (!hasSchema && !hasCustomSettingPath) {
        const QString message = tr("This widget does not provide a settings page.");
        if (errorMessage) {
            *errorMessage = message;
        }
        renderPlaceholder(message);
        return false;
    }

    if (!hasSchema && hasCustomSettingPath && !customHtmlExists) {
        const QString message = tr("Settings page is missing: %1").arg(settingsPath);
        if (errorMessage) {
            *errorMessage = message;
        }
        renderPlaceholder(message);
        return false;
    }

    if (hasSchema && customHtmlExists) {
        auto* rootLayout = new QVBoxLayout(this);
        rootLayout->setContentsMargins(12, 12, 12, 12);
        auto* tabs = new QTabWidget(this);
        tabs->addTab(createSchemaSettingsPanel(tabs), tr("快速配置"));
        tabs->addTab(createCustomHtmlSettingsPanel(settingsPath, tabs), tr("自定义页面"));
        rootLayout->addWidget(tabs, 1);
    } else if (hasSchema) {
        auto* rootLayout = new QVBoxLayout(this);
        rootLayout->setContentsMargins(0, 0, 0, 0);
        rootLayout->addWidget(createSchemaSettingsPanel(this), 1);
    } else {
        renderSettingsPage(settingsPath);
    }

    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

void WidgetSettingsDialog::renderPlaceholder(const QString& message)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    auto* label = new QLabel(message, this);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(QStringLiteral(
        "QLabel {"
        "background: #ffffff;"
        "border: 1px solid rgba(15, 23, 42, 0.12);"
        "border-radius: 10px;"
        "color: #374151;"
        "font-size: 14px;"
        "padding: 20px;"
        "}"));
    layout->addWidget(label, 1);
}

void WidgetSettingsDialog::renderSettingsPage(const QString& settingsPath)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(createCustomHtmlSettingsPanel(settingsPath, this), 1);
}

void WidgetSettingsDialog::handleSettingsWebMessage(const QJsonObject& object)
{
    const QString type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("console_log")) {
        appendLog(object.value(QStringLiteral("level")).toString(QStringLiteral("log")),
                  object.value(QStringLiteral("message")).toString());
        return;
    }

    const bool isCreate = type == QStringLiteral("linwidget_create_instance_request");
    const bool isList = type == QStringLiteral("linwidget_list_instances_request");
    const bool isClose = type == QStringLiteral("linwidget_close_instance_request");
    const bool isRemove = type == QStringLiteral("linwidget_remove_instance_request");
    const bool isLoad = type == QStringLiteral("linwidget_load_instance_request");
    const bool isSettingsChanged = type == QStringLiteral("linwidget_settings_changed_request");
    if (isSettingsChanged) {
        WidgetManager* manager = WidgetManager::instance();
        QJsonObject notifyMessage;
        notifyMessage.insert(QStringLiteral("type"), QStringLiteral("linwidget_appearance_changed_notification"));
        if (manager) {
            manager->broadcastToWidgetInstances(m_manifest.id.trimmed().toLower(), notifyMessage);
        }
        // Forward notification to other open settings dialogs for the same widget package
        for (const auto& dialog : openSettingsDialogs()) {
            if (dialog && dialog->m_manifest.id.trimmed().toLower() == m_manifest.id.trimmed().toLower()) {
                // Settings dialogs in CEF OSR mode will handle notifications via CEF bridge when implemented
            }
        }
        return;
    }
    if (!isCreate && !isList && !isClose && !isRemove && !isLoad) {
        return;
    }

    const QString requestId = object.value(QStringLiteral("requestId")).toString();
    QJsonObject response;
    response.insert(QStringLiteral("type"), isCreate
        ? QStringLiteral("linwidget_create_instance_response")
        : (isList
            ? QStringLiteral("linwidget_list_instances_response")
            : (isClose ? QStringLiteral("linwidget_close_instance_response")
                       : (isRemove ? QStringLiteral("linwidget_remove_instance_response")
                                   : QStringLiteral("linwidget_load_instance_response")))));
    response.insert(QStringLiteral("requestId"), requestId);

    QString newInstanceId;
    QString errorMessage;
    bool ok = false;
    if (m_instanceId.trimmed().isEmpty()) {
        errorMessage = QStringLiteral("Settings page is not attached to a running widget instance.");
    } else {
        WidgetManager* manager = WidgetManager::instance();
        if (manager) {
            if (isCreate) {
                QRect requestedGeometry;
                const QJsonObject geometry = object.value(QStringLiteral("geometry")).toObject();
                if (!geometry.isEmpty()) {
                    requestedGeometry = QRect(geometry.value(QStringLiteral("x")).toInt(),
                                              geometry.value(QStringLiteral("y")).toInt(),
                                              geometry.value(QStringLiteral("width")).toInt(),
                                              geometry.value(QStringLiteral("height")).toInt());
                }
                const WidgetInstance source = manager->widgetInstance(m_instanceId);
                if (!source.instanceId.isEmpty() && manager->hasRunningWidget(m_instanceId)) {
                    ok = manager->createWidgetInstance(m_instanceId, requestedGeometry, &newInstanceId, &errorMessage);
                } else {
                    ok = manager->createWidgetInstanceFromPackage(m_packagePath, m_isDevelopment, false, requestedGeometry, &newInstanceId, &errorMessage);
                }
            } else {
                QJsonArray instances;
                const QString targetRegistryId = m_manifest.id.trimmed().toLower();
                for (const WidgetLayoutState& state : manager->layoutStates()) {
                    const bool sameMode = state.isDevelopment == m_isDevelopment || state.instanceId.startsWith(m_isDevelopment ? QStringLiteral("dev:") : QStringLiteral("installed:"));
                    const bool sameRegistry = state.registryId.trimmed().toLower() == targetRegistryId ||
                        state.instanceId == QStringLiteral("%1:%2").arg(m_isDevelopment ? QStringLiteral("dev") : QStringLiteral("installed"), targetRegistryId) ||
                        state.instanceId.startsWith(QStringLiteral("%1:%2:").arg(m_isDevelopment ? QStringLiteral("dev") : QStringLiteral("installed"), targetRegistryId));
                    if (!sameMode || !sameRegistry) {
                        continue;
                    }
                    QJsonObject item;
                    item.insert(QStringLiteral("instanceId"), state.instanceId);
                    item.insert(QStringLiteral("id"), state.instanceId);
                    item.insert(QStringLiteral("visible"), state.visible);
                    item.insert(QStringLiteral("running"), manager->hasRunningWidget(state.instanceId));
                    item.insert(QStringLiteral("parentInstanceId"), state.parentInstanceId);
                    item.insert(QStringLiteral("createdAt"), state.createdAt);
                    QJsonObject geometry;
                    geometry.insert(QStringLiteral("x"), state.geometry.x());
                    geometry.insert(QStringLiteral("y"), state.geometry.y());
                    geometry.insert(QStringLiteral("width"), state.geometry.width());
                    geometry.insert(QStringLiteral("height"), state.geometry.height());
                    item.insert(QStringLiteral("geometry"), geometry);
                    instances.append(item);
                }
                if (isList) {
                    response.insert(QStringLiteral("instances"), instances);
                    ok = true;
                } else {
                    QString targetInstanceId = object.value(QStringLiteral("targetInstanceId")).toString().trimmed();
                    if (targetInstanceId.isEmpty()) {
                        targetInstanceId = m_instanceId;
                    }
                    bool allowed = false;
                    for (const QJsonValue& value : instances) {
                        if (value.toObject().value(QStringLiteral("instanceId")).toString() == targetInstanceId) {
                            allowed = true;
                            break;
                        }
                    }
                    if (allowed) {
                        ok = isClose ? (manager->stopWidget(targetInstanceId), true) : (isLoad ? manager->restartWidget(targetInstanceId) : manager->removeWidgetState(targetInstanceId));
                        newInstanceId = targetInstanceId;
                    } else {
                        errorMessage = QStringLiteral("Target widget instance is outside the caller scope.");
                    }
                }
            }
        } else {
            errorMessage = QStringLiteral("LinWidget manager is unavailable.");
        }
    }

    response.insert(QStringLiteral("ok"), ok);
    response.insert(QStringLiteral("instanceId"), newInstanceId);
    response.insert(QStringLiteral("error"), errorMessage);

}

void WidgetSettingsDialog::appendLog(const QString& level, const QString& message) const
{
    qDebug().noquote() << QStringLiteral("[LinWidget][WidgetSettingsDialog][%1] %2").arg(level, message);
}

}
