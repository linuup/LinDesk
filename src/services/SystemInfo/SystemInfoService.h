#pragma once

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

#include <QJsonObject>
#include <QString>

#ifdef Q_OS_WIN
#include <windows.h>
#include <pdh.h>
#endif

namespace SystemInfo {

class SystemInfoService {
public:
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
    static quint64 fileTimeToUInt64(const FILETIME& fileTime);
    bool m_hasCpuSample = false;
    quint64 m_lastCpuIdle = 0;
    quint64 m_lastCpuKernel = 0;
    quint64 m_lastCpuUser = 0;
    bool m_hasNetworkSample = false;
    quint64 m_lastNetworkIn = 0;
    quint64 m_lastNetworkOut = 0;
    qint64 m_lastNetworkMs = 0;
    PDH_HQUERY m_gpuQuery = nullptr;
    PDH_HCOUNTER m_gpuUsageCounter = nullptr;
    bool m_hasGpuUsageSample = false;
    bool m_gpuQueryFailed = false;
#endif
};

}
