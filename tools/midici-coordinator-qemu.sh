#!/bin/sh
# Run the AROS-native coordinator ownership probe under raspi-aarch64 QEMU.
# Usage: tools/midici-coordinator-qemu.sh PROBE SD_DIR [WORK_DIR]
set -eu

probe=$(realpath "$1")
sd=$(realpath "$2")
work=$(realpath "${3:-$(mktemp -d)}")
seconds=${MIDICI_COORD_SECONDS:-90}
image_size=${MIDICI_COORD_IMAGE_SIZE:-1G}
img="$work/midici-coordinator.img"
log="$work/midici-coordinator.log"

test -f "$probe"
test -f "$sd/aros-aarch64-raspi.img"
mkdir -p "$work"
rm -f "$img"
truncate -s "$image_size" "$img"
printf 'label: dos\nstart=2048, type=c, bootable\n' |
    sfdisk -q --no-reread --no-tell-kernel "$img" >/dev/null 2>&1 || true
sfdisk -d "$img" | grep -q 'start=.*2048'
mformat -i "$img@@1M" -F -v AROS ::
for entry in "$sd"/*; do
    mcopy -s -Q -i "$img@@1M" "$entry" ::/ 2>/dev/null || true
done
mmd -i "$img@@1M" ::/Extras/aros-midihub/Tests 2>/dev/null || true
mcopy -o -Q -i "$img@@1M" "$probe" ::/Extras/aros-midihub/Tests/MIDICICoordProbe

run="$work/midici-coordinator-run"
startup="$work/User-Startup"
printf '%s\n%s\n' \
    'SYS:Extras/aros-midihub/Tests/MIDICICoordProbe >SYS:midici-coordinator.log' \
    'Echo PROBE-DONE >>SYS:midici-coordinator.log' > "$run"
printf '%s\n' 'Run >NIL: Execute S:midici-coordinator-run' > "$startup"
mcopy -o -Q -i "$img@@1M" "$run" ::/S/midici-coordinator-run
mcopy -o -Q -i "$img@@1M" "$startup" ::/S/User-Startup

timeout "$seconds" qemu-system-aarch64 -M raspi3b \
    -kernel "$sd/aros-aarch64-raspi.img" \
    -initrd "$sd/aros-aarch64-bsp.rom" \
    -dtb "$sd/bcm2710-rpi-3-b.dtb" \
    -drive "file=$img,format=raw,if=sd" \
    -serial null -serial "file:$work/serial.log" \
    -display none >/dev/null 2>&1 || true
mtype -i "$img@@1M" ::/midici-coordinator.log > "$log" 2>/dev/null || true
cat "$log"
grep -q '^PROBE-DONE$' "$log" || {
    echo "coordinator probe did not finish; see $work/serial.log" >&2
    exit 1
}
grep -q '^FAIL' "$log" && exit 1
grep -q '^MIDI-CI-COORDINATOR [0-9][0-9]* checks, 0 failures$' "$log"
rm -f "$img"
