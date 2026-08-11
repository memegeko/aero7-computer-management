#include "backends/AccountsBackend.h"
#include "backends/SystemdBackend.h"
#include "util/Format.h"

#include <QSet>

int main()
{
    const auto timers = SystemdBackend::parseTimers(
        "Tue 2026-08-11 18:00:00 CEST  9min left  Tue 2026-08-11 17:00:00 CEST  50min ago  pkg.timer  pkg.service\n");
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
    return 0;
}

