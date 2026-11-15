#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace SystemInfo {

class SystemInfoService {
public:
    SystemInfoService();
    ~SystemInfoService();

    QJsonObject snapshot();
    QJsonObject categorySnapshot(const QString& category);

private:
    QString cpuName() const;
    QJsonObject cpuSnapshot();
    QJsonObject memorySnapshot() const;
    QJsonObject gpuSnapshot();
    QJsonObject diskSnapshot() const;
    QJsonObject networkSnapshot();
    QJsonObject batterySnapshot() const;
    QString windowsVersionText() const;

#ifdef Q_OS_WIN
    QJsonObject memoryModuleSnapshot() const;
    double gpuUsagePercent();
    static quint64 fileTimeToUInt64(const void* fileTimePtr);
    void initPdhCounters();
    void queryNvmlGpu(QJsonObject& gpuObj);
    bool m_hasCpuSample = false;
    quint64 m_lastCpuIdle = 0;
    quint64 m_lastCpuKernel = 0;
    quint64 m_lastCpuUser = 0;
    quint32 m_cpuBaseFrequencyMHz = 0;
    bool m_hasNetworkSample = false;
    quint64 m_lastNetworkIn = 0;
    quint64 m_lastNetworkOut = 0;
    qint64 m_lastNetworkMs = 0;
    void* m_gpuQuery = nullptr;
    void* m_gpuUsageCounter = nullptr;
    bool m_hasGpuUsageSample = false;
    bool m_gpuQueryFailed = false;
    void* m_cpuQuery = nullptr;
    void* m_cpuPerfCounter = nullptr;
    void* m_cpuTempCounter = nullptr;
    bool m_hasCpuQuery = false;
    bool m_nvmlLoaded = false;
    bool m_nvmlAttempted = false;
    void* m_nvmlModule = nullptr;

    struct CorePerfSample {
        quint64 idle = 0;
        quint64 kernel = 0;
        quint64 user = 0;
    };
    QVector<CorePerfSample> m_lastCoreSamples;
    bool m_hasCoreSamples = false;
    mutable quint64 m_cachedL1CacheBytes = 0;
    mutable quint64 m_cachedL2CacheBytes = 0;
    mutable quint64 m_cachedL3CacheBytes = 0;
    mutable int m_cachedSockets = 0;
    mutable bool m_hasCachedCacheAndSockets = false;
#endif

    // 分频采集缓存与时间戳 (Tiered Sampling Cache)
    qint64 m_lastCpuSampleMs = 0;
    qint64 m_lastGpuSampleMs = 0;
    qint64 m_lastMemorySampleMs = 0;
    qint64 m_lastNetworkSampleMs = 0;
    qint64 m_lastDiskSampleMs = 0;
    qint64 m_lastBatterySampleMs = 0;

    QJsonObject m_cachedCpu;
    QJsonObject m_cachedGpu;
    QJsonObject m_cachedMemory;
    QJsonObject m_cachedNetwork;
    QJsonObject m_cachedDisk;
    QJsonObject m_cachedBattery;

    // 静态硬件规格（启动时检测一次，无需重复扫描）
    mutable QString m_cachedCpuName;
    mutable int m_cachedPhysicalCores = 0;
    mutable int m_cachedLogicalCores = 0;
    mutable QJsonObject m_cachedMemoryModules;
    mutable bool m_hasCachedMemoryModules = false;
    mutable QString m_cachedOsVersion;
};

}
