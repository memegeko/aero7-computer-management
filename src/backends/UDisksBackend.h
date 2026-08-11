#pragma once

#include <QList>
#include <QString>

struct BlockDevice {
    QString objectPath, device, label, fileSystem, mountPoint;
    quint64 size = 0;
    bool mountable = false;
};

class UDisksBackend {
public:
    static bool available();
    static QList<BlockDevice> devices(QString *error = nullptr);
    static bool mount(const QString &objectPath, QString *error = nullptr);
    static bool unmount(const QString &objectPath, QString *error = nullptr);
};

