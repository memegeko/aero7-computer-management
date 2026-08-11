#pragma once

#include <QList>
#include <QString>

enum class UnitScope { System, User };

struct TimerInfo {
    UnitScope scope = UnitScope::System;
    QString unit;
    QString description;
    QString status;
    QString next;
    QString last;
    QString activates;
    QString enabled;
    bool persistent = false;
    QString objectPath;
};

struct ServiceInfo {
    UnitScope scope = UnitScope::System;
    QString unit;
    QString description;
    QString active;
    QString sub;
    QString startup;
    QString objectPath;
};

struct UnitDetails {
    QString unit;
    QString description;
    QString loadState;
    QString activeState;
    QString subState;
    QString startupState;
    QString fragmentPath;
    QString timerExpression;
    QStringList requiredUnits;
    QStringList wants;
    QStringList after;
    QStringList before;
};

class SystemdBackend {
public:
    static QList<TimerInfo> timers(QString *error = nullptr);
    static QList<ServiceInfo> services(QString *error = nullptr);
    static UnitDetails details(const QString &unit, UnitScope scope, QString *error = nullptr);

    static bool startUnit(const QString &unit, UnitScope scope, QString *error = nullptr);
    static bool stopUnit(const QString &unit, UnitScope scope, QString *error = nullptr);
    static bool restartUnit(const QString &unit, UnitScope scope, QString *error = nullptr);
    static bool reloadUnit(const QString &unit, UnitScope scope, QString *error = nullptr);
    static bool setUnitEnabled(const QString &unit, UnitScope scope, bool enabled, QString *error = nullptr);
    static bool setUnitMasked(const QString &unit, UnitScope scope, bool masked, QString *error = nullptr);
    static bool createUserTimer(const QString &name, const QString &description,
                                const QString &timerExpression, const QString &command,
                                QString *error = nullptr);
    static bool deleteUserTimer(const QString &unit, QString *error = nullptr);

    static QString scopeName(UnitScope scope);
    static QList<TimerInfo> parseTimers(const QString &text);
    static QList<ServiceInfo> parseServices(const QString &text);
};
