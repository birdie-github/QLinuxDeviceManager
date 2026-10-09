// Fixture-only checks. No live hardware is touched and no GUI is launched.
#include "device.h"
#include "cpumetadata.h"
#include "devicepresentation.h"
#include "functionnames.h"
#include "devicelabel.h"
#include "efivariables.h"
#include <cstdio>

static int failures = 0;
static void check(bool result, const char *message)
{
    if (!result) { std::fprintf(stderr, "%s\n", message); ++failures; }
}
int main()
{
    Device keyboard;
    keyboard.path = "/sys/devices/pci0000:00/usb1/1-1/input/input7";
    keyboard.subsystem = "input";
    keyboard.sysname = "input7";
    keyboard.properties.insert("ID_INPUT_KEYBOARD", "1");
    classify(keyboard);
    check(keyboard.category == "keyboard" && !keyboard.hidden, "Composite keyboard function must remain visible");
    Device audio = keyboard;
    audio.subsystem = "sound";
    audio.sysname = "card1";
    classify(audio);
    check(audio.category == "audio" && !audio.hidden, "Composite audio function must remain independently visible");
    Device endpoint = keyboard;
    endpoint.sysname = "event3";
    classify(endpoint);
    check(endpoint.hidden, "Redundant input endpoint must be filtered");
    Device virtualNet;
    virtualNet.path = "/sys/devices/virtual/net/lo";
    virtualNet.subsystem = "net";
    classify(virtualNet);
    check(virtualNet.category == "network" && virtualNet.hidden, "Virtual device must retain its category");
    Device partition;
    partition.subsystem = "block";
    partition.devtype = "partition";
    classify(partition);
    check(partition.hidden, "Partitions must not masquerade as whole disks");
    Device pci;
    pci.subsystem = "pci";
    pci.properties.insert("PCI_CLASS", "0c0330");
    classify(pci);
    check(pci.category == "usb", "PCI USB class decoding");
    Device unknown;
    unknown.subsystem = "unknown_bus";
    unknown.driver = "example";
    unknown.sysname = "device0";
    classify(unknown);
    nameDevice(unknown);
    check(unknown.category == "other" && !unknown.hidden && unknown.name == QStringLiteral("example — device0"), "Unknown bound function needs a readable fallback");
    quint64 counter = 0;
    keyboard.incarnation = "first";
    QVector<Device> first {keyboard};
    reconcile(first, {}, counter);
    QVector<Device> next {keyboard};
    reconcile(next, first, counter);
    check(next[0].generation == first[0].generation, "Same instance must preserve selection identity");
    next[0].incarnation = "replacement";
    reconcile(next, first, counter);
    check(next[0].generation != first[0].generation, "Reused path must not retain old instance generation");
    next[0].properties.insert("ID_MODEL", "identical model");
    Device twin = next[0];
    twin.path += "/different";
    next.append(twin);
    reconcile(next, first, counter);
    check(next[0].generation != next[1].generation, "Equal model metadata must not merge different devices");
    const auto cpuModels = parseCpuModels(QStringLiteral(
        "processor : 1\nmodel name : Different CPU\n\n"
        "processor : 0\nmodel name : Intel(R) Core(TM) Ultra X7 358H\n\n"
        "processor : 2\nHardware : Board name\n"));
    check(cpuModels.value(0) == "Intel(R) Core(TM) Ultra X7 358H" && cpuModels.value(1) == "Different CPU",
          "CPU models must match processor numbers, not parse order or a global first model");
    check(!cpuModels.contains(2), "A global board name must not become a CPU model");
    Device cpu;
    cpu.subsystem = "cpu";
    cpu.sysname = "cpu0";
    Attribute model;
    model.state = ReadState::Available;
    model.value = cpuModels.value(0);
    cpu.attributes.insert("cpu_model", model);
    nameDevice(cpu);
    check(cpu.name == model.value && cpu.nameSource.contains("processor 0"), "CPU name must identify its metadata source");
    model.state = ReadState::PermissionDenied;
    cpu.attributes.insert("cpu_model", model);
    nameDevice(cpu);
    check(cpu.name == "cpu0", "Failed CPU metadata must not become an apparently resolved name");
    Device touchpad;
    touchpad.subsystem = "input";
    touchpad.sysname = "input14";
    touchpad.properties.insert("NAME", "\"SYNA3517:00 06CB:CFC6 Touchpad\"");
    touchpad.properties.insert("ID_MODEL", "Generic receiver");
    nameDevice(touchpad);
    check(touchpad.name == "SYNA3517:00 06CB:CFC6 Touchpad", "Input function name must take precedence over generic model names");

    // Paths/classes mirror the submitted HP laptop dump; no MAC/serial data.
    Device wifi;
    wifi.path = "/sys/devices/pci0000:00/0000:00:14.3";
    wifi.subsystem = "pci";
    wifi.properties.insert("PCI_CLASS", "28000");
    wifi.properties.insert("ID_MODEL_FROM_DATABASE", "Wi-Fi 7 BE211 320MHz");
    Device net;
    net.path = wifi.path + "/net/wlo1";
    net.parentPath = wifi.path;
    net.subsystem = "net";
    net.sysname = "wlo1";
    Device nvmePci;
    nvmePci.path = "/sys/devices/pci0000:00/0000:00:06.0/0000:01:00.0";
    nvmePci.subsystem = "pci";
    nvmePci.properties.insert("PCI_CLASS", "10802");
    Device nvme;
    nvme.path = nvmePci.path + "/nvme/nvme0";
    nvme.parentPath = nvmePci.path; // udev parent, not the intermediate nvme directory.
    nvme.subsystem = "nvme";
    Device disk;
    disk.path = nvme.path + "/nvme0n1";
    disk.parentPath = nvme.path;
    disk.subsystem = "block";
    disk.devtype = "disk";
    QVector<Device> laptop {wifi, net, nvmePci, nvme, disk};
    for (auto &d : laptop) classify(d);
    refinePresentation(laptop);
    check(!laptop[0].hidden && laptop[1].hidden && laptop[1].representedByPath == wifi.path,
          "One PCI Wi-Fi function and one interface must yield one default adapter row");
    check(!laptop[2].hidden && laptop[3].hidden && !laptop[4].hidden,
          "One NVMe PCI/controller pair must yield one controller without hiding the disk");
    check(laptop.size() == 5 && laptop[1].parentPath == wifi.path && laptop[1].driver.isEmpty(),
          "Grouping must retain every record, real ancestry and direct driver evidence");
    Device secondPort = net;
    secondPort.path = wifi.path + "/net/other0";
    QVector<Device> multiport {wifi, net, secondPort};
    for (auto &d : multiport) classify(d);
    refinePresentation(multiport);
    check(multiport[0].hidden && !multiport[1].hidden && !multiport[2].hidden,
          "Independent network functions must survive grouping");
    Device secondController = nvmePci;
    secondController.path = "/sys/devices/pci0000:00/0000:02:00.0";
    QVector<Device> twoControllers {nvmePci, nvme, secondController};
    for (auto &d : twoControllers) classify(d);
    refinePresentation(twoControllers);
    check(!twoControllers[0].hidden && !twoControllers[2].hidden,
          "A second real controller must never be merged by name or category");
    Device bridge = nvmePci;
    bridge.properties.insert("PCI_CLASS", "60400");
    QVector<Device> behindBridge {bridge, nvme};
    for (auto &d : behindBridge) classify(d);
    refinePresentation(behindBridge);
    check(!behindBridge[1].hidden, "A PCI bridge is not an NVMe controller duplicate");
    Device embedded;
    embedded.subsystem = "platform";
    embedded.sysname = "PNP0C09:00";
    embedded.properties.insert("MODALIAS", "acpi:PNP0C09:");
    nameDevice(embedded);
    check(embedded.name == "ACPI embedded controller" && embedded.nameTranslated,
          "A standard ACPI identity must have a readable translated function caption");
    embedded.properties.insert("MODALIAS", "acpi:INT340E:PNP0C02:");
    nameDevice(embedded);
    check(embedded.name == "Motherboard resources" && embedded.nameSource.endsWith("PNP0C02"),
          "Compatible ACPI identities must provide a caption without guessing the vendor ID");
    embedded.properties.insert("MODALIAS", "acpi:UNKNOWNPNP0C02:");
    nameDevice(embedded);
    check(embedded.name == embedded.sysname && !embedded.nameTranslated,
          "Substring resemblance must never resolve an unknown identifier");
    embedded.properties.insert("MODALIAS", "acpi:PNP0C09:");
    embedded.properties.insert("ID_MODEL_FROM_DATABASE", "Actual model");
    nameDevice(embedded);
    check(embedded.name == "Actual model" && !embedded.nameTranslated,
          "A function caption must not replace available model metadata");
    Device ps2;
    ps2.subsystem = "pnp";
    ps2.properties.insert("MODALIAS", "pnp:dPNP0303");
    nameDevice(ps2);
    check(ps2.name == "PS/2 keyboard interface", "PNP modalias identifier parsing");
    Device codec;
    codec.subsystem = "hdaudio";
    Attribute chip;
    chip.state = ReadState::Available;
    chip.value = "ALC-test";
    codec.attributes.insert("chip_name", chip);
    Attribute vendor;
    vendor.state = ReadState::PermissionDenied;
    vendor.value = "Stale vendor";
    codec.attributes.insert("vendor_name", vendor);
    nameDevice(codec);
    check(codec.name == "ALC-test", "Codec name must not use failed vendor metadata");
    Device usbProduct;
    usbProduct.subsystem = "usb";
    usbProduct.devtype = "usb_device";
    usbProduct.properties.insert("ID_MODEL", "Generic_Model");
    chip.value = "Readable USB product";
    usbProduct.attributes.insert("product", chip);
    nameDevice(usbProduct);
    check(usbProduct.name == chip.value, "Direct USB product must supersede an encoded generic model");
    Device rootHub;
    rootHub.subsystem = "usb";
    rootHub.devtype = "usb_device";
    rootHub.sysname = "usb2";
    rootHub.properties.insert("DEVNUM", "001");
    rootHub.properties.insert("PRODUCT", "1d6b/3/702");
    rootHub.properties.insert("ID_MODEL_FROM_DATABASE", "3.0 root hub");
    nameDevice(rootHub);
    check(rootHub.name == "USB 3.x root hub" && rootHub.nameTranslated,
          "Root hub label must describe the USB family without inventing a minor revision");
    rootHub.sysname = "2-1";
    nameDevice(rootHub);
    check(rootHub.name == "3.0 root hub" && !rootHub.nameTranslated,
          "An external device must not acquire the special root-hub caption");
    rootHub.sysname = "usb2";
    rootHub.properties.insert("PRODUCT", "1234/3/702");
    nameDevice(rootHub);
    check(!rootHub.nameTranslated, "Root-hub captions require the Linux descriptor vendor identity");
    Device service;
    service.subsystem = "pci_express";
    service.driver = "pciehp";
    nameDevice(service);
    check(service.name == "PCI Express hot-plug service", "PCI Express service must have a readable caption");
    service.subsystem = "unknown_bus";
    nameDevice(service);
    check(!service.nameTranslated, "Driver names outside their known subsystem must not imply a PCI service");
    Device faux;
    faux.subsystem = "faux";
    faux.driver = "faux_driver";
    faux.sysname = "reg-dummy";
    nameDevice(faux);
    check(faux.name == "Dummy voltage regulator", "Faux objects must describe their software function");
    faux.sysname = "reg-dummy-other";
    nameDevice(faux);
    check(!faux.nameTranslated, "Faux captions require an exact known function name");
    Device firmware;
    firmware.subsystem = "platform";
    firmware.sysname = "INTC1025:00";
    firmware.properties.insert("MODALIAS", "acpi:INTC1025:INT_PTL_SINIT:");
    nameDevice(firmware);
    check(firmware.name == firmware.sysname, "Vendor ACPI objects must not receive hardcoded model labels");
    firmware.properties.insert("MODALIAS", "platform:coretemp");
    nameDevice(firmware);
    check(firmware.name == "Intel CPU temperature monitor", "Exact platform alias must identify the kernel function");

    Device audioPci;
    audioPci.path = "/sys/devices/pci0000:00/0000:00:1f.3";
    audioPci.subsystem = "pci";
    audioPci.properties.insert("PCI_CLASS", "40300");
    Device dsp;
    dsp.path = audioPci.path + "/skl_hda_dsp_generic";
    dsp.parentPath = audioPci.path;
    dsp.subsystem = "platform";
    Device card;
    card.path = dsp.path + "/sound/card0";
    card.parentPath = dsp.path;
    card.subsystem = "sound";
    card.sysname = "card0";
    QVector<Device> audioPair {audioPci, dsp, card};
    for (auto &d : audioPair) classify(d);
    refinePresentation(audioPair);
    check(!audioPair[0].hidden && audioPair[2].hidden && audioPair[2].representedByPath == audioPci.path,
          "One PCI audio controller and its ALSA card must yield one default audio row");
    Device card2 = card;
    card2.path = dsp.path + "/sound/card1";
    card2.sysname = "card1";
    QVector<Device> multiCard {audioPci, dsp, card, card2};
    for (auto &d : multiCard) classify(d);
    refinePresentation(multiCard);
    check(multiCard[0].hidden && !multiCard[2].hidden && !multiCard[3].hidden,
          "Distinct ALSA cards must remain independently visible");
    audioPci.properties.insert("PCI_CLASS", "0c0330");
    QVector<Device> usbAudio {audioPci, dsp, card};
    for (auto &d : usbAudio) classify(d);
    refinePresentation(usbAudio);
    check(!usbAudio[2].hidden, "A USB controller must not stand in for a downstream sound card");
    audioPci.properties.insert("PCI_CLASS", "40300");
    Device audioPci2 = audioPci;
    audioPci2.path = "/sys/devices/pci0000:00/0000:01:00.1";
    QVector<Device> twoAudio {audioPci, dsp, card, audioPci2};
    for (auto &d : twoAudio) classify(d);
    refinePresentation(twoAudio);
    check(!twoAudio[0].hidden && !twoAudio[3].hidden,
          "A separate audio controller must not be merged by model or category");
    Device battery;
    battery.subsystem = "power_supply";
    battery.sysname = "BAT0";
    battery.name = "Primary";
    battery.properties.insert("POWER_SUPPLY_TYPE", "Battery");
    battery.properties.insert("POWER_SUPPLY_MANUFACTURER", "ExampleVendor");
    battery.properties.insert("POWER_SUPPLY_MODEL_NAME", "Primary");
    check(deviceDisplayName(battery) == "ExampleVendor battery" && battery.name == "Primary",
          "Battery captions must use reported metadata while preserving the raw name");
    battery.properties.remove("POWER_SUPPLY_MANUFACTURER");
    check(deviceDisplayName(battery) == "Battery", "An unknown battery manufacturer must never be inferred");
    battery.properties.insert("POWER_SUPPLY_MODEL_NAME", "Real model");
    check(deviceDisplayName(battery) == "Battery (Real model)", "Meaningful battery model identity must survive");
    Attribute denied;
    denied.state = ReadState::PermissionDenied;
    denied.value = "Stale vendor";
    battery.attributes.insert("manufacturer", denied);
    battery.properties.insert("POWER_SUPPLY_MANUFACTURER", "Stale vendor");
    check(!deviceDisplayName(battery).contains("Stale"), "Failed fresh metadata must not reuse a stale manufacturer");
    Device supply;
    supply.subsystem = "power_supply";
    supply.sysname = "ADP1";
    supply.properties.insert("POWER_SUPPLY_TYPE", "Mains");
    check(deviceDisplayName(supply) == "AC power adapter", "Mains supply must have a readable role");
    supply.properties.insert("POWER_SUPPLY_TYPE", "USB");
    supply.parentPath = "/sys/devices/platform/USBC000:00";
    supply.sysname = "ucsi-source-psy-USBC000:001";
    check(deviceDisplayName(supply) == "USB-C power supply (port 1)", "UCSI port number must match its direct controller prefix");
    supply.sysname = "ucsi-source-psy-USBC000:002";
    check(deviceDisplayName(supply) == "USB-C power supply (port 2)", "Distinct UCSI supplies must keep distinct port labels");
    supply.parentPath = "/sys/devices/platform/unrelated";
    check(deviceDisplayName(supply) == "USB power supply", "An unmatched UCSI name must not imply a connector number");
    touchpad.properties.insert("PRODUCT", "18/6cb/cfc6/100");
    touchpad.properties.insert("ID_INPUT_TOUCHPAD", "1");
    check(deviceDisplayName(touchpad) == "Touchpad (SYNA3517:00)", "Generated I2C names must put the function before the firmware identifier");
    Device generatedMouse = touchpad;
    generatedMouse.properties.insert("NAME", "\"SYNA3517:00 06CB:CFC6 Mouse\"");
    generatedMouse.properties.remove("ID_INPUT_TOUCHPAD");
    generatedMouse.properties.insert("ID_INPUT_MOUSE", "1");
    check(deviceDisplayName(generatedMouse) == "Mouse (SYNA3517:00)", "Mouse captions must put the function first too");
    touchpad.properties.insert("PRODUCT", "3/6cb/cfc6/100");
    check(deviceDisplayName(touchpad).contains("SYNA3517"), "USB input names must not be shortened by I2C rules");
    touchpad.properties.insert("PRODUCT", "18/6cb/ffff/100");
    check(deviceDisplayName(touchpad).contains("SYNA3517"), "Mismatched generated identity must retain the original name");
    Device jack;
    jack.subsystem = "input";
    jack.sysname = "input23";
    jack.path = card.path + "/input23";
    jack.parentPath = card.path;
    jack.properties.insert("NAME", "\"sof-hda-dsp HDMI/DP,pcm=3\"");
    jack.properties.insert("PHYS", "\"ALSA\"");
    jack.properties.insert("ID_INPUT_SWITCH", "1");
    nameDevice(jack);
    check(deviceDisplayName(jack) == "HDMI/DisplayPort jack detection (PCM 3)", "ALSA HDMI switch must describe jack detection, not a monitor");
    QVector<Device> audioWithJack {audioPci, dsp, card, jack};
    for (auto &d : audioWithJack) classify(d);
    refinePresentation(audioWithJack);
    check(audioWithJack[3].category == "audio" && audioWithJack[3].hidden
          && audioWithJack[3].representedByPath == audioPci.path,
          "ALSA switch must leave the default HID list and remain associated with the audio controller");
    jack.properties.insert("NAME", "\"sof-hda-dsp Headphone\"");
    classify(jack);
    check(jack.category == "audio" && jack.hidden && deviceDisplayName(jack) == "Headphone jack detection",
          "ALSA headphone switches must receive a readable internal audio label");
    jack.properties.insert("NAME", "\"sof-hda-dsp Mic\"");
    check(deviceDisplayName(jack) == "Microphone jack detection", "ALSA microphone switches need a readable label");
    jack.properties.remove("PHYS");
    classify(jack);
    check(!jack.hidden && jack.category == "hid", "An audio-like name without ALSA identity must remain visible");
    jack.properties.insert("PHYS", "\"ALSA\"");
    jack.properties.insert("ID_INPUT_KEYBOARD", "1");
    classify(jack);
    check(jack.category == "keyboard" && !jack.hidden, "Mixed keyboard input must remain visible despite an audio-like name");
    QString efiName, efiGuid;
    check(splitEfiVariableFilename("BootCurrent-8BE4DF61-93CA-11D2-AA0D-00E098032B8C", efiName, efiGuid)
          && efiName == "BootCurrent" && efiGuid == "8be4df61-93ca-11d2-aa0d-00e098032b8c",
          "EFI filenames must separate the complete namespace GUID from the original variable name");
    check(efiVariableLabel(efiName) == "Boot Current" && efiVariableLabel("Boot000A") == "Boot 000A"
          && efiVariableLabel("SecureBoot") == "Secure Boot" && efiVariableLabel("PKDefault") == "PK Default",
          "EFI labels must split concatenated words while preserving acronyms and entry identifiers");
    check(efiVariableLabel("WiMAXModuleID") == "WiMAX Module ID"
          && efiVariableLabel("OfflineUniqueIDEKPubCRC") == "Offline Unique ID EK Pub CRC"
          && efiVariableLabel("LoaderDevicePartUUID") == "Loader Device Part UUID"
          && efiVariableLabel("AMITCGPPIVAR") == "AMITCGPPIVAR"
          && efiVariableLabel("EDID1Nv") == "EDID1 Nv"
          && efiVariableLabel("SmbiosV3EntryPointTable") == "Smbios V3 Entry Point Table",
          "EFI word boundaries must preserve mixed-case terms and capital runs without swallowing ID or version boundaries");
    check(splitEfiVariableFilename("Vendor-Setting-8be4df61-93ca-11d2-aa0d-00e098032b8c", efiName, efiGuid)
          && efiName == "Vendor-Setting", "Variable names may themselves contain hyphens");
    check(!splitEfiVariableFilename("BootCurrent-not-a-guid", efiName, efiGuid)
          && efiName.isEmpty() && efiGuid.isEmpty(), "Malformed EFI filenames must not retain stale metadata");
    Device efiVariable;
    efiVariable.subsystem = "efivarfs";
    efiVariable.name = "BootCurrent";
    efiVariable.properties.insert("EFI_VENDOR_GUID", "8be4df61-93ca-11d2-aa0d-00e098032b8c");
    check(deviceDisplayName(efiVariable) == "Boot Current"
          && deviceDisplayName(efiVariable, true).contains(efiVariable.properties.value("EFI_VENDOR_GUID")),
          "GUIDs must stay out of default labels and remain available in the internal view");
    return failures ? 1 : 0;
}
