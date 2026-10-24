# ACPI/PNP device names

`acpi-pnp.ids` is an external UTF-8 text database derived from ACPICA's
`source/common/ahids.c`. Its header records the exact upstream revision, source
hash and BSD-3-Clause license. It is read once at startup, not embedded into the
executable. Restart after replacing it; no application rebuild is required.

Each record contains an uppercase hardware ID, one tab, and a description.
Blank lines and lines beginning with `#` are ignored. IDs must be unique.
The loader accepts regular files up to 2 MiB, at most 32,768 entries, and
descriptions up to 512 characters. Malformed files are rejected as a whole.
Descriptions are plain text and do not execute commands or load modules.

Lookup order (first valid file wins):

1. `--acpi-ids PATH` selects exactly one file. Failure exits with a diagnostic.
2. `acpi-pnp.ids` beside the executable.
3. `hwdata/acpi-pnp.ids` and `misc/acpi-pnp.ids` in standard XDG data locations.
4. `QLinuxDeviceManager/acpi-pnp.ids` in standard XDG data locations and in the
   executable's `../share` directory, followed by the configured installation path.

Typical distributions do not ship `acpi-pnp.ids`; the project installs its copy
in `${PREFIX}/share/QLinuxDeviceManager/` (subject to `CMAKE_INSTALL_DATADIR`).
CMake also copies it to the build directory for running a local build.
An adjacent copy takes precedence over an installed copy: update the selected
file, remove the adjacent copy, or use `--acpi-ids` to select a different one.
Malformed automatically discovered files produce a warning and the next path is
tried. Without any usable file, kernel/udev naming and other function rules remain.
Source tooltips and copied properties identify the selected file and matched ID.

This file maps complete ACPI/PNP device IDs to function descriptions. It is not
interchangeable with hwdata's `pnp.ids`, which maps vendor codes to manufacturers.
PCI/USB naming continues to use existing system udev/hwdb metadata.

## ACPI vendor attribution

Manufacturer attribution is separate from the function-name database. Some
system hwdb versions let a later three-character glob such as `acpi:INT*:`
override `acpi:INTC*:`, assigning the wrong vendor to an eight-character ACPI ID.
A fresh compiled-hwdb lookup reproduces that result.

For a primary ACPI ID consisting of four vendor characters and four hexadecimal
product digits, the application therefore consults the installed
`20-acpi-vendor.hwdb` registry's literal `acpi:XXXX*:` vendor records directly.
No vendor names or individual device IDs are hardcoded. Compatible IDs do not
determine the vendor. Direct manufacturer attributes retain priority, and the
original udev value remains available in advanced Details.

The registry is read once in a property worker, with a 1 MiB input limit.
Same-name files are searched in `/etc/udev/hwdb.d`, `/run/udev/hwdb.d`,
`/usr/local/lib/udev/hwdb.d`, `/usr/lib/udev/hwdb.d`, then `/lib/udev/hwdb.d`.
A replacement or `/dev/null` mask stops that search. Restart to pick up changes.
This is a narrow vendor-registry reader, not a replacement hwdb rule engine:
complex match records and other hwdb filenames are not interpreted. If no
applicable registry entry can be read, normal udev manufacturer attribution is
retained. Seven-character PNP IDs and unknown formats are not reinterpreted.

## Updating from ACPICA

Keep the downloaded ACPICA checkout outside the application repository. Inspect
the upstream changes and use its exact revision:

```sh
python3 tools/device-name-data.py import-acpica \
    /path/to/acpica/source/common/ahids.c \
    --revision FULL_40_CHARACTER_COMMIT \
    --license /path/to/acpica/LICENSE.BSD-3-Clause
```

The importer accepts only the expected literal data-table format and fails if
that format changes. It applies `overrides.tsv` after the upstream entries.
That small file preserves existing UI captions, adds documented IDs absent
upstream, and corrects ACPICA's misleading PNP0800 speaker description. New
upstream entries retain their supplied descriptions. Review the generated diff
before distributing the updated file, retaining its license header.

The existing `DeviceFunctions` translation context is used for captions.
The `update-translations` maintenance target extracts captions from the shipped
database as well as C++ sources. Only that target and database import require
Python; building and running the application do not. User-added captions display
as supplied unless the installed translation already contains a matching entry.
