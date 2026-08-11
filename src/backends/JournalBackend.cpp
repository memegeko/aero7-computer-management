#include "JournalBackend.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

#include <algorithm>

namespace {
QString valueString(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    if (value.isString()) return value.toString();
    if (value.isDouble()) return QString::number(value.toDouble(), 'f', 0);
    return value.toVariant().toString();
}

bool categoryMatches(const QString &category, const JournalEntry &entry)
{
    if (category.isEmpty() || category == "All Events" || category == "Boot")
        return true;
    const QString source = (entry.source + ' ' + entry.unit + ' ' + entry.executable).toLower();
    if (category == "System")
        return !entry.fields.value("_SYSTEMD_UNIT").isEmpty()
            || entry.fields.value("_TRANSPORT") == "kernel";
    if (category == "Aero7 Logs")
        return source.contains("aero7") || entry.message.contains("Aero7", Qt::CaseInsensitive);
    if (category == "Applications")
        return !entry.fields.value("_SYSTEMD_USER_UNIT").isEmpty()
            || entry.unit.startsWith("app-") || entry.unit.startsWith("user@");
    if (category == "Kernel")
        return entry.fields.value("_TRANSPORT") == "kernel";
    if (category == "Authentication / Security") {
        static const QRegularExpression security("sudo|polkit|login|sshd|pam|systemd-logind");
        return source.contains(security);
    }
    if (category == "Hardware")
        return entry.fields.value("_TRANSPORT") == "kernel" || source.contains("udev");
    return true;
}

QList<JournalEntry> runJournal(QStringList arguments, const QString &category,
                               const QString &search, QString *error, int limit)
{
    arguments << "--no-pager" << "-n" << QString::number(limit) << "-o" << "json";
    QProcess process;
    process.start("journalctl", arguments);
    if (!process.waitForStarted(2000) || !process.waitForFinished(12000)) {
        process.kill();
        if (error) *error = "The system journal did not respond.";
        return {};
    }
    if (process.exitCode() != 0) {
        if (error) *error = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        return {};
    }

    QList<JournalEntry> out;
    for (const QByteArray &line : process.readAllStandardOutput().split('\n')) {
        const QJsonObject object = QJsonDocument::fromJson(line).object();
        if (object.isEmpty()) continue;
        const qint64 micros = valueString(object, "__REALTIME_TIMESTAMP").toLongLong();
        const int priority = valueString(object, "PRIORITY").toInt();
        JournalEntry entry;
        entry.time = QDateTime::fromMSecsSinceEpoch(micros / 1000).toString("yyyy-MM-dd HH:mm:ss.zzz");
        entry.priority = JournalBackend::priorityName(priority);
        entry.unit = valueString(object, "_SYSTEMD_UNIT");
        if (entry.unit.isEmpty()) entry.unit = valueString(object, "_SYSTEMD_USER_UNIT");
        entry.source = entry.unit;
        if (entry.source.isEmpty()) entry.source = valueString(object, "SYSLOG_IDENTIFIER");
        if (entry.source.isEmpty()) entry.source = valueString(object, "_COMM");
        entry.message = valueString(object, "MESSAGE");
        entry.pid = valueString(object, "_PID");
        entry.uid = valueString(object, "_UID");
        entry.executable = valueString(object, "_EXE");
        entry.bootId = valueString(object, "_BOOT_ID");
        for (auto it = object.constBegin(); it != object.constEnd(); ++it)
            entry.fields.insert(it.key(), valueString(object, it.key()));
        if (!categoryMatches(category, entry)) continue;
        if (!search.isEmpty() && !entry.time.contains(search, Qt::CaseInsensitive)
            && !entry.source.contains(search, Qt::CaseInsensitive)
            && !entry.message.contains(search, Qt::CaseInsensitive))
            continue;
        out << entry;
    }
    std::reverse(out.begin(), out.end());
    return out;
}
}

QString JournalBackend::priorityName(int priority)
{
    if (priority <= 2) return "Critical";
    if (priority == 3) return "Error";
    if (priority == 4) return "Warning";
    if (priority <= 6) return "Information";
    return "Verbose";
}

QList<JournalEntry> JournalBackend::recent(QString *error, int limit)
{
    return runJournal({}, "All Events", {}, error, limit);
}

QList<JournalEntry> JournalBackend::query(const QString &category, const QString &search,
                                          QString *error, int limit)
{
    QStringList arguments;
    if (category == "Boot") arguments << "-b";
    else if (category == "Kernel") arguments << "-k";
    return runJournal(arguments, category, search, error, limit);
}

QList<JournalEntry> JournalBackend::saved(const QString &path, QString *error, int limit)
{
    return runJournal({"--file", path}, "All Events", {}, error, limit);
}

QList<JournalEntry> JournalBackend::forUnit(const QString &unit, bool userUnit,
                                            QString *error, int limit)
{
    QStringList arguments;
    if (userUnit) arguments << "--user";
    arguments << "-u" << unit;
    return runJournal(arguments, "All Events", {}, error, limit);
}
