# Control Panel and Start-menu integration

Aero7 Control Panel exposes a JSON settings catalog through:

```console
controlpanel --list-settings-json
```

The Aero7 Start menu consumes that catalog for its **Settings** search result
group. Computer Management pages are registered as external-command settings:

```console
controlpanel --setting management-event-viewer
# launches: aero7-compmgmt --open event-viewer
```

This produces searchable settings without adding eleven application launchers
to All Programs. Only `aero7-computer-management.desktop` is installed as a
normal application shortcut.

| Search result | Node ID |
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

`Disk Cleanup` is catalogued separately as `aero7-cleanmgr`. If that program
is absent, the Control Panel must report it as unavailable rather than opening
an unrelated page.

