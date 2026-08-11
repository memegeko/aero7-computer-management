# Roadmap

## 0.2 real-backend milestone

- Real system summary from os-release, uname, procfs, UDisks2, and
  NetworkManager.
- systemd D-Bus timer and service inventory, properties, and authenticated
  unit operations.
- Basic user task creation as a genuine `.timer` and `.service` pair.
- journald event categories, filtering, saved logs, and event properties.
- Samba shares, sessions, open files, and user-share maintenance.
- Linux users and groups with narrowly scoped, polkit-authenticated changes.
- Live asynchronous performance counters from procfs.
- Existing `devmgmt` integration and UDisks2 disk/volume management.
- Stable deep links and Start-menu search metadata for every management tool.

## Deliberately deferred

- Destructive partition creation, deletion, formatting, and resizing. These
  require a separate UDisks2 safety design and real-hardware test matrix.
- Closing active Samba sessions or remote files. Samba does not expose a
  stable unprivileged API for this operation, so it will need a separately
  reviewed privileged helper.
- System-wide task authoring. User tasks are supported now; writing system
  unit files requires a dedicated polkit action and validation model.
- Advanced event-log export/reporting and saved performance-counter reports.

Deferred controls are not shown as decorative or non-working buttons.
