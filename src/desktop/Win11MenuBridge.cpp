#include "desktop/Win11MenuBridge.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QUuid>

#include <Windows.h>
#include <thread>

namespace {
constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\LinDesk.Win11MenuHost";
constexpr int kHostStartupWaitMs = 1800;
constexpr int kPipeConnectStepMs = 60;
constexpr DWORD kMaxMessageSize = 1024 * 1024;

QString hostExecutablePath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString directPath = QDir(appDir).filePath(QStringLiteral("Win11MenuIsland.exe"));
    if (QFileInfo::exists(directPath)) {
        return directPath;
    }

    const QString debugPath = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("../../W11MenuUI/bin/x64/Debug/Win11MenuIsland.exe"));
    if (QFileInfo::exists(debugPath)) {
        return QFileInfo(debugPath).absoluteFilePath();
    }

    const QString releasePath = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("../../W11MenuUI/bin/x64/Release/Win11MenuIsland.exe"));
    if (QFileInfo::exists(releasePath)) {
        return QFileInfo(releasePath).absoluteFilePath();
    }

    return directPath;
}

bool waitForPipe(int timeoutMs)
{
    int waited = 0;
    while (waited <= timeoutMs) {
        if (WaitNamedPipeW(kPipeName, kPipeConnectStepMs)) {
            return true;
        }

        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PIPE_BUSY) {
            return false;
        }

        waited += kPipeConnectStepMs;
    }

    return false;
}

bool writeAll(HANDLE pipe, const void* data, DWORD size)
{
    const char* cursor = static_cast<const char*>(data);
    DWORD remaining = size;
    while (remaining > 0) {
        DWORD written = 0;
        if (!WriteFile(pipe, cursor, remaining, &written, nullptr) || written == 0) {
            return false;
        }
        cursor += written;
        remaining -= written;
    }
    return true;
}

bool readAll(HANDLE pipe, void* data, DWORD size)
{
    char* cursor = static_cast<char*>(data);
    DWORD remaining = size;
    while (remaining > 0) {
        DWORD read = 0;
        if (!ReadFile(pipe, cursor, remaining, &read, nullptr) || read == 0) {
            return false;
        }
        cursor += read;
        remaining -= read;
    }
    return true;
}

bool writeMessage(HANDLE pipe, const QByteArray& payload)
{
    if (payload.size() <= 0 || payload.size() > static_cast<int>(kMaxMessageSize)) {
        return false;
    }

    const quint32 length = static_cast<quint32>(payload.size());
    return writeAll(pipe, &length, sizeof(length)) &&
        writeAll(pipe, payload.constData(), static_cast<DWORD>(payload.size()));
}

bool readMessage(HANDLE pipe, QByteArray* payload)
{
    quint32 length = 0;
    if (!readAll(pipe, &length, sizeof(length)) || length == 0 || length > kMaxMessageSize) {
        return false;
    }

    QByteArray buffer;
    buffer.resize(static_cast<int>(length));
    if (!readAll(pipe, buffer.data(), length)) {
        return false;
    }

    if (payload) {
        *payload = buffer;
    }
    return true;
}
}

bool Win11MenuBridge::ensureHostRunning()
{
    if (waitForPipe(80)) {
        return true;
    }

    const QString exePath = hostExecutablePath();
    if (!QFileInfo::exists(exePath)) {
        qWarning() << "[Win11MenuBridge] host executable missing:" << exePath;
        return false;
    }

    const QStringList arguments = {
        QStringLiteral("--lindesk-menu-host"),
        QStringLiteral("--parent-pid"),
        QString::number(GetCurrentProcessId())
    };
    const bool started = QProcess::startDetached(exePath, arguments, QFileInfo(exePath).absolutePath());
    if (!started) {
        qWarning() << "[Win11MenuBridge] failed to start host:" << exePath;
        return false;
    }

    return waitForPipe(kHostStartupWaitMs);
}

bool Win11MenuBridge::sendRequest(const QByteArray& payload, QByteArray* response)
{
    HANDLE pipe = CreateFileW(
        kPipeName,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (pipe == INVALID_HANDLE_VALUE) {
        qWarning() << "[Win11MenuBridge] CreateFile failed:" << GetLastError();
        return false;
    }

    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);

    const bool ok = writeMessage(pipe, payload) && readMessage(pipe, response);
    CloseHandle(pipe);

    if (!ok) {
        qWarning() << "[Win11MenuBridge] pipe request failed";
    }
    return ok;
}

bool Win11MenuBridge::showDesktopMenu(const QPoint& screenPos, const QJsonArray& items, QString* commandId)
{
    if (!ensureHostRunning()) {
        return false;
    }

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("showMenu"));
    request.insert(QStringLiteral("requestId"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    request.insert(QStringLiteral("screenX"), screenPos.x());
    request.insert(QStringLiteral("screenY"), screenPos.y());
    request.insert(QStringLiteral("items"), items);

    QByteArray responseBytes;
    if (!sendRequest(QJsonDocument(request).toJson(QJsonDocument::Compact), &responseBytes)) {
        return false;
    }

    const QJsonDocument responseDoc = QJsonDocument::fromJson(responseBytes);
    if (!responseDoc.isObject()) {
        qWarning() << "[Win11MenuBridge] invalid response:" << responseBytes;
        return false;
    }

    const QJsonObject response = responseDoc.object();
    if (response.value(QStringLiteral("requestId")).toString() != request.value(QStringLiteral("requestId")).toString()) {
        qWarning() << "[Win11MenuBridge] response requestId mismatch";
        return false;
    }

    const QString status = response.value(QStringLiteral("status")).toString();
    if (status == QStringLiteral("cancelled")) {
        if (commandId) {
            commandId->clear();
        }
        return true;
    }

    if (status != QStringLiteral("accepted")) {
        qWarning() << "[Win11MenuBridge] response status failed:" << status;
        return false;
    }

    if (commandId) {
        *commandId = response.value(QStringLiteral("commandId")).toString();
    }
    return true;
}

void Win11MenuBridge::shutdownHost()
{
    if (!waitForPipe(80)) {
        return;
    }

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("shutdown"));
    request.insert(QStringLiteral("requestId"), QUuid::createUuid().toString(QUuid::WithoutBraces));

    QByteArray response;
    sendRequest(QJsonDocument(request).toJson(QJsonDocument::Compact), &response);
}

void Win11MenuBridge::warmupHost()
{
    std::thread([]() {
        ensureHostRunning();
    }).detach();
}
