## Architecture and refresh semantics

- `Device`: owned raw metadata, source attribution, canonical path, actual
  udev parent path, visibility and untranslated category ID.
- `Enumerator`: one bounded worker thread owns all udev objects and selected
  metadata reads, producing a value snapshot. No GUI-thread filesystem scans.
- `DeviceModel`: GUI-thread inventory reconciliation and a custom item model;
  presentation nodes store record IDs, never borrowed inventory pointers.
- `MainWindow`: UI, settings and tree-state restoration. Refresh requests
  serialize/coalesce; stale snapshots are superseded. Failed scans retain the
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
