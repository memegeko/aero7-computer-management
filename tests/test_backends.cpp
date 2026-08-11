#include "backends/AccountsBackend.h"
#include "backends/SystemdBackend.h"
#include "backends/SystemInfoBackend.h"
#include "backends/JournalBackend.h"
#include "backends/UDisksBackend.h"
#include "util/Format.h"

#include <QSet>
#include <QTextStream>

int main()
{
    const auto timers = SystemdBackend::parseTimers(
        "[{\"next\":1786467600000000,\"left\":1786467600000000,"
        "\"last\":1786464000000000,\"passed\":0,"
        "\"unit\":\"pkg.timer\",\"activates\":\"pkg.service\"}]");
    if (timers.size() != 1 || timers.first().unit != "pkg.timer"
        || timers.first().activates != "pkg.service")
        return 1;

    const auto services = SystemdBackend::parseServices(
        "sshd.service loaded active running OpenSSH server daemon\n"
        "cups.service loaded inactive dead CUPS Scheduler\n");
    if (services.size() != 2 || services.first().description != "OpenSSH server daemon")
        return 2;

    if (Format::bytes(0) != "0 B" || Format::bytes(1024) != "1.0 KiB"
        || Format::bytes(1073741824ULL) != "1.0 GiB")
        return 3;

    const auto users = AccountsBackend::users();
    const auto groups = AccountsBackend::groups();
    if (users.isEmpty() || groups.isEmpty())
        return 4;
    QSet<QString> userNames;
    for (const auto &user : users) {
        if (user.name.isEmpty() || userNames.contains(user.name)) return 5;
        userNames.insert(user.name);
    }
    if (JournalBackend::priorityName(0) != "Critical"
        || JournalBackend::priorityName(3) != "Error"
        || JournalBackend::priorityName(4) != "Warning"
        || JournalBackend::priorityName(6) != "Information"
        || JournalBackend::priorityName(7) != "Verbose")
        return 6;
    const auto summary = SystemInfoBackend::summary();
    if (summary.computerName.isEmpty() || summary.kernelVersion.isEmpty()
        || summary.cpu.isEmpty() || summary.memory == "0 B") {
        QTextStream(stderr) << "Invalid system summary: host='" << summary.computerName
                            << "' kernel='" << summary.kernelVersion << "' cpu='"
                            << summary.cpu << "' memory='" << summary.memory << "'\n";
        return 7;
    }
    QByteArray rootMount;
    rootMount.append('/');
    rootMount.append('\0');
    QByteArray dataMount("/mnt/data");
    dataMount.append('\0');
    const QStringList mountPoints = UDisksBackend::decodeMountPoints(
        QVariant::fromValue(QList<QByteArray>{rootMount, dataMount}));
    if (mountPoints != QStringList({"/", "/mnt/data"}))
        return 8;
    return 0;
}
