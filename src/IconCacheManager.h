#pragma once

#include <QHash>
#include <QIcon>
#include <QMutex>
#include <QPixmap>
#include <functional>

class IconCacheManager
{
public:
    static IconCacheManager& instance();

    QIcon getIcon(const QString& key, const std::function<QIcon()>& loader);
    QPixmap getPixmap(const QString& key, const std::function<QPixmap()>& loader);

    void invalidateIcon(const QString& key);
    void invalidatePixmap(const QString& key);
    void invalidatePixmapsByPrefix(const QString& prefix);
    void clear();

private:
    IconCacheManager() = default;

    Q_DISABLE_COPY_MOVE(IconCacheManager)

    QMutex m_mutex;
    QHash<QString, QIcon> m_iconCache;
    QHash<QString, QPixmap> m_pixmapCache;
};
