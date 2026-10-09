# QLinuxDeviceManager

A lightweight Linux hardware viewer using C++17, Qt6 Widgets and libudev,
with a conventional desktop interface inspired by Windows Device Manager.
Licensed under GPLv3 (see LICENSE).

## Current scope: Phase 1

Devices by type, a read-only **Refresh (F5)** action, selection, themed icons,
and persistent window geometry, toolbar visibility, category expansion and
visibility settings. Run as a normal user; no daemon, device-management
operations, hardware health claims or bus rescans are implemented.

The complete discovered inventory remains separate from the visible tree.
**View → Show virtual and internal devices** is off by default. It includes
`/sys/devices/virtual` records, partitions, USB interfaces, input endpoints,
HID transport nodes, sound endpoints, DRM outputs, unbound ACPI objects,
SCSI/ATA transport objects and other unbound implementation objects.
This filter does not describe hardware health or disconnected hardware.

Names prefer the device's own udev database model, model, or NAME property,
then explicitly selected direct sysfs text metadata (input/video name, sound
card ID, power-supply model), then its kernel name. Kernel names accompany
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
A PCI adapter and its class function can both appear: they have distinct kernel
identities and are not merged by matching names. DRM connectors are called
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

- `src/devices/`: inventory records, classification and enumeration worker.
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

No hardware was observed through this application. Container sysfs is not
representative of a desktop, so hardware/category acceptance remains pending
real-machine tests. Later phases cover properties, additional views, live
monitoring, resources, storage metadata and narrowly authorized operations.

Relevant API/design references:
- https://www.kernel.org/doc/html/latest/admin-guide/sysfs-rules.html
- https://www.freedesktop.org/software/systemd/man/latest/libudev.html
- https://doc.qt.io/qt-6/qthread.html
