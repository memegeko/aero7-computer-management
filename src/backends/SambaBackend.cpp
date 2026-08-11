#include "SambaBackend.h"

#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace {
QString run(const QString &program, const QStringList &arguments, QString *error, int timeout = 8000)
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(2000) || !process.waitForFinished(timeout)) {
        process.kill();
        if (error) *error = QString("%1 did not respond.").arg(program);
        return {};
    }
    if (process.exitCode() != 0) {
        if (error) *error = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        return {};
    }
    return QString::fromLocal8Bit(process.readAllStandardOutput());
}

bool runAction(const QString &program, const QStringList &arguments, QString *error)
{
    QString actionError;
    run(program, arguments, &actionError);
    if (!actionError.isEmpty()) {
        if (error) *error = actionError;
        return false;
    }
    return true;
}

QList<QStringList> dataLines(const QString &output, int minimumColumns)
{
    QList<QStringList> result;
    for (QString line : output.split('\n')) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith('-') || line.startsWith("Samba version")
            || line.startsWith("PID") || line.startsWith("Service") || line.startsWith("Locked files"))
            continue;
        const QStringList columns = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (columns.size() >= minimumColumns)
            result << columns;
    }
    return result;
}
}

bool SambaBackend::available()
{
    return !QStandardPaths::findExecutable("smbstatus").isEmpty()
        || !QStandardPaths::findExecutable("testparm").isEmpty();
}

QList<SambaShare> SambaBackend::shares(QString *error)
{
    if (!available()) {
        if (error) *error = "Samba file sharing is not installed on this computer.";
        return {};
    }
    QList<SambaShare> result;
    QSet<QString> names;
    const QString testparm = QStandardPaths::findExecutable("testparm");
    if (!testparm.isEmpty()) {
        QString toolError;
        const QString output = run(testparm, {"-s", "--suppress-prompt"}, &toolError);
        SambaShare current;
        auto finish = [&] {
            if (!current.name.isEmpty() && current.name != "global") {
                current.type = current.name == "printers" || current.name == "print$" ? "Printer" : "Disk";
                result << current;
                names.insert(current.name);
            }
            current = {};
        };
        for (QString line : output.split('\n')) {
            line = line.trimmed();
            if (line.startsWith('[') && line.endsWith(']')) {
                finish();
                current.name = line.mid(1, line.size() - 2);
            } else if (line.startsWith("path =")) current.path = line.section('=', 1).trimmed();
            else if (line.startsWith("comment =")) current.comment = line.section('=', 1).trimmed();
        }
        finish();
        if (result.isEmpty() && !toolError.isEmpty() && error) *error = toolError;
    }

    const QString net = QStandardPaths::findExecutable("net");
    if (!net.isEmpty()) {
        QString ignored;
        const QString output = run(net, {"usershare", "info", "--long"}, &ignored);
        SambaShare current;
        auto finish = [&] {
            if (!current.name.isEmpty() && !names.contains(current.name)) {
                current.type = "Disk";
                current.userShare = true;
                result << current;
            }
            current = {};
        };
        for (QString line : output.split('\n')) {
            line = line.trimmed();
            if (line.startsWith('[') && line.endsWith(']')) {
                finish(); current.name = line.mid(1, line.size() - 2); current.userShare = true;
            } else if (line.startsWith("path=")) current.path = line.section('=', 1);
            else if (line.startsWith("comment=")) current.comment = line.section('=', 1);
        }
        finish();
    }
    return result;
}

QList<SambaSession> SambaBackend::sessions(QString *error)
{
    const QString smbstatus = QStandardPaths::findExecutable("smbstatus");
    if (smbstatus.isEmpty()) {
        if (error) *error = "Samba file sharing is not installed on this computer.";
        return {};
    }
    const QString output = run(smbstatus, {"--processes"}, error);
    QList<SambaSession> result;
    for (const QStringList &columns : dataLines(output, 4)) {
        SambaSession session;
        session.pid = columns.value(0);
        session.user = columns.value(1);
        session.computer = columns.value(3);
        const QRegularExpressionMatch address = QRegularExpression("([0-9a-fA-F:.]+)").match(session.computer);
        session.clientIp = address.hasMatch() ? address.captured(1) : session.computer;
        session.protocol = columns.value(4);
        session.connectedSince = columns.mid(5).join(' ');
        if (session.pid.toUInt() > 0) result << session;
    }
    return result;
}

QList<SambaOpenFile> SambaBackend::openFiles(QString *error)
{
    const QString smbstatus = QStandardPaths::findExecutable("smbstatus");
    if (smbstatus.isEmpty()) {
        if (error) *error = "Samba file sharing is not installed on this computer.";
        return {};
    }
    const QString output = run(smbstatus, {"--locks"}, error);
    QList<SambaOpenFile> result;
    for (const QStringList &columns : dataLines(output, 7)) {
        if (columns.value(0).toUInt() == 0) continue;
        SambaOpenFile file;
        file.pid = columns.value(0);
        file.user = columns.value(1);
        file.accessMode = columns.value(3);
        file.client = columns.value(5);
        file.file = columns.mid(6).join(' ');
        result << file;
    }
    return result;
}

bool SambaBackend::createUserShare(const QString &name, const QString &path,
                                   const QString &comment, QString *error)
{
    const QString net = QStandardPaths::findExecutable("net");
    if (net.isEmpty()) {
        if (error) *error = "The Samba 'net usershare' tool is not installed.";
        return false;
    }
    return runAction(net, {"usershare", "add", name, path, comment, "Everyone:F", "guest_ok=n"}, error);
}

bool SambaBackend::removeUserShare(const QString &name, QString *error)
{
    const QString net = QStandardPaths::findExecutable("net");
    if (net.isEmpty()) {
        if (error) *error = "The Samba 'net usershare' tool is not installed.";
        return false;
    }
    return runAction(net, {"usershare", "delete", name}, error);
}
