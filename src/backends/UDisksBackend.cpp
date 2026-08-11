#include "UDisksBackend.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QHash>
#include <QStorageInfo>

#include <algorithm>
#include <utility>

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
        device.readOnly = block.value("ReadOnly").toBool();
        device.systemDevice = block.value("HintSystem").toBool();
        device.size = block.value("Size").toULongLong();
        device.mountable = !filesystem.isEmpty();
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
        device.partitionTable = table.value("Type").toString();
        const QDBusObjectPath drive = block.value("Drive").value<QDBusObjectPath>();
        device.driveObjectPath = drive.path();
        if (!device.driveObjectPath.isEmpty() && device.driveObjectPath != "/") {
            const QVariantMap driveProperties = properties(device.driveObjectPath, "org.freedesktop.UDisks2.Drive");
            device.driveModel = driveProperties.value("Model").toString().trimmed();
            device.driveVendor = driveProperties.value("Vendor").toString().trimmed();
            device.connectionBus = driveProperties.value("ConnectionBus").toString().trimmed();
            device.serial = driveProperties.value("Serial").toString().trimmed();
            device.optical = driveProperties.value("Optical").toBool();
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
        if (device.partition && device.partitionTable.isEmpty())
            device.partitionTable = partitionTables.value(device.driveObjectPath);
    }
    std::sort(result.begin(), result.end(), [](const BlockDevice &a, const BlockDevice &b) {
        if (a.driveObjectPath != b.driveObjectPath) return a.driveObjectPath < b.driveObjectPath;
        if (a.partition != b.partition) return !a.partition;
        return a.partitionOffset < b.partitionOffset;
    });
    return result;
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
