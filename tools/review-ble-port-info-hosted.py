#!/usr/bin/env python3
"""Boot a private image to probe new BLE attributes and legacy fallback.

Arguments: AROS_IMAGE NEW_CLASS LEGACY_CLASS PROBE OUTPUT_DIR.
Inputs are never modified. Requires timeout and xvfb-run; no radio/network
setup is needed. PROBE is the AROS binary from the proposal's query_probe.c.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    if len(sys.argv) != 6:
        raise SystemExit(__doc__)
    image, new_class, legacy_class, probe, output = map(
        lambda value: Path(value).resolve(), sys.argv[1:])
    output.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="ble-query-", dir=output))
    system = work / "AROS"
    subprocess.run(["cp", "-a", "--reflink=auto", str(image), str(system)], check=True)
    shutil.copy2(probe, system / "C/BLEPortQuery")
    for case, cls, argument, marker in [
        ("new", new_class, "", "PASS native BLE query without CAMD"),
        ("legacy", legacy_class, " --legacy", "PASS legacy BLE query fallback"),
    ]:
        shutil.copy2(cls, system / "Classes/Bluetooth/btmidi.class")
        (system / "S/ble-query-run").write_text(
            f"FailAt 21\nBLEPortQuery >SYS:ble-query-{case}.log{argument}\nShutdown\n")
        (system / "S/User-Startup").write_text("Run >NIL: Execute S:ble-query-run\n")
        with (work / f"serial-{case}.log").open("w") as log:
            result = subprocess.run(
                ["timeout", "50", "xvfb-run", "-a", "boot/linux/AROSBootstrap"],
                cwd=system, stdout=log, stderr=subprocess.STDOUT)
        logfile = system / f"ble-query-{case}.log"
        text = logfile.read_text(errors="replace") if logfile.exists() else ""
        if logfile.exists():
            shutil.copy2(logfile, work / logfile.name)
        print(text, end="", flush=True)
        if result.returncode or marker not in text:
            raise SystemExit(f"FAIL {case}: boot status {result.returncode}; logs: {work}")
    print(f"PASS both BLE query boots; logs: {work}")


if __name__ == "__main__":
    main()
