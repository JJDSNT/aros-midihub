#!/usr/bin/env python3
"""Build the pure Bonami tool-formatting helpers with host sanitizers."""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--aros-tree", type=Path, required=True)
    parser.add_argument("--worktree", action="store_true")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    head = subprocess.check_output(
        ["git", "-C", str(args.aros_tree), "rev-parse", "HEAD"], text=True
    ).strip()
    revision = head + ("-worktree" if args.worktree else "")
    output = repo / "build/mdns-tools-review" / revision
    shim = output / "shim"
    shim.mkdir(parents=True, exist_ok=True)

    def source(name):
        path = "workbench/network/mdns/" + name
        if args.worktree:
            return (args.aros_tree / path).read_bytes()
        return subprocess.check_output(
            ["git", "-C", str(args.aros_tree), "show", head + ":" + path]
        )

    for name in ("exec/types.h", "exec/lists.h", "exec/ports.h",
                 "exec/libraries.h", "utility/tagitem.h", "utility/hooks.h"):
        target = shim / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text('#include "aros_host.h"\n')
    for name in ("include/bonami/mdns.h", "include/libraries/bonamibase.h",
                 "tools/ba_format.h", "tools/ba_format.c"):
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(source(name))
    binary = output / "ba-format"
    flags = [os.environ.get("CC", "cc"), "-std=c99", "-O1", "-g", "-Wall",
             "-Wextra", "-Werror", "-fsanitize=address,undefined",
             "-fno-sanitize-recover=undefined", "-fno-omit-frame-pointer",
             "-I" + str(shim), "-I" + str(repo / "tests/review"),
             "-I" + str(output / "include"), "-I" + str(output / "tools")]
    subprocess.run(flags + [str(repo / "tests/review/ba_format.c"),
                            str(output / "tools/ba_format.c"),
                            "-o", str(binary)], check=True)
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:halt_on_error=1")
    subprocess.run([str(binary)], env=env, check=True)
    print("logs/build:", output)


if __name__ == "__main__":
    main()
