#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

struct BlockDevice {
    QString objectPath;
    QString driveObjectPath;
    QString device;
    QString driveModel;
    QString driveVendor;
    QString connectionBus;
    QString serial;
    QString label;
    QString fileSystem;
    QString uuid;
    QString partUuid;
    QString partitionTable;
    QString mountPoint;
    QStringList mountPoints;
    quint64 size = 0;
    quint64 freeBytes = 0;
    quint64 partitionOffset = 0;
    uint partitionNumber = 0;
    bool mountable = false;
    bool freeSpaceKnown = false;
    bool removable = false;
    bool readOnly = false;
    bool partition = false;
    bool systemDevice = false;
    bool optical = false;
};

class UDisksBackend {
public:
    static QStringList decodeMountPoints(const QVariant &value);
    static bool available();
    static QList<BlockDevice> devices(QString *error = nullptr);
    static bool mount(const QString &objectPath, QString *error = nullptr);
    static bool unmount(const QString &objectPath, QString *error = nullptr);
    static bool rescan(const QString &objectPath, QString *error = nullptr);
};
