# Start-menu search integration

Computer Management is a separate application. It does not add entries to
Aero7 Control Panel and does not require Control Panel to route its pages.

```console
aero7-compmgmt --list-settings-json
```

The command prints one JSON object per selectable management page using the
same stable IDs accepted by `--open`. A Start-menu search provider can refresh
from that command and launch a selected result with:

```console
aero7-compmgmt --open event-viewer
```

The main console and every selectable management page install normal desktop
entries. This puts them in the Start menu's Applications section, makes them
searchable through the standard applications runner, and gives each page its
matching Aero-compatible system icon. Opening a page entry launches the main
console directly at that page.

| Application | Stable ID | Theme icon |
|---|---|---|
| Computer Management | `overview` | `computer` |
| Task Scheduler | `task-scheduler` | `appointment-new` |
| Event Viewer | `event-viewer` | `view-list-details` |
| Shared Folders | `shared-folders` | `folder-network` |
| Local Users and Groups | `local-users-groups` | `system-users` |
| Users | `users` | `user-identity` |
| Groups | `groups` | `system-users` |
| Performance Monitor | `performance` | `utilities-system-monitor` |
| Device Manager | `device-manager` | `preferences-system-devices` |
| Disk Management | `disk-management` | `drive-harddisk` |
| Services | `services` | `preferences-system-services` |
