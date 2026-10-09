# Discovery findings from the supplied HP laptop dump

The report is from Fedora 44, kernel 7.1.5, and was inspected offline.
The application was not compiled or launched during this change.

- `/proc/cpuinfo` identifies logical processors 0–15 as
  `Intel(R) Core(TM) Ultra X7 358H`. The initial implementation only inspected
  udev model properties, which do not name these CPU devices.
- `0000:00:14.3` is the PCI Intel Wi-Fi 7 BE211 function. Its only physical
  network interface is `wlo1`, whose udev parent is that PCI device. Both
  received the same network category and model name, causing two default rows.
- `0000:01:00.0` is the only PCI storage controller in this dump (class 0108).
  `nvme0` is its NVMe class record; both were shown as storage controllers.
  The namespace `nvme0n1` is the disk and remains a separate disk-drive entry.
  Its model and the controller's direct sysfs model are `RPEYJ1T24MML1AWX`.
- PCI device models already have useful udev database names. Additional local
  hwdb lookups help only when those properties are absent; they cannot supply
  universally available names for internal firmware/platform objects.
- Input records expose function-specific NAME/sysfs names such as
  `SYNA3517:00 06CB:CFC6 Mouse` and `SYNA3517:00 06CB:CFC6 Touchpad`.
  A generic USB/udev model must not erase that distinction.
- The camera legitimately exposes separate visible/IR capture functions and
  metadata endpoints. Those are not merged by matching generic model strings
  in this patch; V4L capability-based endpoint filtering needs separate work.
- Virtual interfaces remain internal by default. Neither virtual-device status
  nor a missing model string is interpreted as a hardware fault.

Fixtures reproduce these CPU and PCI/network/NVMe relationships without copying
serial numbers, MAC addresses or unrelated personal data. They cover multiple
ports, a second real storage controller and unrelated PCI bridges as safeguards
against over-aggressive grouping. The fixtures are included but not executed,
in accordance with the requested static-only verification workflow.

## Remaining names shown in the follow-up screenshots

Standard identifiers resolve embedded controllers, ACPI power/battery/fan/button/
lid/WMI functions, motherboard resources, the system timer and the display
sensor. `INT340E`/`INTC109D` have a standard `PNP0C02` compatible ID and `INT33D3`
has `PNP0C60`, so their labels do not require guessing Intel-specific meanings.
Direct HDA codec metadata can identify a codec rather than merely the family
of its driver. Module descriptions can explain functions behind names such as
`idma64`, `intel_pmc_core`, `iTCO_wdt`, `hp-wmi`, `pmt_telemetry` and PCIe services,
when descriptions are installed for the running kernel. These optional lookups
have not been run against the user's module directory and need live validation.

Some generic/faux kernel devices and vendor-specific firmware identities still
have no reliable descriptive metadata. They remain raw fallbacks. Serial-base
controller/port objects receive generic function captions; the patch does not
claim that each such object represents a physical serial connector. Existing
classification/visibility is unchanged; naming alone cannot resolve tree noise
or the separate PCI/card audio representations shown in the screenshots.
