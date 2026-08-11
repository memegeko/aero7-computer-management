# Backend map

| Page | Linux source | Current behavior |
|---|---|---|
| Task Scheduler | `systemctl list-timers` | Read-only timer inventory |
| Event Viewer | `journalctl -o json` | Recent journal events; permissions apply |
| Shared Folders | `smbstatus --shares` | Active Samba sessions; clear unavailable state |
| Users / Groups | libc `getpwent` / `getgrent` | Read-only local account database |
| Performance Monitor | `/proc/loadavg`, `/proc/meminfo`, `/proc/uptime` | Live two-second refresh |
| Device Manager | external `devmgmt` | Availability check and launch |
| Disk Management | UDisks2 over Qt DBus | Inventory and safe mount/unmount |
| Services | `systemctl list-units --type=service` | Read-only unit inventory |

Commands are started with argument arrays, never through a shell. Missing
optional programs do not turn into empty or fake results: the page describes
which backend is unavailable.

The labels are Linux translations rather than claims of Windows API
compatibility. There is no WMI, Windows Event Log, Task Scheduler service, or
Service Control Manager emulation.

