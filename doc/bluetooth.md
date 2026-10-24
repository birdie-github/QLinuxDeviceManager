# Bluetooth property snapshots

The tab applies to local Bluetooth subsystem controllers whose kernel name is
exactly `hci` followed by decimal digits. Remote connection objects and HID input
devices do not receive extra tree entries. The `/sys/class/bluetooth/hciN` link
must resolve to the selected canonical device path before and after collection.
The existing property-reader instance/inode checks cover the complete operation;
failed checks invalidate Bluetooth fields and discard known-device associations.
This bounds the usual device-index reuse problem but is not an atomic snapshot
of the kernel and bluetoothd.

Collection runs only for explicit Properties/Reload requests in the existing
property worker, never during enumeration or deep search. Native transport is
the nearest recognized bus in actual sysfs ancestry. Software/hardware rfkill
state is taken only from a single Bluetooth rfkill child of that controller.
Absent or unreadable evidence is unavailable, not an unblocked radio.

The optional `BLUETOOTH` CMake option enables QtDBus independently of `UDISKS2`.
The worker asynchronously resolves the current `org.bluez` owner, then queries
that unique owner's ObjectManager once. Each call has a one-second deadline and
checks cancellation; service activation is disabled. Only Adapter1 properties at
the matching controller path and Device1 objects whose Adapter property matches
that path are retained. There is no discovery, pairing, connection management,
trust modification, GATT characteristic read or background polling.

The decoded ObjectManager result is limited to 4,096 objects; retained remote
metadata is limited to 256 devices and 2 MiB of text. These limits apply after
QtDBus receives/decodes the reply, not to QtDBus's wire buffer. Individual strings
are capped at 4,096 characters, UUID/role lists at 128 entries and 16,384
characters, and manufacturer data at 16 companies with 256 payload bytes each.
Truncation is indicated. No discovery is requested to fill absent properties.
Optional properties remain unavailable when older BlueZ releases omit them.

Adapter Version and Manufacturer properties are used when available. An optional
Linux Bluetooth management socket sends only Read Controller Information,
opcode 0x0004, for the verified controller index. It has a 750 ms budget, 128-packet
limit and 1,024-byte receive buffer. It does not send raw HCI commands, modify
settings or power on the adapter. Access failures stay separate from capability
results; no failed query becomes "unsupported" settings. On success, supported
and current masks are decoded separately, including Classic, LE, security,
privacy, PHY configuration, wideband speech and isochronous settings. Full masks
remain visible for bits unknown to this application. These are settings reported
by the installed kernel/controller combination, not a promise of all features in
the reported Core version. PHY configuration support is not a live PHY/rate read.

`libbluetooth.so.3`, when present, provides its maintained SIG company-name
lookup. Common standard service UUID captions come from the external
`bluetooth-services.ids` file, copied to local build directories and installed in
`${PREFIX}/share/QLinuxDeviceManager`. It is searched beside the executable,
relative to its installed share directory, in XDG data locations and finally at
the configured install location. The first valid file wins; the file is bounded
to 64 KiB and is read once in the worker. UUIDs outside this small common-service
list, including vendor UUIDs, remain visible in full. BLE appearance is shown as
its assigned numeric code, while Class of Device is decoded to its major class.
All signal strength/transmit-power values are last-reported metadata, not live
telemetry. Service availability does not prove that a profile is currently active.

The Copy button includes controller fields, settings and all retained known
devices with sources. The known-device selector controls only the visible detail
panel. Devices are naturally sorted, and tables use the common ReportTable with
page-level scrolling and a fixed bottom action row.

API references:

- https://github.com/bluez/bluez/blob/master/doc/org.bluez.Adapter.rst
- https://github.com/bluez/bluez/blob/master/doc/org.bluez.Device.rst
- https://github.com/bluez/bluez/blob/master/doc/mgmt-protocol.rst
- https://www.bluetooth.com/specifications/assigned-numbers/
