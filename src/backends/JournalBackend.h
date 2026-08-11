#pragma once

#include <QList>
#include <QMap>
#include <QString>

struct JournalEntry {
    QString time;
    QString priority;
    QString source;
    QString message;
    QString pid;
    QString uid;
    QString unit;
    QString executable;
    QString bootId;
    QMap<QString, QString> fields;
};

class JournalBackend {
public:
    static QList<JournalEntry> recent(QString *error = nullptr, int limit = 250);
    static QList<JournalEntry> query(const QString &category, const QString &search,
                                     QString *error = nullptr, int limit = 500);
    static QList<JournalEntry> saved(const QString &path, QString *error = nullptr, int limit = 500);
    static QList<JournalEntry> forUnit(const QString &unit, bool userUnit,
                                       QString *error = nullptr, int limit = 100);
    static QString priorityName(int priority);
};
