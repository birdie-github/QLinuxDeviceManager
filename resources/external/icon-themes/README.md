# Application icon sets

Install location: `$PREFIX/share/QLinuxDeviceManager/icons/{ThemeName}` (or the
configured CMake data directory). The directory name is the menu label under
**Options → Icons**. **Default (System)** preserves desktop/Qt icons. The desktop
launcher and application/window icon are independent of this selection.

Place PNGs under `16x16`, `20x20`, `24x24`, `32x32`, `40x40`, `48x48`, `64x64`,
`96x96` or `128x128` subdirectories. Names are
case-sensitive. Every PNG must be square with the exact directory dimensions.
For example: `Icons8 Windows 11 Color/32x32/processor.png`.
Recommended starting sizes: 16, 20, 24, 32, 40, 48 and 64 pixels. Larger sizes
support higher scaling factors. Export each size from the original artwork;
upscaling a small PNG cannot restore detail. Keep SVG masters outside the runtime
set; the loader uses PNG only and needs no QtSvg dependency.

Qt chooses from the supplied pixel sizes for the logical icon size and display
scale. The tree/menu sizes follow Qt; the toolbar remains 20 logical pixels.
Missing resolutions use the available raster sizes. Supplying only tiny images
cannot guarantee sharp HiDPI rendering. Colour artwork is not automatically
recoloured for dark mode; choose artwork that works on both backgrounds.

## Roles / collection checklist

Use the filename below plus `.png` at every supplied size. All roles are optional.

| Filename | Artwork / use |
| --- | --- |
| computer | Computer tree root |
| audio | Audio devices |
| battery | Batteries and power supplies |
| bluetooth | Bluetooth |
| camera | Webcam / cameras and video devices |
| disk | Generic disk / HDD |
| graphics-card | Display adapters |
| human-interface | Generic input device |
| keyboard | Keyboard |
| mouse | Mouse / pointing device |
| monitor | Display output / monitor |
| network | Network category and generic fallback |
| processor | Processor |
| storage-controller | Storage controller |
| system-device | Motherboard / system device |
| usb | USB controllers and devices |
| uefi | BIOS artwork / UEFI variables |
| other-device | Unclassified hardware |
| system-information | Information action |
| refresh | Refresh action |
| properties | Properties action |
| ethernet | PCI Ethernet controller (PCI class 02, subclass 00) |
| network-card | Other network device, including interface records |
| ssd | Non-rotational ATA disk |
| usb-memory-stick | USB disk explicitly identified by udev as ID_DRIVE_THUMB=1 |
| nvme | NVMe disk identified by udev ID_BUS=nvme; M.2 SSD artwork is suitable |

Subtypes are presentation hints from existing/allowlisted metadata, not a new
hardware classification. NVMe does not establish an M.2 form factor. A USB disk
is not assumed to be a memory stick. Unidentified storage remains generic;
non-rotational SCSI/USB/virtual storage is not assumed to be an SSD. The network
interface and its PCI controller may use different icons because their available
metadata differs. Camera and UEFI roles use the Webcam and BIOS artwork directly.

Lookup order: selected set's device role, selected set's category role, existing
system-theme icon, then Qt's standard icon. Category rows use category roles.
Driver/resource rows retain their category icons. Incomplete sets are supported.

Themes with at least one recognized PNG filename in a supported size directory
are discovered whenever the menu opens. Empty/documentation-only directories
are hidden. Missing, corrupt, incorrectly sized or over-1-MiB files fall back individually.
The choice is saved in QSettings and applied immediately without resetting the
tree, selection or hardware inventory. A missing set falls back to system icons.
To reload edited files, select the set again. Theme files are decoded and cached
on selection; model painting performs no theme-directory reads or image decoding.
A removed set is detected at startup or when the Icons menu next opens.

## Installation and licensing

Add sets to this source directory and run the normal CMake install step, or add
a set directly to the installed icon directory. No `index.theme` is required.
Relative installed layouts (including AppImages) are resolved beside the executable;
otherwise the configured absolute installation directory is used. Build-tree
executables use that installed location; source artwork is not embedded.

The `Icons8 Windows 11 Color` directory contains notices only until the project
maintainer adds authorized downloads. Select the corresponding Windows 11 Color
artwork from https://icons8.com/icons/set/computer-hardware--style-fluency and the
same style's other categories. Rename files to the roles above. Do not add artwork
without keeping its applicable copyright/permission notices.

Icons8 assets are separately licensed: see that directory's `LICENSE.txt` and
`PERMISSION.txt`. Their copyright and restrictions do not apply to the application
code or independently authored third-party themes. New themes must supply their
own notices. Preserve attribution in the repository README and application About.

`-DICONS8_ICONS=OFF` omits the Icons8 directory from installation while retaining
other sets and full system-icon support. This does not remove an already installed
copy: packagers must use a clean staging directory or remove the old set explicitly.
Source archives containing Icons8 artwork still contain separately licensed assets.
