#include "services/SystemInfo/SystemInfoService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QThread>
#include <gtest/gtest.h>
#include <iostream>

namespace {

QJsonObject requireObject(const QJsonObject& parent, const char* key)
{
    const QString name = QString::fromLatin1(key);
    EXPECT_TRUE(parent.contains(name));
    EXPECT_TRUE(parent.value(name).isObject());
    return parent.value(name).toObject();
}

QJsonArray requireArray(const QJsonObject& parent, const char* key)
{
    const QString name = QString::fromLatin1(key);
    EXPECT_TRUE(parent.contains(name));
    EXPECT_TRUE(parent.value(name).isArray());
    return parent.value(name).toArray();
}

double requireNumber(const QJsonObject& parent, const char* key)
{
    const QString name = QString::fromLatin1(key);
    EXPECT_TRUE(parent.contains(name));
    EXPECT_TRUE(parent.value(name).isDouble());
    return parent.value(name).toDouble();
}

bool requireBool(const QJsonObject& parent, const char* key)
{
    const QString name = QString::fromLatin1(key);
    EXPECT_TRUE(parent.contains(name));
    EXPECT_TRUE(parent.value(name).isBool());
    return parent.value(name).toBool();
}

QString requireString(const QJsonObject& parent, const char* key)
{
    const QString name = QString::fromLatin1(key);
    EXPECT_TRUE(parent.contains(name));
    EXPECT_TRUE(parent.value(name).isString());
    return parent.value(name).toString();
}

void expectPercentRange(double value)
{
    EXPECT_GE(value, 0.0);
    EXPECT_LE(value, 100.0);
}

}  // namespace

TEST(SystemInfoServiceTests, SnapshotContainsExpectedSections)
{
    SystemInfo::SystemInfoService service;
    const QJsonObject snapshot = service.snapshot();
    const QByteArray json = QJsonDocument(snapshot).toJson(QJsonDocument::Indented);
    std::cout << "\nSystemInfo first snapshot:\n" << json.constData() << std::endl;

    QThread::msleep(250);
    const QJsonObject warmedSnapshot = service.snapshot();
    const QByteArray warmedJson = QJsonDocument(warmedSnapshot).toJson(QJsonDocument::Indented);
    std::cout << "\nSystemInfo warmed snapshot:\n" << warmedJson.constData() << std::endl;

    requireObject(snapshot, "cpu");
    requireObject(snapshot, "memory");
    requireObject(snapshot, "gpu");
    requireObject(snapshot, "disk");
    requireObject(snapshot, "network");
    requireObject(snapshot, "battery");
    EXPECT_FALSE(requireString(snapshot, "osVersion").trimmed().isEmpty());
    EXPECT_GE(requireNumber(snapshot, "uptimeSeconds"), 0.0);
    EXPECT_GT(requireNumber(snapshot, "timestampMs"), 0.0);
}

TEST(SystemInfoServiceTests, CpuSnapshotHasUsableValues)
{
    SystemInfo::SystemInfoService service;
    service.snapshot();
    QThread::msleep(250);
    const QJsonObject cpu = requireObject(service.snapshot(), "cpu");

    EXPECT_FALSE(requireString(cpu, "name").trimmed().isEmpty());
    EXPECT_GE(requireNumber(cpu, "physicalCores"), 1.0);
    EXPECT_GE(requireNumber(cpu, "logicalCores"), 1.0);
    expectPercentRange(requireNumber(cpu, "usagePercent"));
}

TEST(SystemInfoServiceTests, MemorySnapshotHasConsistentTotals)
{
    SystemInfo::SystemInfoService service;
    const QJsonObject memory = requireObject(service.snapshot(), "memory");

    const double totalBytes = requireNumber(memory, "totalBytes");
    const double usedBytes = requireNumber(memory, "usedBytes");
    requireString(memory, "name");
    EXPECT_GE(requireNumber(memory, "frequencyMHz"), 0.0);
    const QJsonArray modules = requireArray(memory, "modules");
    EXPECT_GT(totalBytes, 0.0);
    EXPECT_GE(usedBytes, 0.0);
    EXPECT_LE(usedBytes, totalBytes);
    expectPercentRange(requireNumber(memory, "usagePercent"));

    for (const QJsonValue& value : modules) {
        ASSERT_TRUE(value.isObject());
        const QJsonObject module = value.toObject();
        requireString(module, "name");
        requireString(module, "locator");
        requireString(module, "manufacturer");
        requireString(module, "partNumber");
        EXPECT_GT(requireNumber(module, "sizeBytes"), 0.0);
        EXPECT_GE(requireNumber(module, "speedMHz"), 0.0);
        EXPECT_GE(requireNumber(module, "configuredSpeedMHz"), 0.0);
        EXPECT_GE(requireNumber(module, "frequencyMHz"), 0.0);
    }
}

TEST(SystemInfoServiceTests, GpuSnapshotHasAdapterData)
{
    SystemInfo::SystemInfoService service;
    const QJsonObject gpu = requireObject(service.snapshot(), "gpu");

    requireString(gpu, "name");
    expectPercentRange(requireNumber(gpu, "usagePercent"));
    EXPECT_GE(requireNumber(gpu, "dedicatedVideoMemoryBytes"), 0.0);
    EXPECT_GE(requireNumber(gpu, "dedicatedSystemMemoryBytes"), 0.0);
    EXPECT_GE(requireNumber(gpu, "sharedSystemMemoryBytes"), 0.0);
    const QJsonArray adapters = requireArray(gpu, "adapters");
    EXPECT_FALSE(adapters.isEmpty());

    for (const QJsonValue& value : adapters) {
        ASSERT_TRUE(value.isObject());
        const QJsonObject adapter = value.toObject();
        EXPECT_FALSE(requireString(adapter, "name").trimmed().isEmpty());
        EXPECT_GE(requireNumber(adapter, "vendorId"), 0.0);
        EXPECT_GE(requireNumber(adapter, "deviceId"), 0.0);
        EXPECT_GE(requireNumber(adapter, "subSysId"), 0.0);
        EXPECT_GE(requireNumber(adapter, "revision"), 0.0);
        EXPECT_FALSE(requireString(adapter, "luid").trimmed().isEmpty());
        EXPECT_GE(requireNumber(adapter, "dedicatedVideoMemoryBytes"), 0.0);
        EXPECT_GE(requireNumber(adapter, "dedicatedSystemMemoryBytes"), 0.0);
        EXPECT_GE(requireNumber(adapter, "sharedSystemMemoryBytes"), 0.0);
        expectPercentRange(requireNumber(adapter, "usagePercent"));
        requireBool(adapter, "software");
    }
}

TEST(SystemInfoServiceTests, DiskSnapshotHasFixedDriveData)
{
    SystemInfo::SystemInfoService service;
    const QJsonObject disk = requireObject(service.snapshot(), "disk");

    const double totalBytes = requireNumber(disk, "totalBytes");
    const double freeBytes = requireNumber(disk, "freeBytes");
    const double usedBytes = requireNumber(disk, "usedBytes");
    const QJsonArray drives = requireArray(disk, "drives");

    EXPECT_GT(totalBytes, 0.0);
    EXPECT_GE(freeBytes, 0.0);
    EXPECT_LE(freeBytes, totalBytes);
    EXPECT_GE(usedBytes, 0.0);
    EXPECT_LE(usedBytes, totalBytes);
    EXPECT_FALSE(drives.isEmpty());

    for (const QJsonValue& value : drives) {
        ASSERT_TRUE(value.isObject());
        const QJsonObject drive = value.toObject();
        EXPECT_FALSE(requireString(drive, "root").trimmed().isEmpty());
        const double driveTotal = requireNumber(drive, "totalBytes");
        const double driveFree = requireNumber(drive, "freeBytes");
        EXPECT_GT(driveTotal, 0.0);
        EXPECT_GE(driveFree, 0.0);
        EXPECT_LE(driveFree, driveTotal);
    }
}

TEST(SystemInfoServiceTests, NetworkAndBatterySnapshotsAreWellFormed)
{
    SystemInfo::SystemInfoService service;
    service.snapshot();
    const QJsonObject snapshot = service.snapshot();
    const QJsonObject network = requireObject(snapshot, "network");
    const QJsonObject battery = requireObject(snapshot, "battery");

    EXPECT_GE(requireNumber(network, "downloadBytesPerSecond"), 0.0);
    EXPECT_GE(requireNumber(network, "uploadBytesPerSecond"), 0.0);
    EXPECT_GE(requireNumber(network, "totalReceivedBytes"), 0.0);
    EXPECT_GE(requireNumber(network, "totalSentBytes"), 0.0);

    const bool available = requireBool(battery, "available");
    requireBool(battery, "charging");
    requireBool(battery, "acLineOnline");
    const double percent = requireNumber(battery, "percent");
    const double lifeSeconds = requireNumber(battery, "lifeSeconds");
    if (available) {
        EXPECT_GE(percent, 0.0);
        EXPECT_LE(percent, 100.0);
    } else {
        EXPECT_LE(percent, 0.0);
    }
    EXPECT_GE(lifeSeconds, -1.0);
}
