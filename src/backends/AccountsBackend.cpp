#include "AccountsBackend.h"

#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>

#include <grp.h>
#include <pwd.h>
#include <shadow.h>
#include <unistd.h>

#include <algorithm>

namespace {
QString knownGroupDescription(const QString &name)
{
    if (name == "wheel") return "Users allowed administrative sudo privileges when configured by the system.";
    if (name == "audio") return "Audio-related device access where this compatibility group is used.";
    if (name == "video") return "Video and graphics device access where this compatibility group is used.";
    if (name == "storage") return "Storage-device access where this compatibility group is used.";
    return {};
}

QString helperPath()
{
    const QString configured = qEnvironmentVariable("AERO7_COMPMGMT_HELPER");
    if (!configured.isEmpty()) return configured;
    return "/usr/lib/aero7/aero7-compmgmt-helper";
}

bool privileged(const QStringList &arguments, const QByteArray &input, QString *error)
{
    const QString pkexec = QStandardPaths::findExecutable("pkexec");
    const QString helper = helperPath();
    if (pkexec.isEmpty()) {
        if (error) *error = "polkit's pkexec command is not installed.";
        return false;
    }
    if (!QFileInfo::exists(helper)) {
        if (error) *error = "The Aero7 account-management helper is not installed.";
        return false;
    }
    QProcess process;
    process.start(pkexec, QStringList{helper} + arguments);
    if (!process.waitForStarted(3000)) {
        if (error) *error = "The authorization helper could not be started.";
        return false;
    }
    if (!input.isEmpty()) process.write(input);
    process.closeWriteChannel();
    if (!process.waitForFinished(120000)) {
        process.kill();
        if (error) *error = "The account-management authorization request timed out.";
        return false;
    }
    if (process.exitCode() != 0) {
        QString message = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (message.isEmpty()) message = "The account operation was canceled or failed.";
        if (error) *error = message;
        return false;
    }
    return true;
}
}

QList<LocalUser> AccountsBackend::users()
{
    QList<LocalUser> result;
    setpwent();
    while (passwd *entry = getpwent()) {
        const QString name = QString::fromLocal8Bit(entry->pw_name);
        const QString gecos = QString::fromLocal8Bit(entry->pw_gecos ? entry->pw_gecos : "");
        LocalUser user;
        user.name = name;
        user.fullName = gecos.section(',', 0, 0);
        user.home = QString::fromLocal8Bit(entry->pw_dir);
        user.shell = QString::fromLocal8Bit(entry->pw_shell);
        user.uid = entry->pw_uid;
        user.gid = entry->pw_gid;
        user.systemAccount = entry->pw_uid < 1000 && entry->pw_uid != getuid();
        if (spwd *shadow = getspnam(entry->pw_name)) {
            const QString hash = QString::fromLocal8Bit(shadow->sp_pwdp ? shadow->sp_pwdp : "");
            user.lockKnown = true;
            user.locked = hash.startsWith('!') || hash.startsWith('*');
        }
        result << user;
    }
    endpwent();
    std::sort(result.begin(), result.end(), [](const LocalUser &a, const LocalUser &b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return result;
}

QList<LocalGroup> AccountsBackend::groups()
{
    QList<LocalGroup> result;
    setgrent();
    while (group *entry = getgrent()) {
        QStringList members;
        if (entry->gr_mem)
            for (char **member = entry->gr_mem; *member; ++member)
                members << QString::fromLocal8Bit(*member);
        const QString name = QString::fromLocal8Bit(entry->gr_name);
        result.push_back({name, knownGroupDescription(name), entry->gr_gid, members});
    }
    endgrent();
    std::sort(result.begin(), result.end(), [](const LocalGroup &a, const LocalGroup &b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return result;
}

QStringList AccountsBackend::groupsForUser(const QString &user)
{
    const passwd *account = getpwnam(user.toLocal8Bit().constData());
    if (!account) return {};
    QStringList result;
    const gid_t primary = account->pw_gid;
    setgrent();
    while (group *entry = getgrent()) {
        bool member = entry->gr_gid == primary;
        if (entry->gr_mem)
            for (char **name = entry->gr_mem; *name; ++name)
                member = member || user == QString::fromLocal8Bit(*name);
        if (member) result << QString::fromLocal8Bit(entry->gr_name);
    }
    endgrent();
    result.removeDuplicates();
    result.sort(Qt::CaseInsensitive);
    return result;
}

bool AccountsBackend::createUser(const QString &name, const QString &fullName,
                                 const QString &home, const QString &shell, QString *error)
{ return privileged({"user-add", name, fullName, home, shell}, {}, error); }
bool AccountsBackend::deleteUser(const QString &name, bool removeHome, QString *error)
{ return privileged({"user-delete", name, removeHome ? "1" : "0"}, {}, error); }
bool AccountsBackend::renameUser(const QString &name, const QString &newName, QString *error)
{ return privileged({"user-rename", name, newName}, {}, error); }
bool AccountsBackend::changeFullName(const QString &name, const QString &fullName, QString *error)
{ return privileged({"user-full-name", name, fullName}, {}, error); }
bool AccountsBackend::changePassword(const QString &name, const QString &password, QString *error)
{ return privileged({"user-password", name}, password.toUtf8() + '\n', error); }
bool AccountsBackend::setLocked(const QString &name, bool locked, QString *error)
{ return privileged({locked ? "user-lock" : "user-unlock", name}, {}, error); }
bool AccountsBackend::addUserToGroup(const QString &name, const QString &group, QString *error)
{ return privileged({"user-add-group", name, group}, {}, error); }
bool AccountsBackend::removeUserFromGroup(const QString &name, const QString &group, QString *error)
{ return privileged({"user-remove-group", name, group}, {}, error); }
bool AccountsBackend::createGroup(const QString &name, QString *error)
{ return privileged({"group-add", name}, {}, error); }
bool AccountsBackend::deleteGroup(const QString &name, QString *error)
{ return privileged({"group-delete", name}, {}, error); }
bool AccountsBackend::renameGroup(const QString &name, const QString &newName, QString *error)
{ return privileged({"group-rename", name, newName}, {}, error); }
