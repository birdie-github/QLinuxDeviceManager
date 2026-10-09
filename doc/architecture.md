## Architecture and refresh semantics

- `Device`: owned raw metadata, source attribution, canonical path, actual
  udev parent path, visibility and untranslated category ID.
- `Enumerator`: one bounded worker thread owns all udev objects and selected
  metadata reads, producing a value snapshot. CPU model parsing and cross-record
  presentation grouping live in separate modules. No GUI-thread filesystem scans.
- `DeviceModel`: GUI-thread inventory reconciliation and a custom item model;
  presentation nodes store record IDs, never borrowed inventory pointers.
  A separate `devicelabel` module formats GUI-translated captions from owned
  metadata, without filesystem reads. Display labels are computed once during
  rebuild, used for sorting, and disambiguated within each category with kernel
  identifiers. Raw inventory names and name sources are never rewritten.
- `MainWindow`: UI, settings and tree-state restoration. Refresh requests
  serialize/coalesce; useful snapshots are applied without starving under churn. Categories start collapsed
  on every launch; expanded state is retained only within a running session. Failed scans retain the
  previous inventory. Shutdown requests interruption between records and waits
  asynchronously for the worker before destroying its owner.

Selection survives refresh only for matching path and local generation. The
incarnation hint combines sysfs directory device/inode numbers and an optional
udev initialization timestamp. Phase 4 adds removal evidence and conservative
identity invalidation after monitor loss, described below. Neither inventory
snapshots nor binding observations are atomic with respect to the kernel.

The worker checks cancellation between devices. A kernel metadata read already
in progress cannot safely be forcibly canceled; shutdown can wait for that read.
The GUI event loop remains running during normal shutdown. No asynchronous
callback is permitted to access a destroyed window.

## Default type-view grouping

The inventory retains every discovered record and its real parent/binding.
After classification, presentation refinement follows explicit udev parent
links to the nearest PCI function. Network functions pair only with PCI network
class devices, and NVMe class controllers pair only with PCI class 01/08.
A sound card pairs only with a PCI audio function (class 04), including through
intermediate DSP platform records. A single network interface, ALSA card or NVMe
class record is hidden in favor of the PCI controller row. Multiple network
interfaces or ALSA cards remain visible while the redundant PCI aggregate row
is hidden. Missing parents or unrelated classes never justify
suppression. The internal-device toggle reveals every retained record.

Hardware database lookups are local and optional through the existing libudev
library. They supplement missing names using a device's own modalias and never
borrow an ancestor's driver/vendor properties. Input/video function names are
preferred to generic shared USB model labels. CPU models are matched to the
numeric processor record in /proc/cpuinfo, not inferred from the first CPU or
from global board fields. An unavailable or oversized file preserves readable
kernel-name fallbacks and explicit read states.

## Function descriptions for unresolved devices

The naming priority remains direct device identity/model metadata first, then
selected direct sysfs strings, exact standard ACPI/PNP and kernel function IDs, optional module
descriptions, and existing kernel/driver fallbacks. Function labels are stored
as untranslated text and translated by the UI. Raw kernel names accompany them.
No name lookup changes record IDs, category, driver binding or visibility.
Verified Linux USB root-hub captions override generic database/product text;
external USB devices retain the usual model-name priority.

`ModuleNames` owns its libkmod context in the enumeration worker. It reads the
running kernel's installed module indexes without modprobe.d overrides or
commands. The refresh-scoped cache contains at most 512 entries, including
misses; each description is capped at 4096 bytes. A new context/cache on every
refresh allows installed module metadata to be updated without retaining stale
labels across refreshes. Repeated devices sharing a module reuse the cached
result. No subprocess, module insertion/removal or root access is involved.

A `driver/module` link establishes which module owns the device's direct driver.
Absent that link, modalias lookup can produce only an installed-module candidate;
multiple matches are deliberately rejected. An absent module link never proves
a built-in driver. Installed metadata can differ from code already loaded in
the kernel, and a candidate is not evidence that any driver is loaded or bound.

`libkmod >= 30` is optional. Missing library support, missing kernel modules,
missing built-in metadata, no description and access errors preserve other
naming methods and usable basic viewing. Neither ACPI vendor-prefix guesses nor
recursive sysfs reads are used. Firmware `_STR` methods are not evaluated to
manufacture names for unknown vendor-specific firmware objects.

References:
- https://kmod-project.github.io/kmod/libkmod-libkmod-module.html
- https://github.com/kmod-project/kmod/blob/master/libkmod/libkmod.h
- https://github.com/open-acpica/acpica/blob/master/source/common/ahids.c
- https://github.com/torvalds/linux/blob/master/sound/hda/core/sysfs.c

## UEFI variable directory inventory

`efivariables` adds metadata-only records after ordinary udev presentation
refinement. The worker opens the fixed efivarfs directory without following a
directory symlink, examines at most 4096 entries and checks interruption between
entries. `fstatat` without symlink following accepts only regular files with
S_IROTH set. It never opens a variable value, reads a firmware runtime service,
mounts efivarfs or changes permissions. The public-file filter is independent
of effective UID and the internal-device toggle.

Each record retains its full filename/path as its inventory identity and its
GUID as owned metadata. Names must end in a complete canonical-format GUID;
the preceding original variable name is retained verbatim. The display formatter
separates word/acronym boundaries and separators, preserves standard mixed-case
terms such as WiMAX, NVMe and PCIe, and keeps unknown capital runs intact. ID is
separated at word boundaries and in the known IDEK compound; EDID, UUID and IDLE
remain whole rather than applying an arbitrary split to every ID substring.
It assigns no semantic meaning to vendor variables. Tooltips suppress GUID-bearing filenames and paths
unless the internal view is enabled. Equal display names are disambiguated with
namespace ordinals in path order; these ordinals are presentation labels, not
firmware namespace identifiers. The existing incarnation/generation rules apply
to variable records, and inventory totals now include firmware-variable rows.

Directory enumeration is not an atomic firmware snapshot. Entries may disappear
or change permissions concurrently; refresh reconciles the observed metadata.
The separate Phase 2 properties reader below provides bounded hex inspection
with renewed permission and identity checks; enumeration remains metadata-only.

## Phase 2: device properties

`DeviceModel::device` returns an owned record copy matched by path and generation,
including records hidden by the visibility filter. The modeless properties dialog
holds that identity and never borrows a pointer into the inventory. Only one
properties dialog is open at a time; opening another device closes the previous
one. The tree and Refresh remain usable while a dialog is open.

`PropertiesReader` is a second, window-owned worker, separate from enumeration.
It reads properties only when Properties is opened or Reload properties is
requested. There are at most two worker threads, one active properties request
and one superseding pending request. Closing a dialog invalidates its request;
late results cannot reach a subsequent dialog. Normal shutdown requests
interruption and keeps the GUI event loop running until both workers finish.
Already-running kernel reads and libkmod calls cannot be forcibly canceled.

The properties reader pins the selected sysfs directory with a descriptor and
checks its device/inode identity against the inventory. It also checks the udev
initialization stamp, rechecks the path after collection and rejects a changed
driver link. Documented direct driver/module links are followed explicitly;
no ancestor binding is presented as the child's binding. This improves detection
of observed replacement but is not an atomic kernel snapshot or a guarantee
against every hotplug or driver-rebinding race. The Phase 1 inter-scan instance
limitation still applies until Phase 4 adds udev monitoring.

Collection produces raw `Attribute` values, provenance and bytes. The dialog
alone translates labels, interprets presence/binding/USB authorization, and
formats hex. Sysfs reads use a subsystem-specific allowlist of text metadata;
there is no recursive scan, resource/MMIO access, SMART query or firmware method
evaluation. Text values are limited to 4096 bytes; libkmod output is restricted
to selected fields and at most 4096 metadata entries. Raw values and inventory
naming provenance are available through advanced Details. The copy-all action
includes only currently enabled basic/advanced properties and their sources.

The Driver tab separates direct binding, owning module, running module version,
and installed `.modinfo` values. A live/coming/going module initstate establishes
modular code. Missing module links, versions or initstate do not establish a
built-in driver; ambiguous cases remain undetermined. Installed versions,
authors, licenses, descriptions and firmware declarations can differ from code
already running after an update. Firmware declarations are filenames, not proof
of loaded firmware or firmware versions. Libkmod remains optional; no subprocess,
module insertion or removal is used. Other firmware revisions are shown only
where directly reported by the selected device (currently NVMe).

EFI value collection first checks the entry's identity, regular-file type and
world-read bit, then repeats those checks on the opened descriptor. A final
identity/permission check precedes publishing. This filter also applies to root.
At most 65536 bytes plus a one-byte truncation probe are read; larger values
receive an explicit truncation caption. Details renders the entire file prefix
and payload as hex/ASCII without decoding. Namespace GUIDs, full filenames and
full variable paths remain advanced-only, including in copy-all output.
Permissions or contents may still change concurrently; this is a bounded
read-only observation, not an atomic firmware snapshot.

Removal/replacement detected by a property read or successful inventory refresh
freezes the existing dialog, labels it as a removed snapshot and disables Reload.
Selection, Details and copying remain available. No live removal notification is
claimed before Phase 4. Failed reads preserve the previous snapshot with an
explicit error banner, rather than replacing errors with blank or healthy values.

## Direct resource metadata in properties

`deviceresources` owns typed raw range/IRQ records and strict PCI/PnP parsers.
The existing properties worker passes its pinned device-directory descriptor to
this collector; all reads remain inside the same before/after identity checks.
Collection is on demand with no additional worker, polling or parent-resource
inheritance. The UI adds Resources only when records or explicit collection
errors exist, and retains that data in removed-device snapshots. Successful
property reads hide the routine banner; loading and errors still show it.

PCI input comes from the ASCII `resource` file, `irq`, and the documented
`msi_irqs` directory. `resourceN`, `rom`, `config`, legacy memory/I/O access files
and `/proc` resource tables are not opened. Type and allocation come from the
stable Linux resource flags. Empty slots are skipped without renumbering.
Slots 0–5 are BARs, slot 6 is expansion ROM; later slots retain their numeric
resource identity rather than guessing that every later slot is a bridge window.
64-bit addresses are preserved. Explicit UNSET/DISABLED flags suppress assigned
range presentation, and typed zero/zero ranges are labelled unavailable.
Bridge windows, prefetchability, read-only and 64-bit-memory flags are decoded;
raw flags remain visible. Neither flags nor overlap establishes a conflict.

All observed MSI/MSI-X filenames are interpreted as IRQ vector numbers and their
mode attributes must be `msi` or `msix`. Vectors sort numerically. If vectors are
observed, the legacy `irq` attribute is not added as a second active allocation:
its meaning differs between MSI and MSI-X. Without observed vectors, a positive
`irq` is shown as a reported value with active mode undetermined. Zero in that
PCI attribute indicates no legacy INTx capability and is not presented as IRQ 0.
Disappearing/unreadable vector metadata remains an explicit incomplete read.

PnP uses the kernel's direct `resources` text representation: device state,
io/mem/irq/dma/bus entries, disabled markers and explicit window markers. PnP
IRQ 0 is retained; its semantics differ from the PCI `irq` attribute. A disabled
PnP device does not acquire apparently active ranges from its stored assignments.
No resource writes, automatic configuration or change-settings controls exist.

Each resource file is limited to 64 KiB and at most 256 resource rows; MSI
collection examines at most 4096 directory entries and reads at most 32 bytes
per mode/IRQ attribute. Malformed input never becomes a valid zero or a guessed
range; parser errors and any collection limit are shown in the table. The
Resources table and clipboard exports include source attribution. Basic and
advanced Details share the same formatted resource result. Fixture tests cover
64-bit values, empty slots, unassigned/disabled/zeroed resources, malformed input
and PnP-specific IRQ-zero and state semantics; they do not access real hardware.

Sources:
- https://docs.kernel.org/PCI/sysfs-pci.html
- https://github.com/torvalds/linux/blob/master/Documentation/ABI/testing/sysfs-bus-pci
- https://github.com/torvalds/linux/blob/master/include/linux/ioport.h
- https://github.com/torvalds/linux/blob/master/drivers/pci/pci-sysfs.c
- https://github.com/torvalds/linux/blob/master/drivers/pnp/interface.c

The common interfaces above expose allocations rather than a Windows-style
per-device conflict verdict. A conflict field is therefore omitted; neither
sharing nor address containment is used to manufacture one. Broader Resources
by type/connection tree views from Phase 5 remain outside this change.

## Device filtering

View → Filter (Ctrl+F) reveals a window-width row above the tree. The edit
stretches between its label and Deep search checkbox. Ctrl+F or Escape in the
main tree/filter hides the row and clears the query. Properties keeps its own
Escape handling. Launch starts with the filter hidden and Deep search off.
Matching is a case-insensitive literal substring after trimming the query;
there are no regular expressions, wildcards or numeric interval queries.

Ordinary filtering uses exactly the model's displayed device label, including
any visible disambiguating identifiers. Categories survive only when a child
matches. Search temporarily expands matching categories; clearing restores the
unfiltered category expansion and selection, if that instance is still visible.
The existing virtual/internal visibility setting applies before both searches.

Deep search includes all General, Driver and Details text (advanced entries as
well), provenance, inventory properties/attributes, grouped-record paths, EFI
hex/ASCII and resource types, ranges, flags and IRQs. The Properties text
formatter is shared and uses Qt Core only. Known numeric identifiers and
resource numbers also get hexadecimal and decimal aliases; arbitrary product
names and serial strings are not parsed as numbers. Only metadata already
exposed by this application is collected; search does not walk arbitrary sysfs
files or decode EFI payloads. Existing collection limits and access restrictions
still apply. Failed property reads retain searchable last-inventory metadata
and are counted in the status bar; partial results from invalidated reads are
discarded. Search results are observations, not live device state.

A single additional serial QThread collects metadata using the same pinned,
instance-checked collector as Properties, even for unopened dialogs. A 150 ms
input debounce coalesces typing. Owned batches report matches and progress;
request IDs reject obsolete batches, and each match must still have the source
model's generation. Edits, visibility changes, refresh and close interrupt old
work. Thread reuse waits for finished delivery and final cleanup. The GUI never
performs device metadata reads. Selected metadata-only hits show their matching
field in the status bar.

Documents cache within one inventory/visibility revision, with a 64 MiB payload
budget. Over-budget records are still fully searched but not cached. F5 clears
this cache on accepted inventory refresh, so dynamic metadata is not repeatedly
read on every keystroke. Properties reload has its independent snapshot and does
not update search's cache; use F5 to refresh search observations. Cancellation is
cooperative and cannot interrupt an already blocked kernel read.

The device-search fixture checks cover numeric aliases, raw/EFI text, invalidated
property reads, exact displayed labels, category retention, hidden records and
stale-generation matches. They need compilation/execution on the target machine.

## Phase 3: inventory projections

`DeviceModel::View` selects one of five projections. Stable untranslated view
IDs are settings keys; translated captions are presentation only. Owned tree
nodes refer to inventory records by canonical path and expose a separate
NodeKeyRole for expansion restoration. Synthetic category/driver/module nodes
have no PathRole or GenerationRole and cannot be passed to Properties.

The enumeration worker captures only the explicitly selected `driver/module`
symlink for directly bound records. Its absence (including permission or removal
races) is undetermined, never proof of a built-in driver. A module link establishes
ownership rather than whether code is built-in/modular or which installed file
matches running code. No libkmod dependency is added. Non-atomic binding limitations remain; Phase 4 adds monitored instance reconciliation.

Connection projection closes visible records over available inventory parent
links, with cycle guards; missing parents are not guessed from path prefixes.
Hidden context ancestors retain their actual record identity. Driver projections
use subsystem plus driver name, never a module name or naming candidate, as the
binding key. The device-centric projection exposes nearest bound ancestor
relationships separately, including its named device. Many drivers may share
one module without merging their bindings or devices.

Traversal for lookup, search inputs, visible counts and expansion restoration
now visits arbitrary depth. Recursive proxy filtering keeps ancestors of matching
devices and the synthetic driver/module children of a matching device. It does
not automatically accept unrelated descendants of a matching connection parent.
View changes interrupt/invalidate deep-search requests and advance its revision.
Expansion keys are scoped by view; device selection still requires a matching
instance generation. There is no additional worker or device discovery per view.

## System overview

`systemproperties` collects an owned SystemProperties snapshot (raw Attributes
and source paths) in one MainWindow-owned SystemPropertiesReader. `systemdialog`
formats and translates it independently of device dialogs. MainWindow retains
one dialog while hidden, serializes Refresh including queued finished delivery,
joins final thread cleanup before reuse, and participates in the existing
cooperative shutdown flow. There is no device identity to retarget and no callback
capturing a disposable dialog. Opening File → System Information works even if
inventory enumeration fails or no root row is selected.

The collector reads only os-release (64 KiB, /etc takes precedence), cpuinfo
(existing 4 MiB bound), meminfo (64 KiB), uptime, six DMI text fields, EFI-directory
presence, hypervisor/type and selected CPU topology/cache text fields (4 KiB each).
CPU lists contain at most 4096 unique IDs; cache collection examines at most
16384 entries. No recursive arbitrary attribute reader, subprocess, D-Bus service,
privileged helper, module change, firmware execution or memory mapping is used.
Interruption is checked between reads; an already blocking kernel read cannot
be forcibly canceled. Cached snapshots are refreshed only by the dialog button.

Topology counts unique package IDs and package/die/core tuples across present
CPUs; absent optional die IDs remain undetermined components. Negative or missing
package/core IDs invalidate totals rather than manufacture socket/core zero.
Cache totals deduplicate level/type/sharing sets, validate that each set contains
its reporting CPU, reject inconsistent duplicate sizes and invalidate incomplete
results. Rechecking present/online lists detects observed hotplug but cannot make
the snapshot atomic or detect every change-and-return during collection.
Virtualization uses positive kernel hypervisor/type or x86 CPUID evidence; no
negative detection is promoted into a physical-machine verdict. UEFI absence
is similarly qualified. Values are plain text; Copy all exports the same captions
and displayed values, while source attribution stays in tooltips.

Primary references:
- https://www.freedesktop.org/software/systemd/man/latest/os-release.html
- https://docs.kernel.org/admin-guide/cputopology.html
- https://github.com/torvalds/linux/blob/master/Documentation/ABI/testing/sysfs-devices-system-cpu

Installed RAM uses `udev_device_new_from_subsystem_sysname(..., "dmi", "id")`
and selected cached MEMORY_ARRAY_NUM_DEVICES / MEMORY_DEVICE_n_SIZE, PRESENT and
NON_VOLATILE_SIZE properties in the system worker. Slot count is checked before
bounded copying. The pure parser requires complete advertised records, checks
unsigned-decimal input and sum overflow, accepts explicitly empty slots, and
refuses known persistent-memory capacity. Unknown optional nonvolatile size does
not invalidate known installed sizes. Physical and usable capacity remain separate
raw byte values; only SystemDialog formats GiB and the nonnegative MiB difference.
No guessed total, cgroup adjustment or detailed reservation attribution is made.
The system-properties fixture target now links the existing libudev dependency.

## Phase 4: live inventory reconciliation

Enumerator now lives for the window lifetime. Its native `udev` monitor is
worker-owned and enabled before the initial enumeration. No subsystem filter is
installed: add/remove/change/bind/unbind/move hints can affect all projections.
Event records are never replayed into inventory. Each coalesced batch triggers a
fresh full enumeration on the worker, including ancestry, naming, visibility and
EFI metadata. This deliberately favors one maintainable snapshot collector over
separate per-subsystem incremental discovery rules. Only changed model branches
are inserted, removed or moved; unchanged nodes and persistent indexes survive.
An unchanged snapshot skips projection rebuilding and deep-search invalidation.
View/visibility changes still use model resets, as before.

The monitor is drained before and after enumeration. If events arrive during a
scan, a second fresh scan reconciles them before publication. After at most two
scans, still-changing paths, their ancestors/descendants and linked presentation
representatives are withheld and another scan is scheduled. Unrelated siblings
can remain visible even if their connection-context ancestor is temporarily
withheld. This is conservative, may briefly hide a changing branch, and never
promotes a stale add payload into an authoritative device. Snapshots cannot be
atomic with respect to the kernel: events arriving after the final drain are
handled by the next update. Device Properties remains an instance-checked read.

Quiet-time coalescing is 100 ms, with a 500 ms maximum delay from the first event;
scan duration adds to that deadline. There are at most 4096 changed paths and
4096 removal hints per batch, and at most 4096 receives per drain. Hitting either
bound, socket errors (including detected receive loss), or monitor failure
invalidates old instance generations and triggers a fresh reconciliation. The
socket is reopened before scanning an abandoned queue. Sequence-number gaps are
not used as loss evidence: an unfiltered udev stream still need not expose every
kernel event. A periodic full reconciliation every 30 seconds repairs otherwise
undetectable drift (also refreshes EFI directory metadata, which is not a udev
device stream). While monitoring is unavailable, recovery/enumeration is retried
every two seconds and the status bar reports reduced monitoring. F5 remains a
read-only request for a fresh snapshot.

Remove evidence survives all reconciliation rounds until publication and forces
new generations for that path and its descendants, even when inode/timestamp
hints match. Reopening after a monitoring gap invalidates every previous
generation. The GUI never carries selection or a Properties dialog over to a
replacement. Failed enumeration normally retains the previous inventory; failure
with removal/loss evidence clears it conservatively rather than retaining phantom
instances. Late property/search results are canceled or rejected by request and
generation checks.

An atomic acknowledgement bounds delivery to one queued snapshot, with no GUI
wait for a live worker. Manual refresh requests are atomic counters; requests
arriving during a scan are serviced next without discarding useful snapshots or
restarting endlessly. The worker polls in 50 ms intervals between scans and checks
interruption between records. Shutdown keeps the GUI event loop alive until the
monitor and existing read workers finish; blocking kernel reads retain the prior
cooperative-cancellation limitation.

Fixture checks cover bounded event hints, scan change exclusion, retained
remove/add evidence, loss recovery, persistent indexes through rename/reorder,
replacement of descendant generations, and additions/removals in all five views.
They are provided for target-machine execution; this delivery is statically
verified only.

API references:
- https://www.freedesktop.org/software/systemd/man/latest/udev_monitor_receive_device.html
- https://doc.qt.io/qt-6/qabstractitemmodel.html#beginMoveRows

## Phase 5: resource projections

The existing enumerator collects bounded PCI/PnP resource metadata relative to a
pinned device directory between the existing before/after instance checks. Each
Device holds an immutable shared DeviceResources value snapshot; no filesystem
access moves to the model. Resource changes participate in metadata comparison,
so live/F5/periodic snapshots update assignment rows even if a device label and
generation remain unchanged. Properties retains its independently timed collection.

resourceformat is a Qt Core-only formatter shared by the Resources tab and both
resource projections. It preserves the existing translation context and tab text,
exposes untranslated group IDs, and supplies stable row keys based on source/type/
slot/range/mode with duplicate suffixes. Presentation never claims exclusive
ownership or a conflict from shared IRQs, range overlaps or resource flags.

Resources by type gives each type its own device membership node; their expansion
keys are distinct, while record identity remains path plus generation. Resources by
connection closes resource-bearing eligible devices over real inventoried parents.
Hidden context ancestors retain record identity, while hidden assignments follow
the internal-device setting. Unsupported/missing resource information contributes
no manufactured ranges. Error observations remain visible in a metadata group/row.

Assignment nodes have ResourceRole and separate owner-path/generation roles, but
no PathRole/GenerationRole device identity. UI actions target their direct device
parent, validated through the current model generation. Node-key restoration keeps
an assignment selection when it still exists and falls back to the reporting
device after its assignment changes; replacing that instance invalidates both.
Unique paths deduplicate model/proxy counts and deep-search inputs across repeated
type memberships. Name filtering also accepts assignment text and recursively
retains its reporting device and ancestors. View changes read no metadata.

Source-level/fixture coverage includes shared formatting, multiple type membership,
resource-text filtering, actual hidden ancestry, snapshot equality, hot removal,
and generation-safe assignment ownership/selection. No build or GUI test is run
for this delivery.
