#include "services/DesktopItemLoader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include "desktop/ListItem.h"
#include <SystemApi.h>

namespace {

QString objectTypeToString(ObjectType type)
{
    switch (type) {
    case ObjectType::BackGround: return "BackGround";
    case ObjectType::File: return "File";
    case ObjectType::Link: return "Link";
    case ObjectType::Folder_ObjectType: return "Folder";
    case ObjectType::DiskLetter: return "DiskLetter";
    case ObjectType::SpecialFolder: return "SpecialFolder";
    case ObjectType::Empty_ObjectType: return "Empty";
    default: return "Unknown";
    }
}

QString fileFormToString(FileForm form)
{
    switch (form) {
    case FileForm::Unknown: return "Unknown";
    case FileForm::RegularFile: return "RegularFile";
    case FileForm::Directory: return "Directory";
    case FileForm::ShortcutFile: return "ShortcutFile";
    case FileForm::DriveRoot: return "DriveRoot";
    case FileForm::VirtualShellItem: return "VirtualShellItem";
    default: return "Unknown";
    }
}

QString targetKindToString(TargetKind kind)
{
    switch (kind) {
    case TargetKind::Unknown: return "Unknown";
    case TargetKind::RegularFile: return "RegularFile";
    case TargetKind::Folder: return "Folder";
    case TargetKind::DriveRoot: return "DriveRoot";
    case TargetKind::SpecialFolder: return "SpecialFolder";
    default: return "Unknown";
    }
}

QJsonObject toJsonObject(const ListItemData& data)
{
    const QFileInfo fileInfo = data.fileInfo();
    QJsonObject obj;
    obj["name"] = data.fullName();
    obj["path"] = data.filePath();
    obj["clsid"] = data.getCLSID();
    obj["type"] = objectTypeToString(data.type());
    obj["typeValue"] = static_cast<int>(data.type());
    obj["fileForm"] = fileFormToString(data.fileForm());
    obj["fileFormValue"] = static_cast<int>(data.fileForm());
    obj["targetKind"] = targetKindToString(data.targetKind());
    obj["targetKindValue"] = static_cast<int>(data.targetKind());
    obj["specialFolderType"] = static_cast<int>(data.specialFolderType());
    obj["exists"] = fileInfo.exists();
    obj["isDir"] = fileInfo.isDir();
    obj["suffix"] = fileInfo.suffix();
    obj["size"] = static_cast<qint64>(fileInfo.exists() ? fileInfo.size() : 0);
    obj["lastModified"] = fileInfo.exists() ? fileInfo.lastModified().toString(Qt::ISODate) : QString();
    return obj;
}

QJsonObject toJsonObject(const ListItem* item)
{
    QJsonObject obj;
    if (!item) {
        obj["isNull"] = true;
        return obj;
    }

    const QFileInfo fileInfo = item->fileInfo();
    const ListItemData data = item->getItemData();
    obj["name"] = item->baseName();
    obj["path"] = item->filePath();
    obj["normalizedPath"] = item->normalizedPath();
    obj["clsid"] = data.getCLSID();
    obj["type"] = objectTypeToString(item->type());
    obj["typeValue"] = static_cast<int>(item->type());
    obj["fileForm"] = fileFormToString(data.fileForm());
    obj["fileFormValue"] = static_cast<int>(data.fileForm());
    obj["targetKind"] = targetKindToString(data.targetKind());
    obj["targetKindValue"] = static_cast<int>(data.targetKind());
    obj["boxId"] = item->boxId();
    obj["visible"] = item->isVisible();
    obj["selected"] = item->isSelected;
    obj["ready"] = item->isReady;
    obj["positionX"] = item->x();
    obj["positionY"] = item->y();
    obj["width"] = item->width();
    obj["height"] = item->height();
    obj["exists"] = fileInfo.exists();
    obj["isDir"] = fileInfo.isDir();
    obj["suffix"] = fileInfo.suffix();
    obj["size"] = static_cast<qint64>(fileInfo.exists() ? fileInfo.size() : 0);
    obj["lastModified"] = fileInfo.exists() ? fileInfo.lastModified().toString(Qt::ISODate) : QString();
    return obj;
}

QJsonObject toJsonObject(const DesktopRootItemState& state)
{
    const DesktopRootItemModel& model = state.model;
    QJsonObject obj;
    obj["name"] = model.displayName;
    obj["path"] = model.filePath;
    obj["normalizedPath"] = model.normalizedPath;
    obj["clsid"] = model.itemData.getCLSID();
    obj["type"] = objectTypeToString(model.objectType);
    obj["typeValue"] = static_cast<int>(model.objectType);
    obj["fileForm"] = fileFormToString(model.itemData.fileForm());
    obj["fileFormValue"] = static_cast<int>(model.itemData.fileForm());
    obj["targetKind"] = targetKindToString(model.itemData.targetKind());
    obj["targetKindValue"] = static_cast<int>(model.itemData.targetKind());
    obj["boxId"] = model.boxId;
    obj["visible"] = model.visible;
    obj["selected"] = model.isSelected;
    obj["ready"] = model.isReady;
    obj["positionX"] = model.position.x();
    obj["positionY"] = model.position.y();
    obj["width"] = model.renderData.itemRect.width();
    obj["height"] = model.renderData.itemRect.height();
    obj["exists"] = model.fileInfo.exists();
    obj["isDir"] = model.fileInfo.isDir();
    obj["suffix"] = model.fileInfo.suffix();
    obj["size"] = static_cast<qint64>(model.fileInfo.exists() ? model.fileInfo.size() : 0);
    obj["lastModified"] = model.fileInfo.exists() ? model.fileInfo.lastModified().toString(Qt::ISODate) : QString();
    return obj;
}

QString normalizedExistingPath(const QString& path)
{
    if (path.isEmpty()) {
        return QString();
    }

    QFileInfo info(path);
    QString normalized = info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
    normalized = QDir::toNativeSeparators(normalized);
    while (normalized.endsWith('\\') || normalized.endsWith('/')) {
        normalized.chop(1);
    }
    return normalized.toLower();
}

QSet<QString> hiddenShellFolderPaths()
{
    QSet<QString> result;

    const QList<QStandardPaths::StandardLocation> locations = {
        QStandardPaths::DesktopLocation,
        QStandardPaths::DocumentsLocation,
        QStandardPaths::DownloadLocation,
        QStandardPaths::MoviesLocation,
        QStandardPaths::MusicLocation,
        QStandardPaths::PicturesLocation,
    };

    for (const QStandardPaths::StandardLocation location : locations) {
        const QString path = QStandardPaths::writableLocation(location);
        const QString normalized = normalizedExistingPath(path);
        if (!normalized.isEmpty()) {
            result.insert(normalized);
        }
    }

    const QString userProfilePath = QDir::homePath();
    const QString normalizedOneDrivePath = normalizedExistingPath(userProfilePath + "/OneDrive");
    if (!normalizedOneDrivePath.isEmpty()) {
        result.insert(normalizedOneDrivePath);
    }

    return result;
}

}

ListItemData DesktopItemLoader::toListItemData(const IconItem& item)
{
    return ListItemData(item);
}

std::vector<ListItemData> DesktopItemLoader::loadAllItems() const
{
    std::vector<ListItemData> allItemsData;
    CSystemApi systemApi;
    const std::vector<IconItem> items = systemApi.GetDesktopPIDLItems();
    allItemsData.reserve(items.size());

    for (const IconItem& item : items) {
        allItemsData.push_back(toListItemData(item));
    }

    return allItemsData;
}

ListItemData DesktopItemLoader::loadItem(const QString& filePath) const
{
    const IconItem item = CSystemApi::CreateIconItem(filePath.toStdWString().c_str());
    return toListItemData(item);
}

bool DesktopItemLoader::shouldTreatAsHiddenShellFolder(const ListItemData& item)
{
    if (item.type() != ObjectType::Folder_ObjectType &&
        item.type() != ObjectType::SpecialFolder &&
        item.type() != ObjectType::DiskLetter) {
        return false;
    }

    static const QSet<QString> kHiddenShellPaths = hiddenShellFolderPaths();
    const QString normalizedItemPath = normalizedExistingPath(item.filePath());
    return !normalizedItemPath.isEmpty() && kHiddenShellPaths.contains(normalizedItemPath);
}

QString DesktopItemLoader::dumpJsonFileContent(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QJsonObject error;
        error["filePath"] = filePath;
        error["error"] = file.errorString();
        return QString::fromUtf8(QJsonDocument(error).toJson(QJsonDocument::Indented));
    }

    const QByteArray content = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(content, &parseError);
    if (parseError.error == QJsonParseError::NoError && !doc.isNull()) {
        return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    }

    QJsonObject raw;
    raw["filePath"] = filePath;
    raw["error"] = parseError.errorString();
    raw["rawText"] = QString::fromUtf8(content);
    return QString::fromUtf8(QJsonDocument(raw).toJson(QJsonDocument::Indented));
}

QString DesktopItemLoader::dumpDirectoryJsonFiles(const QString& directoryPath)
{
    QDir dir(directoryPath);
    QJsonObject result;
    result["directoryPath"] = dir.absolutePath();

    QJsonArray filesArray;
    const QFileInfoList files = dir.entryInfoList(QStringList() << "*.json", QDir::Files | QDir::NoSymLinks, QDir::Name);
    for (const QFileInfo& fileInfo : files) {
        QJsonObject fileObject;
        fileObject["fileName"] = fileInfo.fileName();
        fileObject["filePath"] = fileInfo.absoluteFilePath();

        QFile file(fileInfo.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QByteArray content = file.readAll();
            file.close();

            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(content, &parseError);
            if (parseError.error == QJsonParseError::NoError && !doc.isNull()) {
                fileObject["content"] = doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
            } else {
                fileObject["error"] = parseError.errorString();
                fileObject["rawText"] = QString::fromUtf8(content);
            }
        } else {
            fileObject["error"] = file.errorString();
        }

        filesArray.append(fileObject);
    }

    result["files"] = filesArray;
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Indented));
}

QString DesktopItemLoader::dumpListItemDataJson(const std::vector<ListItemData>& items)
{
    QJsonArray array;
    for (const ListItemData& item : items) {
        array.append(toJsonObject(item));
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Indented));
}

QString DesktopItemLoader::dumpDesktopRootStatesJson(const QVector<DesktopRootItemState>& states)
{
    QJsonArray array;
    for (const DesktopRootItemState& state : states) {
        array.append(toJsonObject(state));
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Indented));
}
