#include "model/NavigationHistory.h"
#include "model/NavigationNodes.h"

#include <QSet>

int main()
{
    const QStringList expected = {
        "overview", "task-scheduler", "event-viewer", "shared-folders",
        "local-users-groups", "users", "groups", "performance",
        "device-manager", "disk-management", "services"
    };
    if (NavigationNodes::validIds() != expected)
        return 1;
    QSet<QString> unique;
    for (const auto &node : NavigationNodes::all()) {
        if (node.id.isEmpty() || node.name.isEmpty() || unique.contains(node.id))
            return 2;
        unique.insert(node.id);
        if (!node.parentId.isEmpty() && !NavigationNodes::find(node.parentId))
            return 3;
    }
    if (NavigationNodes::isValid("system-tools") || NavigationNodes::isValid("unknown"))
        return 4;

    NavigationHistory history;
    history.visit("overview"); history.visit("services"); history.visit("users");
    if (!history.canGoBack() || history.back() != "services" || history.back() != "overview")
        return 5;
    if (history.forward() != "services")
        return 6;
    history.visit("event-viewer");
    if (history.canGoForward() || history.current() != "event-viewer")
        return 7;
    return 0;
}

