#pragma once

#include <QList>
#include <QString>

struct LocalUser {
    QString name;
    QString fullName;
    QString home;
    QString shell;
    uint uid = 0;
    uint gid = 0;
    bool systemAccount = false;
    bool locked = false;
    bool lockKnown = false;
};

struct LocalGroup {
    QString name;
    QString description;
    uint gid = 0;
    QStringList members;
};

class AccountsBackend {
public:
    static QList<LocalUser> users();
    static QList<LocalGroup> groups();
    static QStringList groupsForUser(const QString &user);

    static bool createUser(const QString &name, const QString &fullName,
                           const QString &home, const QString &shell, QString *error = nullptr);
    static bool deleteUser(const QString &name, bool removeHome, QString *error = nullptr);
    static bool renameUser(const QString &name, const QString &newName, QString *error = nullptr);
    static bool changeFullName(const QString &name, const QString &fullName, QString *error = nullptr);
    static bool changePassword(const QString &name, const QString &password, QString *error = nullptr);
    static bool setLocked(const QString &name, bool locked, QString *error = nullptr);
    static bool addUserToGroup(const QString &name, const QString &group, QString *error = nullptr);
    static bool removeUserFromGroup(const QString &name, const QString &group, QString *error = nullptr);
    static bool createGroup(const QString &name, QString *error = nullptr);
    static bool deleteGroup(const QString &name, QString *error = nullptr);
    static bool renameGroup(const QString &name, const QString &newName, QString *error = nullptr);
};
