#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <optional>

struct BlockDevice {
    QString objectPath;
    QString driveObjectPath;
    QString device;
    QString driveModel;
    QString driveVendor;
    QString connectionBus;
    QString serial;
    QString label;
    QString fileSystem;
    QString uuid;
    QString partUuid;
    QString stableId;
    QString idUsage;
    QString partitionTable;
    QString partitionTableObjectPath;
    QString partitionType;
    QString mountPoint;
    QString startupMountPoint;
    QStringList mountPoints;
    quint64 size = 0;
    quint64 freeBytes = 0;
    quint64 partitionOffset = 0;
    quint64 deviceNumber = 0;
    quint64 fileSystemSize = 0;
    uint partitionNumber = 0;
    bool mountable = false;
    bool freeSpaceKnown = false;
    bool removable = false;
    bool readOnly = false;
    bool partition = false;
    bool systemDevice = false;
    bool optical = false;
    bool partitionable = false;
    bool critical = false;
    bool mountAtStartup = false;
    bool aeroManagedStartupMount = false;
};

struct DiskFreeRegion {
    QString diskObjectPath;
    QString diskStableId;
    quint64 diskDeviceNumber = 0;
    quint64 diskSize = 0;
    quint64 offset = 0;
    quint64 size = 0;
};

struct DiskOperationTarget {
    QString objectPath;
    QString stableId;
    QString device;
    QString uuid;
    QString partUuid;
    quint64 deviceNumber = 0;
    quint64 size = 0;
    quint64 offset = 0;
    bool partition = false;
};

struct FileSystemCapability {
    QString type;
    QString displayName;
    bool canFormat = false;
    bool canShrinkMounted = false;
    bool canShrinkUnmounted = false;
    bool canGrowMounted = false;
    bool canGrowUnmounted = false;
    QString missingUtility;
};

struct CreateVolumeRequest {
    DiskOperationTarget disk;
    quint64 regionOffset = 0;
    quint64 regionSize = 0;
    quint64 requestedSize = 0;
    QString fileSystem;
    QString label;
    bool format = true;
    bool quickFormat = true;
    bool mount = true;
};

struct FormatVolumeRequest {
    DiskOperationTarget volume;
    QString fileSystem;
    QString label;
    bool quickFormat = true;
    bool remount = true;
};

struct ExtendVolumeRequest {
    DiskOperationTarget volume;
    quint64 additionalBytes = 0;
    quint64 expectedAdjacentOffset = 0;
    quint64 expectedAdjacentSize = 0;
};

struct ShrinkVolumeRequest {
    DiskOperationTarget volume;
    quint64 amountBytes = 0;
    bool remount = true;
};

struct DiskOperationResult {
    bool success = false;
    bool partial = false;
    QString objectPath;
    QString mountPoint;
    QString message;
};

class UDisksBackend {
public:
    static QStringList decodeMountPoints(const QVariant &value);
    static bool available();
    static QList<BlockDevice> devices(QString *error = nullptr);
    static bool isDiskManagementDevice(const BlockDevice &device);
    static bool mount(const QString &objectPath, QString *error = nullptr);
    static bool unmount(const QString &objectPath, QString *error = nullptr);
    static bool rescan(const QString &objectPath, QString *error = nullptr);
    static DiskOperationTarget targetFor(const BlockDevice &device);
    static QList<DiskFreeRegion> freeRegions(const QList<BlockDevice> &devices,
                                             quint64 minimumSize = 8ULL * 1024ULL * 1024ULL);
    static std::optional<DiskFreeRegion> adjacentFreeRegion(
        const QList<BlockDevice> &devices, const BlockDevice &partition,
        quint64 minimumSize = 8ULL * 1024ULL * 1024ULL);
    static QList<FileSystemCapability> fileSystemCapabilities(QString *error = nullptr);
    static std::optional<FileSystemCapability> capabilityFor(const QString &type,
                                                              QString *error = nullptr);
    static quint64 maximumShrinkBytes(const DiskOperationTarget &volume,
                                      QString *error = nullptr);
    static bool initializeDisk(const DiskOperationTarget &disk, const QString &tableType,
                               QString *error = nullptr);
    static DiskOperationResult createVolume(const CreateVolumeRequest &request);
    static DiskOperationResult formatVolume(const FormatVolumeRequest &request);
    static DiskOperationResult extendVolume(const ExtendVolumeRequest &request);
    static DiskOperationResult shrinkVolume(const ShrinkVolumeRequest &request);
    static DiskOperationResult deleteVolume(const DiskOperationTarget &volume);
    static QString recommendedStartupMountPoint(const BlockDevice &volume);
    static DiskOperationResult setMountAtStartup(const DiskOperationTarget &volume,
                                                 bool enabled,
                                                 const QString &mountPoint = {});
};
