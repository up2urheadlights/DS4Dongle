#!/usr/bin/env bash
# Copy a .uf2 onto a Pico BOOTSEL mass-storage drive, mounting it if needed.
# Usage: flash.sh <path-to.uf2> [timeout-seconds]
set -euo pipefail

UF2="${1:?usage: flash.sh <path-to.uf2> [timeout-seconds]}"
TIMEOUT="${2:-15}"
LABELS=("RP2350" "RPI-RP2")

[ -f "$UF2" ] || { echo "flash.sh: no such file: $UF2" >&2; exit 1; }

find_device() {
    for label in "${LABELS[@]}"; do
        blkid -t "LABEL=$label" -o device 2>/dev/null | head -n1 && return 0
    done
    return 1
}

echo "flash.sh: waiting for BOOTSEL drive (${LABELS[*]})..."
dev=""
for ((i = 0; i < TIMEOUT * 2; i++)); do
    dev="$(find_device || true)"
    [ -n "$dev" ] && break
    sleep 0.5
done
[ -n "$dev" ] || { echo "flash.sh: no BOOTSEL drive appeared after ${TIMEOUT}s" >&2; exit 1; }

mountpoint="$(lsblk -no MOUNTPOINT "$dev" | head -n1)"
did_mount=0
if [ -z "$mountpoint" ]; then
    echo "flash.sh: mounting $dev"
    mount_out="$(udisksctl mount -b "$dev" --no-user-interaction 2>&1)" || true
    mountpoint="$(printf '%s\n' "$mount_out" | sed -n "s/.*at \(.*\)\.$/\1/p")"
    if [ -n "$mountpoint" ]; then
        did_mount=1
    else
        # A desktop auto-mounter may have raced us and mounted it already
        # (udisksctl then reports AlreadyMounted) -- check once more.
        echo "$mount_out" >&2
        sleep 1
        mountpoint="$(lsblk -no MOUNTPOINT "$dev" | head -n1)"
    fi
fi
[ -n "$mountpoint" ] || { echo "flash.sh: could not mount $dev" >&2; exit 1; }

echo "flash.sh: copying $(basename "$UF2") -> $mountpoint"
cp "$UF2" "$mountpoint/"
sync

if [ "$did_mount" -eq 1 ]; then
    udisksctl unmount -b "$dev" --no-user-interaction >/dev/null 2>&1 || true
fi

echo "flash.sh: done. Device reboots into new firmware."
