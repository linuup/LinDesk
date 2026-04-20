#include "desktop/ListViewFileOpsHelper.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QUrl>
#include <Windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <SystemApi.h>

namespace {
constexpr DWORD kClipboardPreferredDropEffectCopy = 0x00000001;
constexpr DWORD kClipboardPreferredDropEffectMove = 0x00000002;

QString normalizeDesktopPathValue(const QString& path)
{
    QString normalized = QDir::toNativeSeparators(path);
    while (normalized.endsWith('\\') || normalized.endsWith('/')) {
        normalized.chop(1);
    }
    return normalized;
}

bool isDriveRootSourcePath(const QString& sourcePath)
{
    const QString normalizedPath = QDir::toNativeSeparators(QDir(sourcePath).absolutePath());
    return normalizedPath.length() == 3 && normalizedPath[1] == ':' &&
        (normalizedPath[2] == '\\' || normalizedPath[2] == '/');
}

bool hasSystemAttribute(const QString& sourcePath)
{
    const std::wstring nativePath = QDir::toNativeSeparators(sourcePath).toStdWString();
    const DWORD attributes = GetFileAttributesW(nativePath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return (attributes & FILE_ATTRIBUTE_SYSTEM) != 0;
}

QString shortcutBaseNameForSource(const QString& sourcePath)
{
    const QString normalizedSourcePath = QDir::toNativeSeparators(sourcePath);
    if (normalizedSourcePath.startsWith("::")) {
        const IconItem item = CSystemApi::CreateIconItem(normalizedSourcePath.toStdWString().c_str());
        const QString shellDisplayName = QString::fromStdString(item.name);
        if (!shellDisplayName.trimmed().isEmpty()) {
            return shellDisplayName.trimmed();
        }
    }

    const QFileInfo sourceInfo(normalizedSourcePath);
    QString baseName = sourceInfo.fileName();
    if (baseName.isEmpty()) {
        baseName = sourceInfo.completeBaseName();
    }
    if (baseName.isEmpty()) {
        baseName = QCoreApplication::translate("ListView", "New Shortcut");
    }
    return baseName;
}
}

QStringList ListViewFileOpsHelper::extractLocalPathsFromWindowsMimePayload(const QByteArray& payload, bool utf16)
{
    QStringList paths;
    if (payload.isEmpty()) {
        return paths;
    }

    if (utf16) {
        if (payload.size() < 2) {
            return paths;
        }

        const ushort* raw = reinterpret_cast<const ushort*>(payload.constData());
        const int charCount = payload.size() / 2;
        QString current;
        for (int i = 0; i < charCount; ++i) {
            const ushort ch = raw[i];
            if (ch == 0) {
                if (!current.isEmpty()) {
                    paths.append(QDir::toNativeSeparators(current));
                    current.clear();
                }
            } else {
                current.append(QChar(ch));
            }
        }
        if (!current.isEmpty()) {
            paths.append(QDir::toNativeSeparators(current));
        }
        return paths;
    }

    const QList<QByteArray> parts = payload.split('\0');
    for (const QByteArray& part : parts) {
        if (!part.isEmpty()) {
            paths.append(QDir::toNativeSeparators(QString::fromLocal8Bit(part)));
        }
    }
    return paths;
}

QStringList ListViewFileOpsHelper::draggedLocalFilePaths(const QMimeData* mimeData)
{
    QStringList paths;
    if (!mimeData) {
        return paths;
    }

    for (const QUrl& url : mimeData->urls()) {
        if (url.isLocalFile()) {
            const QString localPath = QDir::toNativeSeparators(url.toLocalFile());
            if (!localPath.isEmpty()) {
                paths.append(localPath);
            }
        }
    }

    if (paths.isEmpty()) {
        const QByteArray uriListData = mimeData->data("text/uri-list");
        const QList<QByteArray> lines = uriListData.split('\n');
        for (QByteArray line : lines) {
            line = line.trimmed();
            if (line.isEmpty() || line.startsWith('#')) {
                continue;
            }
            const QUrl url = QUrl(QString::fromUtf8(line));
            if (url.isLocalFile()) {
                const QString localPath = QDir::toNativeSeparators(url.toLocalFile());
                if (!localPath.isEmpty()) {
                    paths.append(localPath);
                }
            }
        }
    }

    if (paths.isEmpty()) {
        const QStringList formats = mimeData->formats();
        for (const QString& format : formats) {
            if (format.contains("FileNameW", Qt::CaseInsensitive)) {
                paths.append(extractLocalPathsFromWindowsMimePayload(mimeData->data(format), true));
            } else if (format.contains("FileName", Qt::CaseInsensitive)) {
                paths.append(extractLocalPathsFromWindowsMimePayload(mimeData->data(format), false));
            }
        }
    }

    paths.removeAll(QString());
    paths.removeDuplicates();
    return paths;
}

QStringList ListViewFileOpsHelper::clipboardLocalFilePaths()
{
    QStringList paths;

    if (const QClipboard* clipboard = QGuiApplication::clipboard()) {
        if (const QMimeData* mimeData = clipboard->mimeData()) {
            for (const QUrl& url : mimeData->urls()) {
                if (url.isLocalFile()) {
                    const QString localPath = QDir::toNativeSeparators(url.toLocalFile());
                    if (!localPath.isEmpty()) {
                        paths.append(localPath);
                    }
                }
            }
        }
    }

    if (!paths.isEmpty()) {
        paths.removeDuplicates();
        return paths;
    }

    if (!IsClipboardFormatAvailable(CF_HDROP) || !OpenClipboard(nullptr)) {
        return paths;
    }

    if (const HANDLE clipboardData = GetClipboardData(CF_HDROP)) {
        const HDROP dropHandle = static_cast<HDROP>(clipboardData);
        const UINT count = DragQueryFileW(dropHandle, 0xFFFFFFFF, nullptr, 0);
        wchar_t buffer[MAX_PATH];
        for (UINT i = 0; i < count; ++i) {
            const UINT len = DragQueryFileW(dropHandle, i, buffer, MAX_PATH);
            if (len > 0) {
                paths.append(QDir::toNativeSeparators(QString::fromWCharArray(buffer, len)));
            }
        }
    }

    CloseClipboard();
    paths.removeDuplicates();
    return paths;
}

bool ListViewFileOpsHelper::clipboardContainsCutItems()
{
    const UINT format = RegisterClipboardFormatW(L"Preferred DropEffect");
    if (!format || !IsClipboardFormatAvailable(format) || !OpenClipboard(nullptr)) {
        return false;
    }

    bool isCutOperation = false;
    if (const HANDLE clipboardData = GetClipboardData(format)) {
        if (const void* locked = GlobalLock(clipboardData)) {
            const DWORD effect = *static_cast<const DWORD*>(locked);
            isCutOperation = (effect & kClipboardPreferredDropEffectMove) != 0;
            GlobalUnlock(clipboardData);
        }
    }

    CloseClipboard();
    return isCutOperation;
}

void ListViewFileOpsHelper::writeUrlsToClipboard(const QList<QUrl>& urls, bool cutOperation)
{
    QMimeData* mimeData = new QMimeData();
    mimeData->setUrls(urls);

    QByteArray effectData(4, '\0');
    const DWORD effect = cutOperation ? kClipboardPreferredDropEffectMove : kClipboardPreferredDropEffectCopy;
    memcpy(effectData.data(), &effect, sizeof(DWORD));
    mimeData->setData("Preferred DropEffect", effectData);

    QGuiApplication::clipboard()->setMimeData(mimeData);
}

QString ListViewFileOpsHelper::uniquePasteDestinationPath(const QString& sourcePath, const QString& destDir)
{
    const QFileInfo sourceInfo(sourcePath);
    const QDir dir(destDir);
    const QString originalFileName = sourceInfo.fileName();
    QString originalCandidate = dir.filePath(originalFileName);

    if (!QFileInfo::exists(originalCandidate)) {
        return QDir::toNativeSeparators(originalCandidate);
    }

    QString extension;
    QString nameStem = originalFileName;
    if (!sourceInfo.isDir()) {
        const QString completeSuffix = sourceInfo.completeSuffix();
        extension = completeSuffix.isEmpty() ? QString() : "." + completeSuffix;
        nameStem = sourceInfo.completeBaseName().isEmpty() ? originalFileName : sourceInfo.completeBaseName();
    }

    QString candidate = dir.filePath(QString("%1(%2)%3").arg(nameStem).arg(1).arg(extension));
    int suffix = 2;
    while (QFileInfo::exists(candidate)) {
        candidate = dir.filePath(QString("%1(%2)%3").arg(nameStem).arg(suffix).arg(extension));
        ++suffix;
    }

    return QDir::toNativeSeparators(candidate);
}

QString ListViewFileOpsHelper::uniqueNewFolderPath(const QString& targetDir, const QString& baseName)
{
    const QString effectiveBaseName = baseName.isEmpty()
        ? QCoreApplication::translate("ListView", "New Folder")
        : baseName;
    QDir dir(targetDir);
    if (!dir.exists()) {
        return QString();
    }

    QString candidate = dir.filePath(effectiveBaseName);
    if (!QFileInfo::exists(candidate)) {
        return QDir::toNativeSeparators(candidate);
    }

    int suffix = 2;
    do {
        candidate = dir.filePath(QString("%1 (%2)").arg(effectiveBaseName).arg(suffix));
        ++suffix;
    } while (QFileInfo::exists(candidate));

    return QDir::toNativeSeparators(candidate);
}

QString ListViewFileOpsHelper::uniqueShortcutPathForTarget(const QString& sourcePath, const QString& targetDir)
{
    const QString baseName = shortcutBaseNameForSource(sourcePath);

    QDir dir(targetDir);
    QString candidate = dir.filePath(baseName + " - Shortcut.lnk");
    int suffix = 2;
    while (QFileInfo::exists(candidate)) {
        candidate = dir.filePath(QString("%1 - Shortcut (%2).lnk").arg(baseName).arg(suffix));
        ++suffix;
    }
    return QDir::toNativeSeparators(candidate);
}

bool ListViewFileOpsHelper::createDirectoryPath(const QString& targetPath)
{
    if (targetPath.isEmpty()) {
        return false;
    }

    QDir parentDir = QFileInfo(targetPath).dir();
    if (!parentDir.exists() && !parentDir.mkpath(".")) {
        return false;
    }

    return QDir().mkdir(targetPath);
}

bool ListViewFileOpsHelper::copyPathToExactDestination(const QString& sourcePath, const QString& destinationPath)
{
    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists()) {
        return false;
    }

    if (sourceInfo.isDir()) {
        QDir destParent = QFileInfo(destinationPath).dir();
        if (!destParent.exists() && !destParent.mkpath(".")) {
            return false;
        }

        if (!QDir().mkpath(destinationPath)) {
            return false;
        }

        QDir sourceDir(sourcePath);
        const QFileInfoList entries = sourceDir.entryInfoList(
            QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System,
            QDir::DirsFirst | QDir::Name
        );

        for (const QFileInfo& entry : entries) {
            const QString childDestination = QDir(destinationPath).filePath(entry.fileName());
            if (!copyPathToExactDestination(entry.absoluteFilePath(), childDestination)) {
                return false;
            }
        }
        return true;
    }

    QDir destParent = QFileInfo(destinationPath).dir();
    if (!destParent.exists() && !destParent.mkpath(".")) {
        return false;
    }

    if (QFile::exists(destinationPath) && !QFile::remove(destinationPath)) {
        return false;
    }

    return QFile::copy(sourcePath, destinationPath);
}

bool ListViewFileOpsHelper::movePathToExactDestination(const QString& sourcePath, const QString& destinationPath)
{
    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists()) {
        return false;
    }

    QDir destParent = QFileInfo(destinationPath).dir();
    if (!destParent.exists() && !destParent.mkpath(".")) {
        return false;
    }

    if (QFileInfo::exists(destinationPath)) {
        if (QFileInfo(destinationPath).isDir()) {
            if (!QDir(destinationPath).removeRecursively()) {
                return false;
            }
        } else if (!QFile::remove(destinationPath)) {
            return false;
        }
    }

    if (sourceInfo.isDir()) {
        if (QDir().rename(sourcePath, destinationPath)) {
            return true;
        }

        if (!copyPathToExactDestination(sourcePath, destinationPath)) {
            return false;
        }
        return QDir(sourcePath).removeRecursively();
    }

    if (QFile::rename(sourcePath, destinationPath)) {
        return true;
    }

    if (!copyPathToExactDestination(sourcePath, destinationPath)) {
        return false;
    }
    return QFile::remove(sourcePath);
}

bool ListViewFileOpsHelper::copyFilesToDirectoryWithRule(const QStringList& sourcePaths, const QString& targetDir)
{
    bool ok = true;
    for (const QString& filePath : sourcePaths) {
        const QString destinationPath = uniquePasteDestinationPath(filePath, targetDir);
        ok = copyPathToExactDestination(filePath, destinationPath) && ok;
    }
    return ok;
}

bool ListViewFileOpsHelper::createShellShortcut(const QString& sourcePath, const QString& shortcutPath)
{
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninit = SUCCEEDED(initHr);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) {
        return false;
    }

    IShellLinkW* shellLink = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&shellLink));
    if (FAILED(hr) || !shellLink) {
        if (shouldUninit) CoUninitialize();
        return false;
    }

    const std::wstring source = QDir::toNativeSeparators(sourcePath).toStdWString();
    shellLink->SetPath(source.c_str());
    shellLink->SetWorkingDirectory(QFileInfo(sourcePath).absolutePath().toStdWString().c_str());

    IPersistFile* persistFile = nullptr;
    hr = shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persistFile));
    bool ok = false;
    if (SUCCEEDED(hr) && persistFile) {
        const std::wstring shortcut = QDir::toNativeSeparators(shortcutPath).toStdWString();
        ok = SUCCEEDED(persistFile->Save(shortcut.c_str(), TRUE));
        persistFile->Release();
    }

    shellLink->Release();
    if (shouldUninit) CoUninitialize();
    return ok;
}

bool ListViewFileOpsHelper::shouldCreateShortcutForExternalPath(const QString& sourcePath)
{
    if (sourcePath.isEmpty()) {
        return false;
    }

    return isDriveRootSourcePath(sourcePath) || hasSystemAttribute(sourcePath);
}
