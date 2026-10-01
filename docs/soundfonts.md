# SoundFont bank for the synthesizer

The initial goal is a CAMD output port that plays General MIDI (GM) with an
SF2 bank. ScummVM and other CAMD clients could then send MIDI to a local
synthesizer. Games written for MT-32 are a separate case:
[ScummVM can convert MT-32 to GM](https://docs.scummvm.org/en/v2.8.0/advanced_topics/understand_audio.html),
but the result varies by game. Faithful MT-32 playback requires an emulator
and ROMs obtained from the user's own module; a GM bank is not a substitute.

## Banks considered

| Bank | License and size | Initial suitability |
| --- | --- | --- |
| [FluidR3 GM](https://github.com/musescore/MuseScore/blob/main/share/sound/FluidR3Mono_License.md) | MIT; original SF2 about 141 MB | Complete GM bank with a clear license, but large for clones, packages, and targets with limited RAM. |
| [GeneralUser GS](https://github.com/mrbumpy409/GeneralUser-GS) | Custom permissive license; SF2 about 31 MB | Smaller GM/GS bank; its author [rates TinySoundFont poorly](https://github.com/mrbumpy409/GeneralUser-GS/blob/main/documentation/README.md) because it lacks modulators. |

GeneralUser GS and TinySoundFont are pinned as submodules in this repository.
After `git submodule update --init --recursive`, run `make test-synth` to load
`soundfonts/GeneralUser-GS/GeneralUser-GS.sf2` and render a note to
`build/generaluser-test.wav`. This verifies loading and PCM output. **It does
not verify sound fidelity:** TinySoundFont still ignores `pmod` and `imod`.
Future work must implement and validate modulators, then compare output with
the bank author's SoundFont specification test.

The [GeneralUser GS license](../soundfonts/GeneralUser-GS/documentation/LICENSE.txt)
allows use in software projects, but its author notes uncertain provenance for
some samples. This submodule is a test bank. Selecting a bank for default
distribution requires a separate decision about its license and sample
provenance.

[TinySoundFont](https://github.com/schellingb/TinySoundFont/blob/main/tsf.h)
is MIT-licensed and portable, but does not yet implement modulators, reverb,
or chorus. GeneralUser GS is therefore known to render some instruments
incorrectly with it.

### SF3

SF3 retains the SF2 musical structure and compresses samples with Ogg Vorbis.
It reduces file size without adding synthesis features or solving the missing
modulators. The examined TinySoundFont revision already has an SF3 path when
built with `stb_vorbis`, which is available under MIT. SF3 thus fits the
project's MIT/BSD engine dependency criterion.

TinySoundFont decodes samples at load time and stores them as `float` values.
A smaller SF3 file may still use considerable RAM and temporary memory while
loading. This must be measured on 68k. SF3 support should be optional at
build time and tested with a real bank for sound quality, load time, and peak
RAM. Vorbis decoding does not fix modulator or big-endian limitations.

### Engine selection

An engine incorporated into the package under AROS `contrib/extras` must have
an MIT or BSD license and verifiable provenance. Apache-2.0 and LGPL do not
meet this project requirement, even where separate distribution is permitted.
Experimental investigations stayed in `/tmp`, outside this repository:

| Engine | License | Assessment |
| --- | --- | --- |
| [TinySoundFont](https://github.com/schellingb/TinySoundFont) | MIT | Portable; needs implementation and testing of modulators for GeneralUser GS. |
| [SF2Lib](https://github.com/bradhowes/SF2Lib) | MIT | Claims modulator support, but uses C++17/23, Apple components, and another library from the same author; porting would be larger. |

FluidLite (LGPL) and SpessaSynth C (Apache-2.0) were examined but are excluded
from MIDIHub integration by that license criterion. An experimental
SpessaSynth C build remained only in `/tmp`.

TinySoundFont is the first engine candidate: implement and validate the
needed modulators and check big-endian portability. SF2Lib remains an MIT
alternative if that implementation costs more than porting it. TinySoundFont
is included as a submodule and exercised by a standalone render test; the
CAMD port is not yet connected to the synthesizer.

## Distribution

The GeneralUser GS submodule is optional for Git checkout and serves tests.
The network package must work without installing a SoundFont. After the
synthesizer and AROS PCM output work, evaluate GM timbres, percussion, SysEx,
and memory use before choosing a default bank. Larger banks could be separate
downloads or packages.

Future `MIDIHubPrefs` should accept a user-selected SF2, save its path in
`ENVARC:`, and preview a note through the same audio engine as the CAMD port.
Define that preference format when synthesis is integrated so it has an
immediate effect.
