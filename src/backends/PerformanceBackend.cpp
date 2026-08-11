#include "PerformanceBackend.h"

#include <QFile>
#include <QRegularExpression>

PerformanceSnapshot PerformanceBackend::snapshot(QString *error)
{
    PerformanceSnapshot result;
    QFile load("/proc/loadavg");
    QFile mem("/proc/meminfo");
    QFile up("/proc/uptime");
    if (!load.open(QIODevice::ReadOnly) || !mem.open(QIODevice::ReadOnly) || !up.open(QIODevice::ReadOnly)) {
        if (error) *error = "Linux performance data is unavailable under /proc.";
        return result;
    }
    const auto loads = QString::fromLatin1(load.readAll()).split(' ');
    result.load1 = loads.value(0).toDouble(); result.load5 = loads.value(1).toDouble(); result.load15 = loads.value(2).toDouble();
    result.uptimeSeconds = QString::fromLatin1(up.readAll()).section(' ', 0, 0).toDouble();
    for (const QString &line : QString::fromLatin1(mem.readAll()).split('\n')) {
        const quint64 value = line.section(QRegularExpression("\\s+"), 1, 1).toULongLong() * 1024;
        if (line.startsWith("MemTotal:")) result.memoryTotal = value;
        if (line.startsWith("MemAvailable:")) result.memoryAvailable = value;
    }
    return result;
}

