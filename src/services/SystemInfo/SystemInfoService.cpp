#ifdef Q_OS_WIN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WINVER
#define WINVER 0x0600
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x06000000
#endif
#endif

#include "services/SystemInfo/SystemInfoService.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QVarLengthArray>
#include <cwchar>
#include <cstring>

#ifdef Q_OS_WIN
#include <dxgi.h>
#include <iphlpapi.h>
#include <pdhmsg.h>
#include <winternl.h>
#endif

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

}  // namespace

namespace SystemInfo {

SystemInfoService::~SystemInfoService()
{
#ifdef Q_OS_WIN
    if (m_gpuQuery) {
        PdhCloseQuery(m_gpuQuery);
        m_gpuQuery = nullptr;
        m_gpuUsageCounter = nullptr;
    }
#endif
}

QJsonObject SystemInfoService::snapshot()
{
    QJsonObject object;
    object.insert(QStringLiteral("cpu"), cpuSnapshot());
    object.insert(QStringLiteral("memory"), memorySnapshot());
    object.insert(QStringLiteral("gpu"), gpuSnapshot());
    object.insert(QStringLiteral("disk"), diskSnapshot());
    object.insert(QStringLiteral("network"), networkSnapshot());
    object.insert(QStringLiteral("battery"), batterySnapshot());
    object.insert(QStringLiteral("osVersion"), windowsVersionText());
#ifdef Q_OS_WIN
    object.insert(QStringLiteral("uptimeSeconds"), static_cast<double>(GetTickCount64() / 1000ULL));
#else
    object.insert(QStringLiteral("uptimeSeconds"), 0);
#endif
    object.insert(QStringLiteral("timestampMs"), static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
    return object;
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

QJsonObject SystemInfoService::cpuSnapshot()
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), cpuName());

#ifdef Q_OS_WIN
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
    const DWORD logicalCores = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);

    FILETIME idleTime = {};
    FILETIME kernelTime = {};
    FILETIME userTime = {};
    double usage = 0.0;
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        const quint64 idle = fileTimeToUInt64(idleTime);
        const quint64 kernel = fileTimeToUInt64(kernelTime);
        const quint64 user = fileTimeToUInt64(userTime);
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

    object.insert(QStringLiteral("physicalCores"), physicalCores);
    object.insert(QStringLiteral("logicalCores"), static_cast<int>(logicalCores));
    object.insert(QStringLiteral("usagePercent"), usage);
#else
    object.insert(QStringLiteral("physicalCores"), 0);
    object.insert(QStringLiteral("logicalCores"), 0);
    object.insert(QStringLiteral("usagePercent"), 0);
#endif
    return object;
}

QJsonObject SystemInfoService::memorySnapshot() const
{
    QJsonObject object;
#ifdef Q_OS_WIN
    const QJsonObject modules = memoryModuleSnapshot();
    for (auto it = modules.constBegin(); it != modules.constEnd(); ++it) {
        object.insert(it.key(), it.value());
    }

    MEMORYSTATUSEX status = {};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        const quint64 total = status.ullTotalPhys;
        const quint64 free = status.ullAvailPhys;
        const quint64 used = total > free ? total - free : 0;
        object.insert(QStringLiteral("totalBytes"), static_cast<double>(total));
        object.insert(QStringLiteral("usedBytes"), static_cast<double>(used));
        object.insert(QStringLiteral("usagePercent"), total > 0 ? static_cast<double>(used) * 100.0 / static_cast<double>(total) : 0.0);
        return object;
    }
#endif
    object.insert(QStringLiteral("totalBytes"), 0);
    object.insert(QStringLiteral("usedBytes"), 0);
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
                        const int speedMHz = length >= 0x17 ? static_cast<int>(readLe16(cursor, 0x15)) : 0;
                        const int configuredSpeedMHz = length >= 0x22 ? static_cast<int>(readLe16(cursor, 0x20)) : 0;
                        const int frequencyMHz = configuredSpeedMHz > 0 ? configuredSpeedMHz : speedMHz;

                        QString name = QStringList({manufacturer, partNumber}).join(QLatin1Char(' ')).trimmed();
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
        if (PdhOpenQueryW(nullptr, 0, &m_gpuQuery) != ERROR_SUCCESS) {
            m_gpuQuery = nullptr;
            return 0.0;
        }

        constexpr wchar_t counterPath[] = L"\\GPU Engine(*)\\Utilization Percentage";
        if (PdhAddEnglishCounterW(m_gpuQuery, counterPath, 0, &m_gpuUsageCounter) != ERROR_SUCCESS &&
            PdhAddCounterW(m_gpuQuery, counterPath, 0, &m_gpuUsageCounter) != ERROR_SUCCESS) {
            PdhCloseQuery(m_gpuQuery);
            m_gpuQuery = nullptr;
            m_gpuUsageCounter = nullptr;
            return 0.0;
        }
    }

    if (PdhCollectQueryData(m_gpuQuery) != ERROR_SUCCESS) {
        return 0.0;
    }
    if (!m_hasGpuUsageSample) {
        m_hasGpuUsageSample = true;
        return 0.0;
    }

    DWORD bufferSize = 0;
    DWORD itemCount = 0;
    PDH_STATUS status = PdhGetFormattedCounterArrayW(m_gpuUsageCounter,
                                                     PDH_FMT_DOUBLE,
                                                     &bufferSize,
                                                     &itemCount,
                                                     nullptr);
    if (status != PDH_MORE_DATA || bufferSize == 0 || itemCount == 0) {
        return 0.0;
    }

    QVarLengthArray<BYTE> buffer(static_cast<int>(bufferSize));
    auto* items = reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(buffer.data());
    status = PdhGetFormattedCounterArrayW(m_gpuUsageCounter,
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
    const DWORD length = GetLogicalDriveStringsW(static_cast<DWORD>(sizeof(driveBuffer) / sizeof(driveBuffer[0])), driveBuffer);
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
#endif

    object.insert(QStringLiteral("totalBytes"), static_cast<double>(totalBytes));
    object.insert(QStringLiteral("freeBytes"), static_cast<double>(freeBytes));
    object.insert(QStringLiteral("usedBytes"), static_cast<double>(totalBytes > freeBytes ? totalBytes - freeBytes : 0));
    object.insert(QStringLiteral("drives"), drives);
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
#endif

    object.insert(QStringLiteral("downloadBytesPerSecond"), downloadBytesPerSecond);
    object.insert(QStringLiteral("uploadBytesPerSecond"), uploadBytesPerSecond);
    object.insert(QStringLiteral("totalReceivedBytes"), static_cast<double>(inBytes));
    object.insert(QStringLiteral("totalSentBytes"), static_cast<double>(outBytes));
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
quint64 SystemInfoService::fileTimeToUInt64(const FILETIME& fileTime)
{
    ULARGE_INTEGER value = {};
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    return value.QuadPart;
}
#endif

}
