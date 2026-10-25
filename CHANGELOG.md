### v1.4.0 (25 October 2026)

- **Richer processor information:** View core groups, maximum frequencies, performance and efficiency cores where reported, SMT/Hyper-Threading status, and how caches are shared. A dedicated Copy button copies the supported instruction sets and CPU features.
- **Wi-Fi connection details:** The Network tab now shows the connected network name, operating mode, channels, channel widths, signal strength, transmit power, link rates, and security information. Availability depends on your driver, network service, and permissions; some details require administrator access.
- **Bluetooth information:** Inspect controller identity, version, address, capabilities, radio blocking, and supported services. See devices currently known to the system and their connection, pairing, trust, and blocking status. This is a read-only snapshot, not a complete pairing history.
- **Audio devices information:** Find out the PulseAudio server version, audio codec, audio profiles, ports, playback and recording sinks.
- **Clearer drive health reports:** ATA SMART attributes now have more readable names, durations, and units. Read/write totals can use smartmontools when installed and permitted, with clearer warnings when values are uncertain or access is restricted.
- **Optional GPU PCIe link test:** Run a short graphics workload to observe whether a selected GPU's PCIe speed or lane width increases under load. The test is offered when usable link readings are available; it is not a graphics performance benchmark.
- **Easier-to-read RAM information:** Firmware-reported memory devices now have individual chip tabs, making long reports easier to browse.
- **Improved information tables:** Resize columns manually and read full tables using the page's scrollbars, without separate scrollbars inside each table.
- **Volume details on demand:** Open a selected storage volume's details with a button or by double-clicking its row.
- **More useful device names:** Many previously generic system devices now have recognizable names, including firmware-controlled functions and the PC speaker. Device and storage lists also sort numbered names naturally, so item 2 comes before item 10.
- **Display improvements:** Monitor information now includes an estimated pixel density (PPI). Display outputs are matched more reliably on X11 when connector names differ, and unresponsive display queries have time limits.
- **More convenient copying:** Copy the current System Information tab or the entire report to the clipboard.

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
