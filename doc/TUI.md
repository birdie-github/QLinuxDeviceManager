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

## Static FINAL CUT builds

A static `libfinal.a` is supported; its dependencies must also appear on the
executable's link line. Some FINAL CUT installations supply a `finalcut.pc`
containing only `-lfinal`, without the private dependencies needed for static
linking. For a build using GPM and a separate libtinfo, configure with:

```sh
cmake -S . -B build -DTUI=ON -DTUI_FINALCUT_EXTRA_LIBRARIES="gpm;tinfo"
cmake --build build --target qlinuxdevicemanager-tui
```

The list is added after FINAL CUT to preserve static-library link ordering.
Install the development/linker files for these dependency libraries as needed.
If FINAL CUT was configured with another terminal-capability provider, supply
that library instead of `tinfo`. Omit `gpm` when FINAL CUT was built without GPM.
Library names or absolute library paths are accepted as a CMake list.
This option supplies dependencies; it does not force archive selection when
both shared and static FINAL CUT libraries are installed. With only
`libfinal.a` installed, the existing discovery selects that archive.
Embedding FINAL CUT removes its runtime library dependency, while Qt and any
GPM/terminal libraries linked dynamically remain runtime dependencies.

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
that may since have disappeared. Unfiltered selection and expansion are
remembered separately for each view, using device paths and group identities.
Removed selections fall back to the computer row.
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

## Filtering devices

F4 focuses a persistent Filter field above the tree. Typing a case-insensitive
substring immediately filters the current projection. Matching devices and
their ancestors remain; all retained branches expand so results are visible.
The status bar reports the number of matching devices, excluding context-only
ancestors. With no matches, only the computer row remains.

Matching uses names, identifiers, driver/module names, paths, category/subsystem,
udev property values and available captured attribute values. It performs no
additional hardware reads. Hidden records omitted from the normal view remain
excluded; retained connection-context ancestors are eligible for matching.
This does not include the GUI's hardware-specific deep-property collection.

Enter in the field selects a match and focuses the tree; Enter on a device opens
properties. F3/F6 cycle matches forward/backward with wraparound. Escape clears
the filter and focuses the tree. Backspacing to an empty query also restores the
full projection and its pre-filter selection and expansion. Whitespace alone
means no filter. The query remains active across F2 and F5. Each view retains
its own unfiltered state; paths absent after a refresh are skipped on restore.
Header sorting resets when the tree is rebuilt.

The TUI source is separated by responsibility:

- `main.cpp`: QtCore context, initial enumeration and FINAL CUT startup.
- `browser.cpp`: controls, callbacks and match navigation.
- `treeprojection.cpp`: type/connection construction, ancestor retention and state.
- `inventoryfilter.cpp`: snapshot-only matching.
- `devicetree.cpp`: FINAL CUT 0.9.1-compatible selection and scrolling.
- `propertiesdialog.cpp`: modal General/Driver presentation.
- `terminaltext.cpp`: conversion and control-character sanitization.

Maintainer checks: type and erase queries while focus stays in the field; verify
unrelated branches disappear and matching ancestors remain. Check no-match and
whitespace queries, F3/F6 wraparound, Enter-to-properties and Escape. Before
filtering, select a nested device and expand several branches; clear the filter
and verify that state returns. Repeat in both views, after sorting, after a
refresh that removes the selected device, and at 80x25. Confirm the Qt GUI builds
and its search behavior is unchanged.
