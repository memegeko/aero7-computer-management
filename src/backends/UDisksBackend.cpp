#include "UDisksBackend.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusMetaType>
#include <QDBusVariant>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QThread>

#include <algorithm>
#include <limits>
#include <functional>
#include <utility>

struct UDisksConfigurationItem {
    QString type;
    QVariantMap details;
};

Q_DECLARE_METATYPE(UDisksConfigurationItem)
using UDisksConfiguration = QList<UDisksConfigurationItem>;
Q_DECLARE_METATYPE(UDisksConfiguration)

QDBusArgument &operator<<(QDBusArgument &argument, const UDisksConfigurationItem &item)
{
    argument.beginStructure();
    argument << item.type << item.details;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, UDisksConfigurationItem &item)
{
    argument.beginStructure();
    argument >> item.type >> item.details;
    argument.endStructure();
    return argument;
}

namespace {
QVariantMap properties(const QString &path, const QString &interface)
{
    QDBusInterface props("org.freedesktop.UDisks2", path,
                         "org.freedesktop.DBus.Properties", QDBusConnection::systemBus());
    const QDBusReply<QVariantMap> reply = props.call("GetAll", interface);
    return reply.isValid() ? reply.value() : QVariantMap{};
}

QString decodedBytes(const QVariant &value)
{
    QVariant unwrapped = value;
    if (value.metaType().id() == qMetaTypeId<QDBusVariant>())
        unwrapped = value.value<QDBusVariant>().variant();
    QByteArray bytes = qdbus_cast<QByteArray>(unwrapped);
    if (bytes.isEmpty()) bytes = unwrapped.toByteArray();
    if (!bytes.isEmpty() && bytes.endsWith('\0')) bytes.chop(1);
    return QString::fromLocal8Bit(bytes);
}

UDisksConfiguration configurationItems(const QVariant &value)
{
    qDBusRegisterMetaType<UDisksConfigurationItem>();
    qDBusRegisterMetaType<UDisksConfiguration>();
    return qdbus_cast<UDisksConfiguration>(value);
}

bool isAeroStartupMount(const UDisksConfigurationItem &item)
{
    return item.type == "fstab"
        && decodedBytes(item.details.value("opts")).split(',').contains("x-aero7-managed");
}

quint64 shrinkCapacity(const BlockDevice &volume)
{
    constexpr quint64 MiB = 1024ULL * 1024ULL;
    if (!volume.freeSpaceKnown || !volume.size || !volume.fileSystemSize) return 0;
    const quint64 reserve = qMax<quint64>(256ULL * MiB, volume.fileSystemSize / 20);
    if (volume.freeBytes <= reserve) return 0;
    quint64 maximum = volume.freeBytes - reserve;
    const quint64 minimumVolume = 512ULL * MiB;
    if (volume.size <= minimumVolume) return 0;
    maximum = qMin(maximum, volume.size - minimumVolume);
    return maximum / MiB * MiB;
}

bool requiresPreShrinkRepair(const QString &fileSystem)
{
    // resize2fs refuses to shrink an ext filesystem unless a forced e2fsck
    // has completed since it was last mounted. UDisks2's Filesystem.Check is
    // deliberately read-only and does not satisfy that requirement, while
    // Filesystem.Repair performs the required offline e2fsck through the
    // normal UDisks2/Polkit authorization path.
    const QString type = fileSystem.toLower();
    return type == "ext2" || type == "ext3" || type == "ext4";
}

bool interfaceCall(const QString &path, const QString &interface, const QString &method,
                   QString *error)
{
    QDBusInterface object("org.freedesktop.UDisks2", path, interface,
                          QDBusConnection::systemBus());
    // Mount and unmount can wait for a Polkit agent. QDBusInterface otherwise
    // uses its short default timeout, which can expire while the user is still
    // reading or answering the administrator prompt.
    object.setTimeout(300000);
    if (!object.isValid()) {
        if (error) *error = QString("This item has no %1 interface.").arg(interface.section('.', -1));
        return false;
    }
    const QDBusMessage reply = object.call(method, QVariantMap{});
    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (error) *error = reply.errorMessage();
        return false;
    }
    return true;
}

QDBusMessage interfaceCall(const QString &path, const QString &interface, const QString &method,
                           const QVariantList &arguments, QString *error,
                           int timeout = 300000)
{
    QDBusInterface object("org.freedesktop.UDisks2", path, interface,
                          QDBusConnection::systemBus());
    object.setTimeout(timeout);
    if (!object.isValid()) {
        if (error) *error = QString("This item has no %1 interface.").arg(interface.section('.', -1));
        return {};
    }
    const QDBusMessage reply = object.callWithArgumentList(QDBus::Block, method, arguments);
    if (reply.type() == QDBusMessage::ErrorMessage && error)
        *error = reply.errorMessage();
    return reply;
}

QString humanError(const QString &message)
{
    if (message.contains("Not authorized", Qt::CaseInsensitive)
        || message.contains("permission", Qt::CaseInsensitive))
        return "Administrator authorization was not granted.";
    if (message.contains("busy", Qt::CaseInsensitive))
        return "The volume is busy. Close files and applications using it, then try again.";
    if (message.contains("read-only", Qt::CaseInsensitive))
        return "The selected disk is read-only.";
    if (message.contains("Did not receive a reply", Qt::CaseInsensitive)
        || message.contains("timed out", Qt::CaseInsensitive))
        return "The authorization request timed out before the disk operation completed.";
    return message.trimmed().isEmpty() ? "The disk operation failed." : message.trimmed();
}

const BlockDevice *findDevice(const QList<BlockDevice> &devices, const QString &objectPath)
{
    for (const BlockDevice &device : devices)
        if (device.objectPath == objectPath) return &device;
    return nullptr;
}

bool sameTarget(const BlockDevice &current, const DiskOperationTarget &expected, QString *error,
                bool allowSizeChange = false)
{
    auto fail = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    if (current.objectPath != expected.objectPath || current.deviceNumber != expected.deviceNumber)
        return fail("The selected disk changed or disappeared. Refresh Disk Management and try again.");
    if (!expected.stableId.isEmpty() && current.stableId != expected.stableId)
        return fail("The identity of the selected disk changed. The operation was cancelled.");
    if (current.partition != expected.partition)
        return fail("The selected disk layout changed. Refresh Disk Management and try again.");
    if (!allowSizeChange && current.size != expected.size)
        return fail("The selected disk size changed. Refresh Disk Management and try again.");
    if (expected.partition && current.partitionOffset != expected.offset)
        return fail("The selected partition moved. Refresh Disk Management and try again.");
    if (!expected.partUuid.isEmpty() && current.partUuid != expected.partUuid)
        return fail("The selected partition identity changed. The operation was cancelled.");
    if (!expected.uuid.isEmpty() && current.uuid != expected.uuid)
        return fail("The selected filesystem identity changed. The operation was cancelled.");
    return true;
}

bool currentTarget(const DiskOperationTarget &target, BlockDevice *current, QString *error,
                   bool allowSizeChange = false)
{
    QString inventoryError;
    const QList<BlockDevice> inventory = UDisksBackend::devices(&inventoryError);
    if (!inventoryError.isEmpty()) {
        if (error) *error = inventoryError;
        return false;
    }
    const BlockDevice *found = findDevice(inventory, target.objectPath);
    if (!found) {
        if (error) *error = "The selected disk disappeared. Refresh Disk Management and try again.";
        return false;
    }
    if (!sameTarget(*found, target, error, allowSizeChange)) return false;
    if (found->readOnly) {
        if (error) *error = "The selected disk is read-only.";
        return false;
    }
    if (found->critical) {
        if (error) *error = "Aero7 blocked this operation because the selected disk contains the running system or boot files.";
        return false;
    }
    if (current) *current = *found;
    return true;
}

bool waitForDevice(const QString &objectPath, BlockDevice *device, QString *error,
                   int timeout = 12000,
                   const std::function<bool(const BlockDevice &)> &predicate = {})
{
    QElapsedTimer timer;
    timer.start();
    QString lastError;
    while (timer.elapsed() < timeout) {
        const QList<BlockDevice> inventory = UDisksBackend::devices(&lastError);
        if (const BlockDevice *found = findDevice(inventory, objectPath)) {
            if (!predicate || predicate(*found)) {
                if (device) *device = *found;
                return true;
            }
        }
        QThread::msleep(200);
    }
    if (error) *error = lastError.isEmpty()
        ? "The kernel did not expose the changed partition in time." : lastError;
    return false;
}

bool waitForTable(const QString &objectPath, const QString &type, QString *error)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 12000) {
        QString inventoryError;
        const QList<BlockDevice> inventory = UDisksBackend::devices(&inventoryError);
        if (const BlockDevice *disk = findDevice(inventory, objectPath)) {
            if (disk->partitionTable == type) return true;
        }
        QThread::msleep(200);
    }
    if (error) *error = "The partition table was not detected after initialization.";
    return false;
}

bool waitForDisappearance(const QString &objectPath, QString *error)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 12000) {
        QString inventoryError;
        const QList<BlockDevice> inventory = UDisksBackend::devices(&inventoryError);
        if (!findDevice(inventory, objectPath)) return true;
        QThread::msleep(200);
    }
    if (error) *error = "The partition deletion returned, but the device is still present.";
    return false;
}

bool callBooleanString(const QString &method, const QString &type, bool *available,
                       QString *utility, quint64 *flags, QString *error)
{
    QDBusInterface manager("org.freedesktop.UDisks2", "/org/freedesktop/UDisks2/Manager",
                           "org.freedesktop.UDisks2.Manager", QDBusConnection::systemBus());
    const QDBusMessage reply = manager.call(method, type);
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) {
        if (error) *error = reply.errorMessage();
        return false;
    }
    const QDBusArgument argument = reply.arguments().first().value<QDBusArgument>();
    bool result = false;
    QString missing;
    quint64 modes = 0;
    argument.beginStructure();
    argument >> result;
    if (flags) argument >> modes;
    argument >> missing;
    argument.endStructure();
    if (available) *available = result;
    if (utility) *utility = missing;
    if (flags) *flags = modes;
    return true;
}

QVariantMap formatOptions(const QString &label, bool quick)
{
    QVariantMap options;
    if (!label.trimmed().isEmpty()) options.insert("label", label.trimmed());
    options.insert("update-partition-type", true);
    if (!quick) options.insert("erase", "zero");
    return options;
}

QByteArray nulTerminated(const QString &text)
{
    QByteArray result = text.toLocal8Bit();
    result.append('\0');
    return result;
}

UDisksConfigurationItem startupMountItem(const QString &folder, const QString &fileSystem)
{
    UDisksConfigurationItem item;
    item.type = "fstab";
    item.details.insert("dir", nulTerminated(QDir::cleanPath(folder)));
    item.details.insert("type", nulTerminated(fileSystem));
    item.details.insert("opts", nulTerminated("defaults,nofail,x-gvfs-show,x-aero7-managed"));
    item.details.insert("freq", 0);
    item.details.insert("passno", 0);
    item.details.insert("track-parents", true);
    return item;
}
}

QStringList UDisksBackend::decodeMountPoints(const QVariant &value)
{
    // UDisks2 exposes MountPoints as D-Bus signature aay. Depending on the
    // QtDBus path used to obtain the property this arrives either as the
    // native QList<QByteArray> or as a QDBusArgument. qdbus_cast handles both.
    const QList<QByteArray> mounts = qdbus_cast<QList<QByteArray>>(value);
    QStringList result;
    for (QByteArray path : mounts) {
        if (path.endsWith('\0')) path.chop(1);
        if (!path.isEmpty()) result << QString::fromLocal8Bit(path);
    }
    return result;
}

bool UDisksBackend::available()
{
    return QDBusConnection::systemBus().interface()
        && QDBusConnection::systemBus().interface()->isServiceRegistered("org.freedesktop.UDisks2");
}

QList<BlockDevice> UDisksBackend::devices(QString *error)
{
    if (!available()) {
        if (error) *error = "UDisks2 is not available on the system bus.";
        return {};
    }
    QDBusInterface manager("org.freedesktop.UDisks2", "/org/freedesktop/UDisks2/Manager",
                           "org.freedesktop.UDisks2.Manager", QDBusConnection::systemBus());
    const QDBusReply<QList<QDBusObjectPath>> reply = manager.call("GetBlockDevices", QVariantMap{});
    if (!reply.isValid()) {
        if (error) *error = reply.error().message();
        return {};
    }

    QList<BlockDevice> result;
    for (const QDBusObjectPath &path : reply.value()) {
        const QVariantMap block = properties(path.path(), "org.freedesktop.UDisks2.Block");
        const QVariantMap filesystem = properties(path.path(), "org.freedesktop.UDisks2.Filesystem");
        const QVariantMap partition = properties(path.path(), "org.freedesktop.UDisks2.Partition");
        const QVariantMap table = properties(path.path(), "org.freedesktop.UDisks2.PartitionTable");
        BlockDevice device;
        device.objectPath = path.path();
        device.device = decodedBytes(block.value("PreferredDevice"));
        device.label = block.value("IdLabel").toString();
        device.fileSystem = block.value("IdType").toString();
        device.uuid = block.value("IdUUID").toString();
        device.stableId = block.value("Id").toString();
        device.idUsage = block.value("IdUsage").toString();
        device.deviceNumber = block.value("DeviceNumber").toULongLong();
        device.readOnly = block.value("ReadOnly").toBool();
        device.systemDevice = block.value("HintSystem").toBool();
        device.partitionable = block.value("HintPartitionable").toBool();
        device.size = block.value("Size").toULongLong();
        device.mountable = !filesystem.isEmpty();
        device.fileSystemSize = filesystem.value("Size").toULongLong();
        const UDisksConfiguration configuration = configurationItems(block.value("Configuration"));
        for (const UDisksConfigurationItem &item : configuration) {
            if (item.type != "fstab") continue;
            const QString options = decodedBytes(item.details.value("opts"));
            if (options.split(',').contains("noauto")) continue;
            device.mountAtStartup = true;
            device.startupMountPoint = decodedBytes(item.details.value("dir"));
            if (isAeroStartupMount(item)) device.aeroManagedStartupMount = true;
        }
        device.mountPoints = decodeMountPoints(filesystem.value("MountPoints"));
        device.mountPoint = device.mountPoints.value(0);
        if (!device.mountPoint.isEmpty()) {
            const QStorageInfo storage(device.mountPoint);
            if (storage.isValid()) {
                device.freeBytes = storage.bytesAvailable();
                device.freeSpaceKnown = true;
            }
        }
        device.partition = !partition.isEmpty();
        device.partitionNumber = partition.value("Number").toUInt();
        device.partitionOffset = partition.value("Offset").toULongLong();
        device.partUuid = partition.value("UUID").toString();
        device.partitionType = partition.value("Type").toString();
        device.partitionTableObjectPath = partition.value("Table").value<QDBusObjectPath>().path();
        device.partitionTable = table.value("Type").toString();
        const QDBusObjectPath drive = block.value("Drive").value<QDBusObjectPath>();
        device.driveObjectPath = drive.path();
        if (!device.driveObjectPath.isEmpty() && device.driveObjectPath != "/") {
            const QVariantMap driveProperties = properties(device.driveObjectPath, "org.freedesktop.UDisks2.Drive");
            device.driveModel = driveProperties.value("Model").toString().trimmed();
            device.driveVendor = driveProperties.value("Vendor").toString().trimmed();
            device.connectionBus = driveProperties.value("ConnectionBus").toString().trimmed();
            device.serial = driveProperties.value("Serial").toString().trimmed();
            const QStringList mediaCompatibility =
                driveProperties.value("MediaCompatibility").toStringList();
            device.optical = driveProperties.value("Optical").toBool()
                || std::any_of(mediaCompatibility.cbegin(), mediaCompatibility.cend(),
                               [](const QString &media) {
                    return media.startsWith("optical_");
                });
            device.removable = driveProperties.value("Removable").toBool()
                || driveProperties.value("MediaRemovable").toBool();
        }
        result << device;
    }
    QHash<QString, QString> partitionTables;
    for (const BlockDevice &device : std::as_const(result)) {
        if (!device.partition && !device.partitionTable.isEmpty())
            partitionTables.insert(device.driveObjectPath, device.partitionTable);
    }
    for (BlockDevice &device : result) {
        if (device.partition && device.partitionTable.isEmpty()) {
            const BlockDevice *parent = findDevice(result, device.partitionTableObjectPath);
            if (parent) device.partitionTable = parent->partitionTable;
            if (device.partitionTable.isEmpty())
                device.partitionTable = partitionTables.value(device.driveObjectPath);
        }
    }
    QSet<QString> criticalDrives;
    QSet<QString> criticalTables;
    for (BlockDevice &device : result) {
        const bool criticalMount = device.mountPoints.contains("/")
            || device.mountPoints.contains("/boot") || device.mountPoints.contains("/boot/efi");
        if (!criticalMount) continue;
        device.critical = true;
        if (!device.driveObjectPath.isEmpty()) criticalDrives.insert(device.driveObjectPath);
        if (!device.partitionTableObjectPath.isEmpty())
            criticalTables.insert(device.partitionTableObjectPath);
    }
    for (BlockDevice &device : result) {
        if (criticalDrives.contains(device.driveObjectPath)
            || criticalTables.contains(device.objectPath)
            || criticalTables.contains(device.partitionTableObjectPath))
            device.critical = true;
    }
    result.erase(std::remove_if(result.begin(), result.end(), [](const BlockDevice &device) {
        return !UDisksBackend::isDiskManagementDevice(device);
    }), result.end());
    std::sort(result.begin(), result.end(), [](const BlockDevice &a, const BlockDevice &b) {
        if (a.driveObjectPath != b.driveObjectPath) return a.driveObjectPath < b.driveObjectPath;
        if (a.partition != b.partition) return !a.partition;
        return a.partitionOffset < b.partitionOffset;
    });
    return result;
}

bool UDisksBackend::isDiskManagementDevice(const BlockDevice &device)
{
    // Optical media belongs in File Explorer, not in the fixed/removable disk map.
    // UDisks exposes a virtual CD/DVD drive even when no ISO is attached, which
    // otherwise looks like a misleading zero-byte uninitialized hard disk.
    return !device.optical;
}

bool UDisksBackend::mount(const QString &objectPath, QString *error)
{
    return interfaceCall(objectPath, "org.freedesktop.UDisks2.Filesystem", "Mount", error);
}

bool UDisksBackend::unmount(const QString &objectPath, QString *error)
{
    return interfaceCall(objectPath, "org.freedesktop.UDisks2.Filesystem", "Unmount", error);
}

bool UDisksBackend::rescan(const QString &objectPath, QString *error)
{
    return interfaceCall(objectPath, "org.freedesktop.UDisks2.Block", "Rescan", error);
}

DiskOperationTarget UDisksBackend::targetFor(const BlockDevice &device)
{
    return {device.objectPath, device.stableId, device.device, device.uuid, device.partUuid,
            device.deviceNumber, device.size, device.partitionOffset, device.partition};
}

QList<DiskFreeRegion> UDisksBackend::freeRegions(const QList<BlockDevice> &devices,
                                                 quint64 minimumSize)
{
    QList<DiskFreeRegion> result;
    for (const BlockDevice &disk : devices) {
        if (disk.partition || !disk.partitionable || disk.optical || disk.size == 0) continue;
        QList<const BlockDevice *> partitions;
        for (const BlockDevice &candidate : devices) {
            if (!candidate.partition) continue;
            if (candidate.partitionTableObjectPath == disk.objectPath
                || (!disk.driveObjectPath.isEmpty()
                    && candidate.driveObjectPath == disk.driveObjectPath))
                partitions << &candidate;
        }
        std::sort(partitions.begin(), partitions.end(), [](const auto *left, const auto *right) {
            return left->partitionOffset < right->partitionOffset;
        });
        quint64 cursor = 0;
        for (const BlockDevice *partition : std::as_const(partitions)) {
            if (partition->partitionOffset > cursor
                && partition->partitionOffset - cursor >= minimumSize)
                result << DiskFreeRegion{disk.objectPath, disk.stableId, disk.deviceNumber,
                                         disk.size, cursor, partition->partitionOffset - cursor};
            cursor = qMax(cursor, partition->partitionOffset + partition->size);
        }
        if (disk.size > cursor && disk.size - cursor >= minimumSize)
            result << DiskFreeRegion{disk.objectPath, disk.stableId, disk.deviceNumber,
                                     disk.size, cursor, disk.size - cursor};
    }
    return result;
}

std::optional<DiskFreeRegion> UDisksBackend::adjacentFreeRegion(
    const QList<BlockDevice> &devices, const BlockDevice &partition, quint64 minimumSize)
{
    if (!partition.partition) return std::nullopt;
    for (const DiskFreeRegion &region : freeRegions(devices, minimumSize)) {
        const bool sameTable = region.diskObjectPath == partition.partitionTableObjectPath;
        const BlockDevice *disk = findDevice(devices, region.diskObjectPath);
        const bool sameDrive = disk && !partition.driveObjectPath.isEmpty()
            && disk->driveObjectPath == partition.driveObjectPath;
        if ((sameTable || sameDrive)
            && region.offset >= partition.partitionOffset + partition.size
            && region.offset - (partition.partitionOffset + partition.size) < 4ULL * 1024ULL * 1024ULL)
            return region;
    }
    return std::nullopt;
}

QList<FileSystemCapability> UDisksBackend::fileSystemCapabilities(QString *error)
{
    const QList<QPair<QString, QString>> types{{"ntfs", "NTFS"}, {"vfat", "FAT32"},
        {"exfat", "exFAT"}, {"ext4", "ext4"}, {"btrfs", "Btrfs"}, {"xfs", "XFS"}};
    QList<FileSystemCapability> result;
    for (const auto &[type, name] : types) {
        FileSystemCapability capability;
        capability.type = type;
        capability.displayName = name;
        QString localError;
        callBooleanString("CanFormat", type, &capability.canFormat,
                          &capability.missingUtility, nullptr, &localError);
        quint64 flags = 0;
        bool canResize = false;
        QString resizeUtility;
        if (callBooleanString("CanResize", type, &canResize, &resizeUtility, &flags, nullptr)
            && canResize) {
            capability.canShrinkUnmounted = flags & 2;
            capability.canGrowUnmounted = flags & 4;
            capability.canShrinkMounted = flags & 8;
            capability.canGrowMounted = flags & 16;
        }
        if (capability.missingUtility.isEmpty() && !resizeUtility.isEmpty())
            capability.missingUtility = resizeUtility;
        result << capability;
    }
    if (result.isEmpty() && error) *error = "UDisks2 did not report any supported filesystems.";
    return result;
}

quint64 UDisksBackend::maximumShrinkBytes(const DiskOperationTarget &target, QString *error)
{
    BlockDevice volume;
    if (!currentTarget(target, &volume, error) || !volume.partition
        || volume.fileSystem.isEmpty()) return 0;
    const auto capability = capabilityFor(volume.fileSystem, error);
    if (!capability || (!capability->canShrinkMounted && !capability->canShrinkUnmounted)) {
        if (error && error->isEmpty())
            *error = QString("The %1 filesystem cannot be safely shrunk.").arg(volume.fileSystem);
        return 0;
    }
    const bool temporarilyMount = volume.mountPoint.isEmpty();
    if (temporarilyMount) {
        QString mountError;
        if (!mount(volume.objectPath, &mountError)) {
            if (error) *error = humanError(mountError);
            return 0;
        }
        if (!waitForDevice(volume.objectPath, &volume, error, 12000,
                           [](const BlockDevice &current) {
                               return current.freeSpaceKnown && !current.mountPoint.isEmpty();
                           })) {
            unmount(volume.objectPath, nullptr);
            return 0;
        }
    }
    const quint64 maximum = shrinkCapacity(volume);
    if (temporarilyMount) unmount(volume.objectPath, nullptr);
    if (!maximum && error)
        *error = "The volume does not have enough verified free space to shrink safely.";
    return maximum;
}

std::optional<FileSystemCapability> UDisksBackend::capabilityFor(const QString &type,
                                                                  QString *error)
{
    for (const FileSystemCapability &capability : fileSystemCapabilities(error))
        if (capability.type.compare(type, Qt::CaseInsensitive) == 0) return capability;
    if (error) *error = QString("The %1 filesystem is not supported by UDisks2.").arg(type);
    return std::nullopt;
}

bool UDisksBackend::initializeDisk(const DiskOperationTarget &disk, const QString &tableType,
                                   QString *error)
{
    if (tableType != "gpt" && tableType != "dos") {
        if (error) *error = "Choose either GPT or MBR.";
        return false;
    }
    BlockDevice current;
    if (!currentTarget(disk, &current, error) || current.partition) return false;
    if (!current.partitionable || current.optical) {
        if (error) *error = "The selected device cannot contain a partition table.";
        return false;
    }
    if (!current.partitionTable.isEmpty()) {
        if (error) *error = "The selected disk is already initialized.";
        return false;
    }
    QString callError;
    const QDBusMessage reply = interfaceCall(current.objectPath,
        "org.freedesktop.UDisks2.Block", "Format", {tableType, QVariantMap{}}, &callError);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (error) *error = humanError(callError);
        return false;
    }
    return waitForTable(current.objectPath, tableType, error);
}

DiskOperationResult UDisksBackend::createVolume(const CreateVolumeRequest &request)
{
    DiskOperationResult result;
    BlockDevice disk;
    QString error;
    if (!currentTarget(request.disk, &disk, &error) || disk.partition) {
        result.message = humanError(error);
        return result;
    }
    if (disk.partitionTable != "gpt" && disk.partitionTable != "dos") {
        result.message = "Initialize the disk before creating a volume.";
        return result;
    }
    if (request.requestedSize < 8ULL * 1024ULL * 1024ULL
        || request.requestedSize > request.regionSize) {
        result.message = "The requested volume size is outside the available region.";
        return result;
    }
    const QList<BlockDevice> inventory = devices(&error);
    bool regionStillFree = false;
    for (const DiskFreeRegion &region : freeRegions(inventory)) {
        if (region.diskObjectPath == disk.objectPath
            && request.regionOffset >= region.offset
            && request.regionOffset + request.requestedSize <= region.offset + region.size) {
            regionStillFree = true;
            break;
        }
    }
    if (!regionStillFree) {
        result.message = "The unallocated region changed. Refresh Disk Management and try again.";
        return result;
    }
    QVariantMap partitionOptions;
    if (disk.partitionTable == "dos") partitionOptions.insert("partition-type", "primary");
    const QDBusMessage created = interfaceCall(disk.objectPath,
        "org.freedesktop.UDisks2.PartitionTable", "CreatePartition",
        {QVariant::fromValue<qulonglong>(request.regionOffset),
         QVariant::fromValue<qulonglong>(request.regionSize - request.requestedSize
             < 4ULL * 1024ULL * 1024ULL ? 0 : request.requestedSize),
         QString{}, request.label,
         partitionOptions}, &error);
    if (created.type() == QDBusMessage::ErrorMessage || created.arguments().isEmpty()) {
        result.message = humanError(error);
        return result;
    }
    const QString createdPath = created.arguments().first().value<QDBusObjectPath>().path();
    result.objectPath = createdPath;
    BlockDevice partition;
    if (!waitForDevice(createdPath, &partition, &error)) {
        result.partial = true;
        result.message = "The partition was created, but the kernel did not expose it in time. " + error;
        return result;
    }
    if (partition.partitionOffset + partition.size > request.regionOffset + request.regionSize
        + 4ULL * 1024ULL * 1024ULL) {
        result.partial = true;
        result.message = "The partition was created with an unexpected size. Formatting was stopped.";
        return result;
    }
    if (!request.format) {
        result.success = true;
        result.message = "The simple volume was created without a filesystem.";
        return result;
    }
    const auto capability = capabilityFor(request.fileSystem, &error);
    if (!capability || !capability->canFormat) {
        result.partial = true;
        result.message = QString("The partition exists, but %1 formatting is unavailable%2.")
            .arg(request.fileSystem,
                 capability && !capability->missingUtility.isEmpty()
                    ? QString(" because %1 is missing").arg(capability->missingUtility) : QString{});
        return result;
    }
    const QDBusMessage formatted = interfaceCall(createdPath, "org.freedesktop.UDisks2.Block",
        "Format", {request.fileSystem, formatOptions(request.label, request.quickFormat)}, &error);
    if (formatted.type() == QDBusMessage::ErrorMessage) {
        result.partial = true;
        result.message = "The partition exists, but formatting failed: " + humanError(error);
        return result;
    }
    if (!waitForDevice(createdPath, &partition, &error, 12000,
            [&request](const BlockDevice &current) {
                return current.fileSystem.compare(request.fileSystem, Qt::CaseInsensitive) == 0;
            })) {
        result.partial = true;
        result.message = "The partition exists, but the requested filesystem was not detected after formatting.";
        return result;
    }
    if (request.mount) {
        if (!mount(createdPath, &error)) {
            result.partial = true;
            result.message = "The volume was created and formatted, but it could not be mounted: "
                + humanError(error);
            return result;
        }
        waitForDevice(createdPath, &partition, nullptr);
        result.mountPoint = partition.mountPoint;
    }
    result.success = true;
    result.message = "The new simple volume is ready to use.";
    return result;
}

DiskOperationResult UDisksBackend::formatVolume(const FormatVolumeRequest &request)
{
    DiskOperationResult result;
    BlockDevice volume;
    QString error;
    if (!currentTarget(request.volume, &volume, &error) || !volume.partition) {
        result.message = humanError(error);
        return result;
    }
    const auto capability = capabilityFor(request.fileSystem, &error);
    if (!capability || !capability->canFormat) {
        result.message = capability && !capability->missingUtility.isEmpty()
            ? QString("Formatting requires %1.").arg(capability->missingUtility)
            : humanError(error);
        return result;
    }
    const bool wasMounted = !volume.mountPoint.isEmpty();
    if (wasMounted && !unmount(volume.objectPath, &error)) {
        result.message = humanError(error);
        return result;
    }
    const QDBusMessage formatted = interfaceCall(volume.objectPath, "org.freedesktop.UDisks2.Block",
        "Format", {request.fileSystem, formatOptions(request.label, request.quickFormat)}, &error);
    if (formatted.type() == QDBusMessage::ErrorMessage) {
        result.message = humanError(error);
        return result;
    }
    BlockDevice refreshed;
    if (!waitForDevice(volume.objectPath, &refreshed, &error, 12000,
            [&request, &volume](const BlockDevice &current) {
                const bool fileSystemMatches =
                    current.fileSystem.compare(request.fileSystem, Qt::CaseInsensitive) == 0;
                const bool labelMatches = request.label.trimmed().isEmpty()
                    ? current.label.isEmpty()
                    : current.label.compare(request.label.trimmed(), Qt::CaseInsensitive) == 0;
                // Reformatting a filesystem of the same type can otherwise look
                // successful even when UDisks never changed the on-disk data.
                const bool identityChanged = volume.uuid.isEmpty()
                    ? !current.uuid.isEmpty()
                    : !current.uuid.isEmpty() && current.uuid != volume.uuid;
                return fileSystemMatches && labelMatches && identityChanged;
            })) {
        result.message = "Formatting returned, but the new filesystem identity, type, and label "
                         "could not all be verified.";
        return result;
    }
    if (wasMounted && request.remount) {
        if (!mount(volume.objectPath, &error)) {
            result.partial = true;
            result.message = "Formatting completed, but the volume could not be remounted: "
                + humanError(error);
            return result;
        }
        waitForDevice(volume.objectPath, &refreshed, nullptr);
        result.mountPoint = refreshed.mountPoint;
    }
    result.success = true;
    result.objectPath = volume.objectPath;
    result.message = "The volume was formatted successfully.";
    return result;
}

DiskOperationResult UDisksBackend::extendVolume(const ExtendVolumeRequest &request)
{
    DiskOperationResult result;
    BlockDevice volume;
    QString error;
    if (!currentTarget(request.volume, &volume, &error) || !volume.partition
        || volume.fileSystem.isEmpty()) {
        result.message = humanError(error.isEmpty() ? "Select a formatted basic partition." : error);
        return result;
    }
    const auto capability = capabilityFor(volume.fileSystem, &error);
    if (!capability || (!capability->canGrowMounted && !capability->canGrowUnmounted)) {
        result.message = capability && !capability->missingUtility.isEmpty()
            ? QString("Extending %1 requires %2.").arg(volume.fileSystem, capability->missingUtility)
            : QString("The %1 filesystem cannot be safely extended.").arg(volume.fileSystem);
        return result;
    }
    QString inventoryError;
    const QList<BlockDevice> inventory = devices(&inventoryError);
    const auto adjacent = adjacentFreeRegion(inventory, volume);
    if (!adjacent || adjacent->offset != request.expectedAdjacentOffset
        || adjacent->size != request.expectedAdjacentSize
        || request.additionalBytes < 8ULL * 1024ULL * 1024ULL
        || request.additionalBytes > adjacent->size) {
        result.message = "The adjacent unallocated space changed. Refresh Disk Management and try again.";
        return result;
    }
    const bool wasMounted = !volume.mountPoint.isEmpty();
    const bool needsOffline = wasMounted && !capability->canGrowMounted;
    if (needsOffline && !unmount(volume.objectPath, &error)) {
        result.message = humanError(error);
        return result;
    }
    const quint64 requestedPartitionSize = volume.size + request.additionalBytes;
    const bool useMaximum = adjacent->size - request.additionalBytes < 4ULL * 1024ULL * 1024ULL;
    const QDBusMessage partitionResize = interfaceCall(volume.objectPath,
        "org.freedesktop.UDisks2.Partition", "Resize",
        {QVariant::fromValue<qulonglong>(useMaximum ? 0 : requestedPartitionSize), QVariantMap{}},
        &error);
    if (partitionResize.type() == QDBusMessage::ErrorMessage) {
        if (needsOffline) mount(volume.objectPath, nullptr);
        result.message = humanError(error);
        return result;
    }
    BlockDevice grown;
    if (!waitForDevice(volume.objectPath, &grown, &error, 12000,
            [requestedPartitionSize](const BlockDevice &current) {
                return current.size + 4ULL * 1024ULL * 1024ULL >= requestedPartitionSize;
            })) {
        if (needsOffline) mount(volume.objectPath, nullptr);
        result.partial = true;
        result.message = "The partition resize returned, but the expected larger partition was not detected.";
        return result;
    }
    const QDBusMessage fileSystemResize = interfaceCall(volume.objectPath,
        "org.freedesktop.UDisks2.Filesystem", "Resize",
        {QVariant::fromValue<qulonglong>(0), QVariantMap{}}, &error);
    if (fileSystemResize.type() == QDBusMessage::ErrorMessage) {
        if (needsOffline) mount(volume.objectPath, nullptr);
        result.partial = true;
        result.message = "The partition grew, but the filesystem did not: " + humanError(error);
        return result;
    }
    if (needsOffline && !mount(volume.objectPath, &error)) {
        result.partial = true;
        result.message = "The partition and filesystem grew, but the volume could not be remounted: "
            + humanError(error);
        return result;
    }
    if (!waitForDevice(volume.objectPath, &grown, &error, 12000,
            [&volume, &request, requestedPartitionSize](const BlockDevice &current) {
                return current.size + 4ULL * 1024ULL * 1024ULL >= requestedPartitionSize
                    && current.fileSystem == volume.fileSystem
                    && (!volume.fileSystemSize || !current.fileSystemSize
                        || current.fileSystemSize + 4ULL * 1024ULL * 1024ULL
                            >= volume.fileSystemSize + request.additionalBytes);
            })) {
        result.partial = true;
        result.message = "The resize completed, but the final disk state could not be verified.";
        return result;
    }
    result.success = true;
    result.objectPath = volume.objectPath;
    result.mountPoint = grown.mountPoint;
    result.message = "The volume was extended successfully.";
    return result;
}

DiskOperationResult UDisksBackend::shrinkVolume(const ShrinkVolumeRequest &request)
{
    constexpr quint64 MiB = 1024ULL * 1024ULL;
    DiskOperationResult result;
    BlockDevice volume;
    QString error;
    if (!currentTarget(request.volume, &volume, &error) || !volume.partition
        || volume.fileSystem.isEmpty()) {
        result.message = humanError(error.isEmpty() ? "Select a formatted basic partition." : error);
        return result;
    }
    const auto capability = capabilityFor(volume.fileSystem, &error);
    if (!capability || (!capability->canShrinkMounted && !capability->canShrinkUnmounted)) {
        result.message = capability && !capability->missingUtility.isEmpty()
            ? QString("Shrinking %1 requires %2.").arg(volume.fileSystem,
                                                       capability->missingUtility)
            : QString("The %1 filesystem cannot be safely shrunk.").arg(volume.fileSystem);
        return result;
    }
    const bool wasMounted = !volume.mountPoint.isEmpty();
    if (!wasMounted) {
        const bool mountedForMeasurement = mount(volume.objectPath, &error);
        if (!mountedForMeasurement
            || !waitForDevice(volume.objectPath, &volume, &error, 12000,
                              [](const BlockDevice &current) {
                                  return current.freeSpaceKnown && !current.mountPoint.isEmpty();
                              })) {
            if (mountedForMeasurement) unmount(volume.objectPath, nullptr);
            result.message = "Free space could not be measured safely: " + humanError(error);
            return result;
        }
    }
    const quint64 maximum = shrinkCapacity(volume);
    if (request.amountBytes < 8ULL * MiB || request.amountBytes > maximum
        || request.amountBytes >= volume.size) {
        if (!wasMounted) unmount(volume.objectPath, nullptr);
        result.message = "The requested shrink size exceeds the currently verified free space.";
        return result;
    }
    auto restoreOriginalMountState = [&] {
        BlockDevice current;
        if (!waitForDevice(volume.objectPath, &current, nullptr, 1000)) return;
        if (wasMounted && request.remount && current.mountPoint.isEmpty())
            mount(volume.objectPath, nullptr);
        else if (!wasMounted && !current.mountPoint.isEmpty())
            unmount(volume.objectPath, nullptr);
    };
    const bool needsOfflinePreparation = requiresPreShrinkRepair(volume.fileSystem);
    if ((!capability->canShrinkMounted || needsOfflinePreparation)
        && !unmount(volume.objectPath, &error)) {
        restoreOriginalMountState();
        result.message = humanError(error);
        return result;
    }
    if (needsOfflinePreparation) {
        const QDBusMessage repair = interfaceCall(volume.objectPath,
            "org.freedesktop.UDisks2.Filesystem", "Repair", {QVariantMap{}}, &error,
            1800000);
        const bool repaired = repair.type() != QDBusMessage::ErrorMessage
            && !repair.arguments().isEmpty() && repair.arguments().constFirst().toBool();
        if (!repaired) {
            restoreOriginalMountState();
            result.message = repair.type() == QDBusMessage::ErrorMessage
                ? "The required filesystem check could not be completed: " + humanError(error)
                : "The required filesystem check found errors that could not be repaired. "
                  "The volume was left unchanged.";
            return result;
        }
    }
    const quint64 requestedSize = volume.size - request.amountBytes;
    const QDBusMessage fileSystemResize = interfaceCall(volume.objectPath,
        "org.freedesktop.UDisks2.Filesystem", "Resize",
        {QVariant::fromValue<qulonglong>(requestedSize), QVariantMap{}}, &error);
    if (fileSystemResize.type() == QDBusMessage::ErrorMessage) {
        restoreOriginalMountState();
        result.message = humanError(error);
        return result;
    }
    BlockDevice shrunk;
    if (!waitForDevice(volume.objectPath, &shrunk, &error, 30000,
            [requestedSize](const BlockDevice &current) {
                return current.fileSystemSize > 0
                    && current.fileSystemSize <= requestedSize + 4ULL * 1024ULL * 1024ULL;
            })) {
        restoreOriginalMountState();
        result.partial = true;
        result.message = "The filesystem resize returned, but its smaller size could not be verified. "
                         "The partition boundary was left unchanged.";
        return result;
    }
    // Even when a filesystem supports shrinking while mounted, moving the
    // containing partition boundary is an offline operation.
    if (!shrunk.mountPoint.isEmpty() && !unmount(volume.objectPath, &error)) {
        restoreOriginalMountState();
        result.partial = true;
        result.message = "The filesystem was shrunk, but the volume could not be unmounted before "
                         "moving the partition boundary: " + humanError(error);
        return result;
    }
    const QDBusMessage partitionResize = interfaceCall(volume.objectPath,
        "org.freedesktop.UDisks2.Partition", "Resize",
        {QVariant::fromValue<qulonglong>(requestedSize), QVariantMap{}}, &error);
    if (partitionResize.type() == QDBusMessage::ErrorMessage) {
        restoreOriginalMountState();
        result.partial = true;
        result.message = "The filesystem was shrunk safely, but the partition boundary did not move: "
            + humanError(error);
        return result;
    }
    if (!waitForDevice(volume.objectPath, &shrunk, &error, 12000,
            [requestedSize](const BlockDevice &current) {
                const quint64 tolerance = 4ULL * 1024ULL * 1024ULL;
                return current.size + tolerance >= requestedSize
                    && current.size <= requestedSize + tolerance;
            })) {
        restoreOriginalMountState();
        result.partial = true;
        result.message = "The shrink completed, but the final partition size could not be verified.";
        return result;
    }
    if (wasMounted && request.remount && shrunk.mountPoint.isEmpty()
        && !mount(volume.objectPath, &error)) {
        result.partial = true;
        result.message = "The volume was shrunk, but it could not be remounted: " + humanError(error);
        return result;
    }
    result.success = true;
    result.objectPath = volume.objectPath;
    result.message = "The volume was shrunk successfully. The released space is now unallocated.";
    return result;
}

DiskOperationResult UDisksBackend::deleteVolume(const DiskOperationTarget &target)
{
    DiskOperationResult result;
    BlockDevice volume;
    QString error;
    if (!currentTarget(target, &volume, &error) || !volume.partition) {
        result.message = humanError(error.isEmpty() ? "Select a basic partition." : error);
        return result;
    }
    if (!volume.mountPoint.isEmpty() && !unmount(volume.objectPath, &error)) {
        result.message = humanError(error);
        return result;
    }
    const QDBusMessage deleted = interfaceCall(volume.objectPath,
        "org.freedesktop.UDisks2.Partition", "Delete",
        {QVariantMap{{"tear-down", true}}}, &error);
    if (deleted.type() == QDBusMessage::ErrorMessage) {
        result.message = humanError(error);
        return result;
    }
    if (!waitForDisappearance(volume.objectPath, &error)) {
        result.partial = true;
        result.message = error;
        return result;
    }
    result.success = true;
    result.message = "The volume was deleted. Its space is now unallocated.";
    return result;
}

QString UDisksBackend::recommendedStartupMountPoint(const BlockDevice &volume)
{
    QString name = volume.label.trimmed();
    if (name.isEmpty()) name = volume.uuid.left(8);
    if (name.isEmpty()) name = QString("volume-%1").arg(volume.partitionNumber);
    name = name.toLower();
    name.replace(QRegularExpression("[^a-z0-9._-]+"), "-");
    name.remove(QRegularExpression("^-+|-+$"));
    if (name.isEmpty()) name = "volume";
    return "/mnt/aero7-" + name.left(48);
}

DiskOperationResult UDisksBackend::setMountAtStartup(const DiskOperationTarget &target,
                                                      bool enabled,
                                                      const QString &mountPoint)
{
    DiskOperationResult result;
    BlockDevice volume;
    QString error;
    if (!currentTarget(target, &volume, &error) || !volume.mountable
        || volume.uuid.isEmpty()) {
        result.message = humanError(error.isEmpty()
            ? "A formatted volume with a stable UUID is required." : error);
        return result;
    }
    const QVariantMap block = properties(volume.objectPath, "org.freedesktop.UDisks2.Block");
    const UDisksConfiguration existing = configurationItems(block.value("Configuration"));
    if (enabled) {
        if (volume.mountAtStartup) {
            result.success = true;
            result.message = "This volume is already configured to mount at startup.";
            return result;
        }
        const QString folder = QDir::cleanPath(mountPoint.isEmpty()
            ? recommendedStartupMountPoint(volume) : mountPoint);
        if (!folder.startsWith("/mnt/aero7-") || folder == "/mnt/aero7-") {
            result.message = "Choose an Aero7 startup mount point below /mnt (for example /mnt/aero7-data).";
            return result;
        }
        if (QFileInfo(folder).path() != "/mnt") {
            result.message = "The startup mount point must be directly below /mnt.";
            return result;
        }
        const QFileInfo folderInfo(folder);
        if (folderInfo.exists()
            && (!folderInfo.isDir() || folderInfo.isSymLink()
                || !QDir(folder).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty())) {
            result.message = "The startup mount point already exists and is not an empty directory.";
            return result;
        }
        const QList<BlockDevice> inventory = devices(&error);
        if (!error.isEmpty()) {
            result.message = humanError(error);
            return result;
        }
        for (const BlockDevice &candidate : inventory) {
            if (candidate.objectPath == volume.objectPath) continue;
            if (candidate.startupMountPoint == folder || candidate.mountPoints.contains(folder)) {
                result.message = "Another volume already uses this mount point.";
                return result;
            }
        }
        qDBusRegisterMetaType<UDisksConfigurationItem>();
        const UDisksConfigurationItem item = startupMountItem(folder, volume.fileSystem);
        const QDBusMessage reply = interfaceCall(volume.objectPath,
            "org.freedesktop.UDisks2.Block", "AddConfigurationItem",
            {QVariant::fromValue(item), QVariantMap{}}, &error);
        if (reply.type() == QDBusMessage::ErrorMessage) {
            result.message = humanError(error);
            return result;
        }
        BlockDevice refreshed;
        if (!waitForDevice(volume.objectPath, &refreshed, &error, 12000,
                [](const BlockDevice &current) { return current.aeroManagedStartupMount; })) {
            result.partial = true;
            result.message = "The startup mount was written, but UDisks2 did not report it back in time.";
            return result;
        }
        result.success = true;
        result.objectPath = volume.objectPath;
        result.mountPoint = folder;
        result.message = QString("The volume will mount at %1 when Aero7 starts.").arg(folder);
        return result;
    }

    bool found = false;
    for (const UDisksConfigurationItem &item : existing) {
        if (!isAeroStartupMount(item)) continue;
        found = true;
        qDBusRegisterMetaType<UDisksConfigurationItem>();
        const QDBusMessage reply = interfaceCall(volume.objectPath,
            "org.freedesktop.UDisks2.Block", "RemoveConfigurationItem",
            {QVariant::fromValue(item), QVariantMap{}}, &error);
        if (reply.type() == QDBusMessage::ErrorMessage) {
            result.message = humanError(error);
            return result;
        }
    }
    if (!found) {
        result.message = "This startup mount is not managed by Aero7, so it was left unchanged.";
        return result;
    }
    BlockDevice refreshed;
    if (!waitForDevice(volume.objectPath, &refreshed, &error, 12000,
            [](const BlockDevice &current) { return !current.aeroManagedStartupMount; })) {
        result.partial = true;
        result.message = "The startup mount removal was not reported back in time.";
        return result;
    }
    result.success = true;
    result.objectPath = volume.objectPath;
    result.message = "The volume will no longer be mounted automatically at startup.";
    return result;
}
