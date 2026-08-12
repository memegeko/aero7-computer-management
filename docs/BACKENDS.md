# Backend map

| Page | Linux source | Implemented behavior |
|---|---|---|
| Overview | `/etc/os-release`, uname, procfs, QStorageInfo, NetworkManager D-Bus | Truthful local-system summary and Control Panel link |
| Task Scheduler | systemd system/user D-Bus managers and unit files | Timer inventory, properties, enable/disable, run service, create/delete user tasks |
| Event Viewer | `journalctl -o json` through direct QProcess argument arrays | Categories, search, saved logs, field-level properties, per-unit service logs |
| Shared Folders | `testparm`, `smbstatus`, and `net usershare` | Shares, sessions, open files, safe user-share creation/removal |
| Users / Groups | libc `getpwent`/`getgrent`; shadow tools through polkit helper | Inventory, properties, user/group creation and authenticated maintenance |
| Performance Monitor | `/proc/stat`, `/proc/meminfo`, `/proc/diskstats`, `/proc/net/dev` | Asynchronous one-second sampling and a 120-sample selectable graph |
| Device Manager | external `devmgmt` | Availability check and launch without duplicating hardware scanning |
| Disk Management | UDisks2 over Qt D-Bus | Inventory, free-region modelling, GPT/MBR initialization, partition creation, filesystem formatting, same-disk extension, tracked mount folders, mount/unmount, rescan, and properties |
| Services | systemd system/user D-Bus managers | Inventory, startup state, dependencies, logs, lifecycle and unit-file actions |

The labels translate Linux concepts into the Aero7 interface; they do not
claim Windows API compatibility. There is no WMI, Windows Event Log, Service
Control Manager, or Windows Task Scheduler emulation.

## Privileged changes

- systemd and UDisks2 authenticate through their existing polkit policies.
- Account changes authenticate against
  `com.aero7.computermanagement.accounts` and execute a fixed helper command.
- The helper rejects invalid names and text, protects root/current accounts,
  and never invokes a shell.
- Samba user shares use the calling user's `net usershare` permissions.

## Disk-operation safety

The GUI keeps a UDisks object path, persistent block identifier, device number,
partition UUID/filesystem UUID, size, and offset snapshot. The backend reloads
the UDisks inventory and compares that identity immediately before every
mutation. It refuses read-only media and any drive containing `/`, `/boot`, or
`/boot/efi`. Each destructive flow has a final confirmation and every result is
verified from a fresh inventory.

UDisks2's `CanFormat` and `CanResize` determine which filesystems and extend
actions are enabled. Extension first grows the partition and then the
filesystem. If the second step fails, the UI reports the partial result rather
than claiming success. Dynamic-disk spanning, deletion, and shrink remain
deliberately deferred.
