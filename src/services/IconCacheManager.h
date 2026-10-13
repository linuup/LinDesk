#pragma once

#include <QHash>
#include <QIcon>
#include <QImage>
#include <QMutex>
#include <QPixmap>
#include <functional>

class IconCacheManager
{
public:
    static IconCacheManager& instance();

    QIcon getIcon(const QString& key, const std::function<QIcon()>& loader);
    QImage getImage(const QString& key, const std::function<QImage()>& loader);
    QPixmap getPixmap(const QString& key, const std::function<QPixmap()>& loader);

    void invalidateIcon(const QString& key);
    void invalidateIconsByPrefix(const QString& prefix);
    void invalidateImage(const QString& key);
    void invalidatePixmap(const QString& key);
    void invalidatePixmapsByPrefix(const QString& prefix);
    void clear();

private:
    IconCacheManager() = default;

    Q_DISABLE_COPY_MOVE(IconCacheManager)

    QMutex m_mutex;
    QHash<QString, QIcon> m_iconCache;
    QHash<QString, QImage> m_imageCache; // 缓存 QImage（位于普通堆内存），避免在 Windows 上占用 GDI HBITMAP 句柄与显存
};
