# Testing

Run the normal build and test sequence:

```console
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

The automated suite covers stable node IDs, navigation history, CLI routing,
systemd output translation, byte formatting, and libc account enumeration.
Backend pages should additionally be tested in an Aero7 VM with:

- systemd and journald running;
- UDisks2 on the system bus;
- `devmgmt` installed;
- Samba both installed and absent, to exercise the optional-backend state.

Launch individual pages during visual testing:

```console
./build/src/aero7-compmgmt --open task-scheduler
./build/src/aero7-compmgmt --open event-viewer
./build/src/aero7-compmgmt --open disk-management
```

Do not run the application as root. Disk Management deliberately has no
format, delete, partition, or resize controls.

