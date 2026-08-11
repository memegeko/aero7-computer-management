#include "PerformanceBackend.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>

namespace {
QStringList fields(const QString &line)
{
    return line.simplified().split(' ', Qt::SkipEmptyParts);
}
}

PerformanceSnapshot PerformanceBackend::snapshot(QString *error)
{
    PerformanceSnapshot result;
    QFile load("/proc/loadavg");
    QFile mem("/proc/meminfo");
    QFile up("/proc/uptime");
    QFile stat("/proc/stat");
    QFile disks("/proc/diskstats");
    QFile network("/proc/net/dev");
    if (!load.open(QIODevice::ReadOnly) || !mem.open(QIODevice::ReadOnly)
        || !up.open(QIODevice::ReadOnly) || !stat.open(QIODevice::ReadOnly)
        || !disks.open(QIODevice::ReadOnly) || !network.open(QIODevice::ReadOnly)) {
        if (error) *error = "Linux performance data is unavailable under /proc.";
        return result;
    }

    const QStringList loads = fields(QString::fromLatin1(load.readAll()));
    result.load1 = loads.value(0).toDouble();
    result.load5 = loads.value(1).toDouble();
    result.load15 = loads.value(2).toDouble();
    result.processCount = loads.value(3).section('/', 0, 0).toULongLong();
    result.uptimeSeconds = QString::fromLatin1(up.readAll()).section(' ', 0, 0).toDouble();

    for (const QString &line : QString::fromLatin1(mem.readAll()).split('\n')) {
        const quint64 value = fields(line).value(1).toULongLong() * 1024;
        if (line.startsWith("MemTotal:")) result.memoryTotal = value;
        else if (line.startsWith("MemAvailable:")) result.memoryAvailable = value;
        else if (line.startsWith("SwapTotal:")) result.swapTotal = value;
        else if (line.startsWith("SwapFree:")) result.swapFree = value;
    }

    for (const QString &line : QString::fromLatin1(stat.readAll()).split('\n')) {
        const QStringList values = fields(line);
        if (values.value(0) == "cpu") {
            for (int i = 1; i < values.size(); ++i) result.cpuTotalTicks += values[i].toULongLong();
            result.cpuIdleTicks = values.value(4).toULongLong() + values.value(5).toULongLong();
        } else if (values.value(0) == "ctxt") {
            result.contextSwitches = values.value(1).toULongLong();
        }
    }

    const QDir sysBlock("/sys/block");
    for (const QString &line : QString::fromLatin1(disks.readAll()).split('\n')) {
        const QStringList values = fields(line);
        if (values.size() < 14 || !sysBlock.exists(values[2])) continue;
        result.diskReadBytes += values[5].toULongLong() * 512;
        result.diskWriteBytes += values[9].toULongLong() * 512;
    }

    for (QString line : QString::fromLatin1(network.readAll()).split('\n')) {
        if (!line.contains(':')) continue;
        const QString interface = line.section(':', 0, 0).trimmed();
        if (interface == "lo") continue;
        line = line.section(':', 1);
        const QStringList values = fields(line);
        if (values.size() < 16) continue;
        result.networkReceiveBytes += values[0].toULongLong();
        result.networkTransmitBytes += values[8].toULongLong();
    }
    return result;
}
