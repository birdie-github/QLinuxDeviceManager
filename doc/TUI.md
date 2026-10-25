# FINAL CUT terminal proof of concept

Install FINAL CUT >= 0.9.0 with its development headers and `finalcut.pc`.
Use your distribution's development package where available; otherwise follow
FINAL CUT's upstream installation instructions at https://github.com/gansm/finalcut.
Check discovery with `pkg-config --modversion finalcut`. For a custom prefix,
add its pkgconfig directory to `PKG_CONFIG_PATH`.

The existing project development dependencies are still required to configure
this combined build. A desktop session is not required to run the TUI.

```sh
cmake -S . -B build -DTUI=ON
cmake --build build --target qlinuxdevicemanager qlinuxdevicemanager-tui
./build/qlinuxdevicemanager-tui
```

TUI defaults to OFF. Missing FINAL CUT skips only the TUI target with a warning;
the GUI retains its existing installation conventions.

F2 switches devices by type / connection. Arrow keys navigate, Right/Left
expand/collapse, Enter (or the native mouse activation) opens properties.
General shows inventory fields, udev properties and captured kernel attributes;
Driver shows the direct binding and observed module ownership. Alt+D / Alt+G
switch the property page; Close or Escape returns. Text views scroll vertically
and horizontally, so long values remain accessible. F5 takes a new synchronous
snapshot; F10 quits. Category nodes start collapsed. Start at 80x25 or larger.
Resizing adjusts tree and property view geometry.

This is a read-only snapshot utility. It creates no monitor, daemon or hotplug
subscription. Startup and manual refresh enumerate synchronously; refresh can
briefly block navigation. Properties describe that snapshot, including devices
that may since have disappeared. Refresh resets selection and expansion.
No absent binding/module/status value is treated as proof of good health or
built-in driver status. Raw property/attribute source keys are retained.

## Source boundaries

- `src/core/`: synchronous inventory entry point and shared connection projection.
- `src/devices/`: existing toolkit-independent records, naming, classification
  and collectors. The subset needed by enumeration is in `qldm-core`, linked
  against QtCore, libudev and optionally libkmod. No frontend widgets belong here.
- `src/ui/` and `src/main.cpp`: existing Qt Widgets frontend and GUI monitoring.
- `src/tui/`: native FINAL CUT tree and dialogs. No Qt Widgets linkage.

A QCoreApplication instance provides Qt utility/translation context, but its
event loop is never run. FINAL CUT owns terminal setup and event dispatch.
Both executables use the same discovery and classification implementation.
Connection edges use the GUI's existing directory-ancestry bridging, hidden
ancestor retention and separate UEFI firmware-record presentation group.
The TUI omits hidden records except ancestors needed for connection context.

Hardware-specific collectors can later be shared individually where independent
of widgets. Their presentation needs separate FINAL CUT pages. System Information
will similarly need its own text presentation; neither requires replacing the
inventory backend. GUI-only asynchronous/session collectors need a separate
review before reuse without Qt event dispatch.

## Maintainer validation checklist

Compilation and runtime validation are deliberately left to the maintainer.
Build both targets; also configure with TUI off and with FINAL CUT absent.
Compare visible categories and device parents with the GUI on the same host.
Open two distinctly named devices and verify their paths/bindings differ as
expected. Inspect unbound and permission-restricted records. Exercise expand,
collapse, Enter, mouse activation, both property pages and scrolling of long
values. Resize at 80x25 and larger. Refresh after a hotplug change and confirm
no automatic change occurs before refresh. Close properties, then quit normally.
