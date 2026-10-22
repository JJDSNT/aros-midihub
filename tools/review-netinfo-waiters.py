#!/usr/bin/env python3
"""Run exact netinfo DevOpen/DevExpunge callbacks with forced expunge timing."""
import argparse
from pathlib import Path
import os
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', type=Path, help='netinfo.c under review')
parser.add_argument('output', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
source = args.source.read_bytes().decode('latin1')

def extract(name):
    begin = source.index('static int '+name+'(')
    body = source.index('{', begin)
    depth = 1
    end = body + 1
    while depth:
        if source[end] == '{': depth += 1
        elif source[end] == '}': depth -= 1
        end += 1
    return source[begin:end]

fixture = '#include "netinfo_waiters_mock.h"\n'
fixture += extract('NetInfo__DevOpen')+'\n'+extract('NetInfo__DevExpunge')+'\n'
fixture += (root/'tests/review/netinfo_waiters_cases.c').read_text()
(out/'callbacks.c').write_text(fixture)
subprocess.run(['cc', '-std=c99', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-parameter', '-Wno-unused-but-set-variable',
                '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                '-I'+str(root/'tests/review'), str(out/'callbacks.c'),
                '-o', str(out/'callbacks')], check=True)
with (out/'result.log').open('w') as log:
    result = subprocess.run([str(out/'callbacks')], stdout=log, stderr=subprocess.STDOUT,
        env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0:halt_on_error=1'))
print((out/'result.log').read_text(), end='')
raise SystemExit(result.returncode)
