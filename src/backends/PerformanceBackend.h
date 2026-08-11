#pragma once

#include <QString>

struct PerformanceSnapshot {
    double load1 = 0, load5 = 0, load15 = 0;
    quint64 memoryTotal = 0, memoryAvailable = 0, uptimeSeconds = 0;
};

class PerformanceBackend {
public:
    static PerformanceSnapshot snapshot(QString *error = nullptr);
};

