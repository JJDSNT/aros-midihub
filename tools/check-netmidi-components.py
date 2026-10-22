#!/usr/bin/env python3
"""Compile and run protocol regressions with only the allowed public headers.

Also link the MIDI 2.0 runtime without any RTP-MIDI or AppleMIDI source.
This guards component boundaries that a combined application build can hide.
"""
import os
from pathlib import Path
import shlex
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / 'build/component-boundaries'
out.mkdir(parents=True, exist_ok=True)
cc = shlex.split(os.environ.get('CC', 'cc'))
flags = shlex.split(os.environ.get('CFLAGS', '-O2 -Wall -Wextra -Werror -std=c99'))


def run(*args):
    subprocess.run([str(a) for a in args], cwd=root, check=True)


def headers(name, components):
    directory = out / name / 'include'
    if directory.exists():
        shutil.rmtree(directory)
    directory.mkdir(parents=True, exist_ok=True)
    for component in components:
        shutil.copytree(root / 'include' / component, directory / component,
                        dirs_exist_ok=True)
    return '-I' + str(directory)


def sources(directory, names):
    return [root / directory / (name + '.c') for name in names.split()]


common = sources('network/netmidi', 'ports text queue')
payload = sources('network/rtpmidi', 'rtpmidi sender receiver timing')
control = sources('network/applemidi', 'applemidi session peers clock')
for name, components, code, test, marks in [
    ('rtpmidi', ['rtpmidi', 'netmidi'], payload + common,
     'tests/protocol.c', []),
    ('applemidi', ['applemidi', 'netmidi'], control + common,
     'tests/applemidi.c', []),
]:
    include = headers(name, components)
    binary = out / name / 'test'
    run(*cc, *flags, include, *marks, root / test, *code, '-o', binary)
    run(binary)
    print('PASS isolated', name, flush=True)

# No protocol is part of the core; MIDI 2.0 only needs its own wire codec.
for name, marks, transport_sources in [
    ('core', [], []),
    ('midi2', ['-DNETMIDI_MIDI2'],
     sources('ports/aros/midihub', 'transport_midi2') +
     sources('src', 'config_midi2 netmidi2 ump')),
]:
    include = headers(name, ['netmidi', 'netmidi2', 'ump'])
    binary = out / name / 'MIDIHub'
    code = (common + sources('ports/aros/midihub', 'main camd network') +
            sources('ports/aros', 'camd_bridge') + sources('src', 'config') +
            transport_sources)
    run(*cc, *flags, include, *marks, *code, '-o', binary)
    print('PASS isolated runtime', name, flush=True)
