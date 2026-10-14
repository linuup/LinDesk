#include "services/IconCacheManager.h"
#include <QDir>
#include <QMutexLocker>

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

