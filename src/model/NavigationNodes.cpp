#include "NavigationNodes.h"

namespace NavigationNodes {

const QList<NavigationNode> &all()
{
    static const QList<NavigationNode> nodes = {
        {"overview", "Computer Management (Local)", "computer", {}},
        {"system-tools", "System Tools", "applications-system", "overview", true, false},
        {"task-scheduler", "Task Scheduler", "appointment-new", "system-tools"},
        {"event-viewer", "Event Viewer", "view-list-details", "system-tools"},
        {"shared-folders", "Shared Folders", "folder-network", "system-tools"},
        {"local-users-groups", "Local Users and Groups", "system-users", "system-tools"},
        {"users", "Users", "user-identity", "local-users-groups"},
        {"groups", "Groups", "system-users", "local-users-groups"},
        {"performance", "Performance Monitor", "utilities-system-monitor", "system-tools"},
        {"device-manager", "Device Manager", "preferences-system-devices", "system-tools"},
        {"storage", "Storage", "drive-harddisk", "overview", true, false},
        {"disk-management", "Disk Management", "drive-harddisk", "storage"},
        {"services-applications", "Services and Applications", "preferences-system", "overview", true, false},
        {"services", "Services", "preferences-system-services", "services-applications"},
    };
    return nodes;
}

const NavigationNode *find(const QString &id)
{
    for (const auto &node : all())
        if (node.id == id)
            return &node;
    return nullptr;
}

bool isValid(const QString &id)
{
    const auto *node = find(id);
    return node && node->selectable;
}

QStringList validIds()
{
    QStringList ids;
    for (const auto &node : all())
        if (node.selectable)
            ids << node.id;
    return ids;
}

} // namespace NavigationNodes
