<a id="readme-top"></a>

<div align="center">

<img src="https://raw.githubusercontent.com/memegeko/aero7-shell/beta/docs/assets/aero7-logo.png" width="150" alt="Aero7 logo">

# Aero7 Computer Management

### Familiar system administration for Aero7 and Linux

A native Qt 6 administration console that brings real Linux management tools
together in one Aero7-style interface.

[![Arch Linux](https://img.shields.io/badge/Arch_Linux-supported-1793D1?logo=archlinux&logoColor=white)](https://archlinux.org/)
[![KDE Plasma](https://img.shields.io/badge/KDE_Plasma-6-1D99F3?logo=kde&logoColor=white)](https://kde.org/plasma-desktop/)
[![MIT License](https://img.shields.io/badge/license-MIT-2ea44f.svg)](LICENSE)

[Features](#features) ·
[Installation](#installation) ·
[Documentation](#documentation) ·
[Roadmap](docs/ROADMAP.md) ·
[Report a bug](https://github.com/memegeko/aero7-computer-management/issues/new)

</div>

---

**Aero7 Computer Management is an independent project and is not affiliated
with or endorsed by Microsoft Corporation. Windows is a trademark of the
Microsoft group of companies.**

> [!NOTE]
> Aero7 Computer Management is under active development. Read-only inspection
> and normal service actions are available now. Destructive partition editing
> is restricted to explicit, confirmed operations on non-critical disks and
> should still be exercised on disposable media before real data disks.

## About the project

Aero7 Computer Management is the advanced administration console for the
[Aero7](https://github.com/memegeko/aero7) Linux operating system. It recreates
the familiar organization of a classic Computer Management window while using
real Linux services, devices, accounts, disks, logs, and performance data.

The application does not emulate Windows APIs, invent system information, or
run its graphical interface as root. Each page talks to the appropriate Linux
backend and requests authentication only when an administrative action needs
it.

## Features

| Management tool | Aero7/Linux backend | Current functionality |
| --- | --- | --- |
| Overview | Linux system information | Hostname, Aero7 version, kernel, hardware, uptime, memory, disk, user, session, and network status |
| Task Scheduler | systemd timers | View system and user timers, inspect properties, enable or disable timers, run services, and manage basic user tasks |
| Event Viewer | systemd journal | Browse categorized events, search logs, open saved journals, and inspect full event details |
| Shared Folders | Samba | View configured shares, sessions, and open files, with user-share management when Samba is installed |
| Local Users and Groups | Linux accounts and groups | View accounts and groups and perform authenticated standard account operations |
| Performance Monitor | `/proc` and `/sys` | Live CPU, memory, swap, disk, network, process, and context-switch graphs with stable navigation panes |
| Device Manager | [linux-devmgmt](https://github.com/memegeko/linux-devmgmt) | Open the complete device tree or jump directly to a searchable hardware category |
| Disk Management | UDisks2 | Classic disk map, GPT/MBR initialization, simple-volume creation, formatting, extension, mount/unmount, rescan, and properties |
| Services | systemd | View system and user services, dependencies and logs, and perform service lifecycle actions |

### Disk Management

Disk Management presents real disks and partitions in a two-part layout: a
volume table above and a proportional graphical disk map below. Mounted paths,
file systems, capacity, free space, partition state, and removable media come
from UDisks2.

Safe actions such as refresh, rescan, properties, mount, unmount, and opening a
mount point are available. Initialize Disk, New Simple Volume, Format, and
Extend Volume use classic wizard flows with real UDisks2 operations. Deleting,
shrinking, dynamic-disk spanning, and RAID-5 volume creation remain
intentionally unavailable in this development milestone.

### Start-menu integration

Computer Management and each main module can be found from the Aero7 Start
menu. Application results appear first. Control Panel settings and Device
Manager hardware categories are then shown beneath their own clearly labelled
separators, avoiding duplicate or confusing KDE System Settings results.

Selecting a result such as **Disk Management**, **Event Viewer**, **Services**,
or **Storage Controllers** opens the matching page or hardware category
directly.

## Installation

Aero7 Computer Management is included with Aero7 and distributed through the
official [Aero7 Package Repository](https://github.com/memegeko/aero7-repo).

On an Aero7 system, install or update it with:

```bash
sudo pacman -S aero7-computer-management-git
```

Updates are delivered through the normal Aero7 system update process.

## Security

The graphical application always runs as the signed-in desktop user. systemd
and UDisks2 actions use their D-Bus interfaces and existing polkit policies.
Local account changes use a narrowly scoped helper that validates every
argument and permits only its documented account-management operations.

The application does not execute administrative commands through a shell,
edit the password database directly, or make the entire interface privileged.

## Project status

The current release includes working Linux backends for every main navigation
item. Automated tests cover the management pages, Start-menu catalogs, deep
links, navigation layout, and backend data handling. Releases are also tested
inside an Aero7 virtual machine through the same signed packages delivered to
installed systems.

Destructive storage operations are the main intentionally deferred area. See
the [roadmap](docs/ROADMAP.md) for the remaining work.

## Documentation

| Topic | Document |
| --- | --- |
| Application structure and security boundaries | [Architecture](docs/ARCHITECTURE.md) |
| Linux service and data mappings | [Backends](docs/BACKENDS.md) |
| Start-menu search and direct module links | [Start-menu integration](docs/START-MENU-INTEGRATION.md) |
| Automated and virtual-machine verification | [Testing](docs/TESTING.md) |
| Planned features and milestones | [Roadmap](docs/ROADMAP.md) |

## Related Aero7 projects

- [Aero7](https://github.com/memegeko/aero7) — the Aero7 operating system and installer
- [Aero7 Shell](https://github.com/memegeko/aero7-shell) — desktop shell and system integration
- [Aero7 Control Panel](https://github.com/memegeko/aero7-control-panel-) — everyday settings and configuration
- [Aero7 Package Repository](https://github.com/memegeko/aero7-repo) — signed packages and updates
- [linux-devmgmt](https://github.com/memegeko/linux-devmgmt) — Device Manager backend and interface

## Contributing

Bug reports, tested fixes, backend improvements, and documentation updates are
welcome. Please describe the real Linux API or service behind a proposed
management action and include safe failure handling for privileged operations.

## License

Aero7 Computer Management is distributed under the [MIT License](LICENSE).
Third-party components retain their own licenses and attribution; see
[THIRD_PARTY.md](THIRD_PARTY.md).

## Legal / Trademark Notice

Aero7 Computer Management is an independent open-source project and is not
affiliated with, authorized, sponsored, endorsed, or approved by Microsoft
Corporation.

Microsoft and Windows are trademarks of the Microsoft group of companies. All
other trademarks are the property of their respective owners. This project
recreates interface concepts and does not include or redistribute proprietary
Microsoft assets.

<p align="right">(<a href="#readme-top">back to top</a>)</p>
