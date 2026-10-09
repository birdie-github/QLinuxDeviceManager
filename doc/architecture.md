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

## Function descriptions for unresolved devices

The naming priority remains direct device identity/model metadata first, then
selected direct sysfs strings, exact standard ACPI/PNP IDs, optional module
descriptions, and existing kernel/driver fallbacks. Function labels are stored
as untranslated text and translated by the UI. Raw kernel names accompany them.
No name lookup changes record IDs, category, driver binding or visibility.

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
