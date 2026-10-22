#!/usr/bin/env python3
"""Run review regressions against an exact AROS PR revision, without checkout.

Requires a local AROS git repository and a C compiler with ASan/UBSan.
Returns nonzero when any regression fails; individual logs are preserved.
"""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--aros-tree", type=Path, default=Path.home() / "AROS-netmidi")
    parser.add_argument("--revision", default="953be81c3ee874d040654633d1ed783fcc95dd27")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    revision = subprocess.check_output(
        ["git", "-C", str(args.aros_tree), "rev-parse", "--verify", args.revision + "^{commit}"],
        text=True,
    ).strip()
    output = repo / "build" / "upstream-review" / revision
    output.mkdir(parents=True, exist_ok=True)
    prefix = "workbench/network/netmidid/netmidi2/"
    if subprocess.run(["git", "-C", str(args.aros_tree), "cat-file", "-e",
                       revision + ":" + prefix + "netmidi2.c"],
                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
        prefix = "workbench/network/netmidi/netmidi2/"
    for name in ("netmidi2.c", "ump.c", "include/netmidi2/netmidi2.h", "include/ump/ump.h"):
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(subprocess.check_output(
            ["git", "-C", str(args.aros_tree), "show", revision + ":" + prefix + name]
        ))
    flags = [os.environ.get("CC", "cc"), "-O1", "-g", "-std=c99", "-Wall", "-Wextra",
             "-Werror", "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined",
             "-fno-omit-frame-pointer", "-I" + str(output / "include")]
    for name, source in (("identity-capacity", "tests/review/netmidi2_identity_capacity.c"),
                         ("invalid-accept", "tests/review/netmidi2_invalid_accept.c"),
                         ("netmidi2-test", "tests/netmidi2.c"), ("ump-test", "tests/ump.c")):
        subprocess.run(flags + [str(repo / source), str(output / "netmidi2.c"),
                               str(output / "ump.c"), "-o", str(output / name)], check=True)
    cases = [("identity-capacity", side, str(size))
             for side in ("name", "product") for size in (0, 1, 8)]
    cases += [("invalid-accept",), ("netmidi2-test",), ("ump-test",)]
    failures = 0
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:halt_on_error=1")
    print("Revision:", revision, flush=True)
    for case in cases:
        result = subprocess.run([str(output / case[0]), *case[1:]], env=env,
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        log = output / ("-".join(case) + ".log")
        log.write_text(result.stdout)
        failures += result.returncode != 0
        print(f"{'FAIL' if result.returncode else 'PASS'} {' '.join(case)}: {log}", flush=True)
    print(f"{failures} failing cases out of {len(cases)}")
    return int(failures != 0)


if __name__ == "__main__":
    raise SystemExit(main())
