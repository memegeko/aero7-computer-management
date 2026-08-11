# Architecture

The executable is split into four layers:

1. `model/` owns stable navigation IDs and browser-style history.
2. `backends/` reads Linux services without depending on UI classes.
3. `ui/ManagementPage` defines the refresh/action contract shared by pages.
4. `ui/MainWindow` owns the console tree, page host, action pane, menus,
   toolbar, status bar, and persisted window state.

The tree and CLI use the same stable IDs. A deep link such as
`aero7-compmgmt --open services` therefore reaches the same page as clicking
Services in the tree. Group nodes are intentionally not directly routable.

The process is never meant to run as root. Privileged actions belong behind a
reviewed system service or polkit boundary. The testing milestone exposes only
read operations plus UDisks2 mount/unmount, which already uses the system's
authorization policy.

Device Manager remains a separate program. Computer Management checks for and
launches `devmgmt`; it does not duplicate or embed that code.

