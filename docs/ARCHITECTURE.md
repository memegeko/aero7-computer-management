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

The graphical process never runs as root. systemd and UDisks2 changes use
their D-Bus APIs and polkit authorization. Local account changes cross a
separate, narrowly scoped polkit helper boundary; the helper validates its
arguments and can only invoke a fixed list of standard account tools.

Device Manager remains a separate program. Computer Management checks for and
launches `devmgmt`; it does not duplicate or embed that code.
