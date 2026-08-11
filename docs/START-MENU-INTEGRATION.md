# Start-menu search integration

Computer Management owns its search catalog. It does not add entries to Aero7
Control Panel and does not require Control Panel to route its pages.

```console
aero7-compmgmt --list-settings-json
```

The command prints one JSON object per selectable management page using the
same stable IDs accepted by `--open`. A Start-menu search provider can refresh
from that command and launch a selected result with:

```console
aero7-compmgmt --open event-viewer
```

Only `aero7-computer-management.desktop` is installed as a normal application
shortcut. The page entries remain search results rather than separate items
in All Programs.

| Search result | Stable ID |
|---|---|
| Computer Management | `overview` |
| Task Scheduler | `task-scheduler` |
| Event Viewer | `event-viewer` |
| Shared Folders | `shared-folders` |
| Local Users and Groups | `local-users-groups` |
| Users | `users` |
| Groups | `groups` |
| Performance Monitor | `performance` |
| Device Manager | `device-manager` |
| Disk Management | `disk-management` |
| Services | `services` |

