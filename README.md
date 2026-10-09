# QLinuxDeviceManager

A lightweight Linux hardware viewer using C++17, Qt6 Widgets and libudev,
with a conventional desktop interface inspired by Windows Device Manager.
Licensed under GPLv3 (see LICENSE).

## Current scope: Phase 2

Devices by type, a read-only **Refresh (F5)** action, selection, themed icons,
and persistent window geometry, toolbar visibility and visibility settings.
Categories start collapsed on every launch; expansion is preserved during
refreshes and visibility changes within the running session. Run as a normal user; no daemon, device-management
operations, hardware health claims or bus rescans are implemented.

The complete discovered inventory remains separate from the visible tree.
**View → Show virtual and internal devices** is off by default. It includes
`/sys/devices/virtual` records, partitions, USB interfaces, input endpoints,
HID transport nodes, sound endpoints, DRM outputs, unbound ACPI objects,
SCSI/ATA transport objects and other unbound implementation objects.
This filter does not describe hardware health or disconnected hardware.

CPU model names come from `/proc/cpuinfo`, matched by logical processor number
(the file is read once per refresh with a 4 MiB bound). Each logical CPU retains
its own record; offline CPUs without matching metadata keep their kernel name.
Input/video function names and direct NVMe model metadata take precedence over
generic model names. Other names prefer udev database/model properties, then
selected direct sysfs text metadata. Missing PCI/USB database model properties
can be resolved from the local libudev hwdb using the device's own modalias.
If unresolved, a directly bound driver and kernel name describe the function
without inventing a marketing name; no driver evidence means a kernel-name fallback. Kernel names accompany
labels to distinguish identical models. Missing metadata has a readable
fallback; failed attribute reads retain an explicit state and error separately.
No parent vendor, model or driver is silently attributed to a child.

Additional naming uses exact standard ACPI/PNP identifiers (including compatible
IDs), direct HDA codec `chip_name`/`vendor_name`, and USB `product` text.
Linux USB root hubs receive explicit USB 1.1/2.0/3.x captions, using their
root-bus names, address and descriptor IDs. Separate USB buses remain separate
rows. Known platform, PCI Express service and faux kernel functions receive
readable captions scoped to exact subsystem/alias or driver identities.
Standard function labels are translated in the view; raw IDs remain visible.
An optional libkmod fallback reads installed module descriptions through native
APIs in the enumeration worker. Bound modules are identified through that
device's actual `driver/module` link. When only a modalias is available, exactly
one matching module is required and the label is marked **module candidate**.
Descriptions explain a software/kernel function, not a hardware marketing
model, health, binding status or the version of loaded code. Tooltips record the
naming basis. Missing or ambiguous metadata keeps the previous fallback label.

The default audio view groups a PCI audio function with its single ALSA card.
If the function owns multiple cards, every card remains visible and the extra
PCI row is suppressed. USB sound cards and unrelated audio controllers are
preserved. All grouped records remain available under Show internal devices.

Power supplies use reported type/manufacturer/model metadata for readable role
labels. Battery role placeholders such as `Primary` are retained in tooltips;
no manufacturer is hardcoded. UCSI supplies retain separate connector numbers.
Generated I2C mouse/touchpad names put the function first and retain the firmware
identifier in parentheses, while descriptive product names remain intact.
ALSA headphone, microphone and HDMI/DisplayPort jack-detection
switches appear as internal audio records rather than ordinary HID devices.
Raw names and kernel identifiers remain in tooltips. Repeated friendly labels
receive identifiers to distinguish them; Show internal devices includes IDs.

**UEFI variables** lists regular files with the world-read permission bit set
under `/sys/firmware/efi/efivars`. This is a separate firmware category, not a
claim that variables are hardware devices. Enumeration reads directory entries
and file metadata only; variable contents are not opened, parsed or cached.
Concatenated filename words are separated for display. Original names remain
in tooltips, and namespace GUIDs/full filenames appear only with Show internal
devices enabled. Equal names in different namespaces retain separate rows with
numbered namespace labels. Restricted files remain excluded even when running
as root or showing internal devices. Missing/inaccessible efivarfs yields no
category; this does not prevent ordinary device discovery. Permission bits do
not guarantee that another security policy will permit a future value read.
Opening Properties reads a bounded hex snapshot; see below.

Application categories are deterministic groupings, not kernel device classes:

| Records | Group and default visibility |
| --- | --- |
| PCI class 01/02/03/04/0c03 | Storage/network/display/audio/USB respectively |
| Other PCI, platform, PNP; bound ACPI | System devices |
| USB device / interface | USB; interfaces hidden |
| Block disk / partition | Disk drives; partitions hidden |
| inputN with keyboard or pointing flags | Keyboards / mice; other inputN → HID |
| event, js, mouse endpoints; HID/hidraw | HID/input group; endpoints hidden |
| Sound card / other sound endpoints | Audio; endpoints hidden |
| net, hci Bluetooth, video4linux, power_supply, cpu | Corresponding functional group |
| DRM card / connectors | Display adapters / display outputs; hidden by default |
| NVMe controller; SCSI hosts / ATA ports | Storage controllers; transport nodes hidden |
| Other directly bound physical function | Other devices |
| Other unbound objects | Other devices; hidden |

Composite USB input/audio/video functions remain independently visible.
A PCI network adapter with exactly one visible interface is represented by its
PCI row; the interface remains available in the internal view. Multiple
interfaces remain independently visible, with the redundant aggregate PCI row
hidden. An NVMe class controller backed by a PCI NVMe controller is represented
by that PCI row; namespace disks remain separate. These rules follow recorded
udev ancestry and PCI classes, never matching model names or serial numbers.
Devices without a matching parent remain visible. Tooltips identify grouped
records and their representative. Separate PCI controllers remain separate. DRM connectors are called
outputs, not monitors: their presence does not prove a monitor is connected.
Category mappings require real-machine validation, particularly Bluetooth,
multifunction video devices, firmware/platform devices and unusual buses.

## Device properties

Open **Action → Properties**, the toolbar button, the device context menu,
**Alt+Enter**, or double-click a device. A modeless dialog provides:

- **General**: name, category, manufacturer, bus/subsystem, location, evidence-based
  status, direct driver binding and selected hardware identifiers.
- **Driver**: bound driver, owning module, modular/built-in determination where
  evidence permits, running module version and optional installed-module metadata
  (filename, version, description, author, license, declared firmware names).
  Installed metadata may differ from already loaded code; absent module links
  or versions do not prove a built-in driver. Direct NVMe firmware revisions are
  shown when available; declared firmware filenames are not firmware versions.
- **Details**: a property selector, multiline read-only values, source attribution,
  copy selection/value/all, and an advanced toggle for curated raw udev/sysfs
  metadata and inventory naming sources.

Properties are collected on demand in one dedicated worker; opening another
record closes the previous dialog and supersedes its request. No hardware changes
are available. Reload properties rereads the same instance. F5 still refreshes
only the inventory. A removed/replaced device retains a visibly marked snapshot
and disables Reload when detected by a property read or successful F5 refresh.
Live hotplug detection belongs to Phase 4; unplug/replug entirely between reads
may still escape detection under the existing instance-tracking limitations.

UEFI Details includes a hex/ASCII dump of the first **64 KiB** of the complete
variable file, including its attribute prefix, with an explicit truncation notice
when needed. Contents are not decoded. World readability and identity are
rechecked, including under root; denied reads are reported explicitly. GUIDs and
full variable filenames/paths remain advanced-only. Copy all respects the advanced
toggle. Neither reading sysfs presence nor binding a driver establishes health.

## Build and install

Required: Linux, CMake >= 3.19, a C++17 compiler, pkg-config, Qt >= 6.2 Widgets
and libudev development headers. Optional: Qt Linguist tools for translation
catalog generation and libkmod >= 30 for module-description fallback names and
installed module properties.
Use `-DQLDM_WITH_KMOD=OFF` to explicitly omit libkmod; CMake reports whether
module naming is enabled. QtDBus, UDisks2 and journal libraries are not used
in this phase. No particular desktop environment is required.

Fedora packages: `gcc-c++ cmake make pkgconf-pkg-config qt6-qtbase-devel
systemd-devel`; optional `qt6-qttools-devel` and `kmod-devel`.
Debian/Ubuntu packages: `g++ cmake make pkg-config qt6-base-dev libudev-dev`;
optional `qt6-tools-dev qt6-tools-dev-tools libkmod-dev`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/qlinuxdevicemanager
cmake --install build --prefix /usr/local
```

Tests exercise fixture classification and instance reconciliation; they neither
enumerate hardware nor create a window. Disable with `-DBUILD_TESTING=OFF`.
Translations use `tr()` and Qt Linguist TS/QM catalogs. When Linguist is
available, `cmake --build build --target update-translations` extracts source
strings into the English template; copy it to the desired locale, translate
and add that TS file to CMake's catalog list. Installed catalogs are loaded from
`share/QLinuxDeviceManager/translations`. Qt's own dialogs use installed Qt
catalogs when available. No non-English translation is supplied yet.

X11/Wayland selection follows the standard Qt platform configuration. Root
launch (including `sudo`) is possible only if the existing display environment
permits it. The application does not alter display authorization, invoke sudo,
or request passwords. Root uses `/root/.config`, `/root/.local/share` and
`/root/.cache` and the title identifies administrative mode. Settings live
under `QLinuxDeviceManager` in the normal Qt configuration location.

A Linux desktop entry is installed; icons follow the system theme with Qt
fallbacks. Further desktop integration and UI polish belong to Phase 9.

## Source layout

- `src/devices/`: inventory records, classification, bounded CPU metadata,
  ancestry-based presentation grouping, standard function labels, optional
  module-description naming, enumeration and on-demand properties workers.
- `src/ui/`: tree model, window, actions and UI state.
- `src/main.cpp`: application startup and translation loading.
- `resources/embedded/`: build-time metadata header template.
- `resources/external/`: externally installed translation catalogs.
- `doc/`: architecture and refresh/identity limitations.
- `packaging/`: Linux desktop entry.
- `project.json`: project identity, release and resource metadata; CMake reads
  its name/version and generates application metadata.
- `tests/`: focused fixture checks.

See [doc/architecture.md](doc/architecture.md) for ownership, refresh and
instance-tracking details.

## Verification of this delivery

Static verification only: no application build, test executable or GUI launch
was authorized. Patch application, whitespace, source/CMake references and
source-level ownership/connection review are checked. The included C++ fixture
checks still need compilation and execution on the user's machine.

A user-supplied HP laptop diagnostic dump was inspected offline: 16 logical
Intel Core Ultra X7 358H CPUs, one PCI Wi-Fi adapter plus `wlo1`, and one PCI
NVMe controller plus `nvme0`. See [doc/discovery-notes.md](doc/discovery-notes.md).
This is evidence from the dump, not a runtime application test. GUI/category
acceptance still requires real-machine testing. Later phases cover properties, additional views, live
monitoring, resources, storage metadata and narrowly authorized operations.

Relevant API/design references:
- https://www.kernel.org/doc/html/latest/admin-guide/sysfs-rules.html
- https://www.freedesktop.org/software/systemd/man/latest/libudev.html
- https://doc.qt.io/qt-6/qthread.html
