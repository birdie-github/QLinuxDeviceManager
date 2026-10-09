# QLinuxDeviceManager — Linux Device Manager

**QLinuxDeviceManager is a lightweight graphical Linux device manager and hardware
information viewer inspired by Windows Device Manager.** Browse your computer's
devices, inspect kernel drivers and hardware resources, examine storage volumes
and cached drive health, and watch live device events in one Qt desktop application.

It provides a Linux alternative to Windows Device Manager for hardware inspection
and troubleshooting. It runs as a normal user and reads local information through
libudev, sysfs and procfs, with optional UDisks2 storage metadata. No particular
desktop environment is required.

## Features

- **Hardware browser:** processors, PCI and USB devices, disk drives, storage
  controllers, network adapters, display adapters, audio devices, keyboards,
  pointing devices, Bluetooth, cameras, batteries and system devices.
- **Seven tree views:** inspect devices by type, connection or driver, and browse
  reported memory addresses, I/O ranges, IRQs and other hardware resources.
- **Device properties:** hardware identifiers, manufacturer, kernel paths, direct
  driver bindings, kernel module information and advanced udev/sysfs metadata.
- **Storage information:** SATA, NVMe and USB device metadata, related partitions,
  filesystems, mount points, encrypted/device-mapper layers, LVM and software RAID.
- **Cached drive health:** available ATA SMART failure predictions, temperatures
  and counts, plus NVMe critical warnings, through UDisks2.
- **System information:** CPU models and topology, cache totals, physical and usable
  RAM, motherboard and firmware details, operating system, kernel and uptime.
- **Live hardware monitoring:** automatically update the inventory as devices are
  connected, removed or changed, with an Events tab for observed udev activity.
- **Search and copy:** filter device names or search advanced metadata and resources;
  copy property values, storage information, resource assignments and events.
- **UEFI variable viewer:** browse world-readable firmware variables and inspect
  their raw hex/ASCII contents.

The application is read-only. It does not install drivers, load or unload modules,
change driver bindings, rescan hardware buses, mount or unlock filesystems, modify
firmware variables, or power off devices. A device's presence or driver binding
alone does not establish that its hardware is healthy.

## Using the application

Launch `qlinuxdevicemanager` from a terminal or its desktop application entry.
The tree starts with your computer's hostname. Select a device and open
**Action → Properties**, use the toolbar or context menu, press **Alt+Enter**, or
double-click the device.

| Action | How to use it |
| --- | --- |
| Refresh the device inventory | **F5** or **Action → Refresh** |
| Search device names | **Ctrl+F** or **View → Filter** |
| Search properties and resources | Enable **Deep search** in the search bar |
| Clear and hide the search bar | **Escape** or **Ctrl+F** |
| Reveal partitions and implementation records | **View → Show virtual and internal devices** |
| Open the system overview | **File → System Information** or Properties on the computer root |
| Reread a device's properties | **Reload properties** in its dialog |

Deep search collects metadata in the background. Its cache is refreshed after
inventory changes or an F5 refresh. Storage deep search uses native metadata;
it does not query UDisks2 for every device.

Virtual and internal devices are hidden by default to keep the hardware browser
readable. This includes partitions, USB interfaces, input and sound endpoints,
DRM outputs and many transport or unbound kernel objects. Their visibility is
not a health or connectivity verdict. Friendly names retain kernel identifiers
where needed to distinguish identical devices; tooltips explain their sources.
Some related records are grouped in the default view and remain individually
accessible with internal devices shown.

Window geometry, toolbar visibility, the selected tree view and visibility settings
persist across launches. The computer root starts expanded and other groups start
collapsed; expansion and selection are retained during updates within the session.

## Hardware and driver views

Choose a view from the **View** menu. All views use the same device inventory.

| View | What it shows |
| --- | --- |
| Devices by type | Devices grouped by their hardware or functional category |
| Devices by connection | Devices arranged by their recorded kernel parent relationships |
| Devices by driver | Directly bound devices grouped by subsystem and driver |
| Drivers by device | Each device's direct driver and observed owning kernel module |
| Drivers by type | Device category, bus-qualified driver and associated devices |
| Resources by type | Reported assignments grouped by resource kind and device |
| Resources by connection | Assignments under their reporting devices in the connection tree |

Connection views retain necessary ancestors for context. A parent device's driver
or resource assignments are not presented as belonging to its child. Driver views
show relationships for discovered devices, rather than listing every installed or
loaded module. Missing bindings or module links are reported without assuming a
fault or a built-in driver.

Resources include directly associated PCI and Plug and Play memory/MMIO and I/O
ranges, PCI BARs and expansion ROM resources, IRQs, readable MSI/MSI-X vectors, and
reported PnP DMA channels and bus windows. Unsupported, disabled, unassigned and
restricted observations remain explicit. Shared interrupts or overlapping ranges
are not automatically labelled conflicts. The application reads resource metadata,
not PCI BAR contents.

## Device properties

The modeless Properties dialog provides these tabs when applicable:

| Tab | Information |
| --- | --- |
| General | Name, category, manufacturer, bus/subsystem, location, observed status and selected hardware identifiers |
| Driver | Direct driver binding, owning module, available running version and optional installed-module metadata |
| Details | Generic properties, source attribution, advanced raw metadata and copy controls |
| Resources | Directly reported hardware resource assignments and read/parse errors |
| Storage | Selected storage device identity, capacity, transport, sector sizes and flags |
| Volumes | Related storage layers, content, mount points, identifiers and backing relationships |
| Health | Each related drive's available cached ATA SMART or NVMe health evidence |
| Events | Live udev observations associated with the exact device instance |

Storage-specific information stays in its dedicated tabs, including when advanced
Details are enabled. Sources are available in tooltips or copied text. Compact
information captions expose scope and coverage notes when hovered.

Properties are collected on demand. F5 refreshes the inventory; **Reload properties**
updates the open device snapshot. A removed or replaced device leaves a marked,
read-only snapshot with reload disabled. Reconnecting a device at the same kernel
path does not intentionally retarget the old dialog to the replacement.

Installed module descriptions, filenames, licenses and versions require optional
libkmod support. Installed metadata can differ from code already loaded into the
kernel. Declared firmware filenames do not establish which firmware is loaded or
its version; directly reported firmware revisions are shown where available.

## Storage devices, volumes and health

Block devices and NVMe controllers have separate **Storage**, **Volumes** and
**Health** tabs. Available information includes model, manufacturer, serial number,
firmware revision, transport, capacity, logical/physical sector sizes, read-only
and removable-media flags, partition tables and filesystem identifiers.

Volumes retain explicit partition, backing and using relationships. Encrypted
mappings, LVM and RAID can span several devices; capacities are not added across
layers. Exact kernel/UDisks2 block mappings share one volume row, with conflicting
metadata shown explicitly. Missing or failed reads remain visible.

Mount points come from `/proc/self/mountinfo` and describe the application's
**mount namespace**. They include filesystem type and mount root, so bind mounts
and subvolume mounts can be distinguished. No observed mount is not proof that a
volume is unused elsewhere. No filesystem probing, mounting or unlocking occurs.

Optional UDisks2 support supplements native metadata when the service is already
running and accessible. Missing services, denied access and builds without QtDBus
retain native storage viewing. The UDisks2 removable-drive hint is distinct from
the kernel's removable-media flag.

Health information is **cached evidence**, with the last update timestamp where
available. ATA failure prediction, NVMe critical warnings, cached temperature and
available ATA bad-sector/failing-attribute counts are shown separately. Missing
interfaces or never-updated caches are unavailable; an absence of reported warnings
is not a guarantee of health. Opening or reloading Properties never requests SMART
updates, background polling or self-tests. UDisks2 may update its own cache
independently.

## System information

The system overview shows hostname, operating system, kernel architecture/release/
build, CPU models, sockets and cores, present and online logical processors, unique
cache totals by level/type, uptime, motherboard identity, firmware version/date,
boot-mode evidence and reported hypervisor identity.

**Physical RAM** uses available firmware-reported capacities from udev metadata.
**Usable RAM** uses Linux `MemTotal`; it is not currently free memory. When both
values are valid, their difference is shown as system-reserved RAM, covering all
memory unavailable to Linux rather than only firmware reservations. Missing
capacity is left unavailable instead of guessed.

CPU topology and virtualization information reflect what the running environment
exposes. A container or virtual machine may report a restricted or virtual system
view; absent virtualization evidence does not prove physical hardware. CPU cache
totals cover online processors. The overview collects no machine ID or system
serial number and does not launch external diagnostic programs.

Use **Refresh** to collect a new system snapshot and **Copy all** to copy its values.

## Live device events

The **Events** tab records udev observations received since the application started,
with receipt times, event types, descriptions and available payload details.
It retains at most **1,024 events globally** and displays the latest **256 events
per exact device instance**, newest first. Copy controls include the full coverage
notice.

This is session history, not a persistent Windows-style device log. Historical
journal and kernel messages are not queried. Related devices have separate
histories; UEFI variables have no udev value-change history. Uncertain event
associations are withheld, and detected monitoring failures or losses are reported.
No retained events does not establish that the device has had no errors.

Live device changes update the inventory automatically, with periodic reconciliation
and manual F5 refresh available. If monitoring fails, the application reports it
and retries. Kernel observations remain snapshots: undetected event loss and
reused paths without distinguishing metadata can limit instance tracking.

## UEFI firmware variables

On UEFI systems exposing `/sys/firmware/efi/efivars`, the **UEFI variables** category
lists regular files with the world-read permission bit set. This permission filter
also applies when running as root. Missing or inaccessible efivarfs does not
prevent ordinary hardware discovery.

Names are separated into readable words; original names remain in tooltips.
Namespace GUIDs and full filenames are shown with internal devices enabled.
Equal names in different namespaces remain separate records.

Opening Properties reads up to **64 KiB** of the variable file as a hex/ASCII dump,
including its attribute prefix. Longer values receive a truncation notice.
Contents are not decoded or modified, and actual access can still be denied by
security policy despite the file's permission bits.

## Build and installation

Requirements:

- Linux and a C++17 compiler.
- CMake **3.19 or newer** and pkg-config.
- Qt **6.8 or newer**, including Widgets, and libudev development headers.
- Optional QtDBus **6.8 or newer** for cached UDisks2 storage metadata.
- Optional libkmod **30 or newer** for installed-module metadata and fallback names.
- Optional Qt Linguist tools **6.8 or newer** to build translation catalogs.

Fedora packages:

```sh
sudo dnf install gcc-c++ cmake make pkgconf-pkg-config qt6-qtbase-devel systemd-devel
# Optional:
sudo dnf install qt6-qttools-devel kmod-devel
```

Debian/Ubuntu packages:

```sh
sudo apt install g++ cmake make pkg-config qt6-base-dev libudev-dev
# Optional:
sudo apt install qt6-tools-dev qt6-tools-dev-tools libkmod-dev
```

Check that your distribution's Qt packages meet the minimum version; older
releases may require a newer Qt installation.

Build and run from a source checkout:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build -j
./build/qlinuxdevicemanager
```

Install the executable, desktop entry, application icons and available translation catalogs:

```sh
sudo cmake --install build --prefix /usr/local
```

Use `-DKMOD=OFF` to omit libkmod or `-DUDISKS2=OFF` for a native-only storage build.
Basic hardware viewing requires neither QtDBus nor UDisks2. The interface uses
system-theme icons with Qt fallbacks and standard Qt X11/Wayland platform selection.
The application window and About dialog use embedded application icons, available
without installation. Installation supplies PNG sizes from 16 to 512 pixels and
a scalable SVG in the standard `hicolor` icon theme for desktop launchers.
The supplied interface is English; Qt's own dialogs use installed Qt translations
when available.

## Permissions and settings

Run the application as a normal desktop user. Restricted information is reported
as unavailable or permission denied; no passwords or administrative privileges
are required for ordinary viewing. The application does not upload hardware
information or run a separate diagnostic daemon.

Settings use the normal Qt configuration location under `QLinuxDeviceManager`.
If launched as root, the window identifies administrative mode and settings use
`/root/.config`, `/root/.local/share` and `/root/.cache`. Root launch requires an
already-authorized display/session environment; the application does not change
display access permissions or invoke sudo itself.

## License

QLinuxDeviceManager is written in C++17 using Qt 6 Widgets and libudev and is
licensed under the [GNU General Public License, version 3](LICENSE).

Project: [github.com/birdie-github/QLinuxDeviceManager](https://github.com/birdie-github/QLinuxDeviceManager)
