#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>

#include <unistd.h>
#include <pwd.h>

namespace {
bool validName(const QString &value)
{
    static const QRegularExpression expression("^[a-z_][a-z0-9_-]{0,31}$");
    return expression.match(value).hasMatch();
}

bool safeText(const QString &value)
{
    return !value.contains('\0') && !value.contains('\n') && !value.contains('\r') && !value.contains(':');
}

bool protectedUser(const QString &name)
{
    const passwd *account = getpwnam(name.toLocal8Bit().constData());
    if (!account || account->pw_uid == 0) return true;
    const uid_t caller = qEnvironmentVariableIntValue("PKEXEC_UID");
    return caller != 0 && account->pw_uid == caller;
}

bool protectedGroup(const QString &name)
{
    return name == "root" || name == "wheel";
}

int run(const QString &program, const QStringList &arguments, const QByteArray &input = {})
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(3000)) {
        QTextStream(stderr) << "Could not start " << program << ".\n";
        return 70;
    }
    if (!input.isEmpty()) process.write(input);
    process.closeWriteChannel();
    if (!process.waitForFinished(60000)) {
        process.kill();
        QTextStream(stderr) << program << " timed out.\n";
        return 71;
    }
    if (process.exitCode() != 0)
        QTextStream(stderr) << QString::fromLocal8Bit(process.readAllStandardError()).trimmed() << '\n';
    return process.exitCode();
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    if (geteuid() != 0) {
        err << "This helper must be authorized through polkit.\n";
        return 77;
    }
    const QStringList args = app.arguments().mid(1);
    if (args.isEmpty()) {
        err << "No account operation was supplied.\n";
        return 64;
    }
    const QString action = args[0];
    auto namesValid = [&](std::initializer_list<int> indexes) {
        for (int index : indexes)
            if (index >= args.size() || !validName(args[index])) return false;
        return true;
    };

    if (action == "user-add" && args.size() == 5 && namesValid({1})
        && safeText(args[2]) && args[3].startsWith("/home/") && args[4].startsWith('/'))
        return run("/usr/sbin/useradd", {"-m", "-c", args[2], "-d", args[3], "-s", args[4], args[1]});
    if (action == "user-delete" && args.size() == 3 && namesValid({1}) && !protectedUser(args[1]) && (args[2] == "0" || args[2] == "1"))
        return run("/usr/sbin/userdel", args[2] == "1" ? QStringList{"-r", args[1]} : QStringList{args[1]});
    if (action == "user-rename" && args.size() == 3 && namesValid({1, 2}) && !protectedUser(args[1]))
        return run("/usr/sbin/usermod", {"-l", args[2], args[1]});
    if (action == "user-full-name" && args.size() == 3 && namesValid({1}) && safeText(args[2]))
        return run("/usr/sbin/usermod", {"-c", args[2], args[1]});
    if (action == "user-password" && args.size() == 2 && namesValid({1})) {
        QFile input;
        if (!input.open(stdin, QIODevice::ReadOnly)) {
            err << "The password input could not be read.\n";
            return 65;
        }
        const QByteArray password = input.readAll().trimmed();
        if (password.isEmpty() || password.contains('\n') || password.contains(':')) {
            err << "The password input is invalid.\n";
            return 65;
        }
        return run("/usr/sbin/chpasswd", {}, args[1].toUtf8() + ':' + password + '\n');
    }
    if ((action == "user-lock" || action == "user-unlock") && args.size() == 2 && namesValid({1}) && !protectedUser(args[1]))
        return run("/usr/sbin/usermod", {action == "user-lock" ? "-L" : "-U", args[1]});
    if (action == "user-add-group" && args.size() == 3 && namesValid({1, 2}))
        return run("/usr/sbin/usermod", {"-a", "-G", args[2], args[1]});
    if (action == "user-remove-group" && args.size() == 3 && namesValid({1, 2}))
        return run("/usr/bin/gpasswd", {"-d", args[1], args[2]});
    if (action == "group-add" && args.size() == 2 && namesValid({1}))
        return run("/usr/sbin/groupadd", {args[1]});
    if (action == "group-delete" && args.size() == 2 && namesValid({1}) && !protectedGroup(args[1]))
        return run("/usr/sbin/groupdel", {args[1]});
    if (action == "group-rename" && args.size() == 3 && namesValid({1, 2}) && !protectedGroup(args[1]))
        return run("/usr/sbin/groupmod", {"-n", args[2], args[1]});

    err << "Unsupported or invalid account operation.\n";
    return 64;
}
