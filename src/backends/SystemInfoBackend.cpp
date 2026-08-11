#include "SystemInfoBackend.h"

#include "util/Format.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QFile>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QSysInfo>

#include <pwd.h>
#include <sys/utsname.h>
#include <unistd.h>

namespace {
QMap<QString, QString> readOsRelease(const QString &path)
{
    QFile file(path);
    QMap<QString, QString> values;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return values;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        const int equals = line.indexOf('=');
        if (equals < 1)
            continue;
        QString value = line.mid(equals + 1);
        if (value.size() >= 2 && value.startsWith('"') && value.endsWith('"'))
            value = value.mid(1, value.size() - 2);
        values.insert(line.left(equals), value);
    }
    return values;
}

QString cpuModel()
{
    QFile file("/proc/cpuinfo");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    for (const QString &line : QString::fromUtf8(file.readAll()).split('\n')) {
        if (line.startsWith("model name") || line.startsWith("Hardware")) {
            const int colon = line.indexOf(':');
            if (colon >= 0) return line.mid(colon + 1).trimmed();
        }
    }
    return {};
}

quint64 memoryTotal()
{
    QFile file("/proc/meminfo");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;
    for (const QString &line : QString::fromUtf8(file.readAll()).split('\n')) {
        if (line.startsWith("MemTotal:"))
            return line.simplified().split(' ').value(1).toULongLong() * 1024;
    }
    return 0;
}

quint64 uptimeSeconds()
{
    QFile file("/proc/uptime");
    return file.open(QIODevice::ReadOnly)
        ? QString::fromLatin1(file.readAll()).section(' ', 0, 0).toDouble() : 0;
}

QString networkState()
{
    QDBusInterface properties("org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager",
                              "org.freedesktop.DBus.Properties", QDBusConnection::systemBus());
    if (!properties.isValid())
        return "NetworkManager is not available";
    const QDBusReply<QDBusVariant> reply = properties.call("Get", "org.freedesktop.NetworkManager", "State");
    if (!reply.isValid())
        return "Network state unavailable";
    const uint state = reply.value().variant().toUInt();
    if (state == 70) return "Connected";
    if (state >= 50 && state < 70) return "Connecting";
    if (state == 20) return "Disconnected";
    if (state == 10) return "Sleeping";
    return "Limited or unknown";
}
}

SystemSummary SystemInfoBackend::summary(QString *error)
{
    SystemSummary result;
    const auto os = readOsRelease("/etc/os-release");
    const auto arch = readOsRelease("/usr/lib/os-release");
    struct utsname uts {};
    if (uname(&uts) != 0 && error)
        *error = "Kernel information is unavailable.";

    result.computerName = QSysInfo::machineHostName();
    result.aero7Version = os.value("PRETTY_NAME", os.value("NAME", "Aero7"));
    result.archVersion = os.value("BUILD_ID", arch.value("BUILD_ID"));
    if (result.archVersion.isEmpty())
        result.archVersion = arch.value("PRETTY_NAME", "Arch Linux base");
    result.kernelVersion = QString::fromLocal8Bit(uts.release);
    result.architecture = QSysInfo::currentCpuArchitecture();
    result.uptime = Format::uptime(uptimeSeconds());
    result.cpu = cpuModel();
    result.memory = Format::bytes(memoryTotal());
    const QStorageInfo root = QStorageInfo::root();
    result.systemDisk = root.isValid()
        ? QString("%1 — %2 free of %3").arg(QString::fromLocal8Bit(root.device()),
                                             Format::bytes(root.bytesAvailable()),
                                             Format::bytes(root.bytesTotal()))
        : QString("Unavailable");
    if (const passwd *entry = getpwuid(getuid()))
        result.currentUser = QString::fromLocal8Bit(entry->pw_name);
    result.session = QString("%1 / %2").arg(qEnvironmentVariable("XDG_CURRENT_DESKTOP", "Unknown desktop"),
                                             qEnvironmentVariable("XDG_SESSION_TYPE", "unknown session"));
    result.network = networkState();
    return result;
}
