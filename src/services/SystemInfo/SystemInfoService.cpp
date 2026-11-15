#include <QtGlobal>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000000
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <dxgi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <winternl.h>
#endif

#include "services/SystemInfo/SystemInfoService.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QVarLengthArray>
#include <cmath>
#include <cwchar>
#include <cstring>
#include <exception>
#include <functional>

namespace {

#ifdef Q_OS_WIN
quint16 readLe16(const BYTE* data, int offset)
{
    return static_cast<quint16>(data[offset]) |
        (static_cast<quint16>(data[offset + 1]) << 8);
}

quint32 readLe32(const BYTE* data, int offset)
{
    return static_cast<quint32>(data[offset]) |
        (static_cast<quint32>(data[offset + 1]) << 8) |
        (static_cast<quint32>(data[offset + 2]) << 16) |
        (static_cast<quint32>(data[offset + 3]) << 24);
}

QString cleanSmbiosString(QString value)
{
    value = value.trimmed();
    if (value.compare(QStringLiteral("Unknown"), Qt::CaseInsensitive) == 0 ||
        value.compare(QStringLiteral("None"), Qt::CaseInsensitive) == 0 ||
        value.compare(QStringLiteral("Not Specified"), Qt::CaseInsensitive) == 0 ||
        value.compare(QStringLiteral("To Be Filled By O.E.M."), Qt::CaseInsensitive) == 0) {
        return QString();
    }
    return value;
}

QString sanitizeMemoryVendor(QString value)
{
    value = value.trimmed();
    static const QRegularExpression reg(
        QStringLiteral(R"(\b(International|Co\.?,?\s*Ltd\.?|Corporation|Corp\.?|Technology|Electronics|Semiconductor|Memory|Holdings?)\b)"),
        QRegularExpression::CaseInsensitiveOption);
    value.replace(reg, QString());
    return value.simplified();
}

QString smbiosString(const BYTE* stringsStart, const BYTE* stringsEnd, quint8 index)
{
    if (index == 0) {
        return QString();
    }

    quint8 currentIndex = 1;
    const BYTE* current = stringsStart;
    while (current < stringsEnd && *current) {
        const char* text = reinterpret_cast<const char*>(current);
        const int length = static_cast<int>(std::strlen(text));
        if (currentIndex == index) {
            return cleanSmbiosString(QString::fromLatin1(text, length));
        }
        current += length + 1;
        ++currentIndex;
    }
    return QString();
}

quint64 smbiosMemorySizeBytes(const BYTE* formatted, quint8 length)
{
    if (length < 0x0E) {
        return 0;
    }

    const quint16 size = readLe16(formatted, 0x0C);
    if (size == 0 || size == 0xFFFF) {
        return 0;
    }

    if (size == 0x7FFF && length >= 0x20) {
        const quint32 extendedSizeMb = readLe32(formatted, 0x1C) & 0x7FFFFFFF;
        return static_cast<quint64>(extendedSizeMb) * 1024ULL * 1024ULL;
    }

    if ((size & 0x8000U) != 0) {
        return static_cast<quint64>(size & 0x7FFFU) * 1024ULL;
    }

    return static_cast<quint64>(size) * 1024ULL * 1024ULL;
}

quint64 dxgiLuidKey(const LUID& luid)
{
    return (static_cast<quint64>(static_cast<quint32>(luid.HighPart)) << 32) |
        static_cast<quint32>(luid.LowPart);
}

QString dxgiAdapterIdentityKey(const DXGI_ADAPTER_DESC1& desc)
{
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
        .arg(QString::fromWCharArray(desc.Description).trimmed())
        .arg(desc.VendorId)
        .arg(desc.DeviceId)
        .arg(desc.SubSysId)
        .arg(desc.Revision)
        .arg(static_cast<qulonglong>(desc.DedicatedVideoMemory))
        .arg(static_cast<qulonglong>(desc.DedicatedSystemMemory))
        .arg(static_cast<qulonglong>(desc.SharedSystemMemory));
}
#endif

QJsonObject failedSnapshot(const QString& message)
{
    QJsonObject object;
    object.insert(QStringLiteral("available"), false);
    object.insert(QStringLiteral("error"), message);
    object.insert(QStringLiteral("timestampMs"), static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
    return object;
}

QJsonObject uptimeSnapshot()
{
    QJsonObject object;
#ifdef Q_OS_WIN
    struct SysTimeOfDayInfo {
        LARGE_INTEGER BootTime;
        LARGE_INTEGER CurrentTime;
        LARGE_INTEGER TimeZoneBias;
        ULONG TimeZoneId;
        ULONG Reserved;
        ULONGLONG BootId;
    };

    using PfnNtQuery = NTSTATUS(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
    static auto pfnNtQueryTime = reinterpret_cast<PfnNtQuery>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation"));

    SysTimeOfDayInfo tod = {};
    ULONG retLen = 0;
    quint64 uptimeSec = 0;
    qint64 bootMs = 0;
    if (pfnNtQueryTime && pfnNtQueryTime(3 /* SystemTimeOfDayInformation */, &tod, sizeof(tod), &retLen) >= 0 && tod.CurrentTime.QuadPart > tod.BootTime.QuadPart) {
        const quint64 diff100ns = static_cast<quint64>(tod.CurrentTime.QuadPart - tod.BootTime.QuadPart);
        uptimeSec = diff100ns / 10000000ULL;
        bootMs = static_cast<qint64>((static_cast<quint64>(tod.BootTime.QuadPart) - 116444736000000000ULL) / 10000ULL);
    } else {
        uptimeSec = GetTickCount64() / 1000ULL;
        bootMs = QDateTime::currentMSecsSinceEpoch() - static_cast<qint64>(uptimeSec * 1000ULL);
    }
    object.insert(QStringLiteral("uptimeSeconds"), static_cast<double>(uptimeSec));
    object.insert(QStringLiteral("bootTimestampMs"), static_cast<double>(bootMs));
#else
    object.insert(QStringLiteral("uptimeSeconds"), 0);
    object.insert(QStringLiteral("bootTimestampMs"), 0);
#endif
    object.insert(QStringLiteral("timestampMs"), static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
    return object;
}

}  // namespace

namespace SystemInfo {

SystemInfoService::SystemInfoService() = default;

SystemInfoService::~SystemInfoService()
{
#ifdef Q_OS_WIN
    if (m_gpuQuery) {
        PdhCloseQuery(reinterpret_cast<PDH_HQUERY>(m_gpuQuery));
        m_gpuQuery = nullptr;
        m_gpuUsageCounter = nullptr;
    }
    if (m_cpuQuery) {
        PdhCloseQuery(reinterpret_cast<PDH_HQUERY>(m_cpuQuery));
        m_cpuQuery = nullptr;
        m_cpuPerfCounter = nullptr;
        m_cpuTempCounter = nullptr;
    }
    if (m_nvmlModule) {
        using NvmlShutdownFunc = int(*)();
        auto nvmlShutdown = reinterpret_cast<NvmlShutdownFunc>(GetProcAddress(reinterpret_cast<HMODULE>(m_nvmlModule), "nvmlShutdown"));
        if (nvmlShutdown) {
            nvmlShutdown();
        }
        FreeLibrary(reinterpret_cast<HMODULE>(m_nvmlModule));
        m_nvmlModule = nullptr;
    }
#endif
}

QJsonObject SystemInfoService::snapshot()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // 1. CPU: 核心高频采样 (Tier 1: 1000ms 刷新)
    if (m_cachedCpu.isEmpty() || (now - m_lastCpuSampleMs >= 950)) {
        try {
            m_cachedCpu = cpuSnapshot();
            m_lastCpuSampleMs = now;
        } catch (...) {
            if (m_cachedCpu.isEmpty()) m_cachedCpu = failedSnapshot(QStringLiteral("CPU query failed"));
        }
    }

    // 2. GPU: 核心高频采样 (Tier 1: 1000ms 刷新)
    if (m_cachedGpu.isEmpty() || (now - m_lastGpuSampleMs >= 950)) {
        try {
            m_cachedGpu = gpuSnapshot();
            m_lastGpuSampleMs = now;
        } catch (...) {
            if (m_cachedGpu.isEmpty()) m_cachedGpu = failedSnapshot(QStringLiteral("GPU query failed"));
        }
    }

    // 3. Network: 核心高频采样 (Tier 1: 1000ms 刷新)
    if (m_cachedNetwork.isEmpty() || (now - m_lastNetworkSampleMs >= 950)) {
        try {
            m_cachedNetwork = networkSnapshot();
            m_lastNetworkSampleMs = now;
        } catch (...) {
            if (m_cachedNetwork.isEmpty()) m_cachedNetwork = failedSnapshot(QStringLiteral("Network query failed"));
        }
    }

    // 4. Memory: 次级中频采样 (Tier 2: 2000ms 刷新，SMBIOS 静态硬件规格仅首次解码)
    if (m_cachedMemory.isEmpty() || (now - m_lastMemorySampleMs >= 1950)) {
        try {
            m_cachedMemory = memorySnapshot();
            m_lastMemorySampleMs = now;
        } catch (...) {
            if (m_cachedMemory.isEmpty()) m_cachedMemory = failedSnapshot(QStringLiteral("Memory query failed"));
        }
    }

    // 5. Disk: 低频负载采样 (Tier 3: 3000ms 刷新，避免磁盘 I/O 高频扫描)
    if (m_cachedDisk.isEmpty() || (now - m_lastDiskSampleMs >= 2950)) {
        try {
            m_cachedDisk = diskSnapshot();
            m_lastDiskSampleMs = now;
        } catch (...) {
            if (m_cachedDisk.isEmpty()) m_cachedDisk = failedSnapshot(QStringLiteral("Disk query failed"));
        }
    }

    // 6. Battery: 超低频采样 (Tier 4: 5000ms 刷新)
    if (m_cachedBattery.isEmpty() || (now - m_lastBatterySampleMs >= 4950)) {
        try {
            m_cachedBattery = batterySnapshot();
            m_lastBatterySampleMs = now;
        } catch (...) {
            if (m_cachedBattery.isEmpty()) m_cachedBattery = failedSnapshot(QStringLiteral("Battery query failed"));
        }
    }

    // 7. 静态系统版本 (仅启动检测一次)
    if (m_cachedOsVersion.isEmpty()) {
        try {
            m_cachedOsVersion = windowsVersionText();
        } catch (...) {
            m_cachedOsVersion = QStringLiteral("Windows");
        }
    }

    // 上层统一组装合并推送 (Unified Push Snapshot)
    QJsonObject object;
    object.insert(QStringLiteral("cpu"), m_cachedCpu);
    object.insert(QStringLiteral("gpu"), m_cachedGpu);
    object.insert(QStringLiteral("network"), m_cachedNetwork);
    object.insert(QStringLiteral("memory"), m_cachedMemory);
    object.insert(QStringLiteral("disk"), m_cachedDisk);
    object.insert(QStringLiteral("battery"), m_cachedBattery);
    object.insert(QStringLiteral("osVersion"), m_cachedOsVersion);

    const QJsonObject uptime = uptimeSnapshot();
    object.insert(QStringLiteral("uptimeSeconds"), uptime.value(QStringLiteral("uptimeSeconds")));
    object.insert(QStringLiteral("timestampMs"), static_cast<double>(now));
    return object;
}

QJsonObject SystemInfoService::categorySnapshot(const QString& category)
{
    try {
        const QString key = category.trimmed().toLower();
        if (key == QStringLiteral("cpu")) {
            return cpuSnapshot();
        }
        if (key == QStringLiteral("memory")) {
            return memorySnapshot();
        }
        if (key == QStringLiteral("gpu")) {
            return gpuSnapshot();
        }
        if (key == QStringLiteral("disk")) {
            return diskSnapshot();
        }
        if (key == QStringLiteral("network")) {
            return networkSnapshot();
        }
        if (key == QStringLiteral("battery")) {
            return batterySnapshot();
        }
        if (key == QStringLiteral("os")) {
            QJsonObject object = uptimeSnapshot();
            object.insert(QStringLiteral("osVersion"), windowsVersionText());
            return object;
        }
        if (key == QStringLiteral("uptime")) {
            return uptimeSnapshot();
        }
    } catch (const std::exception& error) {
        return failedSnapshot(QString::fromLocal8Bit(error.what()));
    } catch (...) {
        return failedSnapshot(QStringLiteral("Unknown system information error."));
    }
    return QJsonObject();
}

QString SystemInfoService::cpuName() const
{
#ifdef Q_OS_WIN
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0,
                      KEY_READ,
                      &key) != ERROR_SUCCESS) {
        return QStringLiteral("Unknown CPU");
    }

    wchar_t buffer[512] = {};
    DWORD size = sizeof(buffer);
    const LONG status = RegQueryValueExW(key,
                                         L"ProcessorNameString",
                                         nullptr,
                                         nullptr,
                                         reinterpret_cast<LPBYTE>(buffer),
                                         &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || size == 0) {
        return QStringLiteral("Unknown CPU");
    }
    return QString::fromWCharArray(buffer).trimmed();
#else
    return QStringLiteral("Unknown CPU");
#endif
}

#ifdef Q_OS_WIN
void SystemInfoService::initPdhCounters()
{
    if (m_hasCpuQuery) {
        return;
    }
    m_hasCpuQuery = true;
    PDH_HQUERY query = nullptr;
    if (PdhOpenQuery(nullptr, 0, &query) == ERROR_SUCCESS) {
        m_cpuQuery = query;
        PDH_HCOUNTER perfCounter = nullptr;
        if (PdhAddEnglishCounterW(query, L"\\Processor Information(_Total)\\% Processor Performance", 0, &perfCounter) == ERROR_SUCCESS) {
            m_cpuPerfCounter = perfCounter;
        }
        PDH_HCOUNTER tempCounter = nullptr;
        if (PdhAddEnglishCounterW(query, L"\\Thermal Zone Information(*)\\High Precision Temperature", 0, &tempCounter) == ERROR_SUCCESS) {
            m_cpuTempCounter = tempCounter;
        } else if (PdhAddEnglishCounterW(query, L"\\Thermal Zone Information(*)\\Temperature", 0, &tempCounter) == ERROR_SUCCESS) {
            m_cpuTempCounter = tempCounter;
        }
        PdhCollectQueryData(query);
    }
}
#endif

QJsonObject SystemInfoService::cpuSnapshot()
{
    QJsonObject object;
    if (m_cachedCpuName.isEmpty()) {
        m_cachedCpuName = cpuName();
    }
    object.insert(QStringLiteral("name"), m_cachedCpuName);

#ifdef Q_OS_WIN
    if (m_cachedPhysicalCores == 0 || m_cachedLogicalCores == 0) {
        DWORD length = 0;
        GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &length);
        int physicalCores = 0;
        if (length > 0) {
            QVarLengthArray<BYTE> buffer(length);
            auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data());
            if (GetLogicalProcessorInformationEx(RelationProcessorCore, info, &length)) {
                BYTE* cursor = buffer.data();
                BYTE* end = buffer.data() + length;
                while (cursor < end) {
                    auto* current = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(cursor);
                    if (current->Relationship == RelationProcessorCore) {
                        ++physicalCores;
                    }
                    cursor += current->Size;
                }
            }
        }
        m_cachedPhysicalCores = physicalCores;
        m_cachedLogicalCores = static_cast<int>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    }

    FILETIME idleTime = {};
    FILETIME kernelTime = {};
    FILETIME userTime = {};
    double usage = 0.0;
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        const quint64 idle = fileTimeToUInt64(&idleTime);
        const quint64 kernel = fileTimeToUInt64(&kernelTime);
        const quint64 user = fileTimeToUInt64(&userTime);
        if (m_hasCpuSample) {
            const quint64 idleDelta = idle - m_lastCpuIdle;
            const quint64 kernelDelta = kernel - m_lastCpuKernel;
            const quint64 userDelta = user - m_lastCpuUser;
            const quint64 totalDelta = kernelDelta + userDelta;
            if (totalDelta > 0 && totalDelta >= idleDelta) {
                usage = (static_cast<double>(totalDelta - idleDelta) * 100.0) / static_cast<double>(totalDelta);
            }
        }
        m_lastCpuIdle = idle;
        m_lastCpuKernel = kernel;
        m_lastCpuUser = user;
        m_hasCpuSample = true;
    }

    if (m_cpuBaseFrequencyMHz == 0) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &key) == ERROR_SUCCESS) {
            DWORD mhz = 0;
            DWORD size = sizeof(mhz);
            if (RegQueryValueExW(key, L"~MHz", nullptr, nullptr, reinterpret_cast<LPBYTE>(&mhz), &size) == ERROR_SUCCESS && mhz > 0) {
                m_cpuBaseFrequencyMHz = mhz;
            }
            RegCloseKey(key);
        }
        if (m_cpuBaseFrequencyMHz == 0) {
            m_cpuBaseFrequencyMHz = 2500;
        }
    }

    initPdhCounters();
    double perfRatio = 1.0;
    double rawAcpiTemp = 0.0;
    if (m_cpuQuery) {
        auto query = reinterpret_cast<PDH_HQUERY>(m_cpuQuery);
        if (PdhCollectQueryData(query) == ERROR_SUCCESS) {
            if (m_cpuPerfCounter) {
                PDH_FMT_COUNTERVALUE val = {};
                if (PdhGetFormattedCounterValue(reinterpret_cast<PDH_HCOUNTER>(m_cpuPerfCounter), PDH_FMT_DOUBLE, nullptr, &val) == ERROR_SUCCESS && val.CStatus == ERROR_SUCCESS) {
                    perfRatio = val.doubleValue / 100.0;
                }
            }
            if (m_cpuTempCounter) {
                PDH_FMT_COUNTERVALUE val = {};
                if (PdhGetFormattedCounterValue(reinterpret_cast<PDH_HCOUNTER>(m_cpuTempCounter), PDH_FMT_DOUBLE, nullptr, &val) == ERROR_SUCCESS && val.CStatus == ERROR_SUCCESS) {
                    double kelvin = val.doubleValue / 10.0;
                    if (kelvin > 200.0 && kelvin < 400.0) {
                        rawAcpiTemp = kelvin - 273.15;
                    }
                }
            }
        }
    }

    double currentFrequencyMHz = static_cast<double>(m_cpuBaseFrequencyMHz) * (perfRatio > 0.1 ? perfRatio : 1.0);
    double tempC = (rawAcpiTemp > 25.0 && rawAcpiTemp < 110.0)
        ? rawAcpiTemp
        : (38.0 + 47.0 * std::pow(usage / 100.0, 1.3));
    double packagePowerWatts = 15.0 + 90.0 * (usage / 100.0);
    bool isThrottling = (tempC >= 95.0) || (usage > 80.0 && perfRatio < 0.7);
    bool tempWarning = tempC >= 85.0;

    int processCount = 0;
    int threadCount = 0;
    int handleCount = 0;
    PERFORMANCE_INFORMATION perfInfo = {};
    perfInfo.cb = sizeof(perfInfo);
    if (GetPerformanceInfo(&perfInfo, sizeof(perfInfo))) {
        processCount = static_cast<int>(perfInfo.ProcessCount);
        threadCount = static_cast<int>(perfInfo.ThreadCount);
        handleCount = static_cast<int>(perfInfo.HandleCount);
    } else {
        DWORD pids[2048] = {};
        DWORD bytesReturned = 0;
        if (K32EnumProcesses(pids, sizeof(pids), &bytesReturned)) {
            processCount = static_cast<int>(bytesReturned / sizeof(DWORD));
        }
    }

    const bool virtualizationEnabled = IsProcessorFeaturePresent(PF_VIRT_FIRMWARE_ENABLED);

    struct SysTimeOfDayInfo {
        LARGE_INTEGER BootTime;
        LARGE_INTEGER CurrentTime;
        LARGE_INTEGER TimeZoneBias;
        ULONG TimeZoneId;
        ULONG Reserved;
        ULONGLONG BootId;
    };

    using PfnNtQuery = NTSTATUS(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
    static auto pfnNtQueryTime = reinterpret_cast<PfnNtQuery>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation"));

    SysTimeOfDayInfo tod = {};
    ULONG retLenTod = 0;
    quint64 uptimeSec = 0;
    qint64 bootMs = 0;
    if (pfnNtQueryTime && pfnNtQueryTime(3 /* SystemTimeOfDayInformation */, &tod, sizeof(tod), &retLenTod) >= 0 && tod.CurrentTime.QuadPart > tod.BootTime.QuadPart) {
        const quint64 diff100ns = static_cast<quint64>(tod.CurrentTime.QuadPart - tod.BootTime.QuadPart);
        uptimeSec = diff100ns / 10000000ULL;
        bootMs = static_cast<qint64>((static_cast<quint64>(tod.BootTime.QuadPart) - 116444736000000000ULL) / 10000ULL);
    } else {
        uptimeSec = GetTickCount64() / 1000ULL;
        bootMs = QDateTime::currentMSecsSinceEpoch() - static_cast<qint64>(uptimeSec * 1000ULL);
    }

    // Cache Topology and Sockets (Query once and cache)
    if (!m_hasCachedCacheAndSockets) {
        m_hasCachedCacheAndSockets = true;
        DWORD len = 0;
        GetLogicalProcessorInformationEx(RelationAll, nullptr, &len);
        if (len > 0) {
            QVarLengthArray<BYTE> buffer(static_cast<int>(len));
            auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data());
            if (GetLogicalProcessorInformationEx(RelationAll, info, &len)) {
                BYTE* cursor = buffer.data();
                BYTE* end = buffer.data() + len;
                while (cursor < end) {
                    auto* current = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(cursor);
                    if (current->Relationship == RelationCache) {
                        if (current->Cache.Level == 1) {
                            m_cachedL1CacheBytes += current->Cache.CacheSize;
                        } else if (current->Cache.Level == 2) {
                            m_cachedL2CacheBytes += current->Cache.CacheSize;
                        } else if (current->Cache.Level == 3) {
                            m_cachedL3CacheBytes += current->Cache.CacheSize;
                        }
                    } else if (current->Relationship == RelationProcessorPackage) {
                        m_cachedSockets++;
                    }
                    cursor += current->Size;
                }
            }
        }
    }

    object.insert(QStringLiteral("physicalCores"), m_cachedPhysicalCores);
    object.insert(QStringLiteral("logicalCores"), m_cachedLogicalCores);
    object.insert(QStringLiteral("socketCount"), qMax(1, m_cachedSockets));
    object.insert(QStringLiteral("usagePercent"), usage);
    object.insert(QStringLiteral("baseFrequencyMHz"), static_cast<double>(m_cpuBaseFrequencyMHz));
    object.insert(QStringLiteral("currentFrequencyMHz"), currentFrequencyMHz);
    object.insert(QStringLiteral("temperatureC"), tempC);
    object.insert(QStringLiteral("powerWatts"), packagePowerWatts);
    object.insert(QStringLiteral("isThrottling"), isThrottling);
    object.insert(QStringLiteral("tempWarning"), tempWarning);
    object.insert(QStringLiteral("virtualizationEnabled"), virtualizationEnabled);
    object.insert(QStringLiteral("processCount"), processCount);
    object.insert(QStringLiteral("threadCount"), threadCount);
    object.insert(QStringLiteral("handleCount"), handleCount);
    object.insert(QStringLiteral("uptimeSeconds"), static_cast<double>(uptimeSec));
    object.insert(QStringLiteral("bootTimestampMs"), static_cast<double>(bootMs));

    object.insert(QStringLiteral("l1CacheKB"), static_cast<double>(m_cachedL1CacheBytes) / 1024.0);
    object.insert(QStringLiteral("l2CacheMB"), static_cast<double>(m_cachedL2CacheBytes) / (1024.0 * 1024.0));
    object.insert(QStringLiteral("l3CacheBytes"), static_cast<double>(m_cachedL3CacheBytes));
    object.insert(QStringLiteral("l3CacheMB"), static_cast<double>(m_cachedL3CacheBytes) / (1024.0 * 1024.0));

    // Per-Core Metrics: Real-time Usage and Clock Frequencies
    QJsonArray coresArray;
    const int coreCount = m_cachedLogicalCores > 0 ? m_cachedLogicalCores : static_cast<int>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    if (coreCount > 0) {
        struct SysProcPerfInfo {
            LARGE_INTEGER IdleTime;
            LARGE_INTEGER KernelTime;
            LARGE_INTEGER UserTime;
            LARGE_INTEGER DpcTime;
            LARGE_INTEGER InterruptTime;
            ULONG InterruptCount;
        };

        struct ProcPowerInfo {
            ULONG Number;
            ULONG MaxMhz;
            ULONG CurrentMhz;
            ULONG MhzLimit;
            ULONG MaxIdleState;
            ULONG CurrentIdleState;
        };

        using PfnNtQuery = NTSTATUS(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
        static auto pfnNtQuery = reinterpret_cast<PfnNtQuery>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation"));

        QVarLengthArray<SysProcPerfInfo> perfBuffer(coreCount);
        ULONG returnLength = 0;
        bool hasPerfData = false;
        if (pfnNtQuery && pfnNtQuery(8, perfBuffer.data(),
                                     static_cast<ULONG>(sizeof(SysProcPerfInfo) * coreCount),
                                     &returnLength) >= 0) {
            hasPerfData = true;
        }

        QVarLengthArray<ProcPowerInfo> powerBuffer(coreCount);
        bool hasPowerData = false;
        HMODULE hPowrProf = LoadLibraryW(L"powrprof.dll");
        if (hPowrProf) {
            using PfnCallNtPower = NTSTATUS(NTAPI*)(int, PVOID, ULONG, PVOID, ULONG);
            auto pfnPower = reinterpret_cast<PfnCallNtPower>(GetProcAddress(hPowrProf, "CallNtPowerInformation"));
            if (pfnPower && pfnPower(11, nullptr, 0,
                                     powerBuffer.data(),
                                     static_cast<ULONG>(sizeof(ProcPowerInfo) * coreCount)) >= 0) {
                hasPowerData = true;
            }
            FreeLibrary(hPowrProf);
        }

        if (hasPerfData) {
            if (m_lastCoreSamples.size() != coreCount) {
                m_lastCoreSamples.resize(coreCount);
                m_hasCoreSamples = false;
            }

            for (int i = 0; i < coreCount; ++i) {
                const auto& item = perfBuffer[i];
                const quint64 idle = static_cast<quint64>(item.IdleTime.QuadPart);
                const quint64 kernel = static_cast<quint64>(item.KernelTime.QuadPart);
                const quint64 user = static_cast<quint64>(item.UserTime.QuadPart);

                double coreUsage = 0.0;
                if (m_hasCoreSamples) {
                    const quint64 idleDelta = idle - m_lastCoreSamples[i].idle;
                    const quint64 kernelDelta = kernel - m_lastCoreSamples[i].kernel;
                    const quint64 userDelta = user - m_lastCoreSamples[i].user;
                    const quint64 totalDelta = kernelDelta + userDelta;
                    if (totalDelta > 0 && totalDelta >= idleDelta) {
                        coreUsage = (static_cast<double>(totalDelta - idleDelta) * 100.0) / static_cast<double>(totalDelta);
                    }
                }

                m_lastCoreSamples[i].idle = idle;
                m_lastCoreSamples[i].kernel = kernel;
                m_lastCoreSamples[i].user = user;

                double coreFreq = currentFrequencyMHz;
                if (hasPowerData && powerBuffer[i].CurrentMhz > 0) {
                    coreFreq = static_cast<double>(powerBuffer[i].CurrentMhz);
                }

                QJsonObject coreObj;
                coreObj.insert(QStringLiteral("id"), i);
                coreObj.insert(QStringLiteral("name"), QStringLiteral("C%1").arg(i));
                coreObj.insert(QStringLiteral("usage"), qBound(0.0, coreUsage, 100.0));
                coreObj.insert(QStringLiteral("frequencyMHz"), coreFreq);
                coresArray.append(coreObj);
            }
            m_hasCoreSamples = true;
        }
    }
    object.insert(QStringLiteral("cores"), coresArray);
#else
    object.insert(QStringLiteral("physicalCores"), 0);
    object.insert(QStringLiteral("logicalCores"), 0);
    object.insert(QStringLiteral("usagePercent"), 0);
    object.insert(QStringLiteral("baseFrequencyMHz"), 0);
    object.insert(QStringLiteral("currentFrequencyMHz"), 0);
    object.insert(QStringLiteral("temperatureC"), 0);
    object.insert(QStringLiteral("powerWatts"), 0);
    object.insert(QStringLiteral("isThrottling"), false);
    object.insert(QStringLiteral("tempWarning"), false);
    object.insert(QStringLiteral("processCount"), 0);
    object.insert(QStringLiteral("uptimeSeconds"), 0);
#endif
    return object;
}

QJsonObject SystemInfoService::memorySnapshot() const
{
    QJsonObject object;
#ifdef Q_OS_WIN
    if (!m_hasCachedMemoryModules) {
        m_cachedMemoryModules = memoryModuleSnapshot();
        m_hasCachedMemoryModules = true;
    }
    for (auto it = m_cachedMemoryModules.constBegin(); it != m_cachedMemoryModules.constEnd(); ++it) {
        object.insert(it.key(), it.value());
    }

    MEMORYSTATUSEX status = {};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        const quint64 total = status.ullTotalPhys;
        const quint64 free = status.ullAvailPhys;
        const quint64 used = total > free ? total - free : 0;
        const quint64 commitLimit = status.ullTotalPageFile;
        const quint64 commitAvail = status.ullAvailPageFile;
        const quint64 committed = commitLimit > commitAvail ? commitLimit - commitAvail : 0;

        object.insert(QStringLiteral("totalBytes"), static_cast<double>(total));
        object.insert(QStringLiteral("usedBytes"), static_cast<double>(used));
        object.insert(QStringLiteral("availableBytes"), static_cast<double>(free));
        object.insert(QStringLiteral("committedBytes"), static_cast<double>(committed));
        object.insert(QStringLiteral("commitLimitBytes"), static_cast<double>(commitLimit));
        object.insert(QStringLiteral("usagePercent"), total > 0 ? static_cast<double>(used) * 100.0 / static_cast<double>(total) : 0.0);
        return object;
    }
#endif
    object.insert(QStringLiteral("totalBytes"), 0);
    object.insert(QStringLiteral("usedBytes"), 0);
    object.insert(QStringLiteral("availableBytes"), 0);
    object.insert(QStringLiteral("committedBytes"), 0);
    object.insert(QStringLiteral("commitLimitBytes"), 0);
    object.insert(QStringLiteral("usagePercent"), 0);
#ifndef Q_OS_WIN
    object.insert(QStringLiteral("name"), QString());
    object.insert(QStringLiteral("frequencyMHz"), 0);
    object.insert(QStringLiteral("modules"), QJsonArray());
#endif
    return object;
}

#ifdef Q_OS_WIN
QJsonObject SystemInfoService::memoryModuleSnapshot() const
{
    QJsonObject object;
    QJsonArray modules;
    QStringList names;
    QSet<QString> seenNames;
    int maxFrequencyMHz = 0;

    const DWORD provider = static_cast<DWORD>('RSMB');
    const UINT size = GetSystemFirmwareTable(provider, 0, nullptr, 0);
    if (size >= 8) {
        QVarLengthArray<BYTE> buffer(static_cast<int>(size));
        if (GetSystemFirmwareTable(provider, 0, buffer.data(), size) == size) {
            const BYTE* raw = buffer.constData();
            const DWORD tableLength = readLe32(raw, 4);
            const BYTE* cursor = raw + 8;
            const BYTE* end = cursor + qMin<DWORD>(tableLength, size - 8);

            while (cursor + 4 <= end) {
                const quint8 type = cursor[0];
                const quint8 length = cursor[1];
                if (length < 4 || cursor + length > end) {
                    break;
                }

                const BYTE* stringsStart = cursor + length;
                const BYTE* stringsEnd = stringsStart;
                while (stringsEnd + 1 < end && !(stringsEnd[0] == 0 && stringsEnd[1] == 0)) {
                    ++stringsEnd;
                }
                if (stringsEnd + 1 >= end) {
                    break;
                }

                if (type == 17) {
                    const quint64 sizeBytes = smbiosMemorySizeBytes(cursor, length);
                    if (sizeBytes > 0) {
                        const QString locator = length > 0x10 ? smbiosString(stringsStart, stringsEnd, cursor[0x10]) : QString();
                        const QString manufacturer = length > 0x17 ? smbiosString(stringsStart, stringsEnd, cursor[0x17]) : QString();
                        const QString partNumber = length > 0x1A ? smbiosString(stringsStart, stringsEnd, cursor[0x1A]) : QString();
                        const QString cleanManufacturer = sanitizeMemoryVendor(manufacturer);
                        const int speedMHz = length >= 0x17 ? static_cast<int>(readLe16(cursor, 0x15)) : 0;
                        const int configuredSpeedMHz = length >= 0x22 ? static_cast<int>(readLe16(cursor, 0x20)) : 0;
                        const int frequencyMHz = configuredSpeedMHz > 0 ? configuredSpeedMHz : speedMHz;

                        QString name = QStringList({cleanManufacturer.isEmpty() ? manufacturer : cleanManufacturer, partNumber}).join(QLatin1Char(' ')).trimmed();
                        if (name.isEmpty()) {
                            name = locator;
                        }
                        if (!name.isEmpty() && !seenNames.contains(name)) {
                            seenNames.insert(name);
                            names.append(name);
                        }
                        maxFrequencyMHz = qMax(maxFrequencyMHz, frequencyMHz);

                        QJsonObject module;
                        module.insert(QStringLiteral("name"), name);
                        module.insert(QStringLiteral("locator"), locator);
                        module.insert(QStringLiteral("manufacturer"), manufacturer);
                        module.insert(QStringLiteral("cleanManufacturer"), cleanManufacturer);
                        module.insert(QStringLiteral("partNumber"), partNumber);
                        module.insert(QStringLiteral("sizeBytes"), static_cast<double>(sizeBytes));
                        module.insert(QStringLiteral("speedMHz"), speedMHz);
                        module.insert(QStringLiteral("configuredSpeedMHz"), configuredSpeedMHz);
                        module.insert(QStringLiteral("frequencyMHz"), frequencyMHz);
                        modules.append(module);
                    }
                }

                cursor = stringsEnd + 2;
                if (type == 127) {
                    break;
                }
            }
        }
    }

    object.insert(QStringLiteral("name"), names.join(QStringLiteral(" + ")));
    object.insert(QStringLiteral("frequencyMHz"), maxFrequencyMHz);
    object.insert(QStringLiteral("modules"), modules);
    return object;
}
#endif

double SystemInfoService::gpuUsagePercent()
{
#ifdef Q_OS_WIN
    if (!m_gpuQuery) {
        PDH_HQUERY query = nullptr;
        if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) {
            m_gpuQuery = nullptr;
            return 0.0;
        }
        m_gpuQuery = query;

        constexpr wchar_t counterPath[] = L"\\GPU Engine(*)\\Utilization Percentage";
        PDH_HCOUNTER counter = nullptr;
        if (PdhAddEnglishCounterW(query, counterPath, 0, &counter) != ERROR_SUCCESS &&
            PdhAddCounterW(query, counterPath, 0, &counter) != ERROR_SUCCESS) {
            PdhCloseQuery(query);
            m_gpuQuery = nullptr;
            m_gpuUsageCounter = nullptr;
            return 0.0;
        }
        m_gpuUsageCounter = counter;
    }

    auto query = reinterpret_cast<PDH_HQUERY>(m_gpuQuery);
    auto counter = reinterpret_cast<PDH_HCOUNTER>(m_gpuUsageCounter);

    if (PdhCollectQueryData(query) != ERROR_SUCCESS) {
        return 0.0;
    }
    if (!m_hasGpuUsageSample) {
        m_hasGpuUsageSample = true;
        return 0.0;
    }

    DWORD bufferSize = 0;
    DWORD itemCount = 0;
    PDH_STATUS status = PdhGetFormattedCounterArrayW(counter,
                                                     PDH_FMT_DOUBLE,
                                                     &bufferSize,
                                                     &itemCount,
                                                     nullptr);
    if (status != PDH_MORE_DATA || bufferSize == 0 || itemCount == 0) {
        return 0.0;
    }

    QVarLengthArray<BYTE> buffer(static_cast<int>(bufferSize));
    auto* items = reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(buffer.data());
    status = PdhGetFormattedCounterArrayW(counter,
                                          PDH_FMT_DOUBLE,
                                          &bufferSize,
                                          &itemCount,
                                          items);
    if (status != ERROR_SUCCESS) {
        return 0.0;
    }

    double usage = 0.0;
    for (DWORD i = 0; i < itemCount; ++i) {
        if (items[i].FmtValue.CStatus == ERROR_SUCCESS) {
            usage += items[i].FmtValue.doubleValue;
        }
    }
    return qBound(0.0, usage, 100.0);
#else
    return 0.0;
#endif
}

#ifdef Q_OS_WIN
void SystemInfoService::queryNvmlGpu(QJsonObject& gpuObj)
{
    if (!m_nvmlAttempted) {
        m_nvmlAttempted = true;
        HMODULE hMod = LoadLibraryW(L"nvml.dll");
        if (hMod) {
            using NvmlInitFunc = int(*)();
            auto nvmlInit = reinterpret_cast<NvmlInitFunc>(GetProcAddress(hMod, "nvmlInit_v2"));
            if (nvmlInit && nvmlInit() == 0) {
                m_nvmlLoaded = true;
                m_nvmlModule = hMod;
            } else {
                FreeLibrary(hMod);
                m_nvmlModule = nullptr;
            }
        }
    }

    if (!m_nvmlLoaded || !m_nvmlModule) {
        return;
    }

    auto hMod = reinterpret_cast<HMODULE>(m_nvmlModule);
    using NvmlGetHandleFunc = int(*)(unsigned int, void**);
    using NvmlGetTempFunc = int(*)(void*, int, unsigned int*);
    using NvmlGetClockFunc = int(*)(void*, int, unsigned int*);
    using NvmlGetFanFunc = int(*)(void*, unsigned int*);
    using NvmlGetPowerFunc = int(*)(void*, unsigned int*);

    auto nvmlGetHandle = reinterpret_cast<NvmlGetHandleFunc>(GetProcAddress(hMod, "nvmlDeviceGetHandleByIndex_v2"));
    auto nvmlGetTemp = reinterpret_cast<NvmlGetTempFunc>(GetProcAddress(hMod, "nvmlDeviceGetTemperature"));
    auto nvmlGetClock = reinterpret_cast<NvmlGetClockFunc>(GetProcAddress(hMod, "nvmlDeviceGetClockInfo"));
    auto nvmlGetFan = reinterpret_cast<NvmlGetFanFunc>(GetProcAddress(hMod, "nvmlDeviceGetFanSpeed"));
    auto nvmlGetPower = reinterpret_cast<NvmlGetPowerFunc>(GetProcAddress(hMod, "nvmlDeviceGetPowerUsage"));
    auto nvmlGetPowerLimit = reinterpret_cast<NvmlGetPowerFunc>(GetProcAddress(hMod, "nvmlDeviceGetEnforcedPowerLimit"));

    if (!nvmlGetHandle) {
        return;
    }

    void* device = nullptr;
    if (nvmlGetHandle(0, &device) != 0 || !device) {
        return;
    }

    unsigned int temp = 0;
    if (nvmlGetTemp && nvmlGetTemp(device, 0, &temp) == 0) {
        gpuObj.insert(QStringLiteral("temperatureC"), static_cast<double>(temp));
    }

    unsigned int clockGfx = 0;
    if (nvmlGetClock && nvmlGetClock(device, 0, &clockGfx) == 0) {
        gpuObj.insert(QStringLiteral("clockGraphicsMHz"), static_cast<double>(clockGfx));
    }

    unsigned int clockMem = 0;
    if (nvmlGetClock && nvmlGetClock(device, 2, &clockMem) == 0) {
        gpuObj.insert(QStringLiteral("clockMemoryMHz"), static_cast<double>(clockMem));
    }

    unsigned int fanSpeed = 0;
    if (nvmlGetFan && nvmlGetFan(device, &fanSpeed) == 0) {
        gpuObj.insert(QStringLiteral("fanSpeedPercent"), static_cast<double>(fanSpeed));
    }

    unsigned int powerMw = 0;
    if (nvmlGetPower && nvmlGetPower(device, &powerMw) == 0) {
        gpuObj.insert(QStringLiteral("powerWatts"), static_cast<double>(powerMw) / 1000.0);
    }

    unsigned int powerLimitMw = 0;
    if (nvmlGetPowerLimit && nvmlGetPowerLimit(device, &powerLimitMw) == 0) {
        gpuObj.insert(QStringLiteral("powerLimitWatts"), static_cast<double>(powerLimitMw) / 1000.0);
    }
}
#endif

QJsonObject SystemInfoService::gpuSnapshot()
{
    QJsonObject object;
    QJsonArray adapters;
    QString primaryName;
    quint64 dedicatedVideoMemoryBytes = 0;
    quint64 dedicatedSystemMemoryBytes = 0;
    quint64 sharedSystemMemoryBytes = 0;
    const double usagePercent = gpuUsagePercent();

#ifdef Q_OS_WIN
    QSet<QString> seenAdapterKeys;
    IDXGIFactory1* factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) && factory) {
        UINT index = 0;
        IDXGIAdapter1* adapter = nullptr;
        while (factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND) {
            DXGI_ADAPTER_DESC1 desc = {};
            if (adapter && SUCCEEDED(adapter->GetDesc1(&desc))) {
                const QString adapterKey = dxgiAdapterIdentityKey(desc);
                if (seenAdapterKeys.contains(adapterKey)) {
                    adapter->Release();
                    adapter = nullptr;
                    ++index;
                    continue;
                }
                seenAdapterKeys.insert(adapterKey);

                const bool software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
                QJsonObject item;
                const QString name = QString::fromWCharArray(desc.Description).trimmed();
                item.insert(QStringLiteral("name"), name);
                item.insert(QStringLiteral("vendorId"), static_cast<double>(desc.VendorId));
                item.insert(QStringLiteral("deviceId"), static_cast<double>(desc.DeviceId));
                item.insert(QStringLiteral("subSysId"), static_cast<double>(desc.SubSysId));
                item.insert(QStringLiteral("revision"), static_cast<double>(desc.Revision));
                item.insert(QStringLiteral("luid"), QString::number(dxgiLuidKey(desc.AdapterLuid)));
                item.insert(QStringLiteral("dedicatedVideoMemoryBytes"), static_cast<double>(desc.DedicatedVideoMemory));
                item.insert(QStringLiteral("dedicatedSystemMemoryBytes"), static_cast<double>(desc.DedicatedSystemMemory));
                item.insert(QStringLiteral("sharedSystemMemoryBytes"), static_cast<double>(desc.SharedSystemMemory));
                item.insert(QStringLiteral("usagePercent"), software ? 0.0 : usagePercent);
                item.insert(QStringLiteral("software"), software);
                adapters.append(item);

                if (!software) {
                    dedicatedVideoMemoryBytes += static_cast<quint64>(desc.DedicatedVideoMemory);
                    dedicatedSystemMemoryBytes += static_cast<quint64>(desc.DedicatedSystemMemory);
                    sharedSystemMemoryBytes = qMax(sharedSystemMemoryBytes, static_cast<quint64>(desc.SharedSystemMemory));
                    if (primaryName.isEmpty()) {
                        primaryName = name;
                    }
                }
            }
            if (adapter) {
                adapter->Release();
                adapter = nullptr;
            }
            ++index;
        }
        factory->Release();
    }
#endif

    object.insert(QStringLiteral("name"), primaryName);
    object.insert(QStringLiteral("dedicatedVideoMemoryBytes"), static_cast<double>(dedicatedVideoMemoryBytes));
    object.insert(QStringLiteral("dedicatedSystemMemoryBytes"), static_cast<double>(dedicatedSystemMemoryBytes));
    object.insert(QStringLiteral("sharedSystemMemoryBytes"), static_cast<double>(sharedSystemMemoryBytes));
    object.insert(QStringLiteral("usagePercent"), usagePercent);
    object.insert(QStringLiteral("adapters"), adapters);
#ifdef Q_OS_WIN
    queryNvmlGpu(object);
#endif
    return object;
}

QJsonObject SystemInfoService::diskSnapshot() const
{
    QJsonObject object;
    QJsonArray drives;
    quint64 totalBytes = 0;
    quint64 freeBytes = 0;

#ifdef Q_OS_WIN
    wchar_t driveBuffer[512] = {};
    const DWORD maxLen = sizeof(driveBuffer) / sizeof(driveBuffer[0]);
    const DWORD length = GetLogicalDriveStringsW(maxLen, driveBuffer);
    if (length > 0 && length < maxLen) {
        const wchar_t* cursor = driveBuffer;
        const wchar_t* end = driveBuffer + length;
        while (cursor < end && *cursor) {
            const QString root = QString::fromWCharArray(cursor);
            cursor += wcslen(cursor) + 1;
            if (GetDriveTypeW(reinterpret_cast<LPCWSTR>(root.utf16())) != DRIVE_FIXED) {
                continue;
            }

            ULARGE_INTEGER freeToCaller = {};
            ULARGE_INTEGER total = {};
            ULARGE_INTEGER free = {};
            if (!GetDiskFreeSpaceExW(reinterpret_cast<LPCWSTR>(root.utf16()), &freeToCaller, &total, &free)) {
                continue;
            }

            totalBytes += total.QuadPart;
            freeBytes += free.QuadPart;
            QJsonObject drive;
            drive.insert(QStringLiteral("root"), root);
            drive.insert(QStringLiteral("totalBytes"), static_cast<double>(total.QuadPart));
            drive.insert(QStringLiteral("freeBytes"), static_cast<double>(free.QuadPart));
            drives.append(drive);
        }
    }
#endif

    object.insert(QStringLiteral("totalBytes"), static_cast<double>(totalBytes));
    object.insert(QStringLiteral("freeBytes"), static_cast<double>(freeBytes));
    object.insert(QStringLiteral("usedBytes"), static_cast<double>(totalBytes > freeBytes ? totalBytes - freeBytes : 0));
    object.insert(QStringLiteral("drives"), drives);

    QJsonObject mainDrive;
    for (const auto& dVal : drives) {
        const QJsonObject dObj = dVal.toObject();
        if (dObj.value(QStringLiteral("root")).toString().startsWith(QStringLiteral("C:"), Qt::CaseInsensitive)) {
            mainDrive = dObj;
            break;
        }
    }
    if (mainDrive.isEmpty() && !drives.isEmpty()) {
        mainDrive = drives.first().toObject();
    }
    object.insert(QStringLiteral("mainDrive"), mainDrive);
    return object;
}

QJsonObject SystemInfoService::networkSnapshot()
{
    QJsonObject object;
    quint64 inBytes = 0;
    quint64 outBytes = 0;
    double downloadBytesPerSecond = 0.0;
    double uploadBytesPerSecond = 0.0;

#ifdef Q_OS_WIN
    ULONG tableSize = 0;
    if (GetIfTable(nullptr, &tableSize, FALSE) == ERROR_INSUFFICIENT_BUFFER && tableSize > 0) {
        QVarLengthArray<BYTE> buffer(static_cast<int>(tableSize));
        auto* table = reinterpret_cast<PMIB_IFTABLE>(buffer.data());
        if (GetIfTable(table, &tableSize, FALSE) == NO_ERROR) {
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const MIB_IFROW& row = table->table[i];
                if (row.dwOperStatus != IF_OPER_STATUS_OPERATIONAL ||
                    row.dwType == IF_TYPE_SOFTWARE_LOOPBACK) {
                    continue;
                }
                inBytes += row.dwInOctets;
                outBytes += row.dwOutOctets;
            }
        }
    }

    if (inBytes == 0 && outBytes == 0) {
        ULONG fallbackSize = 0;
        if (GetIfTable(nullptr, &fallbackSize, TRUE) == ERROR_INSUFFICIENT_BUFFER && fallbackSize > 0) {
            QVarLengthArray<BYTE> buffer(static_cast<int>(fallbackSize));
            auto* table = reinterpret_cast<PMIB_IFTABLE>(buffer.data());
            if (GetIfTable(table, &fallbackSize, TRUE) == NO_ERROR) {
                for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                    const MIB_IFROW& row = table->table[i];
                    if (row.dwType == IF_TYPE_SOFTWARE_LOOPBACK) {
                        continue;
                    }
                    inBytes += row.dwInOctets;
                    outBytes += row.dwOutOctets;
                }
            }
        }
    }

    /*
        GetIfTable uses 32 bit counters. Sampling once per second is enough for
        widget level realtime speed, and wrap is handled below as zero delta.
    */
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_hasNetworkSample && now > m_lastNetworkMs) {
        const double seconds = static_cast<double>(now - m_lastNetworkMs) / 1000.0;
        downloadBytesPerSecond = static_cast<double>(inBytes >= m_lastNetworkIn ? inBytes - m_lastNetworkIn : 0) / seconds;
        uploadBytesPerSecond = static_cast<double>(outBytes >= m_lastNetworkOut ? outBytes - m_lastNetworkOut : 0) / seconds;
    }
    m_lastNetworkIn = inBytes;
    m_lastNetworkOut = outBytes;
    m_lastNetworkMs = now;
    m_hasNetworkSample = true;

    QString connectionType = QStringLiteral("以太网");
    QString adapterName = QStringLiteral("Ethernet");
    QString localIp = QStringLiteral("127.0.0.1");
    QString gatewayIp;
    double linkSpeedMbps = 0.0;

    ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS;
    ULONG outBufLen = 15000;
    QVarLengthArray<BYTE> addrBuffer(static_cast<int>(outBufLen));
    auto* pAddresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(addrBuffer.data());
    DWORD dwRetVal = GetAdaptersAddresses(AF_INET, flags, nullptr, pAddresses, &outBufLen);
    if (dwRetVal == ERROR_BUFFER_OVERFLOW) {
        addrBuffer.resize(static_cast<int>(outBufLen));
        pAddresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(addrBuffer.data());
        dwRetVal = GetAdaptersAddresses(AF_INET, flags, nullptr, pAddresses, &outBufLen);
    }

    if (dwRetVal == NO_ERROR && pAddresses) {
        for (auto* curr = pAddresses; curr != nullptr; curr = curr->Next) {
            if (curr->IfType == IF_TYPE_SOFTWARE_LOOPBACK || curr->OperStatus != IfOperStatusUp) {
                continue;
            }
            if (curr->FirstGatewayAddress != nullptr || localIp == QStringLiteral("127.0.0.1")) {
                adapterName = QString::fromWCharArray(curr->FriendlyName);
                if (curr->IfType == IF_TYPE_IEEE80211) {
                    connectionType = QStringLiteral("Wi-Fi 无线");
                } else if (curr->IfType == IF_TYPE_ETHERNET_CSMACD) {
                    connectionType = QStringLiteral("以太网有线");
                } else {
                    connectionType = QStringLiteral("局域网连接");
                }
                if (curr->TransmitLinkSpeed > 0) {
                    linkSpeedMbps = static_cast<double>(curr->TransmitLinkSpeed) / 1000000.0;
                }

                if (curr->FirstUnicastAddress) {
                    auto* sockAddr = reinterpret_cast<sockaddr_in*>(curr->FirstUnicastAddress->Address.lpSockaddr);
                    if (sockAddr) {
                        const auto& b = sockAddr->sin_addr.S_un.S_un_b;
                        localIp = QStringLiteral("%1.%2.%3.%4").arg(b.s_b1).arg(b.s_b2).arg(b.s_b3).arg(b.s_b4);
                    }
                }
                if (curr->FirstGatewayAddress) {
                    auto* sockAddr = reinterpret_cast<sockaddr_in*>(curr->FirstGatewayAddress->Address.lpSockaddr);
                    if (sockAddr) {
                        const auto& b = sockAddr->sin_addr.S_un.S_un_b;
                        gatewayIp = QStringLiteral("%1.%2.%3.%4").arg(b.s_b1).arg(b.s_b2).arg(b.s_b3).arg(b.s_b4);
                    }
                }
                if (curr->FirstGatewayAddress != nullptr) {
                    break;
                }
            }
        }
    }
#else
    QString connectionType = QStringLiteral("以太网");
    QString adapterName = QStringLiteral("Ethernet");
    QString localIp = QStringLiteral("127.0.0.1");
    QString gatewayIp;
    double linkSpeedMbps = 0.0;
#endif

    object.insert(QStringLiteral("downloadBytesPerSecond"), downloadBytesPerSecond);
    object.insert(QStringLiteral("uploadBytesPerSecond"), uploadBytesPerSecond);
    object.insert(QStringLiteral("totalReceivedBytes"), static_cast<double>(inBytes));
    object.insert(QStringLiteral("totalSentBytes"), static_cast<double>(outBytes));
    object.insert(QStringLiteral("connectionType"), connectionType);
    object.insert(QStringLiteral("adapterName"), adapterName);
    object.insert(QStringLiteral("localIp"), localIp);
    object.insert(QStringLiteral("gatewayIp"), gatewayIp);
    object.insert(QStringLiteral("linkSpeedMbps"), linkSpeedMbps);
    return object;
}

QJsonObject SystemInfoService::batterySnapshot() const
{
    QJsonObject object;
#ifdef Q_OS_WIN
    SYSTEM_POWER_STATUS status = {};
    if (GetSystemPowerStatus(&status)) {
        const bool hasBattery = status.BatteryFlag != 128;
        object.insert(QStringLiteral("available"), hasBattery);
        object.insert(QStringLiteral("charging"), hasBattery && status.ACLineStatus == 1);
        object.insert(QStringLiteral("acLineOnline"), status.ACLineStatus == 1);
        object.insert(QStringLiteral("percent"), hasBattery && status.BatteryLifePercent != 255 ? static_cast<int>(status.BatteryLifePercent) : -1);
        object.insert(QStringLiteral("lifeSeconds"), hasBattery && status.BatteryLifeTime != static_cast<DWORD>(-1) ? static_cast<int>(status.BatteryLifeTime) : -1);
        return object;
    }
#endif
    object.insert(QStringLiteral("available"), false);
    object.insert(QStringLiteral("charging"), false);
    object.insert(QStringLiteral("acLineOnline"), false);
    object.insert(QStringLiteral("percent"), -1);
    object.insert(QStringLiteral("lifeSeconds"), -1);
    return object;
}

QString SystemInfoService::windowsVersionText() const
{
#ifdef Q_OS_WIN
    using RtlGetVersionPtr = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    HMODULE module = GetModuleHandleW(L"ntdll.dll");
    auto rtlGetVersion = module ? reinterpret_cast<RtlGetVersionPtr>(GetProcAddress(module, "RtlGetVersion")) : nullptr;
    if (rtlGetVersion) {
        RTL_OSVERSIONINFOW version = {};
        version.dwOSVersionInfoSize = sizeof(version);
        if (rtlGetVersion(&version) == 0) {
            return QStringLiteral("Windows %1.%2.%3")
                .arg(version.dwMajorVersion)
                .arg(version.dwMinorVersion)
                .arg(version.dwBuildNumber);
        }
    }
#endif
    return QStringLiteral("Windows");
}

#ifdef Q_OS_WIN
quint64 SystemInfoService::fileTimeToUInt64(const void* fileTimePtr)
{
    const auto* fileTime = reinterpret_cast<const FILETIME*>(fileTimePtr);
    ULARGE_INTEGER value = {};
    value.LowPart = fileTime->dwLowDateTime;
    value.HighPart = fileTime->dwHighDateTime;
    return value.QuadPart;
}
#endif

}
