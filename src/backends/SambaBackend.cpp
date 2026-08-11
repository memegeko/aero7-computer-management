#include "SambaBackend.h"

#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

bool SambaBackend::available() { return !QStandardPaths::findExecutable("smbstatus").isEmpty(); }

QList<SambaShare> SambaBackend::shares(QString *error)
{
    if (!available()) {
        if (error) *error = "Samba status support is not installed (smbstatus was not found).";
        return {};
    }
    QProcess process;
    process.start("smbstatus", {"--shares"});
    if (!process.waitForFinished(6000) || process.exitCode() != 0) {
        if (error) *error = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        return {};
    }
    QList<SambaShare> out;
    for (const QString &line : QString::fromLocal8Bit(process.readAllStandardOutput()).split('\n')) {
        const QStringList c = line.simplified().split(' ');
        if (c.size() >= 4 && c[1].toInt() > 0)
            out.push_back({c[0], c[1], c[2], c.mid(3).join(' ')});
    }
    return out;
}

