#pragma once

#include <QString>

struct SystemSummary {
    QString computerName;
    QString aero7Version;
    QString archVersion;
    QString kernelVersion;
    QString architecture;
    QString uptime;
    QString cpu;
    QString memory;
    QString systemDisk;
    QString currentUser;
    QString session;
    QString network;
};

class SystemInfoBackend {
public:
    static SystemSummary summary(QString *error = nullptr);
};
