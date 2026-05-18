#include "desktop/ListViewFileOpsHelper.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QTextStream>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <cstring>
#include <Windows.h>
#include <objidl.h>
#include <oleidl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <SystemApi.h>

namespace {
constexpr DWORD kClipboardPreferredDropEffectCopy = 0x00000001;
constexpr DWORD kClipboardPreferredDropEffectMove = 0x00000002;
constexpr UINT kDdwmUpdateWindow = WM_USER + 3;
constexpr int kClipboardPreviewTextLimit = 512;
constexpr int kClipboardPreviewHexLimit = 64;

struct ClipboardVirtualFileEntry {
    QString name;
    QByteArray content;
    bool isDirectory = false;
    bool hasContent = false;
};

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
    auto shellParsingPathForSource = [](const QString& path) -> QString {
        const QString normalizedPath = QDir::toNativeSeparators(path).trimmed();
        if (normalizedPath.isEmpty()) {
            return QString();
        }
        if (normalizedPath.startsWith(QStringLiteral("::")) ||
            normalizedPath.startsWith(QStringLiteral("shell:"), Qt::CaseInsensitive)) {
            return normalizedPath;
        }
        if (normalizedPath.startsWith(QStringLiteral("special:"), Qt::CaseInsensitive)) {
            const QString clsid = normalizedPath.mid(QStringLiteral("special:").size()).trimmed();
            if (!clsid.isEmpty()) {
                return clsid.startsWith(QLatin1Char('{'))
                    ? QStringLiteral("::") + clsid
                    : QStringLiteral("::%1").arg(clsid);
            }
        }
        if (normalizedPath.startsWith(QLatin1Char('{')) && normalizedPath.endsWith(QLatin1Char('}'))) {
            return QStringLiteral("::") + normalizedPath;
        }
        return QString();
    };

    const QString shellParsingPath = shellParsingPathForSource(normalizedSourcePath);
    if (!shellParsingPath.isEmpty()) {
        const IconItem item = CSystemApi::CreateIconItem(shellParsingPath.toStdWString().c_str());
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

QString translatedDropDescriptionMessage(Qt::DropAction action)
{
    switch (action) {
    case Qt::CopyAction:
        return QCoreApplication::translate("ListView", "Copy to %1");
    case Qt::LinkAction:
        return QCoreApplication::translate("ListView", "Create shortcut in %1");
    case Qt::MoveAction:
        return QCoreApplication::translate("ListView", "Move to %1");
    default:
        break;
    }

    return QCoreApplication::translate("ListView", "Move to %1");
}

QString trimmedPreview(const QString& value, int maxLength = kClipboardPreviewTextLimit)
{
    QString preview = value;
    preview.replace("\r\n", "\\r\\n");
    preview.replace('\n', "\\n");
    preview.replace('\r', "\\r");
    if (preview.size() > maxLength) {
        preview = preview.left(maxLength) + "...";
    }
    return preview;
}

QString hexPreview(const QByteArray& data, int maxBytes = kClipboardPreviewHexLimit)
{
    if (data.isEmpty()) {
        return QStringLiteral("<empty>");
    }

    const QByteArray preview = data.left(maxBytes).toHex(' ');
    QString result = QString::fromLatin1(preview);
    if (data.size() > maxBytes) {
        result += QStringLiteral(" ...");
    }
    return result;
}

QString clipboardFormatName(UINT format)
{
    switch (format) {
    case CF_TEXT:
        return QStringLiteral("CF_TEXT");
    case CF_BITMAP:
        return QStringLiteral("CF_BITMAP");
    case CF_METAFILEPICT:
        return QStringLiteral("CF_METAFILEPICT");
    case CF_SYLK:
        return QStringLiteral("CF_SYLK");
    case CF_DIF:
        return QStringLiteral("CF_DIF");
    case CF_TIFF:
        return QStringLiteral("CF_TIFF");
    case CF_OEMTEXT:
        return QStringLiteral("CF_OEMTEXT");
    case CF_DIB:
        return QStringLiteral("CF_DIB");
    case CF_PALETTE:
        return QStringLiteral("CF_PALETTE");
    case CF_PENDATA:
        return QStringLiteral("CF_PENDATA");
    case CF_RIFF:
        return QStringLiteral("CF_RIFF");
    case CF_WAVE:
        return QStringLiteral("CF_WAVE");
    case CF_UNICODETEXT:
        return QStringLiteral("CF_UNICODETEXT");
    case CF_ENHMETAFILE:
        return QStringLiteral("CF_ENHMETAFILE");
    case CF_HDROP:
        return QStringLiteral("CF_HDROP");
    case CF_LOCALE:
        return QStringLiteral("CF_LOCALE");
    case CF_DIBV5:
        return QStringLiteral("CF_DIBV5");
    default:
        break;
    }

    wchar_t nameBuffer[256] = {};
    const int length = GetClipboardFormatNameW(format, nameBuffer, 255);
    if (length > 0) {
        return QString::fromWCharArray(nameBuffer, length);
    }

    return QStringLiteral("Format#%1").arg(format);
}

bool clipboardFormatUsesGlobalMemory(UINT format)
{
    switch (format) {
    case CF_BITMAP:
    case CF_PALETTE:
    case CF_ENHMETAFILE:
    case CF_DSPBITMAP:
    case CF_DSPENHMETAFILE:
        return false;
    default:
        break;
    }

    return true;
}

QString preferredDropEffectDescription(DWORD effect)
{
    QStringList parts;
    if (effect & kClipboardPreferredDropEffectCopy) {
        parts.append(QStringLiteral("copy"));
    }
    if (effect & kClipboardPreferredDropEffectMove) {
        parts.append(QStringLiteral("move"));
    }
    if (parts.isEmpty()) {
        parts.append(QStringLiteral("none"));
    }
    return QStringLiteral("%1 (0x%2)")
        .arg(parts.join('|'))
        .arg(QString::number(effect, 16));
}

QStringList hdropPaths(HDROP dropHandle)
{
    QStringList paths;
    if (!dropHandle) {
        return paths;
    }

    const UINT count = DragQueryFileW(dropHandle, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < count; ++i) {
        const UINT length = DragQueryFileW(dropHandle, i, nullptr, 0);
        if (length == 0) {
            continue;
        }

        std::wstring buffer(length + 1, L'\0');
        const UINT copied = DragQueryFileW(dropHandle, i, buffer.data(), length + 1);
        if (copied > 0) {
            paths.append(QDir::toNativeSeparators(QString::fromWCharArray(buffer.data(), copied)));
        }
    }

    return paths;
}

QStringList fileGroupDescriptorNames(const void* rawData, SIZE_T rawSize, bool wide)
{
    QStringList names;
    if (!rawData || rawSize < sizeof(UINT)) {
        return names;
    }

    const UINT itemCount = *static_cast<const UINT*>(rawData);
    const BYTE* descriptorData = static_cast<const BYTE*>(rawData) + sizeof(UINT);
    const SIZE_T descriptorBytes = rawSize - sizeof(UINT);

    if (wide) {
        const SIZE_T descriptorSize = sizeof(FILEDESCRIPTORW);
        const UINT safeCount = static_cast<UINT>(descriptorBytes / descriptorSize);
        const UINT count = qMin(itemCount, safeCount);
        const FILEDESCRIPTORW* descriptors = reinterpret_cast<const FILEDESCRIPTORW*>(descriptorData);
        for (UINT i = 0; i < count; ++i) {
            names.append(QString::fromWCharArray(descriptors[i].cFileName).trimmed());
        }
        return names;
    }

    const SIZE_T descriptorSize = sizeof(FILEDESCRIPTORA);
    const UINT safeCount = static_cast<UINT>(descriptorBytes / descriptorSize);
    const UINT count = qMin(itemCount, safeCount);
    const FILEDESCRIPTORA* descriptors = reinterpret_cast<const FILEDESCRIPTORA*>(descriptorData);
    for (UINT i = 0; i < count; ++i) {
        names.append(QString::fromLocal8Bit(descriptors[i].cFileName).trimmed());
    }

    return names;
}

void appendClipboardLine(QTextStream& stream, const QString& line)
{
    stream << line << '\n';
}

template <typename T>
void safeRelease(T*& ptr)
{
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

UINT clipboardFormatId(const wchar_t* formatName)
{
    return RegisterClipboardFormatW(formatName);
}

bool readBytesFromIStream(IStream* stream, QByteArray* out)
{
    if (!stream || !out) {
        return false;
    }

    LARGE_INTEGER zero = {};
    if (FAILED(stream->Seek(zero, STREAM_SEEK_SET, nullptr))) {
        return false;
    }

    QByteArray data;
    STATSTG stat = {};
    if (SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME)) && stat.cbSize.QuadPart > 0 &&
        stat.cbSize.QuadPart <= INT_MAX) {
        data.resize(static_cast<int>(stat.cbSize.QuadPart));
        ULONG bytesRead = 0;
        if (FAILED(stream->Read(data.data(), static_cast<ULONG>(data.size()), &bytesRead))) {
            return false;
        }
        data.resize(static_cast<int>(bytesRead));
        *out = data;
        return true;
    }

    char buffer[4096];
    while (true) {
        ULONG bytesRead = 0;
        const HRESULT hr = stream->Read(buffer, sizeof(buffer), &bytesRead);
        if (FAILED(hr)) {
            return false;
        }
        if (bytesRead == 0) {
            break;
        }
        data.append(buffer, static_cast<int>(bytesRead));
    }

    *out = data;
    return true;
}

bool readBytesFromStorageMedium(const STGMEDIUM& medium, QByteArray* out)
{
    if (!out) {
        return false;
    }

    if (medium.tymed == TYMED_HGLOBAL && medium.hGlobal) {
        const SIZE_T rawSize = GlobalSize(medium.hGlobal);
        const void* rawData = GlobalLock(medium.hGlobal);
        if (!rawData) {
            return false;
        }

        out->resize(static_cast<int>(rawSize));
        if (rawSize > 0) {
            memcpy(out->data(), rawData, rawSize);
        }
        GlobalUnlock(medium.hGlobal);
        return true;
    }

    if (medium.tymed == TYMED_ISTREAM && medium.pstm) {
        return readBytesFromIStream(medium.pstm, out);
    }

    return false;
}

QString uniqueDestinationPathForName(const QString& fileName, const QString& destDir)
{
    const QDir dir(destDir);
    const QString safeName = fileName.trimmed().isEmpty()
        ? QStringLiteral("clipboard_item")
        : fileName.trimmed();
    QString candidate = dir.filePath(safeName);
    if (!QFileInfo::exists(candidate)) {
        return QDir::toNativeSeparators(candidate);
    }

    const QFileInfo info(safeName);
    const QString completeSuffix = info.completeSuffix();
    const QString extension = completeSuffix.isEmpty() ? QString() : QStringLiteral(".") + completeSuffix;
    QString baseName = info.completeBaseName();
    if (baseName.isEmpty()) {
        baseName = info.fileName();
    }
    if (baseName.isEmpty()) {
        baseName = QStringLiteral("clipboard_item");
    }

    int suffix = 1;
    do {
        candidate = dir.filePath(QStringLiteral("%1(%2)%3").arg(baseName).arg(suffix).arg(extension));
        ++suffix;
    } while (QFileInfo::exists(candidate));

    return QDir::toNativeSeparators(candidate);
}

QString bestEffortTextFromBytes(const QByteArray& data)
{
    if (data.isEmpty()) {
        return QString();
    }

    if (data.size() >= 2) {
        const uchar first = static_cast<uchar>(data[0]);
        const uchar second = static_cast<uchar>(data[1]);
        if (first == 0xFF && second == 0xFE) {
            return QString::fromUtf16(reinterpret_cast<const ushort*>(data.constData() + 2), (data.size() - 2) / 2);
        }
        if (first == 0xFE && second == 0xFF) {
            QByteArray swapped;
            swapped.resize(data.size() - 2);
            for (int i = 2; i + 1 < data.size(); i += 2) {
                swapped[i - 2] = data[i + 1];
                swapped[i - 1] = data[i];
            }
            return QString::fromUtf16(reinterpret_cast<const ushort*>(swapped.constData()), swapped.size() / 2);
        }
    }

    int nulCount = 0;
    const int inspectCount = qMin(data.size(), 64);
    for (int i = 0; i < inspectCount; ++i) {
        if (data[i] == '\0') {
            ++nulCount;
        }
    }
    if (nulCount > inspectCount / 4 && (data.size() % 2) == 0) {
        return QString::fromUtf16(reinterpret_cast<const ushort*>(data.constData()), data.size() / 2);
    }

    const QString utf8Text = QString::fromUtf8(data.constData(), data.size());
    if (!utf8Text.contains(QChar::ReplacementCharacter)) {
        return utf8Text;
    }

    return QString::fromLocal8Bit(data.constData(), data.size());
}

QString clipboardIniValueForKey(const QString& text, const QString& key)
{
    const QStringList lines = text.split('\n');
    for (QString line : lines) {
        line.remove('\r');
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(';') || trimmed.startsWith('#')) {
            continue;
        }

        const int separatorIndex = trimmed.indexOf('=');
        if (separatorIndex <= 0) {
            continue;
        }

        const QString currentKey = trimmed.left(separatorIndex).trimmed();
        if (currentKey.compare(key, Qt::CaseInsensitive) != 0) {
            continue;
        }

        return trimmed.mid(separatorIndex + 1).trimmed();
    }

    return QString();
}

QString parsedVirtualTargetValue(const ClipboardVirtualFileEntry& entry)
{
    if (!entry.hasContent) {
        return QString();
    }

    const QString text = bestEffortTextFromBytes(entry.content);
    if (text.trimmed().isEmpty()) {
        return QString();
    }

    if (entry.name.endsWith(QStringLiteral(".url"), Qt::CaseInsensitive)) {
        return clipboardIniValueForKey(text, QStringLiteral("URL"));
    }

    return QString();
}

QString parsedVirtualTargetName(const ClipboardVirtualFileEntry& entry)
{
    const QString targetValue = parsedVirtualTargetValue(entry);
    if (targetValue.isEmpty()) {
        return QString();
    }

    const QUrl url(targetValue);
    if (url.isLocalFile()) {
        const QString localPath = QDir::toNativeSeparators(url.toLocalFile());
        const QFileInfo info(localPath);
        return info.fileName().isEmpty() ? localPath : info.fileName();
    }

    QString candidate = url.fileName();
    if (!candidate.isEmpty()) {
        return candidate;
    }

    const QUrlQuery query(url);
    const QStringList queryKeys = {
        QStringLiteral("filename"),
        QStringLiteral("fileName"),
        QStringLiteral("name"),
        QStringLiteral("title")
    };
    for (const QString& queryKey : queryKeys) {
        candidate = query.queryItemValue(queryKey);
        if (!candidate.isEmpty()) {
            return candidate;
        }
    }

    QString path = url.path();
    while (path.endsWith('/')) {
        path.chop(1);
    }
    const int slashIndex = path.lastIndexOf('/');
    if (slashIndex >= 0 && slashIndex + 1 < path.size()) {
        candidate = path.mid(slashIndex + 1);
        if (!candidate.isEmpty()) {
            return candidate;
        }
    }

    return url.host();
}

bool shouldReplaceVirtualFileName(const QString& fileName)
{
    const QString trimmed = fileName.trimmed();
    if (trimmed.isEmpty()) {
        return true;
    }

    if (trimmed.startsWith('.')) {
        return true;
    }

    const QFileInfo info(trimmed);
    return info.completeBaseName().trimmed().isEmpty();
}

QString resolvedVirtualOutputFileName(const ClipboardVirtualFileEntry& entry)
{
    const QString originalName = entry.name.trimmed();
    if (!shouldReplaceVirtualFileName(originalName)) {
        return originalName;
    }

    const QString parsedName = parsedVirtualTargetName(entry).trimmed();
    if (parsedName.isEmpty()) {
        return originalName.isEmpty() ? QStringLiteral("clipboard_item") : originalName;
    }

    const QFileInfo originalInfo(originalName);
    const QString originalSuffix = originalInfo.completeSuffix();
    if (originalSuffix.isEmpty()) {
        return parsedName;
    }

    return QStringLiteral("%1.%2").arg(parsedName, originalSuffix);
}

bool fileDescriptorEntries(IDataObject* dataObject, QVector<ClipboardVirtualFileEntry>* entries)
{
    if (!dataObject || !entries) {
        return false;
    }

    struct DescriptorFormat {
        const wchar_t* formatName;
        bool wide;
    };

    const DescriptorFormat formats[] = {
        { L"FileGroupDescriptorW", true },
        { L"FileGroupDescriptor", false }
    };

    for (const DescriptorFormat& descriptorFormat : formats) {
        const UINT formatId = clipboardFormatId(descriptorFormat.formatName);
        if (!formatId) {
            continue;
        }

        FORMATETC formatEtc = { static_cast<CLIPFORMAT>(formatId), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM medium = {};
        if (FAILED(dataObject->GetData(&formatEtc, &medium))) {
            continue;
        }

        QByteArray descriptorBytes;
        const bool readOk = readBytesFromStorageMedium(medium, &descriptorBytes);
        ReleaseStgMedium(&medium);
        if (!readOk || descriptorBytes.size() < static_cast<int>(sizeof(UINT))) {
            continue;
        }

        const UINT itemCount = *reinterpret_cast<const UINT*>(descriptorBytes.constData());
        const char* descriptorData = descriptorBytes.constData() + sizeof(UINT);
        const SIZE_T descriptorDataSize = static_cast<SIZE_T>(descriptorBytes.size() - sizeof(UINT));

        if (descriptorFormat.wide) {
            const SIZE_T descriptorSize = sizeof(FILEDESCRIPTORW);
            const UINT safeCount = static_cast<UINT>(descriptorDataSize / descriptorSize);
            const UINT count = qMin(itemCount, safeCount);
            const FILEDESCRIPTORW* descriptors = reinterpret_cast<const FILEDESCRIPTORW*>(descriptorData);
            for (UINT i = 0; i < count; ++i) {
                ClipboardVirtualFileEntry entry;
                entry.name = QString::fromWCharArray(descriptors[i].cFileName).trimmed();
                entry.isDirectory = (descriptors[i].dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                entries->append(entry);
            }
        } else {
            const SIZE_T descriptorSize = sizeof(FILEDESCRIPTORA);
            const UINT safeCount = static_cast<UINT>(descriptorDataSize / descriptorSize);
            const UINT count = qMin(itemCount, safeCount);
            const FILEDESCRIPTORA* descriptors = reinterpret_cast<const FILEDESCRIPTORA*>(descriptorData);
            for (UINT i = 0; i < count; ++i) {
                ClipboardVirtualFileEntry entry;
                entry.name = QString::fromLocal8Bit(descriptors[i].cFileName).trimmed();
                entry.isDirectory = (descriptors[i].dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                entries->append(entry);
            }
        }

        return !entries->isEmpty();
    }

    return false;
}

bool readVirtualFileContent(IDataObject* dataObject, LONG index, QByteArray* out)
{
    if (!dataObject || !out) {
        return false;
    }

    const UINT fileContentsFormat = clipboardFormatId(L"FileContents");
    if (!fileContentsFormat) {
        return false;
    }

    FORMATETC formatEtc = {
        static_cast<CLIPFORMAT>(fileContentsFormat),
        nullptr,
        DVASPECT_CONTENT,
        index,
        TYMED_ISTREAM | TYMED_HGLOBAL
    };

    STGMEDIUM medium = {};
    if (FAILED(dataObject->GetData(&formatEtc, &medium))) {
        return false;
    }

    const bool ok = readBytesFromStorageMedium(medium, out);
    ReleaseStgMedium(&medium);
    return ok;
}

QVector<ClipboardVirtualFileEntry> clipboardVirtualFiles(bool includeContents)
{
    QVector<ClipboardVirtualFileEntry> entries;

    IDataObject* dataObject = nullptr;
    if (FAILED(OleGetClipboard(&dataObject)) || !dataObject) {
        return entries;
    }

    const bool hasDescriptors = fileDescriptorEntries(dataObject, &entries);
    if (!hasDescriptors) {
        safeRelease(dataObject);
        return entries;
    }

    if (includeContents) {
        for (int i = 0; i < entries.size(); ++i) {
            if (entries[i].isDirectory) {
                continue;
            }
            QByteArray content;
            if (readVirtualFileContent(dataObject, i, &content)) {
                entries[i].content = content;
                entries[i].hasContent = true;
            }
        }
    }

    safeRelease(dataObject);
    return entries;
}
}

WindowsDragMimeData* WindowsDragMimeData::fromMimeData(const QMimeData* mimeData)
{
    return const_cast<WindowsDragMimeData*>(dynamic_cast<const WindowsDragMimeData*>(mimeData));
}

QString WindowsDragMimeData::windowsMimeType(const char* value)
{
    return QStringLiteral("application/x-qt-windows-mime;value=\"%1\"").arg(QString::fromLatin1(value));
}

void WindowsDragMimeData::setDropDescription(DROPIMAGETYPE type, const QString& message, const QString& insert)
{
    DROPDESCRIPTION description = {};
    description.type = type;

    const QString safeMessage = message.left(MAX_PATH - 1);
    const QString safeInsert = insert.left(MAX_PATH - 1);
    safeMessage.toWCharArray(description.szMessage);
    description.szMessage[safeMessage.size()] = L'\0';
    safeInsert.toWCharArray(description.szInsert);
    description.szInsert[safeInsert.size()] = L'\0';

    setData(windowsMimeType("DropDescription"),
            QByteArray(reinterpret_cast<const char*>(&description), sizeof(description)));
}

void WindowsDragMimeData::setDropDescriptionForAction(Qt::DropAction action, const QString& insertText)
{
    DROPIMAGETYPE imageType = DROPIMAGE_MOVE;
    switch (action) {
    case Qt::CopyAction:
        imageType = DROPIMAGE_COPY;
        break;
    case Qt::LinkAction:
        imageType = DROPIMAGE_LINK;
        break;
    case Qt::MoveAction:
        imageType = DROPIMAGE_MOVE;
        break;
    default:
        imageType = DROPIMAGE_MOVE;
        break;
    }

    setDropDescription(imageType, translatedDropDescriptionMessage(action), insertText);
}

void WindowsDragMimeData::setInvalidDropDescription(const QString& insertText)
{
    setDropDescription(DROPIMAGE_INVALID,
                       QCoreApplication::translate("ListView", "Cannot drop in %1"),
                       insertText);
}

void WindowsDragMimeData::refreshDragWindow() const
{
    const QByteArray raw = data(windowsMimeType("DragWindow"));
    if (raw.isEmpty()) {
        return;
    }

    HWND dragWindow = nullptr;
    if (raw.size() >= static_cast<int>(sizeof(quintptr))) {
        quintptr value = 0;
        memcpy(&value, raw.constData(), sizeof(quintptr));
        dragWindow = reinterpret_cast<HWND>(value);
    } else if (raw.size() >= static_cast<int>(sizeof(DWORD))) {
        DWORD value = 0;
        memcpy(&value, raw.constData(), sizeof(DWORD));
        dragWindow = reinterpret_cast<HWND>(ULongToHandle(value));
    }

    if (dragWindow) {
        SendMessageW(dragWindow, kDdwmUpdateWindow, 0, 0);
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

bool ListViewFileOpsHelper::clipboardHasVirtualFileItems()
{
    const QVector<ClipboardVirtualFileEntry> entries = clipboardVirtualFiles(false);
    return !entries.isEmpty();
}

QString ListViewFileOpsHelper::clipboardFormatListDump()
{
    QString output;
    QTextStream stream(&output);

    appendClipboardLine(stream, QStringLiteral("[ClipboardFormats] begin"));

    const QClipboard* clipboard = QGuiApplication::clipboard();
    const QMimeData* mimeData = clipboard ? clipboard->mimeData() : nullptr;
    if (!mimeData) {
        appendClipboardLine(stream, QStringLiteral("[ClipboardFormats][Qt] mimeData unavailable"));
    } else {
        const QStringList formats = mimeData->formats();
        appendClipboardLine(stream, QStringLiteral("[ClipboardFormats][Qt] formats: %1").arg(formats.size()));
        for (const QString& format : formats) {
            appendClipboardLine(stream, QStringLiteral("[ClipboardFormats][Qt] format: %1").arg(format));
        }
        appendClipboardLine(stream,
            QStringLiteral("[ClipboardFormats][Qt] hasText=%1 hasUrls=%2 hasImage=%3")
                .arg(mimeData->hasText())
                .arg(mimeData->hasUrls())
                .arg(mimeData->hasImage()));
    }

    if (!OpenClipboard(nullptr)) {
        appendClipboardLine(stream, QStringLiteral("[ClipboardFormats][Win32] OpenClipboard failed"));
        appendClipboardLine(stream, QStringLiteral("[ClipboardFormats] end"));
        return output;
    }

    appendClipboardLine(stream, QStringLiteral("[ClipboardFormats][Win32] formats:"));
    UINT format = 0;
    while ((format = EnumClipboardFormats(format)) != 0) {
        appendClipboardLine(stream,
            QStringLiteral("[ClipboardFormats][Win32] format=%1 name=%2")
                .arg(format)
                .arg(clipboardFormatName(format)));
    }

    CloseClipboard();
    appendClipboardLine(stream, QStringLiteral("[ClipboardFormats] end"));
    return output;
}

QString ListViewFileOpsHelper::clipboardDebugDump(bool includeVirtualContents)
{
    QString output;
    QTextStream stream(&output);

    appendClipboardLine(stream, QStringLiteral("[ClipboardDump] begin"));

    const QClipboard* clipboard = QGuiApplication::clipboard();
    const QMimeData* mimeData = clipboard ? clipboard->mimeData() : nullptr;
    if (!mimeData) {
        appendClipboardLine(stream, QStringLiteral("[ClipboardDump][Qt] mimeData unavailable"));
    } else {
        appendClipboardLine(stream, QStringLiteral("[ClipboardDump][Qt] formats: %1").arg(mimeData->formats().size()));

        if (mimeData->hasText()) {
            appendClipboardLine(stream,
                QStringLiteral("[ClipboardDump][Qt] text: \"%1\"").arg(trimmedPreview(mimeData->text())));
        }

        if (mimeData->hasUrls()) {
            const QList<QUrl> urls = mimeData->urls();
            appendClipboardLine(stream, QStringLiteral("[ClipboardDump][Qt] urls: %1").arg(urls.size()));
            for (const QUrl& url : urls) {
                appendClipboardLine(stream, QStringLiteral("  url: %1").arg(url.toString()));
            }
        }

        if (mimeData->hasImage()) {
            appendClipboardLine(stream, QStringLiteral("[ClipboardDump][Qt] image: available"));
        }

        for (const QString& format : mimeData->formats()) {
            if (format.compare(QStringLiteral("application/x-qt-image"), Qt::CaseInsensitive) == 0) {
                appendClipboardLine(stream,
                    QStringLiteral("[ClipboardDump][Qt] format: %1 image payload skipped").arg(format));
                continue;
            }

            const QByteArray data = mimeData->data(format);
            appendClipboardLine(stream,
                QStringLiteral("[ClipboardDump][Qt] format: %1 size=%2 hex=%3")
                    .arg(format)
                    .arg(data.size())
                    .arg(hexPreview(data)));

            if (format.contains(QStringLiteral("FileNameW"), Qt::CaseInsensitive)) {
                const QStringList paths = extractLocalPathsFromWindowsMimePayload(data, true);
                for (const QString& path : paths) {
                    appendClipboardLine(stream, QStringLiteral("  FileNameW path: %1").arg(path));
                }
            } else if (format.contains(QStringLiteral("FileName"), Qt::CaseInsensitive)) {
                const QStringList paths = extractLocalPathsFromWindowsMimePayload(data, false);
                for (const QString& path : paths) {
                    appendClipboardLine(stream, QStringLiteral("  FileName path: %1").arg(path));
                }
            } else if (format.contains(QStringLiteral("Preferred DropEffect"), Qt::CaseInsensitive) &&
                       data.size() >= static_cast<int>(sizeof(DWORD))) {
                DWORD effect = 0;
                memcpy(&effect, data.constData(), sizeof(DWORD));
                appendClipboardLine(stream,
                    QStringLiteral("  Preferred DropEffect: %1").arg(preferredDropEffectDescription(effect)));
            } else if (format.contains(QStringLiteral("FileGroupDescriptorW"), Qt::CaseInsensitive)) {
                const QStringList names = fileGroupDescriptorNames(data.constData(), static_cast<SIZE_T>(data.size()), true);
                appendClipboardLine(stream, QStringLiteral("  FileGroupDescriptorW items: %1").arg(names.size()));
                for (const QString& name : names) {
                    appendClipboardLine(stream, QStringLiteral("  virtual file: %1").arg(name));
                }
            } else if (format.contains(QStringLiteral("FileGroupDescriptor"), Qt::CaseInsensitive)) {
                const QStringList names = fileGroupDescriptorNames(data.constData(), static_cast<SIZE_T>(data.size()), false);
                appendClipboardLine(stream, QStringLiteral("  FileGroupDescriptor items: %1").arg(names.size()));
                for (const QString& name : names) {
                    appendClipboardLine(stream, QStringLiteral("  virtual file: %1").arg(name));
                }
            }
        }
    }

    const QVector<ClipboardVirtualFileEntry> virtualFiles = clipboardVirtualFiles(includeVirtualContents);
    appendClipboardLine(stream, QStringLiteral("[ClipboardDump][OLE] virtual files: %1").arg(virtualFiles.size()));
    if (!includeVirtualContents) {
        appendClipboardLine(stream, QStringLiteral("[ClipboardDump][OLE] content read skipped"));
    }
    for (const ClipboardVirtualFileEntry& entry : virtualFiles) {
        const QString parsedName = parsedVirtualTargetName(entry);
        const QString parsedValue = parsedVirtualTargetValue(entry);
        const QString outputName = resolvedVirtualOutputFileName(entry);
        appendClipboardLine(stream,
            QStringLiteral("[ClipboardDump][OLE] file name=%1 directory=%2 hasContent=%3 size=%4")
                .arg(entry.name)
                .arg(entry.isDirectory)
                .arg(entry.hasContent)
                .arg(entry.content.size()));
        if (!parsedName.isEmpty()) {
            appendClipboardLine(stream, QStringLiteral("  parsed target name: %1").arg(parsedName));
        }
        if (!parsedValue.isEmpty()) {
            appendClipboardLine(stream, QStringLiteral("  parsed target value: %1").arg(parsedValue));
        }
        if (!outputName.isEmpty() && outputName.compare(entry.name, Qt::CaseInsensitive) != 0) {
            appendClipboardLine(stream, QStringLiteral("  resolved output name: %1").arg(outputName));
        }

        if (entry.hasContent) {
            appendClipboardLine(stream,
                QStringLiteral("  content hex=%1").arg(hexPreview(entry.content, 128)));

            const QString previewText = bestEffortTextFromBytes(entry.content);
            if (!previewText.trimmed().isEmpty()) {
                appendClipboardLine(stream,
                    QStringLiteral("  content text: \"%1\"").arg(trimmedPreview(previewText)));
            }
        }
    }

    if (!OpenClipboard(nullptr)) {
        appendClipboardLine(stream, QStringLiteral("[ClipboardDump][Win32] OpenClipboard failed"));
        appendClipboardLine(stream, QStringLiteral("[ClipboardDump] end"));
        return output;
    }

    appendClipboardLine(stream, QStringLiteral("[ClipboardDump][Win32] formats:"));
    UINT format = 0;
    while ((format = EnumClipboardFormats(format)) != 0) {
        const QString formatName = clipboardFormatName(format);
        HANDLE clipboardData = GetClipboardData(format);
        appendClipboardLine(stream,
            QStringLiteral("[ClipboardDump][Win32] format=%1 name=%2 handle=0x%3")
                .arg(format)
                .arg(formatName)
                .arg(reinterpret_cast<quintptr>(clipboardData), 0, 16));

        if (!clipboardData) {
            continue;
        }

        if (format == CF_HDROP) {
            const QStringList paths = hdropPaths(static_cast<HDROP>(clipboardData));
            appendClipboardLine(stream, QStringLiteral("  CF_HDROP paths: %1").arg(paths.size()));
            for (const QString& path : paths) {
                appendClipboardLine(stream, QStringLiteral("  path: %1").arg(path));
            }
            continue;
        }

        if (!clipboardFormatUsesGlobalMemory(format)) {
            appendClipboardLine(stream, QStringLiteral("  raw: skipped non HGLOBAL clipboard handle"));
            continue;
        }

        const SIZE_T rawSize = GlobalSize(clipboardData);
        if (rawSize == 0) {
            appendClipboardLine(stream, QStringLiteral("  raw: unavailable"));
            continue;
        }

        const void* rawData = GlobalLock(clipboardData);
        if (!rawData) {
            appendClipboardLine(stream, QStringLiteral("  raw: unavailable"));
            continue;
        }

        QByteArray preview(static_cast<const char*>(rawData), static_cast<int>(qMin<SIZE_T>(rawSize, kClipboardPreviewHexLimit)));
        appendClipboardLine(stream,
            QStringLiteral("  rawSize=%1 hex=%2").arg(rawSize).arg(hexPreview(preview, kClipboardPreviewHexLimit)));

        if (format == CF_UNICODETEXT) {
            const wchar_t* text = static_cast<const wchar_t*>(rawData);
            appendClipboardLine(stream,
                QStringLiteral("  unicodeText: \"%1\"").arg(trimmedPreview(QString::fromWCharArray(text))));
        } else if (format == CF_TEXT || format == CF_OEMTEXT) {
            const char* text = static_cast<const char*>(rawData);
            appendClipboardLine(stream,
                QStringLiteral("  text: \"%1\"").arg(trimmedPreview(QString::fromLocal8Bit(text))));
        } else if (formatName.compare(QStringLiteral("Preferred DropEffect"), Qt::CaseInsensitive) == 0 &&
                   rawSize >= sizeof(DWORD)) {
            const DWORD effect = *static_cast<const DWORD*>(rawData);
            appendClipboardLine(stream,
                QStringLiteral("  Preferred DropEffect: %1").arg(preferredDropEffectDescription(effect)));
        } else if (formatName.compare(QStringLiteral("FileGroupDescriptorW"), Qt::CaseInsensitive) == 0) {
            const QStringList names = fileGroupDescriptorNames(rawData, rawSize, true);
            appendClipboardLine(stream, QStringLiteral("  FileGroupDescriptorW items: %1").arg(names.size()));
            for (const QString& name : names) {
                appendClipboardLine(stream, QStringLiteral("  virtual file: %1").arg(name));
            }
        } else if (formatName.compare(QStringLiteral("FileGroupDescriptor"), Qt::CaseInsensitive) == 0) {
            const QStringList names = fileGroupDescriptorNames(rawData, rawSize, false);
            appendClipboardLine(stream, QStringLiteral("  FileGroupDescriptor items: %1").arg(names.size()));
            for (const QString& name : names) {
                appendClipboardLine(stream, QStringLiteral("  virtual file: %1").arg(name));
            }
        }

        GlobalUnlock(clipboardData);
    }

    CloseClipboard();
    appendClipboardLine(stream, QStringLiteral("[ClipboardDump] end"));
    return output;
}

int ListViewFileOpsHelper::pasteVirtualClipboardFilesToDirectory(const QString& targetDir, QStringList* savedPaths)
{
    QDir dir(targetDir);
    if (!dir.exists()) {
        return 0;
    }

    const QVector<ClipboardVirtualFileEntry> entries = clipboardVirtualFiles(true);
    int savedCount = 0;
    for (const ClipboardVirtualFileEntry& entry : entries) {
        const QString outputName = resolvedVirtualOutputFileName(entry);
        const QString targetPath = uniqueDestinationPathForName(outputName, targetDir);
        if (entry.isDirectory) {
            if (createDirectoryPath(targetPath)) {
                ++savedCount;
                if (savedPaths) {
                    savedPaths->append(targetPath);
                }
            }
            continue;
        }

        if (!entry.hasContent) {
            continue;
        }

        QFile outputFile(targetPath);
        if (!outputFile.open(QIODevice::WriteOnly)) {
            continue;
        }
        if (outputFile.write(entry.content) != entry.content.size()) {
            outputFile.close();
            outputFile.remove();
            continue;
        }
        outputFile.close();
        if (outputFile.error() != QFile::NoError) {
            outputFile.remove();
            continue;
        }

        ++savedCount;
        if (savedPaths) {
            savedPaths->append(targetPath);
        }
    }

    return savedCount;
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

bool ListViewFileOpsHelper::moveFilesToDirectoryWithRule(const QStringList& sourcePaths, const QString& targetDir)
{
    bool ok = true;
    for (const QString& filePath : sourcePaths) {
        const QString destinationPath = uniquePasteDestinationPath(filePath, targetDir);
        ok = movePathToExactDestination(filePath, destinationPath) && ok;
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
    bool pathAssigned = false;
    QString shellParsingPath;
    const QString normalizedSourcePath = QDir::toNativeSeparators(sourcePath).trimmed();
    if (normalizedSourcePath.startsWith(QStringLiteral("::")) ||
        normalizedSourcePath.startsWith(QStringLiteral("shell:"), Qt::CaseInsensitive)) {
        shellParsingPath = normalizedSourcePath;
    } else if (normalizedSourcePath.startsWith(QStringLiteral("special:"), Qt::CaseInsensitive)) {
        const QString clsid = normalizedSourcePath.mid(QStringLiteral("special:").size()).trimmed();
        if (!clsid.isEmpty()) {
            shellParsingPath = clsid.startsWith(QLatin1Char('{'))
                ? QStringLiteral("::") + clsid
                : QStringLiteral("::%1").arg(clsid);
        }
    } else if (normalizedSourcePath.startsWith(QLatin1Char('{')) &&
               normalizedSourcePath.endsWith(QLatin1Char('}'))) {
        shellParsingPath = QStringLiteral("::") + normalizedSourcePath;
    }

    if (!shellParsingPath.isEmpty()) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        const std::wstring shellSource = shellParsingPath.toStdWString();
        if (SUCCEEDED(SHParseDisplayName(shellSource.c_str(), nullptr, &pidl, 0, nullptr)) && pidl) {
            pathAssigned = SUCCEEDED(shellLink->SetIDList(pidl));
            CoTaskMemFree(pidl);
        }
    }

    if (!pathAssigned) {
        pathAssigned = SUCCEEDED(shellLink->SetPath(source.c_str()));
        const QString workingDirectory = QFileInfo(sourcePath).absolutePath();
        if (!workingDirectory.isEmpty()) {
            shellLink->SetWorkingDirectory(workingDirectory.toStdWString().c_str());
        }
    }

    IPersistFile* persistFile = nullptr;
    hr = shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persistFile));
    bool ok = false;
    if (pathAssigned && SUCCEEDED(hr) && persistFile) {
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

void ListViewFileOpsHelper::updateInternalDragDescription(const QMimeData* mimeData,
                                                          Qt::DropAction action,
                                                          const QString& insertText,
                                                          bool forbidden)
{
    WindowsDragMimeData* windowsMimeData = WindowsDragMimeData::fromMimeData(mimeData);
    if (!windowsMimeData) {
        return;
    }

    if (forbidden) {
        windowsMimeData->setInvalidDropDescription(insertText);
    } else {
        windowsMimeData->setDropDescriptionForAction(action, insertText);
    }
    windowsMimeData->refreshDragWindow();
}

void ListViewFileOpsHelper::updateInternalDragDescriptionCustom(const QMimeData* mimeData,
                                                                DROPIMAGETYPE type,
                                                                const QString& message,
                                                                const QString& insertText)
{
    WindowsDragMimeData* windowsMimeData = WindowsDragMimeData::fromMimeData(mimeData);
    if (!windowsMimeData) {
        return;
    }

    windowsMimeData->setDropDescription(type, message, insertText);
    windowsMimeData->refreshDragWindow();
}
