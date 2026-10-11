#!/bin/sh
# Runs MIDIHubCAMDCompat on raspi-aarch64 AROS under QEMU against a
# camd.library.
#
#   tools/camd-compat-qemu.sh CAMD_LIBRARY SUITE SD_DIR [WORK_DIR]
#
# SUITE is the aarch64 MIDIHubCAMDCompat. SD_DIR is an unpacked
# raspi-aarch64 SD card (the boot files, the system and
# Extras/aros-midihub/C) and is not modified. Boots twice from a FAT image
# built in WORK_DIR: once for the 41.1 contract, once with
# DEVS:Midi/debugdriver moved out for RethinkCAMD(). The first boot also
# runs the camd.library 42 and 43 checks, which skip themselves on older
# libraries. Both boots
# share the image, so the endpoint IDs of the first are compared with those
# of the second. Needs
# qemu-system-aarch64 (raspi3b), sfdisk and mtools.
# Exit status: 0 when no check failed.
set -eu

lib=$(realpath "$1")
suite=$(realpath "$2")
sd=$(realpath "$3")
work=$(realpath "${4:-$(mktemp -d)}")
seconds=${CAMDCOMPAT_SECONDS:-200}
image_size=${CAMDCOMPAT_IMAGE_SIZE:-1G}
mkdir -p "$work"
img="$work/camdcompat.img"

test -f "$lib"
test -f "$suite"
test -f "$sd/aros-aarch64-raspi.img"
test -f "$sd/Devs/Midi/debugdriver"

make_image() {
    rm -f "$img"
    truncate -s "$image_size" "$img"
    printf 'label: dos\nstart=2048, type=c, bootable\n' | sfdisk -q --no-reread --no-tell-kernel "$img" >/dev/null 2>&1 || true
    sfdisk -d "$img" | grep -q "start=.*2048"
    mformat -i "$img@@1M" -F -v AROS ::
    # mcopy reports a failure for Devs without naming a file; the files
    # the runs need are checked below.
    for entry in "$sd"/*; do
        mcopy -s -Q -i "$img@@1M" "$entry" ::/ 2>/dev/null ||
            echo "mcopy: not everything in $(basename "$entry") was copied"
    done
    mcopy -o -Q -i "$img@@1M" "$lib" ::/Libs/camd.library
    mcopy -o -Q -i "$img@@1M" "$suite" ::/Extras/aros-midihub/C/MIDIHubCAMDCompat
    for file in aros-aarch64-raspi.img Devs/Midi/debugdriver Libs/camd.library \
                Extras/aros-midihub/C/MIDIHubCAMDCompat C/Echo; do
        mdir -b -i "$img@@1M" "::/$file" >/dev/null 2>&1 || {
            echo "the image lacks $file"
            exit 1
        }
    done
}

boot() {
    printf '%b\n' "$1" > "$work/User-Startup"
    mcopy -o -Q -i "$img@@1M" "$work/User-Startup" ::/S/User-Startup
    timeout "$seconds" qemu-system-aarch64 -M raspi3b \
        -kernel "$sd/aros-aarch64-raspi.img" \
        -initrd "$sd/aros-aarch64-bsp.rom" \
        -dtb "$sd/bcm2710-rpi-3-b.dtb" \
        -drive "file=$img,format=raw,if=sd" \
        -serial null -serial "file:$work/serial-$2.log" \
        -display none >/dev/null 2>&1 || true
    for run in $3; do
        mtype -i "$img@@1M" "::/camdcompat-$run.log" > "$work/camdcompat-$run.log" 2>/dev/null || true
    done
}

make_image
cmd=SYS:Extras/aros-midihub/C/MIDIHubCAMDCompat
boot "$cmd >SYS:camdcompat-contract.log\n$cmd --v42 >SYS:camdcompat-v42.log\n$cmd --v43 >SYS:camdcompat-v43.log" contract "contract v42 v43"

mcopy -o -Q -i "$img@@1M" "$sd/Devs/Midi/debugdriver" ::/camdcompat-debugdriver
mdel -i "$img@@1M" ::/Devs/Midi/debugdriver
boot "$cmd --rethink >SYS:camdcompat-rethink.log" rethink rethink

status=0
for run in contract v42 v43 rethink; do
    log="$work/camdcompat-$run.log"
    if [ ! -s "$log" ]; then
        echo "no $run log: AROS did not run the suite (see $work/serial-$run.log)"
        status=1
        continue
    fi
    cat "$log"
    grep -q '^FAIL' "$log" && status=1
    tail -n 1 "$log" | grep -q '^camdcompat:' || {
        echo "the $run run did not finish (see $work/serial-$run.log)"
        status=1
    }
done

# A camd.library that publishes driver endpoints prints their stored IDs on
# the serial console. The second boot must find the IDs the first one made.
ids() {
    grep -a 'camd.library: \(provider\|endpoint\) [0-9]* id ' "$work/serial-$1.log" | tr -d '\r' || true
}
first=$(ids contract)
if [ -n "$first" ]; then
    if [ "$first" = "$(ids rethink)" ]; then
        echo "PASS endpoint IDs survive a reboot ($(echo "$first" | wc -l) IDs)"
    else
        echo "FAIL endpoint IDs changed across a reboot"
        status=1
    fi
fi

# The v43 run sends on debugdriver port 2 only: a legacy message, a session's
# two messages and SysEx, a legacy message, and a last session message. The
# driver prints each byte it transmits.
if grep -q '^PASS CloseEndpointSession$' "$work/camdcompat-v43.log" 2>/dev/null; then
    want="92 30 10 91 40 7f 81 40 0 f0 7d 1 2 3 f7 92 31 11 b1 7 64"
    sent=$(grep -a 'Debugdriver has received: .* at port 2' "$work/serial-contract.log" |
        sed 's/.*received: \([0-9a-f]*\) at.*/\1/' | tr '\n' ' ' | sed 's/ $//')
    if [ "$sent" = "$want" ]; then
        echo "PASS the driver transmitted the legacy and session bytes in order"
    else
        echo "FAIL the driver transmitted: $sent"
        status=1
    fi
fi
rm -f "$img"
exit $status
