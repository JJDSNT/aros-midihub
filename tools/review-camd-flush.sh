#!/bin/sh
# Isolate CAMD open/close and expunge without starting the network stack.
# Keeps symbols and task lists for diagnosing faults during Avail FLUSH.
# Usage: sh tools/review-camd-flush.sh AROS_DIR WORK_DIR [CAMD_LIBRARY]
set -eu

aros=$(realpath "$1")
mkdir -p "$2"
work=$(realpath "$2")
sys=$(mktemp -d "$work/AROS.XXXXXX")
test -x "$aros/boot/linux/AROSBootstrap"
test -f "$aros/Extras/aros-midihub/C/MIDIHubCAMDProbe"
cp -a --reflink=auto "$aros/." "$sys/"
if [ "$#" -ge 3 ]; then
    cp "$3" "$sys/Libs/camd.library"
fi
cat > "$sys/S/camd-flush-review" <<'EOF'
FailAt 21
SYS:Extras/aros-midihub/C/MIDIHubCAMDProbe >SYS:probe.log --monitor applemidi.in.0 1
TaskList >SYS:tasks-before.log
SymbolDump
Avail >SYS:flush.log FLUSH
Wait 1
SYS:Extras/aros-midihub/C/MIDIHubCAMDProbe >SYS:probe-reopen.log --monitor applemidi.in.0 1
Avail >SYS:flush-reopen.log FLUSH
Wait 1
TaskList >SYS:tasks-after.log
Echo >SYS:flush-done.log done
Shutdown
EOF
printf 'Run >NIL: Execute S:camd-flush-review\n' > "$sys/S/User-Startup"
boot_status=0
(cd "$sys" && timeout 35 xvfb-run -a boot/linux/AROSBootstrap \
    > "$work/serial.log" 2>&1) || boot_status=$?
printf '%s\n' "$sys" > "$work/image-path.txt"
for name in probe.log probe-reopen.log tasks-before.log tasks-after.log symbols.out \
            flush.log flush-reopen.log flush-done.log; do
    if [ -f "$sys/$name" ]; then
        cp "$sys/$name" "$work/$name"
    fi
done
if grep -Eq 'Trap signal|free-list corruption' "$work/serial.log"; then
    echo "FAIL: native fault during CAMD open/close/flush; logs: $work"
    exit 1
fi
if grep -q '^done$' "$work/flush-done.log" 2>/dev/null &&
   grep -q 'listening on applemidi.in.0' "$work/probe.log" &&
   grep -q 'listening on applemidi.in.0' "$work/probe-reopen.log" &&
   [ "$boot_status" -eq 0 ]; then
    echo "ok: CAMD open/close/flush completed without a native fault; logs: $work"
else
    echo "FAIL: CAMD open/close/flush did not complete; logs: $work"
    exit 1
fi
