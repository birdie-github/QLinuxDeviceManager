## Architecture and refresh semantics

- `Device`: owned raw metadata, source attribution, canonical path, actual
  udev parent path, visibility and untranslated category ID.
- `Enumerator`: one bounded worker thread owns all udev objects and selected
  metadata reads, producing a value snapshot. CPU model parsing and cross-record
  presentation grouping live in separate modules. No GUI-thread filesystem scans.
- `DeviceModel`: GUI-thread inventory reconciliation and a custom item model;
  presentation nodes store record IDs, never borrowed inventory pointers.
- `MainWindow`: UI, settings and tree-state restoration. Refresh requests
  serialize/coalesce; stale snapshots are superseded. Categories start collapsed
  on every launch; expanded state is retained only within a running session. Failed scans retain the
  previous inventory. Shutdown requests interruption between records and waits
  asynchronously for the worker before destroying its owner.

Selection survives refresh only for matching path and local generation. The
incarnation hint combines sysfs directory device/inode numbers and an optional
udev initialization timestamp. This distinguishes observed replacements and
never merges identical serials/model names. **It cannot guarantee detection
of an unplug/replug entirely between scans with reused identifiers.** Phase 4
will add event-based instance tracking and startup reconciliation before any
management operations exist. Initial enumeration is not an atomic kernel
snapshot; concurrent hotplug can require another Refresh. No live monitoring
or automatic refresh is implemented in Phase 1.

The worker checks cancellation between devices. A kernel metadata read already
in progress cannot safely be forcibly canceled; shutdown can wait for that read.
The GUI event loop remains running during normal shutdown. No asynchronous
callback is permitted to access a destroyed window.

## Default type-view grouping

The inventory retains every discovered record and its real parent/binding.
After classification, presentation refinement follows explicit udev parent
links to the nearest PCI function. Network functions pair only with PCI network
class devices, and NVMe class controllers pair only with PCI class 01/08.
A single network interface or NVMe class record is hidden in favor of the PCI
controller row. Multiport network interfaces remain visible while the redundant
PCI aggregate row is hidden. Missing parents or unrelated classes never justify
suppression. The internal-device toggle reveals every retained record.

Hardware database lookups are local and optional through the existing libudev
library. They supplement missing names using a device's own modalias and never
borrow an ancestor's driver/vendor properties. Input/video function names are
preferred to generic shared USB model labels. CPU models are matched to the
numeric processor record in /proc/cpuinfo, not inferred from the first CPU or
from global board fields. An unavailable or oversized file preserves readable
kernel-name fallbacks and explicit read states.
