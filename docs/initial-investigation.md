# Initial investigation — AROS MIDIHub

Status: October 2, 2026. This document records verified findings and the
implementation sequence. The first increment includes AppleMIDI and RTP-MIDI
packet codecs, invitation negotiation for one peer, host tests, a native
self-test, and a UDP diagnostic program. The program opens both ports,
exchanges invitations and CK messages, and can send a test note. It estimates
clock offset and schedules future MIDI and SysEx events. The CAMD bridge for
short messages and SysEx is implemented. On Linux-hosted AROS, two MIDIHub
processes opened the bridge, completed an AppleMIDI session over AROSTCP's
loopback interface, and exchanged Note On and Note Off. A separate native CAMD
client, `MIDIHubCAMDProbe`, sent both messages to `MIDIHub Out` and received
both from `MIDIHub In` after the network round trip. The probe waits briefly
after creating its links before sending the first message. Audio output is
not yet integrated.

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
Linux-hosted `x86_64-aros-gcc` linked its AROS version. A focused hosted AROS
loopback run has now exercised MIDIHub with AROSTCP. The Bellatrix
m68k GCC and its AROS libraries also linked `MIDIHub-m68k` through `make
m68k`; that binary has not yet been run inside m68k AROS. The separate
TinySoundFont smoke test renders GeneralUser GS to a WAV on the host and
compiles for m68k. A separate optional program sends the same rendered note
to `ahi.device`; both use the MIDIHub synthesis wrapper. The optional
`MIDIHubSynth` process connects CAMD input to continuous AHI output and has
linked for AROS x64 and m68k, but live AROS playback has not been run.

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
  MIDIHub creates virtual clusters as a CAMD client. The USB driver's CAMD
  contract is a reference for all MIDIHub endpoints; CAMD's startup-only
  driver scan affects `DEVS:Midi` drivers, not those virtual clusters. See
  [the CAMD integration design](camd-integration.md).
- `rom/usb/classes/camdmidi/` contains a Poseidon class that creates a CAMD
  driver for USB MIDI. The current upstream `mmakefile.src` enables i386,
  x86_64, arm, aarch64, and ppc. SysEx handling has `FIXME` comments.
  MIDIHub's network bridge uses CAMD independently of the USB controller.
  The October 2026 audit found receive and transmit defects; see
  [USB MIDI validation](usb-midi.md).
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
  future short MIDI messages and completed SysEx for delivery at their RTP
  timestamps. The shared queue holds up to 256 events, accepts at most 10
  seconds of lookahead, and limits queued SysEx payloads to 64 KiB; later or
  excess events are discarded. The initiator refreshes synchronization every
  50 seconds.
- Apple advertises `_apple-midi._udp` through Bonjour. Manual connection
  by IP address and port is supported. MIDIHub now advertises
  `_apple-midi._udp.local` over IPv4 mDNS and answers DNS-SD browse queries.
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
  values. The toggle tool handles switch controllers 64–69, and the count
  tool handles All Sound Off (120), All Notes Off (123), Omni Off/On
  (124/125), and Poly Mode (127). Chapter E protects repeated
  Note On reference counts and non-default Note Off release velocities.
  Chapter M protects RPN/NRPN selection, pending selector MSBs, and Data Entry
  MSB/LSB values plus Data Increment/Decrement adjustments. Its
  transaction-count tool tracks up to 128 distinct channel, type, and
  parameter combinations, identifies missed selections, and suppresses
  count-only recovery when the receiver has already observed the count.
  Enhanced Chapter C lists are decoded and recovered in command order; the
  sender uses default Chapter C encoding. Channel journal CHAN and H fields
  use their RFC 6295 bit positions. System Chapters D, V, Q, and F
  are decoded. Chapters D and V recover System Reset, Tune Request, Song Select,
  and Active Sense. Chapter Q recovers standard sequencer transport, downbeat,
  and positions in the MIDI Song Position Pointer range. Larger positions and
  optional TIMETOOLS data are validated without position replay. Outgoing
  channel and D/V system messages, plus Chapter Q sequencer state, include
  journals from a bounded 32-packet history, trimmed by AppleMIDI RS feedback.
  Chapter Q covers Start, Continue, Stop, MIDI Clock, and Song Position
  Pointer. Chapters T and A cover Channel and Poly Aftertouch. Guard packets
  repeat pending recovery data after the last event. Chapter F complete and
  partial MIDI Time Code fields are validated. Universal Real Time MTC
  Full Frame SysEx commands receive outgoing Chapter F protection and are
  recovered after covered loss. Quarter Frame generation and recovery support
  contiguous forward and reverse sequences, partial frames, the forward
  two-frame correction, and 29.97 drop-frame rollover.
  Chapter X protects up to four unconfirmed completed non-MTC SysEx commands
  within a shared 512-byte data budget, using ordered list-tool logs and COUNT
  identities. Exceeding either bound starts a conservative new checkpoint.
  Larger and unfinished commands remain outside journal recovery.

## Initial network implementation

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

The existing USB implementation defines the CAMD interface expected by MIDI
applications. Network and BLE already expose virtual CAMD ports through the
client API; they do not need USB-style driver binaries. See
[the CAMD integration design](camd-integration.md) for the two lifecycles.

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

## Remaining integration phases and dependencies

| Feature | Practical prerequisite |
| --- | --- |
| System mDNS discovery | Move the existing minimal advertisement/answer code behind the shared AROS register/browse service described in [mDNS and AROSTCP](mdns-arostcp.md) |
| Recovery journal interoperability | The codec and bounded send history exist; validate loss recovery against independent peers |
| Multiple peers in the AROS event loop | The portable peer manager exists; create one CAMD client per peer and enforce loop prevention |
| USB MIDI | CAMD class test on targets that provide it; optional transport |
| TinySoundFont/SF2 | Chosen and tested AROS PCM output, bank distribution policy, modulator work |
| Serial/DIN MIDI | Available target-specific transport and hardware |
| BLE MIDI | Current AROS Bluetooth stack integration and 128-bit GATT UUID discovery |
| MIDI 2.0/UMP | Event contract independent of three-byte `MidiMsg` |

## BLE MIDI GATT limitation

As of upstream AROS commit `13c7f81274825dd9bca047fc7caebfa21576a163`
(October 1, 2026), `rom/bluetooth/stack/include/btcore/gatt_client.h`
documents that GATT service and characteristic discovery reports only 16-bit
UUIDs and skips custom 128-bit UUIDs. BLE MIDI uses a 128-bit service UUID
(`03B80E5A-EDE8-4B33-A751-6CE34EC4C700`) and a 128-bit I/O characteristic
UUID. Thus the current GATT client cannot identify a BLE MIDI endpoint through
its normal discovery results. This is a known prerequisite for BLE MIDI, not
an implemented MIDIHub transport.

The AROS prerequisite is to retain 128-bit service and characteristic UUIDs
in GATT discovery results. MIDIHub uses the public Bluetooth API to subscribe
to the MIDI characteristic and route its messages to CAMD. The upstream
[GATT client header](https://github.com/aros-development-team/AROS/blob/13c7f81274825dd9bca047fc7caebfa21576a163/rom/bluetooth/stack/include/btcore/gatt_client.h)
records the discovery restriction; Microsoft's
[BLE MIDI transport documentation](https://microsoft.github.io/MIDI/kb/ble-midi-transport-architecture/)
lists the MIDI service UUID.

The [AROS source patch](../patches/README.md) implements the discovery change
and ATT Write Command support, including the existing public
`BTPR_GATTWRITENORSP` route. The optional [BLE MIDI central transport](ble-midi.md)
now uses that path to connect a BLE MIDI peripheral to CAMD. The AROS x64
transport has compiled and linked with generated Bluetooth headers. The
patched `bluetooth.library` and the runtime have not been exercised with a
BLE MIDI device.

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
