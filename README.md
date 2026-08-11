# Aero7 Computer Management

Aero7 Computer Management is an Aero7-native administration console for
Linux. It brings system tools, logs, accounts, performance, storage, devices,
and services into one familiar three-pane window while using real Linux
backends.

> **Testing software:** this project is under active development. Pages expose
> only operations that can be represented safely and truthfully on Linux.
> Destructive disk editing is deliberately not part of the first release.

## Planned management pages

- Task Scheduler (systemd timers)
- Event Viewer (system journal)
- Shared Folders (Samba)
- Local Users and Groups (libc account database)
- Performance Monitor (`/proc` and `/sys`)
- Device Manager (the external `devmgmt` application)
- Disk Management (UDisks2)
- Services (systemd units)

Every page has a stable command-line deep link, for example:

```console
aero7-compmgmt --open event-viewer
```

These IDs are also used by Aero7 Control Panel and Start-menu Settings search.

## Build

```console
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Requirements: CMake 3.24+, Ninja, a C++17 compiler, Qt 6 Widgets, and Qt 6
DBus. See `docs/TESTING.md` for backend-specific test notes.

## License

MIT. See [LICENSE](LICENSE) and [THIRD_PARTY.md](THIRD_PARTY.md).

