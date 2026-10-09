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

## Audio grouping and kernel-function captions

The submitted dump has one PCI audio function and one ALSA card below its DSP
platform child. The default view now groups this pair using recorded udev
ancestry, stopping at the first PCI function and requiring an audio class.
It never merges equal names. Multiple ALSA cards retain individual rows, and
a USB sound card cannot be represented by its upstream PCI USB controller.

Linux USB root hubs have bus names `usbN`, device address 1 and Linux Foundation
descriptor IDs 1d6b:0001/0002/0003. These identify USB 1.1/2.0/3.x families;
0003 alone does not identify a particular USB 3 minor revision. The four hubs
in the dump belong to separate buses and remain visible independently.

Exact platform aliases label coretemp, rtc-efi and alarmtimer. Direct
`pci_express` service bindings label aer, pcie_bwctrl, pcie_pme and pciehp.
Exact `faux` function names label microcode, reg-dummy, regulatory and
snd-soc-dummy as kernel interfaces or dummy components, not physical devices.
These captions precede optional module descriptions and work without libkmod.

HPIC0003 and INTC1025 remain raw ACPI identities by request; vendor-specific
model mappings are not added. They have no PCI vendor/device identity in the
dump. The udev vendor database text (Headplay/Interphase) is not used to infer
their manufacturer, and an ancestor's PCI identity is not copied to them.

Caption references are the Linux kernel's drivers/usb/core/hcd.c,
drivers/hwmon/coretemp.c, drivers/rtc/rtc-efi.c, kernel/time/alarmtimer.c,
drivers/pci/pcie/{aer,bwctrl,pme}.c, drivers/pci/hotplug/pciehp_core.c,
arch/x86/kernel/cpu/microcode/core.c, drivers/regulator/dummy.c,
net/wireless/reg.c and sound/soc/soc-utils.c.

## Power, pointing-device and audio-jack presentation

The dump reports Mains for ADP1, and Battery with manufacturer HP and model
Primary for BAT0. Labels use the device's own type/manufacturer/model fields,
without a fixed manufacturer or inference about primary/secondary battery roles.
The worker reads only the additional documented type/manufacturer strings;
failed fresh reads do not reuse stale udev manufacturer values for display.
Meaningful battery models remain visible; generic role placeholders remain in
raw metadata/tooltips. Names describe roles, not connection or charging state.

The two UCSI supplies actually end in 001 and 002. Linux constructs their names
from the controller name followed by its connector number. Display parsing
requires an exact match with the recorded direct parent, so unrelated suffixes
are not interpreted as connector numbers. The numbering identifies firmware
connectors and does not imply a left/right physical location or an attached
charger. References: drivers/usb/typec/ucsi/psy.c and
Documentation/ABI/testing/sysfs-class-power in the Linux source.

The three HDMI/DP input records have ALSA physical identity, input-switch flags
and PCM-qualified jack names. Linux sound/core/jack.c creates these input devices
to report audio-jack switches. They now belong to audio and are internal by
default, associated with their sound-card/PCI representative when available.
PCM numbers are preserved and never presented as physical display-port numbers.
Keyboard/mouse/touchpad functions with a similar name are not hidden.

The submitted I2C pointing names contain a firmware identifier and HID vendor/
product numbers, followed by Mouse or Touchpad. Concise captions require the
generated name form, I2C bus code, matching vendor/product numbers, and the
corresponding udev capability flag. Descriptive names and other buses retain
their original product names. Both input functions remain separate; shared HID
ancestry alone is insufficient to prove that a mouse interface is redundant.
