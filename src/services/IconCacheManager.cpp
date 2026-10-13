#include "services/IconCacheManager.h"

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
        const auto it = m_iconCache.constFind(key);
        if (it != m_iconCache.constEnd()) {
            return it.value();
        }
    }

    const QIcon icon = loader ? loader() : QIcon();
    if (!icon.isNull()) {
        QMutexLocker locker(&m_mutex);
        m_iconCache.insert(key, icon);
    }
    return icon;
}

QImage IconCacheManager::getImage(const QString& key, const std::function<QImage()>& loader)
{
    {
        QMutexLocker locker(&m_mutex);
        const auto it = m_imageCache.constFind(key);
        if (it != m_imageCache.constEnd()) {
            return it.value();
        }
    }

    const QImage image = loader ? loader() : QImage();
    if (!image.isNull()) {
        QMutexLocker locker(&m_mutex);
        m_imageCache.insert(key, image);
    }
    return image;
}

QPixmap IconCacheManager::getPixmap(const QString& key, const std::function<QPixmap()>& loader)
{
    {
        QMutexLocker locker(&m_mutex);
        const auto it = m_imageCache.constFind(key);
        if (it != m_imageCache.constEnd()) {
            return QPixmap::fromImage(it.value());
        }
    }

    const QPixmap pixmap = loader ? loader() : QPixmap();
    if (!pixmap.isNull()) {
        QMutexLocker locker(&m_mutex);
        m_imageCache.insert(key, pixmap.toImage());
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
    auto it = m_iconCache.begin();
    while (it != m_iconCache.end()) {
        if (it.key().startsWith(prefix)) {
            it = m_iconCache.erase(it);
        } else {
            ++it;
        }
    }
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
    auto it = m_imageCache.begin();
    while (it != m_imageCache.end()) {
        if (it.key().startsWith(prefix)) {
            it = m_imageCache.erase(it);
        } else {
            ++it;
        }
    }
}

void IconCacheManager::clear()
{
    QMutexLocker locker(&m_mutex);
    m_iconCache.clear();
    m_imageCache.clear();
}
