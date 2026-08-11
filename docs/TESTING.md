# Testing

## Automated checks

```console
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

The suite validates navigation and history, CLI deep links and search JSON,
desktop/polkit packaging, systemd output translation, journal priority
translation, byte formatting, libc account enumeration, and the local system
summary.

## Aero7 VM checks

Run the GUI as the normal desktop user. Do not start it with `sudo`.

- Root overview shows non-empty hostname, CPU, memory, disk, user, session,
  and network values.
- Every tree item and container is selectable, Back/Forward history works,
  and the Actions pane always belongs to the visible page.
- Task Scheduler lists system and user timers; Properties, enable/disable,
  Run Associated Task, and creation/deletion of a disposable user task work.
- Event Viewer loads every category, Find/filter work, saved journal files can
  be opened, and double-click shows the real journal fields.
- Shared Folders works with Samba installed and displays the explicit
  not-installed state when Samba is absent.
- Users and Groups inventory works unprivileged. On a disposable VM, create,
  modify, lock/unlock, and delete a temporary account and group. Confirm that
  root and the signed-in account cannot be deleted or renamed.
- Performance Monitor remains responsive while CPU, memory, swap, disk, and
  network counters update once per second.
- Device Manager launches `devmgmt` when installed and reports its absence
  without crashing otherwise.
- Disk Management lists physical disks and volumes, Properties works, and a
  non-system test volume can be mounted/unmounted through UDisks2.
- Services lists system and user services; Properties includes dependencies
  and logs. Test lifecycle/unit-file actions only on a disposable service.

Useful direct launches:

```console
./build/src/aero7-compmgmt --open task-scheduler
./build/src/aero7-compmgmt --open event-viewer
./build/src/aero7-compmgmt --open shared-folders
./build/src/aero7-compmgmt --open users
./build/src/aero7-compmgmt --open performance
./build/src/aero7-compmgmt --open disk-management
./build/src/aero7-compmgmt --open services
```

Disk creation, deletion, formatting, and resizing are intentionally absent
from this milestone. Test only mount/unmount against non-system media.
