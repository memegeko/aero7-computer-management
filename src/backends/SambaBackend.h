#pragma once

#include <QList>
#include <QString>

struct SambaShare {
    QString name;
    QString path;
    QString type;
    QString comment;
    bool userShare = false;
};

struct SambaSession {
    QString user;
    QString computer;
    QString clientIp;
    QString protocol;
    QString connectedSince;
    QString pid;
};

struct SambaOpenFile {
    QString file;
    QString user;
    QString client;
    QString accessMode;
    QString pid;
};

class SambaBackend {
public:
    static bool available();
    static QList<SambaShare> shares(QString *error = nullptr);
    static QList<SambaSession> sessions(QString *error = nullptr);
    static QList<SambaOpenFile> openFiles(QString *error = nullptr);
    static bool createUserShare(const QString &name, const QString &path,
                                const QString &comment, QString *error = nullptr);
    static bool removeUserShare(const QString &name, QString *error = nullptr);
};
