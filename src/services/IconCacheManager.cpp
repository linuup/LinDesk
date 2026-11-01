#include "services/IconCacheManager.h"
#include "services/AppPaths.h"
#include "services/SysInfoUtil.h"
#include "services/SystemApi/CommonEnum.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QRunnable>
#include <QThreadPool>
#include <windows.h>
#include <objbase.h>

IconCacheManager::IconCacheManager(QObject* parent)
    : QObject(parent)
{
}

IconCacheManager& IconCacheManager::instance()
{
    static IconCacheManager cache;
    return cache;
}

QIcon IconCacheManager::getIcon(const QString& key, const std::function<QIcon()>& loader)
{
    {
        QMutexLocker locker(&m_mutex);
        QIcon cachedIcon;
        if (m_iconCache.get(key, cachedIcon)) {
            return cachedIcon;
        }
    }

    const QIcon icon = loader ? loader() : QIcon();
    if (!icon.isNull()) {
        QMutexLocker locker(&m_mutex);
        m_iconCache.put(key, icon, m_maxCapacity);
    }
    return icon;
}

QImage IconCacheManager::getImage(const QString& key, const std::function<QImage()>& loader)
{
    {
        QMutexLocker locker(&m_mutex);
        QImage cachedImg;
        if (m_imageCache.get(key, cachedImg)) {
            return cachedImg;
        }
    }

    const QImage image = loader ? loader() : QImage();
    if (!image.isNull()) {
        QMutexLocker locker(&m_mutex);
        m_imageCache.put(key, image, m_maxCapacity);
    }
    return image;
}

QPixmap IconCacheManager::getPixmap(const QString& key, const std::function<QPixmap()>& loader)
{
    {
        QMutexLocker locker(&m_mutex);
        QImage cachedImg;
        if (m_imageCache.get(key, cachedImg)) {
            return QPixmap::fromImage(cachedImg);
        }
    }

    const QPixmap pixmap = loader ? loader() : QPixmap();
    if (!pixmap.isNull()) {
        QMutexLocker locker(&m_mutex);
        m_imageCache.put(key, pixmap.toImage(), m_maxCapacity);
    }
    return pixmap;
}

void IconCacheManager::putImage(const QString& key, const QImage& image)
{
    if (image.isNull()) return;
    QMutexLocker locker(&m_mutex);
    m_imageCache.put(key, image, m_maxCapacity);
}

void IconCacheManager::putPixmap(const QString& key, const QPixmap& pixmap)
{
    if (pixmap.isNull()) return;
    putImage(key, pixmap.toImage());
}

void IconCacheManager::invalidateIcon(const QString& key)
{
    QMutexLocker locker(&m_mutex);
    m_iconCache.remove(key);
}

void IconCacheManager::invalidateIconsByPrefix(const QString& prefix)
{
    QMutexLocker locker(&m_mutex);
    m_iconCache.removeByPrefix(prefix);
}

void IconCacheManager::invalidateImage(const QString& key)
{
    QMutexLocker locker(&m_mutex);
    m_imageCache.remove(key);
}

void IconCacheManager::invalidatePixmap(const QString& key)
{
    QMutexLocker locker(&m_mutex);
    m_imageCache.remove(key);
}

void IconCacheManager::invalidatePixmapsByPrefix(const QString& prefix)
{
    QMutexLocker locker(&m_mutex);
    m_imageCache.removeByPrefix(prefix);
}

void IconCacheManager::invalidateItemCache(const QString& iconKey, const QString& normalizedPath)
{
    if (!iconKey.isEmpty()) {
        invalidateIconsByPrefix(iconKey + "|base|");
        invalidatePixmapsByPrefix(iconKey + "|render|");
        invalidatePixmapsByPrefix(iconKey + "|matte|");
    }
    if (!normalizedPath.isEmpty()) {
        const QString norm = QDir::toNativeSeparators(normalizedPath);
        invalidateIconsByPrefix("path:" + norm);
        invalidatePixmapsByPrefix("path:" + norm);
        invalidateIconsByPrefix("dir:v3:" + norm);
        invalidatePixmapsByPrefix("dir:v3:" + norm);
        invalidatePixmapsByPrefix("image_preview:" + norm);
    }
}

void IconCacheManager::clear()
{
    QMutexLocker locker(&m_mutex);
    m_iconCache.clear();
    m_imageCache.clear();
    m_inFlightKeys.clear();
}

void IconCacheManager::setMaxCapacity(int maxCapacity)
{
    QMutexLocker locker(&m_mutex);
    m_maxCapacity = qMax(1, maxCapacity);
}

int IconCacheManager::maxCapacity() const
{
    QMutexLocker locker(&m_mutex);
    return m_maxCapacity;
}

int IconCacheManager::iconCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_iconCache.count();
}

int IconCacheManager::imageCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_imageCache.count();
}

QString IconCacheManager::diskCacheDirectory() const
{
    return AppPaths::userIconCacheDir();
}

QString IconCacheManager::computeDiskKey(const QString& filePath, qint64 mtime, int sizeTier) const
{
    const QString raw = QString("%1|%2|%3").arg(QDir::toNativeSeparators(filePath).toLower()).arg(mtime).arg(sizeTier);
    return QString::fromUtf8(QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Md5).toHex());
}

bool IconCacheManager::hasDiskCache(const QString& diskKey) const
{
    if (diskKey.isEmpty()) return false;
    const QString filePath = QDir(diskCacheDirectory()).filePath(diskKey + QStringLiteral(".png"));
    return QFileInfo::exists(filePath);
}

QImage IconCacheManager::loadDiskCache(const QString& diskKey) const
{
    if (diskKey.isEmpty()) return QImage();
    const QString filePath = QDir(diskCacheDirectory()).filePath(diskKey + QStringLiteral(".png"));
    QImage img;
    if (img.load(filePath, "PNG")) {
        return img;
    }
    return QImage();
}

void IconCacheManager::saveDiskCache(const QString& diskKey, const QImage& image) const
{
    if (diskKey.isEmpty() || image.isNull()) return;
    const QString dir = diskCacheDirectory();
    QDir().mkpath(dir);
    const QString filePath = QDir(dir).filePath(diskKey + QStringLiteral(".png"));
    image.save(filePath, "PNG");
}

QPixmap IconCacheManager::genericFolderPlaceholder(int size)
{
    QMutexLocker locker(&m_mutex);
    auto it = m_placeholderFolders.find(size);
    if (it != m_placeholderFolders.end()) {
        return it.value();
    }
    QIcon icon = SysInfoUtil::SysFileIconByDLL(4, size, "shell32.dll");
    QPixmap pix = (!icon.isNull()) ? icon.pixmap(size, size) : SysInfoUtil::GetBlankFileFallbackIcon(size).pixmap(size, size);
    m_placeholderFolders.insert(size, pix);
    return pix;
}

QPixmap IconCacheManager::genericAppPlaceholder(int size)
{
    QMutexLocker locker(&m_mutex);
    auto it = m_placeholderApps.find(size);
    if (it != m_placeholderApps.end()) {
        return it.value();
    }
    QIcon icon = SysInfoUtil::SysFileIconByDLL(2, size, "shell32.dll");
    QPixmap pix = (!icon.isNull()) ? icon.pixmap(size, size) : SysInfoUtil::GetBlankFileFallbackIcon(size).pixmap(size, size);
    m_placeholderApps.insert(size, pix);
    return pix;
}

QPixmap IconCacheManager::genericFilePlaceholder(int size)
{
    QMutexLocker locker(&m_mutex);
    auto it = m_placeholderFiles.find(size);
    if (it != m_placeholderFiles.end()) {
        return it.value();
    }
    QPixmap pix = SysInfoUtil::GetBlankFileFallbackIcon(size).pixmap(size, size);
    m_placeholderFiles.insert(size, pix);
    return pix;
}

QPixmap IconCacheManager::genericPlaceholder(int objectType, const QString& suffix, int size)
{
    const QString s = suffix.toLower();
    if (objectType == static_cast<int>(ObjectType::Folder_ObjectType)) {
        return genericFolderPlaceholder(size);
    }
    if (s == "exe" || s == "msi" || s == "lnk" || s == "url") {
        return genericAppPlaceholder(size);
    }
    return genericFilePlaceholder(size);
}

bool IconCacheManager::isPendingAsync(const QString& memKey) const
{
    QMutexLocker locker(&m_mutex);
    return m_inFlightKeys.contains(memKey);
}

void IconCacheManager::requestAsyncPixmap(
    const QString& memKey,
    const QString& diskKey,
    const QString& notifyPath,
    const std::function<QImage()>& workerLoader)
{
    {
        QMutexLocker locker(&m_mutex);
        QImage cached;
        if (m_imageCache.get(memKey, cached)) {
            return;
        }
        if (m_inFlightKeys.contains(memKey)) {
            return;
        }
        m_inFlightKeys.insert(memKey);
    }

    struct AsyncIconRunnable : public QRunnable
    {
        IconCacheManager* manager;
        QString memKey;
        QString diskKey;
        QString notifyPath;
        std::function<QImage()> loader;

        AsyncIconRunnable(IconCacheManager* mgr, QString mk, QString dk, QString np, std::function<QImage()> l)
            : manager(mgr), memKey(std::move(mk)), diskKey(std::move(dk)), notifyPath(std::move(np)), loader(std::move(l))
        {
            setAutoDelete(true);
        }

        void run() override
        {
            HRESULT hrCo = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

            QImage result;
            if (!diskKey.isEmpty() && manager->hasDiskCache(diskKey)) {
                result = manager->loadDiskCache(diskKey);
            }
            if (result.isNull() && loader) {
                result = loader();
                if (!result.isNull() && !diskKey.isEmpty()) {
                    manager->saveDiskCache(diskKey, result);
                }
            }

            if (SUCCEEDED(hrCo)) {
                CoUninitialize();
            }

            {
                QMutexLocker locker(&manager->m_mutex);
                manager->m_inFlightKeys.remove(memKey);
                if (!result.isNull()) {
                    manager->m_imageCache.put(memKey, result, manager->m_maxCapacity);
                }
            }

            if (!result.isNull()) {
                const QString nPath = notifyPath;
                const QString mKey = memKey;
                IconCacheManager* m = manager;
                QMetaObject::invokeMethod(m, [m, mKey, nPath]() {
                    emit m->iconLoaded(mKey, nPath);
                }, Qt::QueuedConnection);
            }
        }
    };

    QThreadPool::globalInstance()->start(new AsyncIconRunnable(this, memKey, diskKey, notifyPath, workerLoader));
}
