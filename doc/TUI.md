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

F2 switches devices by type / connection. Arrow keys navigate; Enter toggles
branches and opens properties for leaf devices. Right/Left still expand/collapse.
Native mouse activation is unchanged, including properties for device branches.
General and Driver use the GUI's shared common-property formatter, including
manufacturer, hardware identifiers, binding and module ownership when captured.
Details retains raw inventory properties and attributes separately.
F7 / F8 select the previous / next General, Driver or Details page, wrapping
at either end. Tab / Shift+Tab retain normal control focus traversal, including
Close. Page changes focus the content so arrows and Page Up/Down scroll
immediately. Alt+G / Alt+D / Alt+T still select
pages directly; the title identifies the current page. Close, Alt+C or Escape
returns. Text views scroll vertically
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

The Filter field is hidden initially, giving its space to the tree. F4 or Ctrl+F
reveals and focuses it. Typing a case-insensitive substring immediately filters
the current projection. Matching devices and
their ancestors remain; all retained branches expand so results are visible.
The status bar reports the number of matching devices, excluding context-only
ancestors. With no matches, only the computer row remains.

Matching uses names, identifiers, driver/module names, paths, category/subsystem,
udev property values and available captured attribute values. It performs no
additional hardware reads. Hidden records omitted from the normal view remain
excluded; retained connection-context ancestors are eligible for matching.
This does not include the GUI's hardware-specific deep-property collection.

Enter in the field selects a match and focuses the tree; Enter on a device opens
properties, keeping the filter visible and active. F3/F6 cycle matches
forward/backward with wraparound. Escape clears and hides the field, restores
the full tree's height and focuses it. Backspacing to an empty query also restores
the full projection and its pre-filter selection and expansion, but keeps the
field open for further input until Escape. Whitespace alone
means no filter. The query remains active across F2 and F5. Each view retains
its own unfiltered state; paths absent after a refresh are skipped on restore.
Header sorting resets when the tree is rebuilt.

The browser uses one outer frame, carrying the Device/Enter caption; the native
tree's separate border and headline are suppressed. With the filter hidden,
device rows begin immediately below that frame. The tree's scrollbar ends
immediately above the buttons, reclaiming the former blank rows at both ends.
Native tree item drawing, scrolling and activation remain unchanged.

Maintainer checks: at 80x25 and 211x60, verify a single frame, its caption, the
first device row directly below it, and no blank row before the buttons.
Open/close the filter, switch views and refresh; scroll in both directions and
resize. Confirm the caption stays on the outer frame, buttons and their shadows
remain intact, and Properties restores the browser without stale borders.

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

For filter visibility, check startup and Tab traversal with the field hidden;
open with both F4 and Ctrl+F, then type, erase, and press Enter. The field must
stay visible while results are active. Escape should clear/hide both field and
label, restore prior tree state and reclaim the space. Repeat show/hide after
F2, F5 and terminal resizing at 80x25 and larger. Escape inside Properties should
close only that dialog, leaving the active tree filter intact.

## Common properties

The TUI adapts the captured inventory into `CommonProperties`, a small shared
record with values, sources and read state. Both frontends use
`commonPropertyEntries()` for General/Driver labels, manufacturer precedence,
hardware identifiers, EFI applicability, status wording and module interpretation.
The GUI's common-field formatter keeps its field order and interpretation.
Shared captions are shortened for both frontends (for example, Kernel driver,
Kernel module, Module type and Module file). Status and metadata notes are also
shortened while retaining their qualifications. GUI on-demand collection
continues to supply its data.

The TUI does not run those additional collectors. Missing module files, versions
or metadata remain unavailable; an absent module link never proves a built-in
driver. A description used only as a naming candidate is not promoted to driver
metadata. Device status identifies snapshot scope. Raw values remain in
Details, with the shared read-error/permission wording. `propertypages.cpp` owns
the TUI snapshot adaptation and text rendering; `propertiesdialog.cpp` owns only
page selection and layout. Long values remain accessible by scrolling.

Maintainer checks: compare General/Driver field names with the GUI, opening a
USB device, PCI device, unbound record and UEFI variable. Check manufacturer and
identifier values against Details and confirm absent fields are not reported
as healthy or built-in. Switch all three pages using buttons and mnemonics,
scroll long identifiers and raw attributes, resize, and close with Escape.
Rebuild the GUI and compare its General/Driver output before/after extraction.

## Compact properties layout

Properties is centered and limited to 100 columns and 28 rows. Short pages use
less height; terminal resizing reduces the dialog to the available space.
Fields use aligned label/value columns with no blank rows between fields.
Padding uses terminal column widths, so values and multiline continuations
start at the same column. Every other logical field receives native FINAL CUT
line highlighting with a contrasting light background; continuation lines keep
the same band. Monochrome terminals retain the aligned table without color.
Details keeps a single separating line before each raw-data section. Horizontal and
vertical scrolling preserve access to long values and large snapshots.

Maintainer checks: at 211x60, verify the dialog is centered and no larger than
100x28. At 80x25, verify all page buttons and Close fit. Switch General, Driver
and Details, check aligned columns and alternating field backgrounds, including
multiline identifiers. Scroll long values and resize while each page is open.
Check a monochrome terminal if available; alignment must remain readable.

For Enter navigation, check the computer/category rows and multi-level connection
branches: repeated Enter must expand/collapse without opening Properties. Enter
on a leaf device must still open Properties. Repeat after filtering and while
horizontally scrolled, and confirm Left/Right and mouse activation still work.

For Properties page navigation, use F7/F8 to cycle backward/forward through all
pages, including wraparound. Repeat after clicking a page or Close button, and
confirm scroll keys act on the content immediately after a page change. Check
Alt+G/D/T, Escape and mouse Close. Tab must reach Close; Shift+Tab must traverse
controls in reverse. Main-window Tab behavior must remain unchanged.

While Properties is open, the bottom bar shows F7 Previous and F8 Next, matching
the keyboard bindings. Clicking either hint also switches pages. The browser's
status keys and message return when the dialog closes; browser accelerators are
removed from the bar while Properties is active. `propertiesstatus.cpp` owns this
scoped change, reusing the existing bar rather than replacing its widget.

Maintainer checks: open Properties, use F7/F8 and click both hints, then close
using Escape and the Close button. Confirm the main F2/F4/F3/F6/F5/F10 hints
return and their actions execute once. Repeat opening and closing several times,
and resize with Properties open to check the bottom bar placement.

Properties omits the repeated inventory-snapshot banner and the Captured
attributes heading. Raw attribute rows remain present, aligned and shaded with
the rest of Details. Snapshot behavior is documented here and in Device status;
there is no startup confirmation. Future dialogs should keep routine scope
explanations in documentation and reserve visible notes for necessary context.

Maintainer checks: open all three Properties pages and confirm the banner is
absent. On Details, confirm attribute rows remain, the redundant heading is
absent and alternating bands still follow the correct logical fields.
