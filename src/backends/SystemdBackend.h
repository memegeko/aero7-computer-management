#pragma once

#include <QList>
#include <QString>

struct TimerInfo { QString next, left, last, passed, unit, activates; };
struct ServiceInfo { QString unit, load, active, sub, description; };

class SystemdBackend {
public:
    static QList<TimerInfo> timers(QString *error = nullptr);
    static QList<ServiceInfo> services(QString *error = nullptr);
    static QList<TimerInfo> parseTimers(const QString &text);
    static QList<ServiceInfo> parseServices(const QString &text);
private:
    static QString run(const QStringList &arguments, QString *error);
};

