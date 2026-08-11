#include "JournalBackend.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

QList<JournalEntry> JournalBackend::recent(QString *error, int limit)
{
    QProcess process;
    process.start("journalctl", {"--no-pager", "-n", QString::number(limit), "-o", "json"});
    if (!process.waitForStarted(2000) || !process.waitForFinished(8000)) {
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
        const QJsonObject obj = QJsonDocument::fromJson(line).object();
        if (obj.isEmpty()) continue;
        const qint64 micros = obj.value("__REALTIME_TIMESTAMP").toString().toLongLong();
        out.push_back({QDateTime::fromMSecsSinceEpoch(micros / 1000).toString("yyyy-MM-dd HH:mm:ss"),
                       obj.value("PRIORITY").toString(),
                       obj.value("_SYSTEMD_UNIT").toString(obj.value("SYSLOG_IDENTIFIER").toString()),
                       obj.value("MESSAGE").toVariant().toString()});
    }
    return out;
}

