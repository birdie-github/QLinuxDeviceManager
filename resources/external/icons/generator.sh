#!/bin/bash

# Print instructions only; do not expand or execute the commands below.
cat <<'EOF'
Run these commands from resources/external/icons.

1. Generate PNGs at each required size:
dnf install ImageMagick
for s in 512 256 128 64 48 32 16; do
    mkdir -p "${s}x${s}"
    magick -background none QLinuxDeviceManager.svg \
        -filter Lanczos -resize "${s}x${s}" \
        -gravity center -extent "${s}x${s}" -depth 8 \
        PNG32:"${s}x${s}/QLinuxDeviceManager.png"
done

2. Compress the generated PNGs:
dnf install oxipng
for s in 512 256 128 64 48 32; do
    oxipng -p -o max "${s}x${s}/QLinuxDeviceManager.png"
done

EOF
