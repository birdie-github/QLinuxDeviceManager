# QLinuxDeviceManager: implementation and build reference

This document covers data sources, collection limits, interpretation rules,
optional dependencies and building from source. For an introduction, download
instructions and everyday use, see the [README](../README.md).

The application uses C++17 and Qt 6 Widgets, with libudev for device discovery,
sysfs and procfs for local kernel metadata, and optional UDisks2 storage metadata.

Related reference: [architecture and refresh semantics](architecture.md).

## Contents

- [Application behavior](#application-behavior)
- [Hardware and driver views](#hardware-and-driver-views)
- [Device properties](#device-properties)
- [Storage devices, volumes and health](#storage-devices-volumes-and-health)
- [System information](#system-information)
- [Live device events](#live-device-events)
- [UEFI firmware variables](#uefi-firmware-variables)
- [Linux AppImage packaging](#linux-appimage-packaging)
- [Build and installation](#build-and-installation)
- [Permissions and settings](#permissions-and-settings)

## Application behavior


Launch `qlinuxdevicemanager` from a terminal or its desktop application entry.
The tree starts with your computer's hostname. Select a device and open
**Action → Properties**, use the toolbar or context menu, press **Enter** in the tree, or
double-click a leaf device. Double-clicking an item with children expands or
collapses its branch; Enter or **Properties** opens its properties.

| Action | How to use it |
| --- | --- |
| Refresh the device inventory | **F5** or **Action → Refresh** |
| Search device names | **Ctrl+F** or **View → Filter** |
| Search properties and resources | Enable **Deep search** in the search bar |
| Clear and hide the search bar | **Escape** or **Ctrl+F** |
| Reveal partitions and implementation records | **View → Show all devices** |
| Open the system overview | **Alt+I**, **System Information** (leftmost toolbar button), **File → System Information**, or Properties on the computer root |
| Reread a device's properties | **Reload properties** in its dialog |

Deep search collects device identifiers, generic properties, resources and native
storage metadata in the background. Specialized hardware-tab information is not
fully indexed; camera capabilities, desktop display configuration and UDisks2
health are excluded. Its cache is refreshed after inventory changes or an F5
refresh. Storage deep search does not query UDisks2 for every device.

Generic unsupported fields are labelled **Unsupported** without inferring a build
limitation. Storage reporting distinguishes missing build support, an unavailable
health interface and an unsupported detailed UDisks2 health query. Common read
errors explain timeouts, supported limits, temporary unavailability, invalid data
or queries, and cancellation; errno remains available in the displayed and copied
text. Other errors retain the generic read-error message and errno. Temporary
unavailability alone is not treated as proof that a device changed.

Bounded directory readers distinguish a successful empty directory from open,
enumeration and entry-metadata failures, and discard partial lists on failure.
Directory symlinks, case-insensitive wildcard filters and deterministic sorting
are preserved; dangling links and hidden entries remain omitted. Storage callers
explicitly accept absent optional `slaves`/`holders` directories only while the
owning block-device directory still exists; other read failures remain visible.

Virtual and internal devices are hidden by default to keep the hardware browser
readable. This includes UEFI variables, partitions, USB interfaces, input and sound endpoints,
DRM card/render nodes, disconnected and disabled display outputs, and many
transport or unbound kernel objects. Connected or enabled physical display
outputs remain visible by default; outputs with unknown/unreadable state also
remain visible. F5 rereads their kernel connection and enabled state, independently
of X11 or Wayland. Enabled means configured, not necessarily powered on. Visibility is
not a health or connectivity verdict. Friendly names retain kernel identifiers
where needed to distinguish identical devices; tooltips explain their sources.
Some related records are grouped in the default view and remain individually
accessible with internal devices shown.

USB-backed network interfaces use the nearest owning USB device's vendor and
product name, preferring local hwdb names and falling back to readable USB
manufacturer/product descriptors. Their interface identifiers remain in brackets.
Name sources identify that USB parent; raw interface properties are retained.
Missing device metadata does not cause a hub's or neighboring adapter's name to
be substituted, and genuine underscores in reported names are preserved.

Startup shows **Initializing…** and an indeterminate progress bar in the status
bar while the initial inventory is collected. The scan has no reliable percentage
total. The indicator disappears when the first result or error arrives; later
refreshes use the normal status message.

Every launch starts in **Devices by type**, with **Show all devices** off and the
main toolbar visible. The computer root starts expanded so its categories are
visible; all subtrees start collapsed. View state is not saved on exit. Window
size and position persist, and expansion and selection are retained during
updates within the session.

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
| Network | IPv4/IPv6 addresses and masks, MAC addresses, MTU, and Ethernet link speed, duplex, autonegotiation and modes |
| Display | Reported monitor identity, dimensions and input capabilities, kernel mode names, plus desktop-session resolution, color depth, refresh rate, rotation/reflection and scale |
| PCIe | Current link speed/width, reported device maximum speed/width, and connection type when running as root |
| Camera | Driver-reported capture formats, resolutions and frame intervals |
| Battery | Reported capacities, estimated retention, cycle count, chemistry, voltage, percentage, status, current and power |
| USB | USB version, connection speed, active configuration, declared power/current, remote wakeup capability and interface functions/drivers |
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

The **PCIe** tab uses scrollable, grouped label/value sections on PCI device
nodes exposing at least one PCIe link attribute (including unreadable attributes).
Ordinary PCI devices with none of these attributes and non-PCI children do not
get an empty tab or inherit a parent's link information. Current link speed/width
are separate from the device's reported maximum speed/width. Known rates from
2.5 through 128 GT/s are annotated as PCIe generations 1 through 7; unfamiliar
speed strings are preserved. Lane widths use `xN` notation. Unknown speed, a
reported zero width and the kernel's unknown maximum-width sentinel (`255`)
are unavailable, not a healthy/zero-speed link or a 255-lane capability. Other
widths exceeding the six-bit PCIe width field are malformed. Missing fields,
unsupported queries, permission failures and malformed values are shown explicitly.

These are kernel-reported link snapshots, not measured throughput. Device maxima
are separate capability declarations, not a guarantee about the slot or upstream
path, which is not assessed. Reduced current speed/width is not automatically a
fault: power management and topology can explain it. Four bounded sysfs link
reads run in the existing properties worker, with cancellation and device-instance
checks. The kernel may temporarily runtime-resume a device while servicing these
reads. **Reload properties** refreshes the snapshot; **Copy PCIe properties** keeps
section headings and field labels. Raw readings also remain in advanced Details.

When running as root (including through `sudo`), the same on-demand worker also
reads the selected device's PCIe connection type from its sysfs `config` file.
It follows at most 48 standard capability headers within the first 256 bytes,
reading only the necessary header fields and PCI Express flags; invalid offsets,
loops, truncated reads, permission failures and removal are handled explicitly.
It does not dump configuration space, scan extended capabilities, access BARs,
write settings, invoke subprocesses or poll in the background. Normal-user runs
skip configuration reads and show a note explaining that root access is needed
for the connection type; elevation does not guarantee all information is available.

A confirmed **Root Complex Integrated Endpoint** or **Root Complex Event Collector**
shows that conventional PCIe link speed and lane width are not applicable. Its
current/max-link sections and upstream interpretation are replaced with that
connection explanation. Other devices and failed type reads retain the link
sections. This is a generic PCIe topology classification, not an iGPU-ID database,
and it makes no claim about maximum operating performance. Raw sysfs link readings
remain available in advanced Details, including on integrated endpoints.

The **Battery** tab appears only on `power_supply` nodes identified as type
`Battery`, including peripheral batteries exposed by their drivers; it does not
appear on AC/USB chargers merely because they share the power-supply category.
Its grouped label/value sections show presence, charging status, charge percentage,
voltage, current and power, available energy/charge capacities, cycle count,
chemistry and identity metadata. All values belong to the selected supply.
Reads are bounded to the fixed attribute list in the properties worker, on demand
only; there are no subprocesses, polling, setting changes or battery tests.
**Reload properties** refreshes the snapshot and **Copy battery properties** keeps
section headings and field labels. Sources are available in field tooltips.

Energy is displayed in Wh and charge in mAh, in separate sections when reported.
Estimated capacity retention is `100 × full / design`, computed separately from
positive matching energy or charge measurements; values over 100% are preserved.
It is a firmware-based estimate, not a complete health verdict. No energy/charge
conversion, inferred charge percentage or calculated power is substituted for
missing readings. Current and power retain their reported signs. Cycle count
`0` means unavailable under the kernel ABI; zero full/design values cannot support
a retention estimate. Invalid numeric readings, unsupported queries and read
failures remain explicit. A reported absent battery has measurements marked not
applicable. Changed type/presence during collection invalidates the battery values;
a failed type refresh on a previously identified battery shows its read status.

The **Network** tab uses grouped, aligned label/value columns like System
Information, with Link and IP addresses sections for each interface. Each IP
address is shown as `address/network-mask`, using the literal IPv4 or IPv6 mask.
The tab also covers interfaces grouped under a PCI network adapter. Addresses
are from the application's network namespace, including IPv6 link-local addresses
with scope IDs. The hardware MAC
is the driver's permanent address; zero or missing driver results are unavailable.
An overridden MAC is the current address when it differs from the permanent
address, or Linux reports a random, inherited or explicitly set address. If neither
source establishes an override, its status remains unavailable. Non-Ethernet
interfaces show MAC fields as not applicable. Opening or reloading this tab only
reads configuration; it does not change addresses, MTU or interface state.

For Ethernet-style interfaces without kernel wireless markers, Network adds an
**Ethernet link** section with carrier state, operational speed, duplex,
autonegotiation, supported link modes/capabilities and link-partner advertised
modes. Wi-Fi shares Ethernet-style MAC framing but is excluded using its
`phy80211` and `wireless` sysfs markers; failed classification reads are shown
explicitly rather than treated as proof of Ethernet. Virtual Ethernet interfaces
may provide some fields or report the queries as unsupported; the section does
not establish a physical cable or port. Interfaces remain separately attributed
by their exact ifindex in the application's network namespace.

Collection uses native, read-only ethtool generic-netlink `LINKSTATE_GET` and
`LINKMODES_GET` requests, with a shared two-second reply-wait budget per interface
and cancellation checks at most every 50 ms during waits. Replies are limited to
64 KiB, mode bitsets to 4096 entries, and names to 127 bytes; malformed, oversized,
timed-out, unsupported and permission-denied replies remain explicit. No ethtool
executable, new library dependency, setting changes, cable tests, notification
subscriptions or background polling are added. These queries run only for explicit
Properties snapshots, not deep search. Kernel-provided mode names are preserved,
including capabilities such as pause or media type; unnamed bits retain their
index. Empty/missing partner advertisements stay unavailable. Carrier is checked
again after settings collection; detected changes or interface replacement discard
the Ethernet snapshot. A disconnected link has speed/duplex/partner modes marked
not applicable while supported capabilities and autonegotiation can remain visible.
Forced links do not claim a partner advertisement, and unknown speed never becomes
zero Mbps. Use **Reload properties** and the existing network copy button.

Installed module descriptions, filenames, licenses and versions require optional
libkmod support. Installed metadata can differ from code already loaded into the
kernel. Declared firmware filenames do not establish which firmware is loaded or
its version; directly reported firmware revisions are shown where available.

The **Display** tab is available on DRM output connectors and uses grouped
label/value sections like Network.
Monitor hardware appears above the active desktop configuration, using the exact
selected connector's cached `edid` and `modes` sysfs files. Optional
**libdisplay-info >= 0.1.1** decodes reported manufacturer/model, product code,
serial, image dimensions and digital input type/color depth. No EDID decoder is
bundled: without this library, decoding is explicitly unsupported while kernel
mode names and desktop settings remain available. Configure with
`-DDISPLAY_INFO=OFF` to disable it; CMake reports whether decoding is enabled.

Reads run only in the properties worker on demand, never force connector detection,
change settings or poll in the background. EDID is limited to 32 KiB and mode
text to 64 KiB / 2048 entries. Header, declared length and all block checksums
must pass before parsing. Unsupported EDID versions, corrupt framing, permission
failures and missing data remain distinct. Parser conformance warnings are shown
with decoded reports, rather than claiming that imperfect EDID is fully valid.
Connection status and EDID are reread to detect changes during collection; reload
if the report changed. Disconnected kernel connectors do not expose stale EDID.

Image dimensions are the reported image area, not enclosure dimensions; absent
or aspect-ratio-only measurements remain unavailable. Declared color depth is
separate from active desktop depth. EDID can be overridden or supplied by an
adapter/KVM, so identity and capabilities are reported claims. Kernel mode names
are deduplicated and may reflect driver additions/restrictions: they are not a
complete EDID timing catalog, do not distinguish refresh rates, and do not imply
that a mode is guaranteed to work. The existing display copy button includes
hardware headings and labels, and **Reload properties** refreshes both snapshots.
Native X11/RandR queries run in a private headless child of the same executable,
requested by the properties worker; Wayland queries run directly in that worker.
`libxcb.so.1`, `libxcb-randr.so.0` and `libwayland-client.so.0` are loaded on demand,
without linking the application to either session API.
The selected Wayland session is queried directly even when the application runs
through XWayland; a failed Wayland query never falls back to emulated X11 outputs.
Queries do not change configuration, force detection or create a display surface.
Protocol reply waits share a two-second budget per collection. The parent also
enforces a three-second deadline for the entire X11 child, including connection
setup, blocking XCB calls and cleanup. It checks cancellation at most every 25 ms,
kills the child on cancellation or timeout, and uses only nonblocking child reaping.
The JSON reply is limited to 64 KiB and validated before publication. At most one
unreaped X11 child is retained; subsequent requests refuse with temporary
unavailability until it exits. Parent death also kills the child. This avoids
waiting for an X11 server or a stalled XCB call during application shutdown.

Resolution is the current physical mode before rotation/scaling. Refresh rate is
the nominal mode rate, not a measured or instantaneous variable-refresh rate.
X11 desktop color depth is the screen's root pixel depth, not panel/link depth;
Wayland output protocols do not expose it, so it remains unavailable. Output scale
is the X11 RandR transform (framebuffer pixels per output pixel, excluding desktop
DPI) or the Wayland rotated mode/logical-size ratio. The latter is marked derived
because logical dimensions may be rounded. Without logical-size information,
Wayland reports its integer buffer scale explicitly. Qt application scaling is
excluded; arbitrary X11 transforms are shown as custom instead of inventing a
single scale factor.

Session output names normally match a unique kernel connector. The conventional
X11 `HDMI-N` / kernel `HDMI-A-N` naming difference is normalized. A name match
is checked against complete EDID evidence when available. If no X11 name matches,
its EDID conflicts, or the namesake is disconnected, a fallback compares
complete EDIDs from cached sysfs files and RandR output properties. Descriptors
must have valid framing and checksums, and the selected EDID must occur exactly
once among connected connectors on each side. Missing, corrupt, truncated or
duplicate descriptors leave the fallback unavailable; no numbering offset or
manufacturer/model guess is used. The existing ambiguous kernel-name guard is
retained. Kernel descriptors and the selected RandR descriptor are checked again
before publication, along with RandR timestamps, to detect observed changes.
Connector and EDID reads are bounded (256 connected outputs, 32 KiB per EDID);
RandR requests share the existing two-second budget and cancellation checks.
Sysfs reads access cached metadata. A killed X11 child can remain stuck in an
uninterruptible kernel operation, but the properties worker does not wait for it.
The separate monitor-hardware sysfs reads still run in the properties worker.
The Output tooltip identifies name versus EDID matching and unavailable verification.
Ambiguous or unmatched outputs remain unavailable rather than borrowing another
display's settings. Older Wayland compositors need xdg-output
v2 for names; otherwise wl_output v4 names are used. X11 queries cover the default
X screen. Missing libraries or protocol support leave the hardware view usable.
Disconnected connectors show **Configuration: Disconnected**, based on a fresh
DRM status read. Connected
outputs without a mode retain **No active mode**. Zero rotation is shown as
**0°**; nonzero angles retain their counterclockwise direction.
Use **Reload properties** to refresh the monitor and desktop configuration snapshot.

The **Camera** tab appears on V4L2 image-capture nodes. Driver/card/bus
information uses grouped fields; a mode table keeps capture queue, pixel format,
resolution and frame intervals together. The table shows all rows vertically,
scrolling with the Properties page; column headings and cells wrap to the
available width and are remeasured when resized or the theme/font changes.
The live table omits numeric pixel-format IDs and uses concise names such as
**MJPEG** and **YUYV 4:2:2**. Copy retains the original FourCC, numeric ID, driver
description and format annotations. Unfamiliar formats keep their reported names.
Single-plane and multi-plane capture
are kept separate. Metadata-only, radio/output and memory-to-memory codec nodes
are excluded after capability detection; if access fails, a video node retains
the tab to explain the permission, busy, removed or unsupported query state.
The tab describes capture-node capabilities, not necessarily a physical camera.

The properties worker invokes a private headless mode of the same executable,
using **libv4l2** to pass native `VIDIOC_QUERYCAP`, `VIDIOC_ENUM_FMT`,
`VIDIOC_ENUM_FRAMESIZES` and `VIDIOC_ENUM_FRAMEINTERVALS` queries to the driver.
The device is opened read-only/nonblocking. It is not registered for libv4l's
conversion/streaming setup, so converted formats are not added by the library.
No controls, formats, frame rates, buffers or streaming state are changed; no
frames are read. Opening a device can still wake hardware through its driver.

The helper is isolated because nonblocking opens do not give arbitrary driver
ioctls a timeout. The worker waits at most two seconds for its reply, checking
cancellation every 25 ms; it kills the helper on timeout/cancellation and never
blocks waiting for it to exit. A driver stuck in uninterruptible kernel sleep
may delay exit: only one unreaped helper is allowed, and later camera requests
retry reaping it rather than accumulating processes. This is on-demand collection,
with no background hardware polling. Queries are bounded to 4096 ioctls, 128
formats per queue, 128 sizes per format, 128 intervals per discrete size, 2048
mode rows and a 1 MiB reply. Limits and partial enumeration are explicit.

Each discrete size gets its own frame-interval query. Continuous/stepwise size
ranges remain ranges: frame intervals are marked size-dependent and are not
sampled or assigned to every size. Interval ranges retain their rational seconds
and interval step, rather than inventing a uniform frame-rate step. Discrete
rates are calculated as the reciprocal of the reported interval and retain the
exact rational interval. Missing/unsupported enumeration never becomes zero fps.
These are driver reports for the current media pipeline, not measured capture
rates or guarantees of simultaneous-stream throughput. Device-node major/minor,
connector directory identity and `/sys/dev/char` ownership are checked before
and after querying; a replaced/removed node discards the report. **Reload properties**
and **Copy camera properties** use the same owned snapshot, including headings
and labels for every mode. Capability queries are excluded from deep search.

The **USB** tab appears only on USB device nodes (including hubs), with grouped
label/value sections and a copy button. Its first **Connection** section shows the
reported USB specification version and kernel-reported connection speed in Mbps
or Gbps, using the same snapshot as the raw Details fields. Version is the device
descriptor's specification version, not the port's maximum capability; speed is a
link rate, not measured throughput (for root hubs it describes the USB bus).
It also adds active/available configurations, configuration description, interface
count, declared power source and maximum
current, remote wakeup capability, and each direct USB interface's function codes,
alternate setting, description and bound driver. Values are read in the properties
worker from sysfs; no control transfers or configuration changes are performed.
Power/current declarations are not live measurements, and remote wakeup capability
does not mean wakeup is enabled. Voltage is not inferred. Identity, revision and
bus/address metadata remain on **Details**. USB version and speed remain there
as raw values too; enable **Show advanced properties** to see raw sysfs values.
Interface snapshots are bounded to 256 direct children; read failures and changes
during collection are reported explicitly. Use **Reload properties** to refresh.

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

Health uses scrollable group boxes, with one font-derived caption width across
the report: **Health and endurance** for NVMe (**Health** for ATA), **Usage**,
**Errors** and **Temperatures**, omitting empty groups. The update timestamp
appears once at the top of the summary. Captions avoid repeating "Cached";
the report notice and copied header identify the cached scope. Known NVMe
critical-warning tokens have readable explanations; unknown tokens remain
visible. No generic healthy verdict is inferred from missing fields.

The Health drive selector initially prefers the selected device's uniquely
associated drive, using explicit block mappings or controller descendants.
Related drives remain listed. Reload preserves the user's selected drive when it
is still present; otherwise the unique association is preferred again. When no
unique association exists, the selector uses the first listed drive.

For NVMe controllers with a valid health-cache timestamp, the Health tab also
shows cached power-on hours and calls `SmartGetAttributes` for spare capacity,
spare threshold, estimated endurance used, total bytes read/written, controller
busy time, power cycles, unsafe shutdowns, media/data-integrity errors, error-log
entry count, temperature sensors, temperature thresholds and time above them.
The method returns UDisks2's stored log; it does not request a SMART update.
ATA and NVMe detailed-cache calls share a 2.5-second wait budget across related
drives, starting at the first detailed query. Drives explicitly associated with
the selected block device or its descendant namespaces/partitions are queried
first; object-path order is retained within each priority tier. Spanning volumes
without such an association do not imply a preferred drive. After the budget is
exhausted, remaining detailed queries are reported as **Not queried: shared
health-query time limit reached**, separately from requests that were sent and
timed out. Basic cached properties remain available for all related drives.
Collection uses cancellation checks every 50 ms, no service activation or interactive
authorization, and accepted replies limited to 64 dictionary entries and eight
sensors. Missing keys, invalid D-Bus types, unsupported methods, permission
failures and timeouts stay explicit; the basic cached health fields remain usable.

Counters and percentages preserve zero. Estimated endurance used can exceed
100%; it is a vendor estimate, not a failure verdict. Zero sensor/threshold
values and zero power-on hours are unavailable under the API. Integer-Kelvin NVMe
temperatures are converted to whole Celsius degrees; ATA temperatures retain
one decimal place where the source provides finer precision. Unreported NVMe
sensor slots are omitted; a failed sensor-array decode remains visible as one
sensor-status field. Data read/written are UDisks2 byte
estimates, not raw NVMe data-unit counters. Counts summarize error-log entries;
individual error records, past self-test logs and thermal transition counters
are not exposed by this collection. Health data is controller-wide and is not
attributed as namespace-specific measurements.

ATA HDDs and SSDs also use cached `Drive.Ata.SmartGetAttributes`, including
USB-attached drives when UDisks2 exposes that interface through their bridge.
The Health tab shows power-on seconds/hours, past attribute-failure count and
all returned attributes in an alternating-row, read-only table ordered by ID.
Columns retain the authoritative drive-specific name and ID, current/worst/threshold
normalized values, flags and UDisks2's interpreted value. Header and cell tooltips
explain normalized values, flags and provenance. The table grows with its rows
and scrolls with the report; horizontal scrolling is available at narrow widths.
Both health-copy buttons include the summary groups and every ATA table row,
including exact values and sources.
Unknown normalized values (`-1`) remain unavailable; valid zeros are preserved.
Unit 0 means no interpreted value. Known interpreted units are dimensionless,
milliseconds, sectors and millikelvin; unknown units are not guessed. These are
interpreted values, not raw smartctl counters. Attribute IDs alone are not used
to infer SSD endurance or assign vendor-specific meanings.

ATA replies must match `a(ysqiiixia{sv})`, with at most 256 unique attributes,
128 UTF-8 bytes per name and 64 expansion entries per record. Oversized or
malformed lists are rejected as a whole. No tests, SMART updates, drive wakeups
or SMART-enable operations are requested. Drives without an ATA or NVMe health
interface show unsupported health access: this includes native SAS/SCSI devices
not exposed through these APIs, and many removable/virtual devices. USB support
depends on the bridge and UDisks2. No universal SMART capability is inferred from
whether a device is an HDD, SSD or connected through USB.

## System information

The system overview shows hostname, operating system, kernel architecture/release/
build, CPU models, sockets and cores, present and online logical processors, unique
cache totals by level/type, uptime, motherboard identity, firmware version/date,
boot-mode evidence and reported hypervisor identity. Its existing sections are
presented as scrollable tabs: **System**, **BIOS/UEFI**, **Processor**, and
**RAM/Memory**. The System tab starts with system manufacturer/model and motherboard
manufacturer/model, followed by a horizontal separator and the remaining system
fields. The BIOS/UEFI tab shows firmware version/date, then Secure Boot status
and MOK certificate groups. Secure Boot status also appears directly below Boot
mode on System. Both labels use the same snapshot and source tooltip; Refresh
updates both, and Copy all includes the value once. Refresh and Copy all cover
every tab.

The **System** tab includes **Resizable BAR (GPU)** for PCI display controllers.
It separates kernel-exposed capability (**Supported** or **Not exposed by kernel**),
current allocated BAR aperture sizes, supported sizes and the **BIOS setting**.
The BIOS setting is shown as **Unknown**: the generic sysfs interfaces do not
provide a portable firmware Enabled/Disabled value. GPU support alone does not
prove firmware enablement. A 256 MiB BAR is not treated as disabled, and a large
allocation does not establish that the graphics driver uses the full aperture.
Missing interfaces do not establish that the hardware or BIOS lacks support.

Collection reads only `class`, `resourceN_resize` and `resource` metadata in the
system-information worker, with bounded PCI/GPU enumeration, cancellation and
observed replacement/allocation-change checks. No configuration changes, resizing,
BAR memory access or privileged helper is used. Permission failures remain explicit;
the current allocation can still be shown when supported-size reads are restricted.

The Processor tab also shows instruction-set and CPU-feature declarations from
per-processor `flags`, `Features` or `isa` in the existing `/proc/cpuinfo` snapshot.
Kernel tokens are preserved and identical lists are grouped. Different reported
lists remain separate rather than claiming their union applies to every CPU.
Missing per-processor lists leave the result unavailable. These declarations
include non-instruction features and reflect kernel/OS/hypervisor exposure, not
an exhaustive raw hardware inventory.

**Physical RAM** uses available firmware-reported capacities from udev metadata.
**Usable RAM** uses Linux `MemTotal`; it is not currently free memory. When both
values are valid, their difference is shown as system-reserved RAM, covering all
memory unavailable to Linux rather than only firmware reservations. Missing
capacity is left unavailable instead of guessed.

**RAM/Memory** follows **Processor**, with Physical RAM and Usable RAM at the top,
above the firmware memory-device records. Each cached udev `dmi/id` record shows capacity, explicit presence
when supplied, device/bank locators, type, form factor, rated/configured transfer
rates in MT/s, manufacturer and part number. Volatile and nonvolatile capacities
remain separate when reported. The same cache snapshot supplies the existing
Physical RAM calculation; its interpretation is unchanged.

These are firmware records, not proof of removable modules or physical slots.
Soldered memory and virtual machines may expose several records; duplicate or
missing locators do not merge them. Rates are firmware declarations, not live
measurements. Channels, upgradeability and active ECC are not inferred. Missing
presence does not mean absent, missing capacity does not mean zero, and unknown
or placeholder metadata remains unavailable. Inconsistent or malformed numeric
values retain explicit errors. No raw SMBIOS, SPD/SMBus probing, subprocess,
privilege escalation or extra dependency is used. A missing cache does not imply
that running as root will help.

Collection supports cancellation and bounds strings to 256 bytes, metadata to
1 MiB and detailed records to 256; exceeding a bound publishes an explicit error
instead of a truncated list. Installed-RAM totals retain their existing 4096-record
limit. Optional metadata failures do not invalidate a collected RAM total.
**Refresh**, source tooltips and **Copy all** include the memory records and labels.

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

The category and its subtree are hidden by default. Enable **View → Show all devices**
to reveal them; disabling it hides the subtree again.

Names are separated into readable words; original names remain in tooltips.
Namespace GUIDs and full filenames are shown with internal devices enabled.
Equal names in different namespaces remain separate records.

Opening Properties reads up to **64 KiB** of the variable file as a hex/ASCII dump,
including its attribute prefix. Longer values receive a truncation notice.
Contents are not decoded or modified, and actual access can still be denied by
security policy despite the file's permission bits.

System Information also shows firmware Secure Boot status and exposed runtime MOK
certificates (owner, issuer and expiration in UTC). This is certificate metadata,
not a check of the running kernel's signer or certificate trust. Reading MOK data
may require permissions; unavailable data is shown explicitly. The kernel's MOK
table is preferred, with efivarfs as a fallback. Oversized or split fallback lists
are reported as errors rather than displaying a partial list. Qt's TLS backend
must be installed to decode X.509 certificates.

## Linux AppImage packaging

The [Linux AppImage workflow](https://github.com/birdie-github/QLinuxDeviceManager/actions/workflows/ci.yml)
packages an x86_64 AppImage on pushes, pull requests and manual runs. Download the
artifact from a successful run and extract the ZIP, then launch it:

```sh
chmod +x QLinuxDeviceManager-*.AppImage
./QLinuxDeviceManager-*.AppImage
```

The package includes Qt 6.8, X11/Wayland platform plugins, application icons and
translations, and enables libkmod, QtDBus and libdisplay-info monitor decoding. It is built on Ubuntu 24.04;
older distributions are not guaranteed to be compatible. The host still provides
the Linux kernel, device metadata and optional UDisks2 service.

If FUSE is unavailable, launch with `--appimage-extract-and-run`. Workflow artifacts
are retained for 14 days; they are not automatically published as release assets.

## GPU PCIe link activation test

A PCI display adapter with a conventional PCIe link offers **Test link under
load…** in Properties → PCIe. The optional Vulkan renderer runs only after that
explicit action, as an ordinary user. The popup identifies the GPU by its Vulkan
name and complete PCI address, displays an original procedural wave shader, and
shows live link speed/width. No Seascape code or other supplied shader is used.
There is no NVML, CUDA, vendor command, network access, sysfs write, link retraining
request, or change to driver power settings.

The offscreen graphics load submits work continuously for up to 15 seconds, or
until Stop/close; stopping waits for any already submitted work to finish. The shader is unchanged. A dedicated rendering worker owns a fixed
640 × 360 color target, command buffer and completion fence, with only one
submission in flight. After completion, it immediately records and submits the
next frame: there is no frame timer, swapchain, presentation wait or VSync limit.
The five-millisecond fence timeout only polls cancellation; a completed fence
returns immediately. A GPU submission that does not complete within five seconds
ends the test with an error, subject to the parent deadline if cleanup stalls.

The preview reads back at most one rendered image about every 100 ms. Its GUI timer
consumes one replaceable image slot; it does not schedule rendering or accumulate
per-frame events. Minimized/covered windows do not pause the workload. The popup
also reports completed offscreen frames per second, independently of the preview
refresh rate; this is diagnostic information rather than a GPU benchmark score.

The **Current link** table places **Maximum observed link** between Link speed
and Link width for PCI display adapters. A compact **Test/Retest** button sits
beside that row's caption only when the current kernel speed and width are readable
and pass the sampler's validation. Unknown/zero/sentinel readings show an unavailable
explanation without a test button; they do not establish that the GPU is integrated.
Known root-complex types continue to show no conventional PCIe link. The helper
also checks fresh link readings before Vulkan startup or opening the workload window,
so unavailable readings do not cause a popup to flash and immediately close.
Its value contains only the observed speed/width pair;
the timestamp and interpretation are in its tooltip and timestamp is included by
**Copy PCIe properties**. There is no separate test button or result footer.
Current and observed links use generation and lane count in their main values,
for example **PCIe Gen 3 ×16 (16 lanes)**. Advertised maximum speed uses the
generation alone, with the independently reported maximum width kept in its own
row. Raw signalling rates remain in the corresponding tooltips, explicitly per
lane. Snapshot rows and the load-test popup share the same generation formatter;
unknown rates remain visible without an invented generation. No throughput in
GB/s is measured or inferred for these fields. Empty or whitespace-only tooltip
sources produce an empty QString instead of an empty rich-text tooltip box.
The advertised maximum in **Device capability** remains a separate property.
The child returns numeric speed/width and an ISO timestamp, so the UI never parses
a translated summary to extract the observation. Speed
ranks first; width breaks a speed tie. It never combines separately observed speed
and width maxima. The normal snapshot and device capability fields remain
separate; an observed result is neither a bandwidth benchmark nor proof that the
hardware reached its maximum. A shader workload may leave a link below its
maximum on some drivers. No pass/fail judgment is made from the observed rate.

The child requires Vulkan 1.1 and a unique `VK_EXT_pci_bus_info` match for the
selected domain:bus:device.function. It refuses software renderers and never
selects by vendor/device ID or model name. The selected physical device is
rechecked before creating its logical device and recording the shader workload.
The child creates its Vulkan instance before starting the rendering worker and
destroys it after the worker joins; device/queue/resource access stays on that
worker. No Vulkan instance or GPU context is created by the main device manager.
Rendering needs no presentation support or Vulkan window; the preview is an
ordinary Qt image. Unavailable drivers, PCI identity or offscreen resources
produce an explanation rather than a test on another GPU.

A single child worker samples the selected directory's documented
`current_link_speed` and `current_link_width` attributes at roughly 250 ms
intervals. Each read is capped at 128 bytes. Repeated reads discard an observed
speed/width transition; sysfs does not provide an atomic pair. Only samples near a
completed graphics frame are retained. The child checks the directory identity
against the inventory and during collection, and the parent cancels on an
observed removal/replacement. These checks cannot eliminate every hotplug race.
Permission failures and malformed/missing readings are reported explicitly.

The main process permits one test at a time and uses asynchronous QProcess
signals. Startup has a five-second child deadline; the parent kills the entire
child at 25 seconds, including teardown time. Closing Properties kills its test;
Linux parent-death notification also prevents an orphan workload. Graphics calls
or sysfs reads can still stall inside the child; its own timers cannot interrupt
a blocked driver call. The parent deadline isolates the viewer from that stall,
but cannot guarantee recovery of a hung kernel driver or GPU. The target and
readback buffer are fixed in size (640 × 360 RGBA pixels); their two Vulkan memory
allocations are each capped at 64 MiB to allow driver alignment requirements.
GPU queues, resource allocations and the pending preview remain bounded even
though frame rate is uncapped. Readback memory is invalidated after its fence and
transfer-to-host barrier before copying into an owned QImage. CPU preview copies
never reference memory that the GPU may overwrite.

Build support is enabled automatically when Vulkan development headers,
`glslangValidator`, and Qt's Vulkan instance support are available. The shader is
compiled to embedded SPIR-V at build time; no runtime compiler is required. Use
`-DGPU_LINK_TEST=OFF` to omit it. CMake reports whether support was enabled; normal
hardware viewing works without it. Run the viewer without sudo for this test.
Fixture target `gpu-link-sample-test` checks parsing and paired peak selection;
actual rendering, adapter selection and link activation require hardware tests.

## Build and installation

`project.json` declares the minimum Qt version and C++ standard in `platform`.
CMake reads both values from the manifest, uses the Qt minimum for required and
optional Qt components, and derives `QT_ENABLE_STRICT_MODE_UP_TO` from that version
for the application and fixture targets. A major.minor Qt minimum uses patch zero
for this encoding. The requirements remain Qt 6.8 and C++17.

Requirements:

- Linux and a C++17 compiler.
- CMake **3.19 or newer** and pkg-config.
- Qt **6.8 or newer**, including Widgets and Network, and libudev development headers.
  Qt Network is used locally to decode MOK certificates; this feature makes no network requests.
- libv4l2 development headers/library (Fedora: **libv4l-devel**; Debian/Ubuntu: **libv4l-dev**).
- XCB RandR and Wayland client **1.20 or newer** development headers (runtime
  libraries are required only for the corresponding Display backend).
- Optional QtDBus **6.8 or newer** for cached UDisks2 storage metadata.
- Optional libkmod **30 or newer** for installed-module metadata and fallback names.
- Optional libdisplay-info **0.1.1 or newer** for monitor EDID decoding.
- Optional Qt Linguist tools **6.8 or newer** to build translation catalogs.
- Optional Vulkan development headers and `glslangValidator`, with Vulkan-enabled
  Qt, for the GPU link test. Runtime needs a Vulkan 1.1 driver exposing PCI identity
  (offscreen rendering does not require Vulkan presentation).

Fedora packages:

```sh
# Optional monitor EDID decoding: libdisplay-info-devel
sudo dnf install gcc-c++ cmake make pkgconf-pkg-config qt6-qtbase-devel systemd-devel libv4l-devel libxcb-devel wayland-devel
# Optional:
sudo dnf install qt6-qttools-devel kmod-devel libdisplay-info-devel
# Optional GPU link test:
sudo dnf install vulkan-headers glslang
```

Debian/Ubuntu packages:

```sh
# Optional monitor EDID decoding: libdisplay-info-dev (>= 0.1.1)
sudo apt install g++ cmake make pkg-config qt6-base-dev libudev-dev libv4l-dev libxcb-randr0-dev libwayland-dev
# Optional:
sudo apt install qt6-tools-dev qt6-tools-dev-tools libkmod-dev libdisplay-info-dev
# Optional GPU link test:
sudo apt install libvulkan-dev glslang-tools
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

## Processor core groups and cache sharing

The Processor section retains present/online CPU and physical core counts and adds
SMT policy, online core groups, and cache sharing domains. Core groups use package,
validated disjoint `cpu_core/cpus` and `cpu_atom/cpus` PMU lists when available,
and cache level/type/size/sharing geometry. No CPU-number, clock-speed, model-name,
or missing-L3 heuristic assigns an LP-E label. Without PMU lists the type is
unspecified; without complete cache data grouping uses package and type only.

Each group reports physical core identities, logical CPU IDs, online threads per
core, and the range of `cpuinfo_max_freq` values. A favored core's higher maximum
does not split an otherwise identical group. Missing or malformed frequency data
makes that group's maximum unavailable rather than displaying a partial maximum.
These are kernel-reported limits, not sustained or measured frequencies.

Cache records are deduplicated by level/type and full sharing set, retaining size
and logical CPU associations. Equal-sized instances are summarized together with
each sharing set shown separately; offline siblings may appear in those sets.
The existing limits on CPU lists and cache enumeration are retained, with at most
65536 unique cache/CPU associations. Failed cache collection publishes no partial
hierarchy. Present/online lists are rechecked before publication and observed
changes invalidate both new detail sections along with the existing totals.

Collection uses the existing system information worker and requires no new command,
CPUID affinity changes, MSR access, or elevated permissions. All new fields are
included in refresh and Copy. The system-properties fixture includes 4/8/4 hybrid,
favored-core, missing-cache, homogeneous SMT, and multiple-package cases.

Processor presentation uses full-width, selectable Qt rich-text tables for core
groups and cache instances, with escaped cell values and no fixed colors. Copy
All exports the same cells as tab-separated plain text. Single-logical-CPU cache
instances use compact CPU ranges; shared instances retain their individual
sharing sets, including SMT siblings. Package IDs appear unless one socket is
reported. Equal present/online counts occupy one field; differing counts and
read failures remain explicit. Frequency ranges and collection behavior are
unchanged.
