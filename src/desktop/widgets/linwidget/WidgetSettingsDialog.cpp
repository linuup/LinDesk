#include "desktop/widgets/linwidget/WidgetSettingsDialog.h"

#include "desktop/ListView.h"
#include "desktop/widgets/linwidget/PermissionManager.h"
#include "desktop/widgets/linwidget/WidgetManager.h"
#include "services/AppPaths.h"

#include <QDateTime>
#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QResizeEvent>
#include <QUrl>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace LinWidget {

namespace {
QString jsonStringLiteral(const QString& value)
{
    QString json = QString::fromUtf8(QJsonDocument(QJsonArray{ value }).toJson(QJsonDocument::Compact));
    if (json.size() >= 2) {
        json = json.mid(1, json.size() - 2);
    }
    return json;
}

QString permissionHostSegment(const QStringList& permissions, bool isDevelopment)
{
    if (isDevelopment) {
        return QStringLiteral("pdevnsd");
    }

    QString marker = QStringLiteral("p");
    if (PermissionManager().canUseNetwork(permissions)) {
        marker.append(QLatin1Char('n'));
    }
    if (PermissionManager().canUseStorage(permissions)) {
        marker.append(QLatin1Char('s'));
    }
    if (PermissionManager().canUseDesktopApi(permissions)) {
        marker.append(QLatin1Char('d'));
    }
    return marker;
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
    setWindowTitle(tr("%1 Settings").arg(m_manifest.name.isEmpty() ? m_manifest.id : m_manifest.name));
    resize(860, 640);
    setMinimumSize(520, 360);
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

bool WidgetSettingsDialog::initialize(QString* errorMessage)
{
    if (m_manifest.settings.trimmed().isEmpty()) {
        const QString message = tr("This widget does not provide a settings page.");
        if (errorMessage) {
            *errorMessage = message;
        }
        renderPlaceholder(message);
        return false;
    }

    const QString settingsPath = QDir(m_packagePath).filePath(m_manifest.settings);
    if (!QFileInfo::exists(settingsPath)) {
        const QString message = tr("Settings page is missing: %1").arg(settingsPath);
        if (errorMessage) {
            *errorMessage = message;
        }
        renderPlaceholder(message);
        return false;
    }

    renderSettingsPage(settingsPath);
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
    Q_UNUSED(settingsPath);
    renderPlaceholder(tr("Widget settings page will be rendered in CEF OSR mode."));
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
        ListView* view = ListView::instance();
        WidgetManager* manager = view ? view->linWidgetManager() : nullptr;
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
    } else if (ListView* view = ListView::instance()) {
        if (WidgetManager* manager = view->linWidgetManager()) {
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
    } else {
        errorMessage = QStringLiteral("Desktop view is unavailable.");
    }

    response.insert(QStringLiteral("ok"), ok);
    response.insert(QStringLiteral("instanceId"), newInstanceId);
    response.insert(QStringLiteral("error"), errorMessage);

}

void WidgetSettingsDialog::appendLog(const QString& level, const QString& message) const
{
    qDebug().noquote() << QStringLiteral("[LinWidgetSettings][%1] %2").arg(level, message);
}

}
