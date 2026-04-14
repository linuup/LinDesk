#include "IconCacheManager.h"

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

QPixmap IconCacheManager::getPixmap(const QString& key, const std::function<QPixmap()>& loader)
{
    {
        QMutexLocker locker(&m_mutex);
        const auto it = m_pixmapCache.constFind(key);
        if (it != m_pixmapCache.constEnd()) {
            return it.value();
        }
    }

    const QPixmap pixmap = loader ? loader() : QPixmap();
    if (!pixmap.isNull()) {
        QMutexLocker locker(&m_mutex);
        m_pixmapCache.insert(key, pixmap);
    }
    return pixmap;
}

void IconCacheManager::invalidateIcon(const QString& key)
{
    QMutexLocker locker(&m_mutex);
    m_iconCache.remove(key);
}

void IconCacheManager::invalidatePixmap(const QString& key)
{
    QMutexLocker locker(&m_mutex);
    m_pixmapCache.remove(key);
}

void IconCacheManager::invalidatePixmapsByPrefix(const QString& prefix)
{
    QMutexLocker locker(&m_mutex);
    auto it = m_pixmapCache.begin();
    while (it != m_pixmapCache.end()) {
        if (it.key().startsWith(prefix)) {
            it = m_pixmapCache.erase(it);
        } else {
            ++it;
        }
    }
}

void IconCacheManager::clear()
{
    QMutexLocker locker(&m_mutex);
    m_iconCache.clear();
    m_pixmapCache.clear();
}
