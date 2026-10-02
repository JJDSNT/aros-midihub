# AROS MIDIHub

**Modern MIDI connectivity and software synthesis for AROS**

AROS MIDIHub is an open-source project to expand MIDI support on the **AROS Research Operating System**, providing modern MIDI transports, network MIDI interoperability, and software synthesis using SoundFonts.

The core and the AROS package are intended to work across all AROS targets. Networking, CAMD and audio are common AROS paths on every target, with no target-specific implementation in MIDIHub or direct hardware dependency for network MIDI. Hardware MIDI transports can be added separately.

An initial investigation of the local AROS MIDI facilities, AppleMIDI protocol,
and the planned `contrib/extras` package layout is recorded in
[docs/initial-investigation.md](docs/initial-investigation.md).
The network configuration format and the planned Preferences and SoundFont
preview are described in [docs/preferences.md](docs/preferences.md).
The SoundFont candidates and distribution decision are recorded in
[docs/soundfonts.md](docs/soundfonts.md).

The project aims to provide a common foundation for MIDI applications on AROS, allowing software to communicate with local devices, networked computers, synthesizers, controllers, and software instruments through multiple MIDI transports.

## Goals

AROS MIDIHub aims to provide a modular MIDI infrastructure supporting both traditional and modern MIDI environments.

Initial areas of development include:

- RTP-MIDI
- AppleMIDI / Network MIDI
- MIDI over UDP/IP
- USB MIDI integration
- Serial MIDI / traditional DIN MIDI
- Virtual MIDI ports
- Software synthesis using SoundFonts
- Interoperability with existing AROS MIDI software

The architecture should remain extensible so additional transports and MIDI standards can be incorporated over time.

## Network MIDI

### RTP-MIDI

RTP-MIDI transports MIDI messages using the Real-time Transport Protocol over IP networks.

Support will target interoperability with implementations available on other platforms and dedicated network MIDI hardware.

Relevant specifications include:

- RFC 6295 — RTP Payload Format for MIDI
- RTP / UDP transport
- RTP-MIDI timestamps
- MIDI command sections
- Recovery journals

### AppleMIDI

AppleMIDI provides session management around RTP-MIDI and is commonly used by macOS and iOS Network MIDI implementations.

Planned support includes:

- Session invitation and negotiation
- Session synchronization
- RTP-MIDI transport
- Multiple network peers
- Direct IP connections
- Network MIDI discovery

MIDIHub advertises `_apple-midi._udp.local` over IPv4 mDNS and answers
DNS-SD browse queries when the network stack has a multicast-capable interface.

## Additional MIDI Transports

The project is intended to evolve beyond RTP-MIDI.

Potential transports include:

### USB MIDI

Support for class-compliant USB MIDI devices through the AROS USB stack.

This includes MIDI keyboards, controllers, interfaces and synthesizers.
The upstream Poseidon/CAMD path, a source fix for discovered data-transfer
defects, and the remaining physical test are documented in
[USB MIDI validation](docs/usb-midi.md).

### Serial / DIN MIDI

Traditional MIDI communication using serial interfaces and external MIDI hardware.

### Virtual MIDI

Software MIDI endpoints allowing AROS applications to communicate without physical MIDI hardware.

Virtual ports can also act as bridges between different transports.

For example:

    AROS application
          |
      Virtual MIDI
          |
       RTP-MIDI
          |
       Network
          |
      macOS / iOS

### BLE MIDI

Bluetooth Low Energy MIDI is widely used by modern wireless MIDI controllers and instruments.

The optional [BLE MIDI central transport](docs/ble-midi.md) connects a
registered BLE MIDI peripheral to CAMD through `bluetooth.library`. Its
portable packet codec supports running status, timestamps and multi-packet
SysEx. The [AROS GATT patch](patches/README.md) supplies 128-bit UUID
discovery and Write Without Response. The AROS x64 program compiles and links;
the full package build and physical BLE test remain. AROS cannot advertise
itself as a BLE MIDI peripheral until its Bluetooth stack has GATT server
support.

### MIDI 2.0 / UMP

Future versions may investigate support for the MIDI 2.0 **Universal MIDI Packet (UMP)** format.

The architecture should avoid assumptions that would prevent MIDI 2.0 transports from being introduced later.

## SoundFont Synthesis

AROS MIDIHub also intends to provide lightweight software MIDI synthesis.

The bundled synthesis engine must have an MIT or BSD license to fit the intended AROS
package. TinySoundFont is small and MIT-licensed, but lacks SoundFont modulators
needed by some GM banks. Candidate engines and remaining work are tracked in
[docs/soundfonts.md](docs/soundfonts.md).

The synth interface also supports an opt-in
[FluidSynth](https://github.com/FluidSynth/fluidsynth) backend when compiled
against an external FluidSynth installation. TinySoundFont remains the default
and the only backend in the normal AROS package. FluidSynth is LGPL-licensed;
its source and binaries are not included in this repository or the AROS
package. `make test-synth-fluid` builds a separate host test when its
development package is available. An AROS FluidSynth port is still needed
before that backend can run inside AROS.

The repository includes TinySoundFont and GeneralUser GS as pinned submodules
for development. Clone with `git clone --recurse-submodules`, or run
`git submodule update --init --recursive` in an existing checkout. Run
`make test-synth` to render a note from GeneralUser GS to
`build/generaluser-test.wav`. This confirms loading and PCM generation; it does
not establish correct GeneralUser GS playback. TinySoundFont currently ignores
SoundFont modulators, which GeneralUser GS uses extensively.

The `src/synth.c` wrapper accepts complete MIDI channel messages and renders
mono PCM. Both backends handle notes, Control Change, Program Change, and
pitch bend. FluidSynth also handles channel pressure, polyphonic pressure,
and recognized synthesizer SysEx messages. TinySoundFont does not support
those three message classes. The WAV and AHI test programs use this same
wrapper.

For an AROS build, the optional MetaMake target
`contrib-aros-midihub-soundfonttest` installs `SoundFontTest` and the test bank
under `SYS:Extras/aros-midihub/`. Inside AROS, run
`MIDIHUB:C/SoundFontTest MIDIHUB:SoundFonts/GeneralUser-GS.sf2 RAM:generaluser-test.wav`.
`MIDIHUB:C/SoundFontPlay MIDIHUB:SoundFonts/GeneralUser-GS.sf2` renders the
same test note and plays it through the default `ahi.device` unit. The optional
`contrib-aros-midihub-synth` target installs `MIDIHubSynth`, which accepts
CAMD messages on `MIDIHub Synth` and renders continuously through AHI with a
SoundFont supplied on its command line or configured in
`ENV:MidiHub/SoundFont` or `ENVARC:MidiHub/SoundFont`. The normal package
target does not include the synth, bank, or test programs. Playback on
68k remains unverified: the current TinySoundFont loader assumes little-endian
SF2 data.

`MIDIHubSynth --backend tiny|fluid` selects the engine. Without that option it
reads `ENV:MidiHub/Backend`, then `ENVARC:MidiHub/Backend`, and defaults to
`tiny`. Selecting `fluid` requires a build with the external FluidSynth SDK;
the normal AROS build reports that the backend is unavailable.

The [live synthesizer guide](docs/synth-camd.md) gives the build and CAMD
probe commands. Live AHI playback remains to be tested in AROS.

TinySoundFont is a small SoundFont2 synthesizer implementation written in C/C++ and designed to be embedded directly into applications.

The synthesizer layer should allow:

    MIDI events
        |
        v
    MIDIHub
        |
        v
    TinySoundFont
        |
        v
    SoundFont (.sf2)
        |
        v
    PCM Audio
        |
        v
    AROS audio output

This makes it possible for AROS applications to play General MIDI-compatible instruments without requiring external MIDI hardware.

## CAMD integration architecture

The existing Poseidon USB MIDI class is the design reference for every
MIDIHub integration. AROS MIDI applications use `camd.library` clusters.
Transport code converts between MIDI bytes and its own protocol at the edge;
it does not require applications to use a MIDIHub-specific API.

```text
AROS MIDI applications <-> camd.library / CAMD clusters
                              |          |          |          |
                         USB MIDI   RTP-MIDI   BLE MIDI   MIDIHub Synth
                         Poseidon    AppleMIDI  Bluetooth    |
                              |          |          |     TinySoundFont
                         USB device  UDP/IP   GATT device      |
                                                              AHI
```

Poseidon writes a driver to `DEVS:Midi` for USB devices; CAMD loads it and
creates its ports. MIDIHub's network, BLE, and synth programs create virtual
CAMD ports directly. Both mechanisms give applications the same CAMD interface.
The [rescan patch](patches/aros-camd-rescan.patch) helps drivers installed
after CAMD opens, but the current virtual ports do not depend on it. See
[the CAMD integration design](docs/camd-integration.md).

## Initial Implementation Sources

Several existing open-source projects can serve as implementation references.

### MIDIKit

MIDIKit by Jonas Pommerening contains a C implementation of RTP-MIDI. Its exact license should be verified against the particular upstream source revision before reusing code.

It is a particularly interesting reference for the initial AROS RTP-MIDI implementation because the RTP-MIDI functionality is already separated from much of the platform-specific code.

### cmidid

cmidid contains another implementation derived from MIDIKit and can provide useful examples of adapting AppleMIDI/RTP-MIDI concepts to another operating system.

### librtpmidid

librtpmidid provides a more recent implementation of RTP-MIDI and AppleMIDI and can serve as a protocol and interoperability reference.

### TinySoundFont

TinySoundFont is the default MIT-licensed SF2 engine. It powers the WAV and
AHI diagnostics and the optional CAMD synthesizer. Missing SoundFont
modulators and big-endian loading still require work before reliable GM/GS
playback can be claimed across targets.

## Next milestones

- Run `MIDIHubSynth` with a native CAMD client and audible AHI output, then
  add SoundFont and preview controls to Preferences.
- Test the Poseidon USB MIDI fixes with a physical device, including SysEx
  and reconnect behavior.
- Test BLE MIDI against a physical peripheral and the patched AROS GATT
  client. AROS advertising as a BLE MIDI peripheral needs GATT server work.
- Verify AppleMIDI interoperability and discovery with iOS/macOS on a network
  where AROS multicast traffic is reachable.
- Implement and verify SoundFont modulators and big-endian SF2 loading before
  treating GeneralUser GS as a reliable bank on all targets.

## Interoperability Targets

Network MIDI interoperability should eventually be tested with:

- macOS Network MIDI
- iOS / iPadOS MIDI applications
- Windows RTP-MIDI implementations
- Linux RTP-MIDI implementations
- Hardware RTP-MIDI interfaces

## Project Status

AROS MIDIHub is currently in the **initial implementation phase**. AppleMIDI
control and RTP-MIDI packet codecs, a one-peer invitation state machine, host
tests, an AROS package self-test, and a UDP diagnostic program are present.
The program negotiates a session, exchanges CK packets, and bridges MIDI
messages and SysEx through CAMD clusters on AROS. It acknowledges received
RTP packets, including journal-only guard packets, with AppleMIDI `RS`
feedback. The CK exchange estimates the peer clock offset. MIDI messages and
completed SysEx with future RTP timestamps are queued for delivery to CAMD at
the requested time, with a 10-second lookahead and 256-event limit. Queued
SysEx data is additionally limited to 64 KiB. Events beyond those limits are
discarded. Incoming
recovery journal framing is checked before feedback is sent. Sequence gaps
and whether a journal checkpoint covers them are reported. Chapter N now
recovers missed `Note Off` commands for notes the receiver still considers
active and cancels matching `Note On` events still waiting in the timestamp
queue. Ending a session also releases notes still active at the receiver.
Chapter N also recovers timely lost `Note On` commands when the sender's `Y`
hint requests playback and the clocks are synchronized. Chapters P and W
restore Program Change, bank selection, and Pitch Bend after packet loss.
Chapter C value logs restore Control Change values; alternate toggle and
count logs support sustain (64), All Sound Off (120), and All Notes Off (123).
Other alternate logs, Chapters M and E, extended Chapter X history, and
Preferences remain to be implemented. The optional
CAMD synthesizer now builds, but live AROS audio playback is unverified. A
Linux-hosted AROS loopback run opened
the CAMD bridge and completed AppleMIDI clock sync, Note On, and Note Off
between two MIDIHub processes. The packaged `MIDIHubCAMDProbe` independently
sent Note On and Note Off through `MIDIHub Out` and received both through
`MIDIHub In` after the network round trip.

MIDIHub advertises its control port as `_apple-midi._udp.local` over IPv4
Bonjour/mDNS and answers DNS-SD browse queries. The configured session name
is the visible service name. Discovery requires multicast reachability from
the peer; WSL NAT and the AROS hosted build's unconfigured TAP interface
currently prevent this from being an iPhone browse test inside AROS.

Outgoing messages now include recovery journals for notes, Control Change,
Program Change, Pitch Bend, Channel Aftertouch, Poly Aftertouch, System
Reset, Tune Request, Song Select, Active Sense, and Chapter Q sequencer state.
Chapter Q tracks Start, Continue, Stop, MIDI Clock, and Song Position Pointer.
The receiver decodes System Chapters D, V, Q, and F and recovers the standard
commands. Chapter Q recovery rebuilds transport, downbeat, and positions in
the MIDI Song Position Pointer range. Positions above that range and the
optional TIMETOOLS correction remain validation-only.
Chapter F complete and partial MIDI Time Code fields are decoded and validated.
Universal Real Time MTC Full Frame SysEx commands receive outgoing Chapter F
protection and are recovered after covered loss. Quarter Frame generation and
recovery support contiguous forward and reverse sequences, partial frames,
the forward two-frame correction, and 29.97 drop-frame rollover.
Chapter X protects the most recent completed non-MTC SysEx command when it has
at most 512 data bytes, using the list tool and a COUNT identity, and recovers
it after covered loss. Keeping several unconfirmed SysEx commands, larger
commands, and unfinished commands in Chapter X remains to be implemented.
The sender retains up to 32 packets and removes confirmed history when
AppleMIDI `RS` feedback arrives. While unconfirmed history remains, it sends
an empty MIDI guard packet once per
second so the peer can recover a lost final event. The initiator exchanges
clocks three times during startup, then every 50 seconds. A responder closes
a session after two minutes without peer clock
synchronization and releases active notes.

Run the portable codec tests with `make test`. For an AROS source checkout,
place this repository at `contrib/extras/aros-midihub` and build the
`contrib-aros-midihub` MetaMake target. The package is intended to install
under `SYS:Extras/aros-midihub`, with an `ENVARC:SYS/Packages` registration.

On AROS, `MIDIHub` reads `ENV:MidiHub/Network`, falling back to
`ENVARC:MidiHub/Network`. Use `MIDIHub --config file` to select a different
file. Without a file it listens on control port 5004. The text file accepts
`local_port`, `peer_ip`, `peer_port`, and `session_name`; see the example in
[the configuration guide](docs/preferences.md). A Preferences application and
SoundFont preview are planned but are not present yet.

Run `make test-network` for a localhost test of invitations, CK exchange,
Note On/Off traffic, and SysEx packets. `make demo` builds the same program on
Linux. To listen, run `build/MIDIHub 5004`; to invite a listener at 5004 from
another terminal, run `build/MIDIHub 5006 127.0.0.1 5004 --probe-note`.
The optional probe sends one Note On followed by Note Off. On AROS, the
program uses the same arguments and `bsdsocket.library`. Press Ctrl-C to
leave a session. See the [iPhone smoke test](docs/iphone-smoke-test.md) for
testing a real AppleMIDI peer from WSL.

To check CAMD locally in AROS, start two MIDIHub processes on loopback:
`MIDIHub 5004` and `MIDIHub 5006 127.0.0.1 5004`. Once both report
`session connected`, run `MIDIHubCAMDProbe`. It reports `round trip passed`
after receiving its Note On and Note Off through both CAMD clusters and the
network session. This requires AROSTCP and its loopback interface.

`make m68k` builds `build/MIDIHub-m68k` with an AROS m68k SDK on `PATH`.
If the compiler is elsewhere, pass `M68K_CC=/path/to/m68k-aros-gcc`.
The local Bellatrix SDK builds this executable successfully; execution on
AROS m68k has not yet been verified.

The first development target is establishing RTP-MIDI / AppleMIDI communication between AROS and another system over a local IP network.

## License

The original MIDIHub code is licensed under [MIT](LICENSE), matching the
approach used by aros-bluzing and remaining compatible with the AROS ecosystem.

Third-party components retain their respective licenses and copyright notices.

## Project

**AROS MIDIHub**

Modern MIDI connectivity and software synthesis for AROS.
