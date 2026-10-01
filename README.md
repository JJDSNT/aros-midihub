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

Discovery may later use mDNS/DNS-SD and the `_apple-midi._udp` service.

## Additional MIDI Transports

The project is intended to evolve beyond RTP-MIDI.

Potential transports include:

### USB MIDI

Support for class-compliant USB MIDI devices through the AROS USB stack.

This includes MIDI keyboards, controllers, interfaces and synthesizers.

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

BLE MIDI support can be explored as the Bluetooth capabilities available to AROS evolve.

### MIDI 2.0 / UMP

Future versions may investigate support for the MIDI 2.0 **Universal MIDI Packet (UMP)** format.

The architecture should avoid assumptions that would prevent MIDI 2.0 transports from being introduced later.

## SoundFont Synthesis

AROS MIDIHub also intends to provide lightweight software MIDI synthesis.

The synthesis engine must have an MIT or BSD license to fit the intended AROS
package. TinySoundFont is small and MIT-licensed, but lacks SoundFont modulators
needed by some GM banks. Candidate engines and remaining work are tracked in
[docs/soundfonts.md](docs/soundfonts.md).

The repository includes TinySoundFont and GeneralUser GS as pinned submodules
for development. Clone with `git clone --recurse-submodules`, or run
`git submodule update --init --recursive` in an existing checkout. Run
`make test-synth` to render a note from GeneralUser GS to
`build/generaluser-test.wav`. This confirms loading and PCM generation; it does
not establish correct GeneralUser GS playback. TinySoundFont currently ignores
SoundFont modulators, which GeneralUser GS uses extensively.

The `src/synth.c` wrapper accepts complete MIDI channel messages and renders
mono PCM. It currently handles notes, Control Change, Program Change, and
pitch bend. The WAV and AHI test programs use this same wrapper. Channel
pressure, polyphonic pressure, and synthesizer SysEx are not yet implemented.

For an AROS build, the optional MetaMake target
`contrib-aros-midihub-soundfonttest` installs `SoundFontTest` and the test bank
under `SYS:Extras/aros-midihub/`. Inside AROS, run
`MIDIHUB:C/SoundFontTest MIDIHUB:SoundFonts/GeneralUser-GS.sf2 RAM:generaluser-test.wav`.
`MIDIHUB:C/SoundFontPlay MIDIHUB:SoundFonts/GeneralUser-GS.sf2` renders the
same test note and plays it through the default `ahi.device` unit. The normal
package target does not include the bank or these test programs. Playback on
68k remains unverified: the current TinySoundFont loader assumes little-endian
SF2 data.

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

## Proposed Architecture

The project is intended to separate MIDI applications from individual transports.

    +----------------------------------+
    |        AROS Applications         |
    +----------------------------------+
                    |
                    v
    +----------------------------------+
    |         MIDIHub Core             |
    |                                  |
    | MIDI routing                     |
    | MIDI events                      |
    | Virtual ports                    |
    | Transport abstraction            |
    +----------------------------------+
          |          |          |
          v          v          v
       RTP-MIDI     USB       Serial
          |
      AppleMIDI
          |
       UDP / IP

                    +
                    |
                    v

    +----------------------------------+
    |       Software Synthesizer       |
    |                                  |
    | TinySoundFont                    |
    | SoundFont 2 (.sf2)               |
    +----------------------------------+
                    |
                    v
               AROS Audio

This architecture allows transports and synthesizers to evolve independently.

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

TinySoundFont will be investigated as the initial SoundFont2 synthesis engine.

Its small codebase and minimal dependencies make it particularly suitable for AROS.

## Development Strategy

The initial development can be divided into independent layers:

    Phase 1
    RTP-MIDI packet implementation
          |
          v
    Phase 2
    AppleMIDI session protocol
          |
          v
    Phase 3
    AROS MIDI integration
          |
          v
    Phase 4
    TinySoundFont / SF2 synthesis
          |
          v
    Phase 5
    Additional transports
          |
          +-- USB MIDI
          +-- Virtual MIDI
          +-- Serial MIDI
          +-- BLE MIDI
          |
          v
    Phase 6
    MIDI 2.0 / UMP investigation

The components should remain usable independently whenever possible.

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
feedback. The CK exchange estimates the peer clock offset. Short MIDI
messages with future RTP timestamps are queued for delivery to CAMD at the
requested time, with a 10-second lookahead and 256-event limit. Events beyond
those limits are discarded; SysEx is still delivered on receipt. Incoming
recovery journal framing is checked before feedback is sent. Sequence gaps
and whether a journal checkpoint covers them are reported. Chapter N now
recovers missed `Note Off` commands for notes the receiver still considers
active and cancels matching `Note On` events still waiting in the timestamp
queue. Ending a session also releases notes still active at the receiver.
Chapter N also recovers timely lost `Note On` commands when the sender's `Y`
hint requests playback and the clocks are synchronized. Chapters P and W
restore Program Change, bank selection, and Pitch Bend after packet loss.
Chapter C value logs restore Control Change values; alternate toggle and
count logs still need their own recovery logic. Recovery of the remaining
journal chapters, discovery, Preferences, and connecting the synthesizer to
CAMD remain to be
done. The CAMD bridge has compiled and linked for Linux hosted but has not
yet been exercised inside AROS.

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
leave a session.

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
