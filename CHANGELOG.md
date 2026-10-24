### Upcoming changes:

- **Much richer CPU information**: Core groups (including performance vs. efficiency cores where applicable), how caches are shared between cores, maximum frequencies, and SMT (hyper-threading) status. This gives a clearer picture of how your processor is organized.
- Much improved ATA drives health reporting - more sensible presentation, LBAs read/written are properly read and presented.
- Memory modules from the firmware are shown in their own individual sections/tabs so each stick is easier to read.
- Optional GPU PCIe link test: You can run a short graphics load on a selected GPU to see whether the PCIe connection actually speeds up or uses more lanes under load. Useful for checking if a graphics card is running at its full possible speed. The test only appears when the link readings look usable.
- Better matching of display outputs on X11 systems by using the monitor’s unique EDID identity when the connector names don’t match perfectly.
- More reliable collection of properties for storage, network, cameras, and displays (better error handling and timeouts so the app is less likely to hang or miss data).
- Easier ways to copy the information from the current System Information tab (or everything) to the clipboard.

### v1.3.0 (12 October 2026)
- Much more complete drive health (SMART) reports for both traditional hard drives and NVMe SSDs—temperature, power-on time, endurance, spare capacity, error counts, and more (previously only a couple of attributes were shown).
- Secure Boot status also appears on the main System information tab.
- GPU Resizable BAR status and size information.
- Firmware-reported details about your memory modules (manufacturer, capacity, etc.).
- System information is better organized into clearer sections.
- Health checks prefer the drive you currently have selected so the most relevant information appears first.

### v1.2.0 (12 October 2026)
- Ethernet adapters: current link speed, duplex mode, auto-negotiation status, and the list of supported link modes.
- PCIe devices (including GPUs): current link speed and lane width, the device’s maximum advertised speed/width, and connection type. Some details require running as root.
- Web cameras: supported video formats, resolutions, and refresh rates.
- Monitors: vendor name, model, product code, serial number, and physical size.

### v1.1.0 (11 October 2026)
- Network adapters now show IP addresses, netmasks, and MAC addresses (including any that have been changed or reassigned).
- Display/monitor settings: resolution, refresh rate, color depth, rotation, scaling, and mirroring. Works on both X11 and Wayland.
- USB devices show version, connection speed, and power/voltage details more clearly.
- Various behavior tweaks so the app feels closer to Windows Device Manager (how you expand items, open properties, etc.).

### v1.0.0 (10 October 2026)
- Clearer organization of device details and storage information (overview, volumes, and health kept separate).
- Secure Boot status and Machine Owner Key (MOK) certificates shown in system information.
- Live history of devices connecting or disconnecting, with the ability to inspect past events from the current session.
- Ready-to-run AppImage package for easy installation on recent Linux systems.

### v0.9-beta (9 October 2026) — First public release
- Browse your hardware in a tree (CPU, GPU, storage, USB, network, audio, cameras, batteries, etc.), organized by type, connection, or driver—similar to Windows Device Manager.
- View basic device details, drivers, and assigned resources (memory ranges, interrupts, etc.).
- See core system information (processor, memory, motherboard, BIOS/UEFI).
- Watch devices appear or disappear while the app is running (live plug/unplug monitoring).
- Search for devices by name.
- Look at readable UEFI firmware variables.
- Basic storage volume and health information.
- Records of device events during the current session.
