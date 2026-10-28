#pragma once

#include <QHash>
#include <QIcon>
#include <QImage>
#include <QMutex>
#include <QPixmap>
#include <QString>
#include <functional>
#include <list>

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
    void invalidateItemCache(const QString& iconKey, const QString& normalizedPath = QString());
    void clear();

    void setMaxCapacity(int maxCapacity);
    int maxCapacity() const;
    int iconCount() const;
    int imageCount() const;

private:
    IconCacheManager() = default;

    Q_DISABLE_COPY_MOVE(IconCacheManager)

    template <typename T>
    struct LruCache {
        struct Entry {
            typename std::list<QString>::iterator lruIt;
            T value;
        };

        std::list<QString> lruList;
        QHash<QString, Entry> map;

        bool get(const QString& key, T& outVal) {
            auto it = map.find(key);
            if (it == map.end()) return false;
            lruList.splice(lruList.begin(), lruList, it.value().lruIt);
            outVal = it.value().value;
            return true;
        }

        void put(const QString& key, const T& val, int maxCap) {
            auto it = map.find(key);
            if (it != map.end()) {
                it.value().value = val;
                lruList.splice(lruList.begin(), lruList, it.value().lruIt);
                return;
            }

            if (maxCap > 0 && map.size() >= maxCap && !lruList.empty()) {
                const QString oldestKey = lruList.back();
                lruList.pop_back();
                map.remove(oldestKey);
            }

            lruList.push_front(key);
            Entry entry;
            entry.lruIt = lruList.begin();
            entry.value = val;
            map.insert(key, entry);
        }

        void remove(const QString& key) {
            auto it = map.find(key);
            if (it != map.end()) {
                lruList.erase(it.value().lruIt);
                map.erase(it);
            }
        }

        void removeByPrefix(const QString& prefix) {
            auto it = map.begin();
            while (it != map.end()) {
                if (it.key().startsWith(prefix)) {
                    lruList.erase(it.value().lruIt);
                    it = map.erase(it);
                } else {
                    ++it;
                }
            }
        }

        void clear() {
            lruList.clear();
            map.clear();
        }

        int count() const {
            return map.size();
        }
    };

    mutable QMutex m_mutex;
    int m_maxCapacity = 4096;
    LruCache<QIcon> m_iconCache;
    LruCache<QImage> m_imageCache; // 缓存 QImage（堆内存），避免在 Windows 上占用 GDI 句柄与显存
};
