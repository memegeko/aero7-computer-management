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
    BlockDevice disk;
    disk.objectPath = "/org/freedesktop/UDisks2/block_devices/vdb";
    disk.driveObjectPath = "/org/freedesktop/UDisks2/drives/vdb";
    disk.stableId = "fixture-disk";
    disk.deviceNumber = 1024;
    disk.size = 100ULL * 1024ULL * 1024ULL;
    disk.partitionable = true;
    disk.partitionTable = "gpt";
    BlockDevice first;
    first.objectPath = "/org/freedesktop/UDisks2/block_devices/vdb1";
    first.driveObjectPath = disk.driveObjectPath;
    first.partitionTableObjectPath = disk.objectPath;
    first.partition = true;
    first.partitionOffset = 1ULL * 1024ULL * 1024ULL;
    first.size = 39ULL * 1024ULL * 1024ULL;
    first.fileSystem = "ext4";
    BlockDevice second = first;
    second.objectPath = "/org/freedesktop/UDisks2/block_devices/vdb2";
    second.partitionOffset = 60ULL * 1024ULL * 1024ULL;
    second.size = 20ULL * 1024ULL * 1024ULL;
    const QList<BlockDevice> layout{disk, first, second};
    const QList<DiskFreeRegion> gaps = UDisksBackend::freeRegions(layout);
    if (gaps.size() != 2 || gaps[0].offset != 40ULL * 1024ULL * 1024ULL
        || gaps[0].size != 20ULL * 1024ULL * 1024ULL
        || gaps[1].offset != 80ULL * 1024ULL * 1024ULL
        || gaps[1].size != 20ULL * 1024ULL * 1024ULL)
        return 9;
    const auto adjacent = UDisksBackend::adjacentFreeRegion(layout, first);
    if (!adjacent || adjacent->offset != gaps[0].offset || adjacent->size != gaps[0].size)
        return 10;
    if (UDisksBackend::adjacentFreeRegion(layout, second)->offset != gaps[1].offset)
        return 11;
    const DiskOperationTarget target = UDisksBackend::targetFor(first);
    if (target.objectPath != first.objectPath || target.deviceNumber != first.deviceNumber
        || !target.partition || target.offset != first.partitionOffset)
        return 12;
    if (UDisksBackend::available()) {
        QString capabilityError;
        const auto capabilities = UDisksBackend::fileSystemCapabilities(&capabilityError);
        if (capabilities.isEmpty() || !capabilityError.isEmpty()) return 13;
        bool foundExt4 = false;
        for (const auto &capability : capabilities)
            if (capability.type == "ext4") foundExt4 = true;
        if (!foundExt4) return 14;
    }
    return 0;
}
