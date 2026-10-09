# QLinuxDeviceManager

A lightweight Linux hardware viewer using C++17, Qt6 Widgets and libudev,
with a conventional desktop interface inspired by Windows Device Manager.
Licensed under GPLv3 (see LICENSE).

## Current scope: Phase 1

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

## Build and install

Required: Linux, CMake >= 3.19, a C++17 compiler, pkg-config, Qt >= 6.2 Widgets
and libudev development headers. Optional: Qt Linguist tools for translation
catalog generation. QtDBus, libkmod, UDisks2 and journal libraries are not used
in this phase. No particular desktop environment is required.

Fedora packages: `gcc-c++ cmake make pkgconf-pkg-config qt6-qtbase-devel
systemd-devel`; optional `qt6-qttools-devel`.
Debian/Ubuntu packages: `g++ cmake make pkg-config qt6-base-dev libudev-dev`;
optional `qt6-tools-dev qt6-tools-dev-tools`.

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
  ancestry-based presentation grouping and the enumeration worker.
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
