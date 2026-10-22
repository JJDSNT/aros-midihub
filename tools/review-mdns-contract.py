#!/usr/bin/env python3
"""Host checks of the actual MIDI/Bonami adapter and pinned #1591 DNS helpers.

Mocks library state only; this does not test AROS scheduling, IPC or network I/O.
The exit status is nonzero while any reviewed contract fails.
"""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--aros-tree", type=Path, default=Path.home() / "AROS-net")
    parser.add_argument("--revision", default="24f476235e7ed6babaad84a44e90242948a029f9")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    revision = subprocess.check_output(
        ["git", "-C", str(args.aros_tree), "rev-parse", "--verify", args.revision + "^{commit}"],
        text=True,
    ).strip()
    output = repo / "build" / "mdns-review" / revision
    shim = output / "shim"
    shim.mkdir(parents=True, exist_ok=True)
    for name in ("aros/asmcall.h", "exec/types.h", "exec/lists.h", "exec/libraries.h",
                 "exec/memory.h", "utility/hooks.h", "utility/tagitem.h", "proto/exec.h",
                 "proto/bonami.h"):
        target = shim / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text('#include "aros_host.h"\n')
    for name in ("mdns_dns.c", "mdns_dns.h"):
        (output / name).write_bytes(subprocess.check_output(
            ["git", "-C", str(args.aros_tree), "show",
             revision + ":workbench/network/mdns/" + name]
        ))
    flags = [os.environ.get("CC", "cc"), "-std=c99", "-O1", "-g", "-Wall", "-Wextra",
             "-Werror", "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined",
             "-fno-omit-frame-pointer", "-I" + str(shim), "-I" + str(repo / "tests/review")]
    subprocess.run(flags + ["-D__AROS__", "-DNETMIDI_BROWSE", "-DNETMIDI_DISCOVERY",
        "-I" + str(repo / "network/mdns/aros/include"),
        str(repo / "tests/review/discovery_bonami_contract.c"),
        "-o", str(output / "discovery-contract")], check=True)
    subprocess.run(flags + ["-Wno-unused-parameter", "-I" + str(output),
        str(repo / "tests/review/mdns_wire_contract.c"), str(output / "mdns_dns.c"),
        "-o", str(output / "wire-contract")], check=True)
    subprocess.run(flags + ["-Wno-unused-parameter",
        "-I" + str(repo / "network/mdns/aros/include"),
        "-I" + str(repo / "network/mdns/aros/src/private"),
        str(repo / "tests/review/mdns_text_capacity.c"),
        str(repo / "network/mdns/aros/src/mdns_dns.c"),
        "-o", str(output / "local-text-contract")], check=True)
    cases = [("discovery-contract", case) for case in
             ("spaces", "key-case", "endpoint-98", "restart")]
    cases += [("wire-contract", case) for case in ("txt-spaces", "literal-dot")]
    cases += [("local-text-contract",)]
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:halt_on_error=1")
    failures = 0
    for case in cases:
        result = subprocess.run([str(output / case[0]), *case[1:]], env=env,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        (output / ("-".join(case) + ".log")).write_text(result.stdout)
        failures += result.returncode != 0
        print(f"{'FAIL' if result.returncode else 'PASS'} {' '.join(case)}: {result.stdout.strip()}",
              flush=True)
    print(f"{failures} failing cases out of {len(cases)}; logs: {output}")
    return int(failures != 0)


if __name__ == "__main__":
    raise SystemExit(main())
