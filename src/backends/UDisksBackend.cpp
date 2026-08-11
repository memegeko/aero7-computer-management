#include "UDisksBackend.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusVariant>

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
    QByteArray bytes;
    if (value.canConvert<QByteArray>()) bytes = value.toByteArray();
    if (!bytes.isEmpty() && bytes.endsWith('\0')) bytes.chop(1);
    return QString::fromLocal8Bit(bytes);
}

bool filesystemCall(const QString &path, const QString &method, QString *error)
{
    QDBusInterface fs("org.freedesktop.UDisks2", path,
                      "org.freedesktop.UDisks2.Filesystem", QDBusConnection::systemBus());
    if (!fs.isValid()) { if (error) *error = "This item has no UDisks2 filesystem interface."; return false; }
    const QDBusMessage reply = fs.call(method, QVariantMap{});
    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (error) *error = reply.errorMessage();
        return false;
    }
    return true;
}
}

bool UDisksBackend::available()
{
    return QDBusConnection::systemBus().interface()
        && QDBusConnection::systemBus().interface()->isServiceRegistered("org.freedesktop.UDisks2");
}

QList<BlockDevice> UDisksBackend::devices(QString *error)
{
    if (!available()) { if (error) *error = "UDisks2 is not available on the system bus."; return {}; }
    QDBusInterface manager("org.freedesktop.UDisks2", "/org/freedesktop/UDisks2/Manager",
                           "org.freedesktop.UDisks2.Manager", QDBusConnection::systemBus());
    const QDBusReply<QList<QDBusObjectPath>> reply = manager.call("GetBlockDevices", QVariantMap{});
    if (!reply.isValid()) { if (error) *error = reply.error().message(); return {}; }
    QList<BlockDevice> out;
    for (const QDBusObjectPath &path : reply.value()) {
        const QVariantMap block = properties(path.path(), "org.freedesktop.UDisks2.Block");
        const QVariantMap fs = properties(path.path(), "org.freedesktop.UDisks2.Filesystem");
        BlockDevice dev;
        dev.objectPath = path.path();
        dev.device = decodedBytes(block.value("PreferredDevice"));
        dev.label = block.value("IdLabel").toString();
        dev.fileSystem = block.value("IdType").toString();
        dev.size = block.value("Size").toULongLong();
        dev.mountable = !fs.isEmpty();
        const auto mounts = fs.value("MountPoints").value<QList<QByteArray>>();
        if (!mounts.isEmpty()) dev.mountPoint = QString::fromLocal8Bit(mounts.first().chopped(1));
        out << dev;
    }
    return out;
}

bool UDisksBackend::mount(const QString &objectPath, QString *error) { return filesystemCall(objectPath, "Mount", error); }
bool UDisksBackend::unmount(const QString &objectPath, QString *error) { return filesystemCall(objectPath, "Unmount", error); }
