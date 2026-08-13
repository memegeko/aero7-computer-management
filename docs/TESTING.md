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
- Disk Management lists physical disks, partitions, and unallocated extents.
  On disposable secondary VM disks, test blank GPT and MBR initialization,
  full and partial New Simple Volumes, every available filesystem, mount/no
  mount, reformat, partial/full extension, safe shrinking, confirmed deletion,
  startup-mount enable/disable, and cancellation at every page.
  Validate each result with `lsblk`, `blkid`, and UDisks2—not only the UI.
- Confirm Format, Extend, Shrink, Delete, and startup-mount changes are
  unavailable for the Aero7 root/boot disk and
  that stale identity, read-only, busy, missing-tool, and no-adjacent-space
  errors are human-readable.
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

Never exercise destructive tests on the installed Aero7 system disk or a host
data disk. The ISO repository's `--disk-management-fixture` QEMU option creates
disposable blank/partitioned secondary disks specifically for this matrix.

### Verified disposable-disk workflow (2026-08-12)

The multi-disk fixture was used to verify the following real UDisks2 path:

- Disk 1 (`/dev/vdb`) started as an 8 GiB blank disk; the installed Aero7
  system remained on protected Disk 0 (`/dev/vda`).
- GPT initialization produced an empty partition table and a fully
  unallocated disk.
- New Simple Volume created a 4 GiB ext4 partition labelled `TESTDATA`,
  leaving 4 GiB unallocated, and the result was checked with `lsblk` and
  `blkid`.
- Format changed the filesystem UUID and label to `reformatted`, then
  remounted it at the UDisks2-managed user-media path.
- Extend Volume added 2048 MiB. The partition became exactly 6 GiB, ext4 grew
  to 1,572,864 4096-byte blocks, the mount stayed usable, and 2 GiB remained
  unallocated.
- Keeping the New Simple Volume wizard open across the periodic refresh timer
  no longer invalidates its disk target or crashes the process.

For Shrink Volume, validate the filesystem before and after, confirm the new
unallocated extent matches the selected reduction (within partition alignment),
and verify a previously mounted volume is mounted again. For Mount at Startup,
confirm UDisks reports an `fstab` configuration item, reboot the disposable VM,
verify the `/mnt/aero7-*` mount, disable it through the UI, and verify the entry
is removed. Delete only a fixture partition and confirm both its UDisks object
and any Aero7-managed startup entry disappear.
