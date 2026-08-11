#pragma once

#include <QList>
#include <QString>

struct LocalUser { QString name, fullName, home, shell; uint uid = 0, gid = 0; };
struct LocalGroup { QString name; uint gid = 0; QStringList members; };

class AccountsBackend {
public:
    static QList<LocalUser> users();
    static QList<LocalGroup> groups();
};

