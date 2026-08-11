#include "AccountsBackend.h"

#include <grp.h>
#include <pwd.h>

QList<LocalUser> AccountsBackend::users()
{
    QList<LocalUser> out;
    setpwent();
    while (passwd *entry = getpwent()) {
        const QString gecos = QString::fromLocal8Bit(entry->pw_gecos ? entry->pw_gecos : "");
        out.push_back({QString::fromLocal8Bit(entry->pw_name), gecos.section(',', 0, 0),
                       QString::fromLocal8Bit(entry->pw_dir), QString::fromLocal8Bit(entry->pw_shell),
                       entry->pw_uid, entry->pw_gid});
    }
    endpwent();
    return out;
}

QList<LocalGroup> AccountsBackend::groups()
{
    QList<LocalGroup> out;
    setgrent();
    while (group *entry = getgrent()) {
        QStringList members;
        if (entry->gr_mem)
            for (char **member = entry->gr_mem; *member; ++member)
                members << QString::fromLocal8Bit(*member);
        out.push_back({QString::fromLocal8Bit(entry->gr_name), entry->gr_gid, members});
    }
    endgrent();
    return out;
}

