# QLinuxDeviceManager

A lightweight Linux hardware viewer using C++17, Qt6 Widgets and libudev,
with a conventional desktop interface inspired by Windows Device Manager.
Licensed under GPLv3 (see LICENSE).

## Current scope: Phase 6

Five live device inventory views and two resource views (see below), a read-only **Refresh (F5)** action, selection, themed icons,
and persistent window geometry, toolbar visibility and visibility settings.
Categories start collapsed on every launch; expansion is preserved during
refreshes and visibility changes within the running session. Run as a normal user; no daemon, device-management
operations or bus rescans are implemented. Storage Properties includes available cached health evidence.

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
- **Resources**, when direct resource metadata is available: memory ranges, I/O
  ranges and IRQs (including every observed MSI/MSI-X vector), with PCI BAR/ROM
  identification and selected flag details. PCI and Plug and Play devices are
  supported; PnP DMA channels and bus windows are shown when reported. Disabled,
  unassigned and zeroed addresses retain explicit states, and read/parse errors
  remain visible. Resources can be copied from their table or Details.

The ordinary read-only snapshot notice is omitted; loading, read failures and
removal/replacement still receive explicit messages. The Resources tab has no
settings controls or conflict field: resource overlap and shared IRQs alone
cannot establish a conflict. Only resource metadata is read, never BAR contents.
Resource-oriented tree views are not implemented by this tab.

Properties are collected on demand in one dedicated worker; opening another
record closes the previous dialog and supersedes its request. No hardware changes
are available. Reload properties rereads the same instance. F5 still refreshes
only the inventory. A removed/replaced device retains a visibly marked snapshot
and disables Reload when detected by a property read or accepted inventory update.
Live removal events distinguish unplug/replug at the same path; detected event loss
invalidates previous identities conservatively. Kernel observations remain snapshots,
and undetected event loss can still limit instance tracking.

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
module naming is enabled. Optional QtDBus enables cached UDisks2 storage metadata
when an already-running UDisks2 service is accessible. Use
`-DQLDM_WITH_UDISKS2=OFF` for a native-only build. Basic viewing requires neither
QtDBus nor UDisks2; journal libraries are not used. No particular desktop
environment is required.

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

Use **View → Filter** or **Ctrl+F** to show the full-width search bar. Names are
searched by default; **Deep search** includes advanced properties, resources and
numbers, collecting metadata in the background. Escape or Ctrl+F hides the bar
and clears the filter. Deep-search metadata is cached until a changed live inventory or F5 refresh.

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
acceptance still requires real-machine testing. Later phases cover storage metadata and narrowly authorized operations.

Relevant API/design references:
- https://www.kernel.org/doc/html/latest/admin-guide/sysfs-rules.html
- https://www.freedesktop.org/software/systemd/man/latest/libudev.html
- https://doc.qt.io/qt-6/qthread.html

## Alternative tree views

All seven views have one computer root labelled with the local hostname
(`This computer` if unavailable). It starts expanded; the existing view contents
start collapsed. Root expansion is retained per view within the session.
The root remains visible when filtering finds no devices and is excluded from
device counts, device Properties targets and deep-search inputs.
Its Properties action opens System Information (see below).

View offers Devices by type, Devices by connection, Devices by driver,
Drivers by device and Drivers by type. Every view reuses the same inventory;
switching does not enumerate hardware or read sysfs on the GUI thread.

- **Devices by connection** follows actual inventory parent paths. Hidden
  ancestors needed by visible devices remain as context (identified in their
  tooltips); unrelated hidden branches remain excluded. Missing parents leave
  roots rather than invented Linux/Windows bus objects. UEFI variables remain
  separate roots because their directory is not an inventoried hardware device.
- **Devices by driver** groups directly bound devices by subsystem and driver
  name. Identical names on different buses stay separate. Devices without a
  direct binding have an explicit group; this is not a failure verdict.
- **Drivers by device** shows device → direct driver → owning kernel module
  when its `driver/module` link is observed. The nearest bound ancestor, if any,
  appears as a separately labelled parent-device relationship, never as the
  selected device's own binding. Missing module links leave module/built-in
  status undetermined. Built-in drivers remain visible as bound drivers.
- **Drivers by type** groups category → bus-qualified driver → devices.

Driver/module rows describe relationships and do not open device Properties.
Device rows still support Properties, filtering and deep search. Counts include
only device/firmware records, including connection-context ancestors, never
synthetic driver/module groups. These views do not list every installed or
loaded module. Live udev updates reconcile binding/module observations; F5 also requests a fresh snapshot.

Selection follows the same path and generation between views where visible;
its ancestor chain is expanded to reveal it. Other expansion state is retained
separately per view during this session. The chosen view persists across launches,
which still start collapsed. No Devices by container view is offered: neither
ancestry nor matching names/serials establishes a reliable Linux container
identity; grouping them would invent a hardware ownership relationship.

## System Information

File → System Information and Properties on the hostname root open the same
modeless system overview. It has selectable plain-text values, source tooltips,
Copy all, Refresh and Close. Reopening reuses its last snapshot; Refresh collects
new values. Hardware enumeration and device Properties remain usable.

The dialog shows hostname, OS name, kernel architecture/release/build, CPU models,
sockets/cores, present/online logical CPUs, physical and usable RAM in GiB, uptime,
system and motherboard manufacturer/model, firmware version/date, boot-mode
evidence, reported hypervisor identity and unique CPU cache totals by level/type.
Missing or restricted metadata stays explicitly unavailable; absent virtualization
evidence does not prove physical hardware. Firmware dates retain their reported
format. RAM is Linux MemTotal, not installed DIMM capacity or currently free RAM.

Collection uses uname/gethostname, selected /proc and sysfs files and, on x86,
CPUID hypervisor identification. No lsb_release, uname, dmidecode or other program
is launched. OS naming follows os-release precedence and quoting without shell
execution. CPU socket/core/die IDs are kernel-reported topology (a VM may expose
virtual topology); incomplete observations do not produce partial totals. Caches
are deduplicated by level/type and the CPU-sharing set across online CPUs; totals
need not include caches belonging exclusively to offline CPUs. A change in CPU
presence/online lists invalidates topology and cache results; Refresh can retry.
Boot mode identifies UEFI when its kernel directory is exposed; absence leaves
legacy boot versus a restricted environment unresolved.

One additional serial worker is active only on request. Reads and list expansion
are bounded and shutdown cooperatively cancels collection. In a container, OS,
hostname, /proc and sysfs may reflect different namespace scopes; the overview
reports this process's exposed system view rather than promising host identity.
Serial numbers, machine IDs and live utilization are not collected.

Physical RAM sums firmware-reported capacities from the DMI device's udev
properties, using libudev and the existing system-information worker. Usable RAM
still comes from MemTotal. Its system-reserved annotation is their byte difference,
rounded to MiB; the tooltip explains that this covers all RAM unavailable to Linux,
not a detailed firmware-reservation map. Copy all includes both rows and the same
annotation. The difference is omitted when physical capacity is unavailable or
smaller than usable capacity; missing firmware data never causes rounding MemTotal
up to guess an installed size.

All advertised memory records must have a valid capacity or explicit empty-slot
marker. Missing capacity makes the total unavailable; malformed data, conflicting
empty markers and overflow produce errors. Known nonvolatile capacity is unsupported
rather than silently included as RAM. Up to 4096 records and 64 bytes per selected
property are accepted. This uses cached udev metadata and depends on firmware and
the distribution's memory-identification rule; no SMBIOS parsing, subprocess,
extra dependency or elevation is added to the application.

## Live hardware monitoring

USB and other udev device changes update all views automatically. Bursts are
coalesced (100 ms quiet time, 500 ms maximum scheduling delay, plus scan time).
The monitor starts before initial enumeration; fresh worker snapshots reconcile
events rather than replaying stale add records. Model updates retain unchanged
rows, expansion and selected instances. Unplugging the selected instance clears
selection and marks its Properties dialog removed; replugging at the same path
creates a new instance.

Continuous churn may briefly hide uncertain branches until a fresh scan confirms
them. Detected event loss or monitor failure triggers full reconciliation and
invalidates old identities. Monitoring recovery retries automatically; its status
is visible. A 30-second reconciliation also catches silent drift and EFI directory
changes. F5 remains available throughout and never rescans a hardware bus.
No subprocess, elevation, extra library or hardware-management action is added.

## Resource views

View → Resources by type groups reported assignments as resource kind → device →
assignment. View → Resources by connection follows actual inventory parent links,
with assignment rows under their reporting devices. Both retain the hostname root,
use the existing virtual/internal visibility setting, and preserve selection and
expansion during live updates. Connection ancestors are retained for context; they
do not acquire their children's assignments. Devices with no supported resource
information are omitted unless needed as ancestors. A resource kind appears only
when an assignment or explicit read/parse error is available.

The current collector covers PCI memory/MMIO and I/O ranges, BAR slots, expansion
ROM/bridge resources, reported IRQs and every readable MSI/MSI-X vector. PnP metadata
also supplies IRQs, DMA channels and bus/address ranges where the kernel exposes
them. Unassigned, disabled, zeroed/masked and restricted observations retain their
explicit states. Unsupported buses do not receive guessed parent resources or
Windows-only resource categories.

The tree and device Resources tab use the same native collector and formatter,
including documented flags, allocation states and source attribution. Tree resource
snapshots are collected in the existing inventory worker, refreshed by live events,
F5 and periodic reconciliation. The tab collects its own snapshot on opening or
Reload, so observations made at different times may differ. No additional worker,
external utility, permission change or PCI BAR-content access is added.

Double-click or Properties on an assignment opens its reporting device's dialog.
Resource rows are not extra devices: shown counts and deep-search inputs count each
visible device once even when it occurs under multiple resource types. Ordinary
filtering matches assignment captions/settings as well as device names; deep search
uses the existing metadata collector. Tooltips identify the reporting device and
source. The chosen view persists across launches, with the root expanded and other
branches initially collapsed.

Shared interrupts and overlapping ranges are reported without asserting exclusive
ownership or conflicts. No global /proc entry is attributed by a device-name guess;
these views currently use directly associated PCI/PnP sysfs metadata only.

## Storage properties (Phase 6)

Block devices and NVMe controllers have a **Storage** tab with an entity selector,
source tooltips, relationship notes and a Copy storage snapshot button. Native metadata
includes capacity (kernel `size` is always in 512-byte units), logical/physical
sector sizes, kernel removable-media and read-only flags, udev model/vendor,
serial, revision, transport, partition and cached filesystem identifiers.
Selected SCSI/NVMe identity fields retain their actual parent source paths.
Missing, restricted and failed reads are explicit; no raw disk node is opened,
filesystem probed, filesystem mounted or encryption unlocked.

Partitions are available through **Show virtual and internal devices**, and a
disk's Storage snapshot also includes related partitions. Kernel partition
ancestry and `slaves`/`holders` links expose backing and using devices, including
encrypted/device-mapper, LVM and software RAID layers. Each canonical block
path appears once. Related volumes may span other drives: explicit links preserve
this scope rather than inventing a one-disk/one-volume ownership tree. Capacities
are never summed across layers. Device-mapper names/UUIDs and available RAID
level/state/degraded counts are shown without claiming general disk health.

Mount points come from `/proc/self/mountinfo`, matched by major:minor rather
than guessed device names. They cover the application's mount namespace only;
other containers, sessions and the UDisks daemon may observe different mounts.
Filesystem type and mount root are included so bind mounts/subvolume mounts are
not mistaken for independent physical volumes. No matching entry means no mount
was observed in this namespace, not proof that the volume is globally unused.

Optional UDisks2 uses one asynchronous ObjectManager cache request with a
2.5-second timeout in the existing Properties worker. It does not auto-start the
service. Exact device numbers associate block objects; explicit Drive,
CryptoBackingDevice, Partition.Table, MDRaid/member and optional LVM object links
add service entities. Available drive identity/transport, partition tables,
partition offsets/types/UUIDs and content metadata supplement native data.
Removable/fixed is labelled as a UDisks2 hint, separately from the kernel's
removable-media flag. ConnectionBus is an external-bus hint; native NVMe ancestry
and udev ID_ATA_SATA identify NVMe/SATA where available without guessing from names.
Absent services, denied access, errors and native-only builds leave viewing usable.
Service links and sources are available in Details and Copy all values.

ATA failure prediction and NVMe critical warnings are shown only with a nonzero
cached `SmartUpdated` timestamp, displayed in local time with its UTC offset.
Cached temperature and available ATA bad-sector/failing-attribute counts are
shown separately. Missing interfaces or never-updated caches remain unavailable;
a negative failure prediction is not a guarantee of health. Opening, Reload and
inventory reconciliation never request SMART updates, polling or self-tests.
The service may maintain its cache independently. No write-cache policies or
modifying UDisks operations are added.

Storage reads occur on demand, with bounds of 4096 block/service objects, 256
related entities per source, 4 KiB per metadata string and 4 MiB for mountinfo.
Interruptions and the existing dialog instance checks reject obsolete results.
Related native identities are checked again before delivery; topology remains a
snapshot rather than an atomic kernel/service transaction. Deep search collects
native storage metadata only, avoiding a full service request per searched record.
SATA, NVMe, USB, encrypted/LVM and multi-drive RAID behavior require real-machine
validation; this phase was statically reviewed, not compiled or run.
