#pragma once

#include <QString>

struct PerformanceSnapshot {
    double load1 = 0;
    double load5 = 0;
    double load15 = 0;
    quint64 memoryTotal = 0;
    quint64 memoryAvailable = 0;
    quint64 swapTotal = 0;
    quint64 swapFree = 0;
    quint64 uptimeSeconds = 0;
    quint64 cpuTotalTicks = 0;
    quint64 cpuIdleTicks = 0;
    quint64 contextSwitches = 0;
    quint64 processCount = 0;
    quint64 diskReadBytes = 0;
    quint64 diskWriteBytes = 0;
    quint64 networkReceiveBytes = 0;
    quint64 networkTransmitBytes = 0;
};

class PerformanceBackend {
public:
    static PerformanceSnapshot snapshot(QString *error = nullptr);
};
