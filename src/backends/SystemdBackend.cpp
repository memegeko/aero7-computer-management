#include "SystemdBackend.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QProcess>

#include <algorithm>

struct ListedUnit {
    QString name;
    QString description;
    QString load;
    QString active;
    QString sub;
    QString path;
    uint jobId = 0;
    QString jobType;
    QString jobPath;
};

struct UnitFileEntry {
    QString path;
    QString state;
};

Q_DECLARE_METATYPE(ListedUnit)
Q_DECLARE_METATYPE(QList<ListedUnit>)
Q_DECLARE_METATYPE(UnitFileEntry)
Q_DECLARE_METATYPE(QList<UnitFileEntry>)

QDBusArgument &operator<<(QDBusArgument &argument, const ListedUnit &unit)
{
    argument.beginStructure();
    argument << unit.name << unit.description << unit.load << unit.active << unit.sub
             << QString{} << QDBusObjectPath(unit.path.isEmpty() ? "/" : unit.path)
             << unit.jobId << unit.jobType
             << QDBusObjectPath(unit.jobPath.isEmpty() ? "/" : unit.jobPath);
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, ListedUnit &unit)
{
    QString followed;
    QDBusObjectPath path;
    QDBusObjectPath jobPath;
    argument.beginStructure();
    argument >> unit.name >> unit.description >> unit.load >> unit.active >> unit.sub
             >> followed >> path >> unit.jobId >> unit.jobType >> jobPath;
    argument.endStructure();
    unit.path = path.path();
    unit.jobPath = jobPath.path();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const UnitFileEntry &entry)
{
    argument.beginStructure(); argument << entry.path << entry.state; argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, UnitFileEntry &entry)
{
    argument.beginStructure(); argument >> entry.path >> entry.state; argument.endStructure();
    return argument;
}

namespace {

QDBusConnection connection(UnitScope scope)
{
    return scope == UnitScope::System ? QDBusConnection::systemBus() : QDBusConnection::sessionBus();
}

QVariantMap properties(const QDBusConnection &bus, const QString &path, const QString &interface)
{
    if (path.isEmpty())
        return {};
    QDBusInterface props("org.freedesktop.systemd1", path,
                         "org.freedesktop.DBus.Properties", bus);
    const QDBusReply<QVariantMap> reply = props.call("GetAll", interface);
    return reply.isValid() ? reply.value() : QVariantMap{};
}

QList<ListedUnit> listUnits(UnitScope scope, QString *error)
{
    const QDBusConnection bus = connection(scope);
    QDBusInterface manager("org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                           "org.freedesktop.systemd1.Manager", bus);
    if (!manager.isValid()) {
        if (error) *error = QString("The %1 systemd manager is unavailable.").arg(SystemdBackend::scopeName(scope));
        return {};
    }
    qDBusRegisterMetaType<ListedUnit>();
    qDBusRegisterMetaType<QList<ListedUnit>>();
    const QDBusReply<QList<ListedUnit>> reply = manager.call("ListUnits");
    if (!reply.isValid()) {
        if (error) *error = reply.error().message();
        return {};
    }
    return reply.value();
}

QList<UnitFileEntry> listUnitFiles(UnitScope scope)
{
    qDBusRegisterMetaType<UnitFileEntry>();
    qDBusRegisterMetaType<QList<UnitFileEntry>>();
    QDBusInterface manager("org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                           "org.freedesktop.systemd1.Manager", connection(scope));
    const QDBusReply<QList<UnitFileEntry>> reply = manager.call("ListUnitFiles");
    return reply.isValid() ? reply.value() : QList<UnitFileEntry>{};
}

QString unitFileState(const QString &unit, UnitScope scope)
{
    QDBusInterface manager("org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                           "org.freedesktop.systemd1.Manager", connection(scope));
    const QDBusReply<QString> reply = manager.call("GetUnitFileState", unit);
    return reply.isValid() ? reply.value() : QString("transient");
}

QString unitPath(const QString &unit, UnitScope scope, QString *error)
{
    QDBusInterface manager("org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                           "org.freedesktop.systemd1.Manager", connection(scope));
    QDBusReply<QDBusObjectPath> reply = manager.call("GetUnit", unit);
    if (!reply.isValid()) reply = manager.call("LoadUnit", unit);
    if (!reply.isValid()) {
        if (error) *error = reply.error().message();
        return {};
    }
    return reply.value().path();
}

QString formatTimestamp(qulonglong micros)
{
    return micros == 0 ? QString("Never")
                       : QDateTime::fromMSecsSinceEpoch(micros / 1000).toString("yyyy-MM-dd HH:mm:ss");
}

QString timerExpression(const QString &fragmentPath)
{
    QFile file(fragmentPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QStringList expressions;
    const QRegularExpression timerLine("^(On[A-Za-z]+Sec|OnCalendar|AccuracySec|RandomizedDelaySec)\\s*=");
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (timerLine.match(line).hasMatch())
            expressions << line;
    }
    return expressions.join("; ");
}

bool managerCall(const QString &method, const QVariantList &arguments, UnitScope scope, QString *error)
{
    QDBusInterface manager("org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                           "org.freedesktop.systemd1.Manager", connection(scope));
    if (!manager.isValid()) {
        if (error) *error = "The systemd D-Bus manager is unavailable.";
        return false;
    }
    const QDBusMessage reply = manager.callWithArgumentList(QDBus::Block, method, arguments);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (error) *error = reply.errorMessage();
        return false;
    }
    return true;
}

bool unitAction(const QString &method, const QString &unit, UnitScope scope, QString *error)
{
    return managerCall(method, {unit, QString("replace")}, scope, error);
}

QString quotedSystemdArgument(QString value)
{
    value.replace('\\', "\\\\");
    value.replace('"', "\\\"");
    return '"' + value + '"';
}

bool writeUnitFile(const QString &path, const QByteArray &contents, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit()) {
        if (error) *error = QString("Could not write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}
}

QString SystemdBackend::scopeName(UnitScope scope)
{
    return scope == UnitScope::System ? "System" : "User";
}

QList<TimerInfo> SystemdBackend::timers(QString *error)
{
    QList<TimerInfo> result;
    QStringList errors;
    for (const UnitScope scope : {UnitScope::System, UnitScope::User}) {
        QString scopeError;
        QSet<QString> present;
        for (const ListedUnit &unit : listUnits(scope, &scopeError)) {
            if (!unit.name.endsWith(".timer"))
                continue;
            const QVariantMap timer = properties(connection(scope), unit.path, "org.freedesktop.systemd1.Timer");
            const QVariantMap common = properties(connection(scope), unit.path, "org.freedesktop.systemd1.Unit");
            TimerInfo item;
            item.scope = scope;
            item.unit = unit.name;
            item.description = unit.description;
            item.status = unit.active;
            item.next = formatTimestamp(timer.value("NextElapseUSecRealtime").toULongLong());
            item.last = formatTimestamp(timer.value("LastTriggerUSec").toULongLong());
            item.activates = common.value("Triggers").toStringList().value(0);
            item.enabled = unitFileState(unit.name, scope);
            item.persistent = timer.value("Persistent").toBool();
            item.objectPath = unit.path;
            result << item;
            present.insert(item.unit);
        }
        for (const UnitFileEntry &file : listUnitFiles(scope)) {
            const QString name = QFileInfo(file.path).fileName();
            if (!name.endsWith(".timer") || present.contains(name)) continue;
            result << TimerInfo{scope, name, {}, "inactive", "Never", "Never", {}, file.state, false, {}};
        }
        if (!scopeError.isEmpty() && scope == UnitScope::System)
            errors << scopeError;
    }
    if (error) *error = errors.join(' ');
    std::sort(result.begin(), result.end(), [](const TimerInfo &a, const TimerInfo &b) {
        if (a.scope != b.scope) return a.scope == UnitScope::System;
        return a.unit.localeAwareCompare(b.unit) < 0;
    });
    return result;
}

QList<ServiceInfo> SystemdBackend::services(QString *error)
{
    QList<ServiceInfo> result;
    QStringList errors;
    for (const UnitScope scope : {UnitScope::System, UnitScope::User}) {
        QString scopeError;
        QSet<QString> present;
        for (const ListedUnit &unit : listUnits(scope, &scopeError)) {
            if (!unit.name.endsWith(".service"))
                continue;
            result << ServiceInfo{scope, unit.name, unit.description, unit.active, unit.sub,
                                  unitFileState(unit.name, scope), unit.path};
            present.insert(unit.name);
        }
        for (const UnitFileEntry &file : listUnitFiles(scope)) {
            const QString name = QFileInfo(file.path).fileName();
            if (!name.endsWith(".service") || present.contains(name)) continue;
            result << ServiceInfo{scope, name, {}, "inactive", "dead", file.state, {}};
        }
        if (!scopeError.isEmpty() && scope == UnitScope::System)
            errors << scopeError;
    }
    if (error) *error = errors.join(' ');
    std::sort(result.begin(), result.end(), [](const ServiceInfo &a, const ServiceInfo &b) {
        if (a.scope != b.scope) return a.scope == UnitScope::System;
        return a.unit.localeAwareCompare(b.unit) < 0;
    });
    return result;
}

UnitDetails SystemdBackend::details(const QString &unit, UnitScope scope, QString *error)
{
    UnitDetails result;
    result.unit = unit;
    result.startupState = unitFileState(unit, scope);
    const QString path = unitPath(unit, scope, error);
    if (path.isEmpty())
        return result;
    const QVariantMap common = properties(connection(scope), path, "org.freedesktop.systemd1.Unit");
    result.description = common.value("Description").toString();
    result.loadState = common.value("LoadState").toString();
    result.activeState = common.value("ActiveState").toString();
    result.subState = common.value("SubState").toString();
    result.fragmentPath = common.value("FragmentPath").toString();
    result.requiredUnits = common.value("Requires").toStringList();
    result.wants = common.value("Wants").toStringList();
    result.after = common.value("After").toStringList();
    result.before = common.value("Before").toStringList();
    if (unit.endsWith(".timer"))
        result.timerExpression = timerExpression(result.fragmentPath);
    return result;
}

bool SystemdBackend::startUnit(const QString &unit, UnitScope scope, QString *error) { return unitAction("StartUnit", unit, scope, error); }
bool SystemdBackend::stopUnit(const QString &unit, UnitScope scope, QString *error) { return unitAction("StopUnit", unit, scope, error); }
bool SystemdBackend::restartUnit(const QString &unit, UnitScope scope, QString *error) { return unitAction("RestartUnit", unit, scope, error); }
bool SystemdBackend::reloadUnit(const QString &unit, UnitScope scope, QString *error) { return unitAction("ReloadUnit", unit, scope, error); }

bool SystemdBackend::setUnitEnabled(const QString &unit, UnitScope scope, bool enabled, QString *error)
{
    const bool ok = enabled
        ? managerCall("EnableUnitFiles", {QStringList{unit}, false, true}, scope, error)
        : managerCall("DisableUnitFiles", {QStringList{unit}, false}, scope, error);
    if (ok)
        managerCall("Reload", {}, scope, nullptr);
    return ok;
}

bool SystemdBackend::setUnitMasked(const QString &unit, UnitScope scope, bool masked, QString *error)
{
    const bool ok = masked
        ? managerCall("MaskUnitFiles", {QStringList{unit}, false, true}, scope, error)
        : managerCall("UnmaskUnitFiles", {QStringList{unit}, false}, scope, error);
    if (ok)
        managerCall("Reload", {}, scope, nullptr);
    return ok;
}

bool SystemdBackend::createUserTimer(const QString &name, const QString &description,
                                     const QString &timerExpression, const QString &command,
                                     QString *error)
{
    static const QRegularExpression validName("^[A-Za-z0-9_.@-]+$");
    QString stem = name.trimmed();
    if (stem.endsWith(".timer")) stem.chop(6);
    if (!validName.match(stem).hasMatch() || stem.isEmpty()) {
        if (error) *error = "The task name may contain letters, numbers, dots, underscores, @, and hyphens.";
        return false;
    }
    const QStringList commandParts = QProcess::splitCommand(command);
    if (commandParts.isEmpty() || QStandardPaths::findExecutable(commandParts.first()).isEmpty()) {
        if (error) *error = "The task command must start with an installed executable.";
        return false;
    }
    QStringList quoted;
    for (const QString &part : commandParts) quoted << quotedSystemdArgument(part);
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + "/systemd/user";
    if (!QDir().mkpath(directory)) {
        if (error) *error = "The user systemd unit directory could not be created.";
        return false;
    }
    const QString service = stem + ".service";
    const QString timer = stem + ".timer";
    const QByteArray serviceData = QString("[Unit]\nDescription=%1\n\n[Service]\nType=oneshot\nExecStart=%2\n")
        .arg(description, quoted.join(' ')).toUtf8();
    QString trigger = timerExpression.trimmed();
    if (!trigger.contains('=')) trigger.prepend("OnCalendar=");
    const QByteArray timerData = QString("[Unit]\nDescription=%1\n\n[Timer]\n%2\nPersistent=true\nUnit=%3\n\n[Install]\nWantedBy=timers.target\n")
        .arg(description, trigger, service).toUtf8();
    if (!writeUnitFile(directory + '/' + service, serviceData, error)
        || !writeUnitFile(directory + '/' + timer, timerData, error))
        return false;
    if (!managerCall("Reload", {}, UnitScope::User, error)
        || !setUnitEnabled(timer, UnitScope::User, true, error)
        || !startUnit(timer, UnitScope::User, error))
        return false;
    return true;
}

bool SystemdBackend::deleteUserTimer(const QString &unit, QString *error)
{
    if (!unit.endsWith(".timer") || unit.contains('/')) {
        if (error) *error = "Only user timer units can be deleted here.";
        return false;
    }
    const QString stem = unit.left(unit.size() - 6);
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + "/systemd/user";
    stopUnit(unit, UnitScope::User, nullptr);
    setUnitEnabled(unit, UnitScope::User, false, nullptr);
    const bool timerRemoved = QFile::remove(directory + '/' + unit);
    const bool serviceRemoved = QFile::remove(directory + '/' + stem + ".service");
    managerCall("Reload", {}, UnitScope::User, nullptr);
    if (!timerRemoved) {
        if (error) *error = "The selected timer is not an Aero7 user task or could not be removed.";
        return false;
    }
    Q_UNUSED(serviceRemoved)
    return true;
}

QList<TimerInfo> SystemdBackend::parseTimers(const QString &text)
{
    QList<TimerInfo> out;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
    if (!document.isArray()) return out;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        out.push_back({UnitScope::System, object.value("unit").toString(), {}, {},
                       formatTimestamp(object.value("next").toVariant().toULongLong()),
                       formatTimestamp(object.value("last").toVariant().toULongLong()),
                       object.value("activates").toString(), {}, false, {}});
    }
    return out;
}

QList<ServiceInfo> SystemdBackend::parseServices(const QString &text)
{
    QList<ServiceInfo> out;
    const QRegularExpression spaces("\\s+");
    for (QString line : text.split('\n', Qt::SkipEmptyParts)) {
        const QStringList columns = line.trimmed().split(spaces, Qt::SkipEmptyParts);
        if (columns.size() < 5) continue;
        out.push_back({UnitScope::System, columns[0], columns.mid(4).join(' '),
                       columns[2], columns[3], {}, {}});
    }
    return out;
}
