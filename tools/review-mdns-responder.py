#!/usr/bin/env python3
"""Exercise the pinned #1591 responder against RFC regression fixtures.

The host fixture substitutes Exec allocation/lists, clock and network output;
packet parsing and host/service protocol code are the exact AROS PR sources.
"""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--aros-tree", type=Path, default=Path.home() / "AROS-net")
    parser.add_argument("--revision", default="24f476235e7ed6babaad84a44e90242948a029f9")
    parser.add_argument("--discovery", action="store_true")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    revision = subprocess.check_output(
        ["git", "-C", str(args.aros_tree), "rev-parse", "--verify", args.revision + "^{commit}"],
        text=True,
    ).strip()
    output = repo / "build/mdns-responder-review" / revision
    shim = output / "shim"
    shim.mkdir(parents=True, exist_ok=True)
    for name in ("exec/types.h", "exec/lists.h", "exec/libraries.h", "exec/memory.h",
                 "exec/ports.h", "exec/tasks.h", "exec/execbase.h", "utility/tagitem.h", "utility/hooks.h", "proto/exec.h",
                 "proto/utility.h"):
        target = shim / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text('#include "mdns_exec_mock.h"\n')
    sources = ("mdns_dns.c", "mdns_host.c", "mdns_reg.c", "mdns_packet.c")
    ipc = subprocess.check_output(["git", "-C", str(args.aros_tree), "show", revision + ":workbench/network/mdns/mdns_ipc.h"]).decode()
    if "mi_TaskID" in ipc:
        sources += ("mdns_client.c",)
    if "MDNS_IPC_READ" in ipc:
        sources += ("mdns_discovery.c",)
    for name in (*sources, "mdns_dns.h", "mdnsd.h", "mdns_ipc.h",
                 "include/bonami/mdns.h", "include/libraries/bonamibase.h"):
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(subprocess.check_output(
            ["git", "-C", str(args.aros_tree), "show",
             revision + ":workbench/network/mdns/" + name]
        ))
    binary = output / ("discovery" if args.discovery else "responder-rfc")
    reap = b"mdnsDiscoveryReap" in (output / "mdnsd.h").read_bytes()
    flags = [os.environ.get("CC", "cc"), "-std=c99", "-O1", "-g", "-Wall", "-Wextra",
             "-Werror", "-Wno-unused-parameter", "-fno-strict-aliasing",
             "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined",
             "-fno-omit-frame-pointer", "-I" + str(shim), "-I" + str(repo / "tests/review"),
             "-I" + str(output / "include"), "-I" + str(output)]
    if reap:
        flags.append("-DMDNS_DISCOVERY_REAP")
    subprocess.run(flags + [str(repo / ("tests/review/mdns_discovery.c" if args.discovery else "tests/review/mdns_responder_rfc.c")),
                           *(str(output / source) for source in sources),
                           "-o", str(binary)], check=True)
    cases = ("known-default", "known-expiring", "known-short", "known-wrong-owner",
             "identical-srv", "different-srv", "class-in", "class-any", "class-ch",
             "opcode", "rcode", "withdraw-service", "probe-txt",
             "announce-initial", "update-port", "update-unchanged")
    if "mi_TaskID" in ipc:
        cases += ("identical-txt", "goodbye-txt", "goodbye-srv", "compressed-srv",
                  "local-rename", "reject-instance", "proxy-own-name",
                  "restart-probes", "task-ids", "update-ttl")
    if args.discovery:
        cases = ("labels-order-txt", "expire-ptr", "expire-srv", "expire-txt", "expire-a",
                 "goodbye", "rescue", "update", "lifecycle", "overflow", "resolve",
                 "timeout", "network-reset", "refresh", "compressed", "wrong-class",
                 "pointer-cycle", "short-rdata", "wrong-port", "multiple-links",
                 "clock-wrap", "release-quiet", "missing-backoff")
        if b"services_key" in (output / "mdns_discovery.c").read_bytes():
            cases += ("enumerate-types",)
        if reap:
            cases += ("unrelated-not-cached", "evict-unwanted", "views-scoped",
                      "a-before-srv")
        if "MDNS_IPC_NOTIFY" in ipc:
            cases += ("notify-pending", "notify-stop", "notify-event-then-stop",
                      "notify-refused", "notify-overflow", "notify-shutdown",
                      "notify-release", "notify-vanished", "notify-resolve",
                      "notify-reset")
    failures = 0
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:halt_on_error=1")
    print("Revision:", revision, flush=True)
    for case in cases:
        result = subprocess.run([str(binary), case], env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        (output / (case + ".log")).write_text(result.stdout)
        failures += result.returncode != 0
        print(f"{'FAIL' if result.returncode else 'PASS'} {case}: {result.stdout.strip()}", flush=True)
    print(f"{failures} failing cases out of {len(cases)}; logs: {output}")
    return int(failures != 0)


if __name__ == "__main__":
    raise SystemExit(main())
