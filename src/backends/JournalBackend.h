#pragma once

#include <QList>
#include <QString>

struct JournalEntry { QString time, priority, unit, message; };

class JournalBackend {
public:
    static QList<JournalEntry> recent(QString *error = nullptr, int limit = 250);
};

