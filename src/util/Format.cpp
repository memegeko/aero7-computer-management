#include "Format.h"

namespace Format {
QString bytes(quint64 value)
{
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double size = value;
    int unit = 0;
    while (size >= 1024.0 && unit < 4) { size /= 1024.0; ++unit; }
    return QString::number(size, unit == 0 ? 'f' : 'f', unit == 0 ? 0 : 1)
        + ' ' + units[unit];
}

QString uptime(quint64 seconds)
{
    const quint64 days = seconds / 86400;
    seconds %= 86400;
    const quint64 hours = seconds / 3600;
    const quint64 minutes = (seconds % 3600) / 60;
    return QString("%1d %2h %3m").arg(days).arg(hours).arg(minutes);
}
}

