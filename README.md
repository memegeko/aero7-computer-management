# Aero7 Computer Management

Aero7 Computer Management is a Windows 7-inspired administration console for
Aero7 and Linux. It presents familiar management pages while reading and
changing the real Linux system underneath. It does not emulate Windows APIs,
invent data, or run the graphical application as root.

## Implemented pages

- Computer Management overview: hostname, Aero7/Arch versions, kernel,
  architecture, uptime, CPU, RAM, root disk, session, user, and network state.
- Task Scheduler: system and user systemd timers, properties, enable/disable,
  manual service runs, and creation/deletion of basic user timer tasks.
- Event Viewer: journald categories, search, saved journal files, priority
  translation, and complete event properties.
- Shared Folders: configured Samba shares, sessions, open files, and Samba
  user-share creation/removal when Samba is installed.
- Local Users and Groups: libc account inventory plus authenticated standard
  account and group operations through a narrowly scoped polkit helper.
- Performance Monitor: asynchronous CPU, memory, swap, disk, network,
  process, and context-switch sampling with selectable live graph counters.
- Device Manager: integration with the existing `devmgmt` hardware tool.
- Disk Management: UDisks2 disk/volume inventory, properties, rescan,
  mount/unmount, and mount-point opening.
- Services: system and user systemd units, dependencies, recent journal logs,
  and start/stop/restart/reload/enable/disable/mask actions over D-Bus.

Destructive partition editing is intentionally disabled until its UDisks2 and
system-partition protection path receives separate hardware testing.

Every tool supports a stable command-line deep link, for example:

```console
aero7-compmgmt --open event-viewer
aero7-compmgmt --open disk-management
aero7-compmgmt --open services
```

The Aero7 Start menu consumes the standalone search catalog printed by:

```console
aero7-compmgmt --list-settings-json
```

## Build and test

```console
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Requirements: CMake 3.24+, C++17, Qt 6 Widgets/DBus/Concurrent, systemd,
UDisks2, polkit, and shadow. Samba and linux-devmgmt are optional integrations.

## Security model

The GUI runs as the desktop user. systemd and UDisks2 actions use their D-Bus
interfaces and existing polkit policies. Local account changes invoke only
`/usr/lib/aero7/aero7-compmgmt-helper` through polkit; that helper validates
every argument and permits only its fixed set of standard account-tool calls.
No backend uses `sh -c`, edits `/etc/passwd` directly, or executes `sudo`.

## License

MIT. See [LICENSE](LICENSE) and [THIRD_PARTY.md](THIRD_PARTY.md).
