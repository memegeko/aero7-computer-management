#pragma once

#include <QStringList>

struct SambaShare { QString service, pid, machine, connectedAt; };

class SambaBackend {
public:
    static bool available();
    static QList<SambaShare> shares(QString *error = nullptr);
};

