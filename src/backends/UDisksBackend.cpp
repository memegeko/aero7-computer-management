#include "UDisksBackend.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusMetaType>
#include <QDir>
#include <QElapsedTimer>
#include <QHash>
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
    QByteArray bytes = value.toByteArray();
    if (!bytes.isEmpty() && bytes.endsWith('\0')) bytes.chop(1);
    return QString::fromLocal8Bit(bytes);
}

bool interfaceCall(const QString &path, const QString &interface, const QString &method,
                   QString *error)
{
    QDBusInterface object("org.freedesktop.UDisks2", path, interface,
                          QDBusConnection::systemBus());
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

bool configureMountFolder(const QString &objectPath, const QString &folder,
                          const QString &fileSystem, QString *error)
{
    if (folder.isEmpty()) return true;
    const QFileInfo info(folder);
    const QDir directory(folder);
    if (!info.isAbsolute() || !info.exists() || !info.isDir() || info.isSymLink()) {
        if (error) *error = "The mount folder must be an existing absolute directory and may not be a symbolic link.";
        return false;
    }
    if (folder == "/" || folder == "/boot" || folder == "/boot/efi"
        || !directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty()) {
        if (error) *error = "The selected mount folder must be empty and cannot be a system directory.";
        return false;
    }
    qDBusRegisterMetaType<UDisksConfigurationItem>();
    UDisksConfigurationItem item;
    item.type = "fstab";
    item.details.insert("dir", nulTerminated(QDir::cleanPath(folder)));
    item.details.insert("type", nulTerminated(fileSystem));
    item.details.insert("opts", nulTerminated("defaults,nofail,x-gvfs-show"));
    item.details.insert("freq", 0);
    item.details.insert("passno", 0);
    item.details.insert("track-parents", true);
    const QDBusMessage reply = interfaceCall(objectPath, "org.freedesktop.UDisks2.Block",
        "AddConfigurationItem", {QVariant::fromValue(item), QVariantMap{}}, error);
    return reply.type() != QDBusMessage::ErrorMessage;
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
            capability.canGrowUnmounted = flags & 4;
            capability.canGrowMounted = flags & 16;
        }
        if (capability.missingUtility.isEmpty() && !resizeUtility.isEmpty())
            capability.missingUtility = resizeUtility;
        result << capability;
    }
    if (result.isEmpty() && error) *error = "UDisks2 did not report any supported filesystems.";
    return result;
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
    if (!request.mountFolder.isEmpty()
        && !configureMountFolder(createdPath, request.mountFolder, request.fileSystem, &error)) {
        result.partial = true;
        result.message = "The volume was created and formatted, but its mount folder could not be configured: "
            + humanError(error);
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
            [&request](const BlockDevice &current) {
                return current.fileSystem.compare(request.fileSystem, Qt::CaseInsensitive) == 0;
            })) {
        result.message = "Formatting returned, but the requested filesystem was not detected.";
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
