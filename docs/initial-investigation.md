# Initial investigation — AROS MIDIHub

Status: October 1, 2026. This document records verified findings and the
implementation sequence. The first increment includes AppleMIDI and RTP-MIDI
packet codecs, invitation negotiation for one peer, host tests, a native
self-test, and a UDP diagnostic program. The program opens both ports,
exchanges invitations and CK messages, and can send a test note. It does not
yet estimate clock offset or schedule future events. The CAMD bridge for
short messages and SysEx is implemented but has not been exercised inside a
running AROS instance. Audio output is not yet integrated.

Current protocol tests use vectors constructed from the specifications. They
do not include captures from a real peer or establish interoperability with
another product. The network test runs two MIDIHub processes on localhost.

Local checks: `make test` passed on Linux, including AddressSanitizer and
UndefinedBehaviorSanitizer runs. Leak detection was disabled because the
environment runs under `ptrace`. Bellatrix's m68k GCC accepted the portable
core and native self-test with `-Wall -Wextra -Werror -fsyntax-only`. The
Linux-hosted `x86_64-aros-gcc` compiled the core and produced
`libarosmidihub.a`. After building `compiler` and `core-linklibs`, the
self-test and MIDIHub linked in the Linux-hosted build. The network test also
covers complete and segmented SysEx.

The UDP program compiles and passes its loopback test on the host. The
Linux-hosted `x86_64-aros-gcc` linked its AROS version. Running it inside
AROS still requires a working Linux-hosted AROS environment. The Bellatrix
m68k GCC and its AROS libraries also linked `MIDIHub-m68k` through `make
m68k`; that binary has not yet been run inside m68k AROS. The separate
TinySoundFont smoke test renders GeneralUser GS to a WAV on the host and
compiles for m68k. A separate optional program sends the same rendered note
to `ahi.device`; both use the MIDIHub synthesis wrapper. The wrapper is not
yet connected to CAMD, and AROS playback has not been run.

## Existing facilities in the local AROS checkout

- `workbench/libs/camd/` implements `camd.library` (configured ABI version
  41.1). Its API includes `CreateMidiA`, `AddMidiLinkA`, `PutMidi`, `GetMidi`,
  `PutSysEx`, `GetSysEx`, and cluster notifications. Headers are under
  `compiler/include/midi/`. This is an immediate interface for existing MIDI
  applications; the first bridge does not require a public MIDIHub library.
- At startup, `camd.library` scans `DEVS:Midi`, loads each driver with
  `LoadSeg`, and creates `<driver>.in.<port>` and `<driver>.out.<port>`
  clusters. The USB MIDI class places a file there. This checkout has no
  MIDI/CAMD Preferences application in `workbench/prefs/` or `contrib/`.
  MIDIHub currently creates clusters as a CAMD client. A virtual driver under
  `DEVS:Midi` is an alternative worth evaluating if ports must exist before
  the network session starts.
- `rom/usb/classes/camdmidi/` contains a Poseidon class that creates a CAMD
  driver for USB MIDI. Its `mmakefile.src` enables i386, x86_64, and ppc;
  arm is disabled and aarch64 is absent. SysEx handling has `FIXME` comments.
  This limits that USB class, not MIDIHub: the network bridge uses CAMD and
  does not depend on a USB MIDI controller.
- `rom/usb/classes/simplemidi/` is another USB MIDI class, separate from
  CAMD integration. Do not assume both expose the same ports to applications.
- A search found no usable native RTP-MIDI/AppleMIDI implementation for
  AmigaOS, AROS, or MorphOS. [amiditools](https://github.com/cnvogelg/amiditools)
  provides a UDP MIDI CAMD driver for AmigaOS 3, but uses its own protocol,
  explicitly incompatible with RTP-MIDI. It may inform CAMD integration;
  its GPL-3.0 code must not be incorporated into this MIT project.
- `workbench/network/stacks/AROSTCP/` supplies the network stack. The AROS
  application requires `bsdsocket.library` at runtime.
- The `~/AROS` checkout has `contrib/aros-bluzing` as a symlink to a separate
  project. A `contrib/extras` directory and a local link to this repository
  were created for MIDIHub integration.

## Protocol facts that define the first milestone

- [RFC 6295](https://www.rfc-editor.org/rfc/rfc6295.html) specifies the
  RTP-MIDI payload, including timestamps, MIDI commands, and recovery
  journals. It does not establish an AppleMIDI session. Apple's
  [network MIDI protocol](https://developer.apple.com/library/archive/documentation/Audio/Conceptual/MIDINetworkDriverProtocol/MIDI/MIDI.html)
  documents that layer and describes its payload as mostly conforming to
  RFC 6295. The two layers should be modeled separately.
- AppleMIDI uses consecutive UDP ports: N for control and N+1 for data.
  The `IN`/`OK`/`NO` invitation exchange occurs on the control port, then
  on the data port. `BY` closes the session. Tokens, SSRC values, and
  multibyte fields use network byte order.
- `CK` synchronization uses timestamps in 100-microsecond units and a
  three-message exchange. MIDIHub now estimates the clock offset and queues
  future short MIDI messages for delivery at their RTP timestamps. The queue
  holds up to 256 events and accepts at most 10 seconds of lookahead; later
  events are discarded. SysEx scheduling remains to be implemented. The
  initiator refreshes synchronization every 50 seconds.
- Apple advertises `_apple-midi._udp` through Bonjour. Manual connection
  by IP address and port can come first; mDNS/DNS-SD discovery can follow.
- Apple's driver sends journals but accepts packets without them. For the
  first interoperability test, the sender can use `J=0`. The receiver should
  at least identify and bound a present journal before applying it. AppleMIDI
  receiver feedback (`RS`) reports the most recent accepted RTP sequence on
  the control port, including journal-only guard packets; MIDIHub now sends
  this feedback. MIDIHub validates top-level journal framing and channel
  lengths, detects RTP sequence gaps, and reports whether the journal
  checkpoint covers a gap. It interprets Chapter N NoteOff bitfields to
  release notes that are still active after a covered gap and to cancel
  matching future NoteOn events. It also recovers timely lost NoteOn commands
  when the journal's Y hint requests playback and clock sync is available.
  Chapters P and W restore Program Change, bank selection, and Pitch Bend
  after covered packet loss. Chapter C value logs restore Control Change
  values; alternate logs are skipped. Other chapters and outgoing journals
  remain to be implemented.

## Recommended architecture

```text
AROS MIDI applications <-> camd.library <-> MIDIHub CAMD integration
                                            |
                                      AppleMIDI session
                                            |
                                       RTP-MIDI codec
                                            |
                                  UDP / bsdsocket.library
```

The RTP-MIDI codec and AppleMIDI state machine should remain portable C,
without Exec, CAMD, or socket dependencies and without assumptions about
pointer width, alignment, or host byte order. AROS integration uses the same
CAMD, network, system clock, and eventually audio paths on every target;
these services do not require per-architecture implementations. The console
program `MIDIHub` can host the first bridge. The core preserves MIDI events
as explicit bytes and timestamps. Conversion to CAMD `MidiMsg` belongs in
AROS integration to avoid endianness errors and `PutMidi`'s three-byte
limit. SysEx uses `PutSysEx` and `GetSysEx`.

### First verifiable milestone

1. Host tests for AppleMIDI and RTP-MIDI codecs: valid and truncated packets,
   lengths, running status, timestamps, byte order, and received journals.
   Keep specification-derived vectors distinct from real traffic captures.
2. Manual one-peer LAN session: initiate and accept invitations on both
   ports, synchronize, exchange Note On/Off and Control Change both ways,
   close with `BY`, and handle timeouts and repeated invitations.
3. CAMD integration that announces input/output clusters, receives network
   events, and sends application events without loops. Verify
   `MLTYPE_Sender` and `MLTYPE_Receiver` link semantics in a native test.
4. Build as a MetaMake component for all AROS targets, using common CAMD and
   network services without architecture conditionals in the core or
   package. Register startup in `ENVARC:SYS/Packages/aros-midihub`. Test
   execution in AROS with `bsdsocket.library` and a known AppleMIDI peer,
   then validate builds and execution across targets with different byte
   order and pointer widths as environments become available.

Acceptance criterion: Note On/Off works in both directions between CAMD
clients over an established network session, and sessions close without
orphaned resources. Measure latency and jitter before setting numeric goals.

## Packaging

The intended AROS layout is `AROS/contrib/extras/aros-midihub/`, as a
checkout or symlink to this repository. Its `mmakefile.src` registers
`contrib-aros-midihub` without absolute paths. As with `aros-bluzing`, the
install destination is `SYS:Extras/aros-midihub/`, with an executable in
`C/`, `S/Package-Startup`, and a registration file in
`ENVARC:SYS/Packages/aros-midihub`. The registration points to the package
directory; startup creates `MIDIHUB:` and appends `MIDIHUB:C` to `Path`.

This repository contains the MetaMake file, startup script, and registration
file. A local link exists at `~/AROS/contrib/extras/aros-midihub`.
Linux-hosted MetaMake found the nested file and generated its target. The
library, startup script, license, and registration were built or copied to
the Linux-hosted image. The executable and self-test linked after the base
AROS components were built. The local `build-aros-linux/` directory is
ignored by Git. Because MetaMake scans source directories independently of
Git, that build also excludes the directory in its local `mmake.config`.
Future builds are easier in a sibling directory. The initial integration
does not copy executables to `SYS:C` or modify `camd.library`.

## Later phases and dependencies

| Feature | Practical prerequisite |
| --- | --- |
| mDNS discovery | Stable manual session and UDP networking |
| Recovery journal | Basic interoperability, loss metrics, and test vectors |
| Multiple peers and routing | CAMD port contract and loop prevention |
| USB MIDI | CAMD class test on targets that provide it; optional transport |
| TinySoundFont/SF2 | Chosen and tested AROS PCM output, bank distribution policy, modulator work |
| Serial/DIN and BLE MIDI | Available target-specific transport and hardware |
| MIDI 2.0/UMP | Event contract independent of three-byte `MidiMsg` |

## Third-party code and licenses

- [TinySoundFont](https://github.com/schellingb/TinySoundFont) is MIT-licensed
  and pinned as a submodule. Preserve its license notice. Upstream also
  prohibits AI-generated code contributions; local use and any future
  upstream contribution must be handled separately.
- GeneralUser GS is pinned as a test-bank submodule. Its custom license
  permits use in software, but the author notes uncertain provenance for
  some samples. See [soundfonts.md](soundfonts.md) before distributing it.
- [librtpmidid](https://github.com/davidmoreno/rtpmidid) is LGPL-2.1 and is
  only an interoperability reference, not an integration candidate under
  the project's MIT/BSD dependency criterion.
- The README also mentions MIDIKit and cmidid. Confirm the exact upstream
  license and revision before reusing code; secondary descriptions differ.
  For now, implement codecs from RFC 6295 and Apple's documentation and
  record the origin of test vectors.

## Open decisions

1. Order of available validation environments. This organizes testing but
   does not restrict the project's supported targets.
2. First interoperability peer available for testing.
3. Whether the package should start automatically or only when `MIDIHub` is
   run. Manual startup reduces variables for the first milestone.
