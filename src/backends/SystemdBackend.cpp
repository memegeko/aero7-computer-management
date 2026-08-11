#include "SystemdBackend.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

QString SystemdBackend::run(const QStringList &arguments, QString *error)
{
    QProcess process;
    process.start("systemctl", arguments);
    if (!process.waitForStarted(2000) || !process.waitForFinished(7000)) {
        process.kill();
        if (error) *error = "systemctl did not respond.";
        return {};
    }
    if (process.exitCode() != 0 && error)
        *error = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    return QString::fromLocal8Bit(process.readAllStandardOutput());
}

QList<TimerInfo> SystemdBackend::timers(QString *error)
{
    return parseTimers(run({"list-timers", "--all", "--no-pager", "--output=json"}, error));
}

QList<ServiceInfo> SystemdBackend::services(QString *error)
{
    return parseServices(run({"list-units", "--type=service", "--all", "--no-legend", "--no-pager"}, error));
}

QList<TimerInfo> SystemdBackend::parseTimers(const QString &text)
{
    QList<TimerInfo> out;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
    if (document.isArray()) {
        const QDateTime now = QDateTime::currentDateTime();
        for (const QJsonValue &value : document.array()) {
            const QJsonObject object = value.toObject();
            const qint64 nextMicros = object.value("next").toVariant().toLongLong();
            const qint64 lastMicros = object.value("last").toVariant().toLongLong();
            const QDateTime next = QDateTime::fromMSecsSinceEpoch(nextMicros / 1000);
            const QDateTime last = QDateTime::fromMSecsSinceEpoch(lastMicros / 1000);
            const qint64 seconds = now.secsTo(next);
            out.push_back({
                nextMicros > 0 ? next.toString("yyyy-MM-dd HH:mm:ss") : QString("-"),
                seconds >= 0 ? QString("%1 min").arg(seconds / 60) : QString("-"),
                lastMicros > 0 ? last.toString("yyyy-MM-dd HH:mm:ss") : QString("-"),
                {}, object.value("unit").toString(), object.value("activates").toString()
            });
        }
        return out;
    }
    const QRegularExpression spaces("\\s{2,}");
    for (const QString &line : text.split('\n', Qt::SkipEmptyParts)) {
        const QStringList columns = line.trimmed().split(spaces);
        if (columns.size() < 6) continue;
        out.push_back({columns.value(0), columns.value(1), columns.value(2), columns.value(3),
                       columns.value(4), columns.mid(5).join("  ")});
    }
    return out;
}

QList<ServiceInfo> SystemdBackend::parseServices(const QString &text)
{
    QList<ServiceInfo> out;
    const QRegularExpression spaces("\\s+");
    for (QString line : text.split('\n', Qt::SkipEmptyParts)) {
        line = line.trimmed();
        const QStringList columns = line.split(spaces, Qt::SkipEmptyParts);
        if (columns.size() < 5) continue;
        out.push_back({columns[0], columns[1], columns[2], columns[3], columns.mid(4).join(' ')});
    }
    return out;
}
