# AROS MIDIHub

**Modern MIDI connectivity and software synthesis for AROS**

AROS MIDIHub is an open-source project to expand MIDI support on the **AROS Research Operating System**, providing modern MIDI transports, network MIDI interoperability, and software synthesis using SoundFonts.

The core and the AROS package are intended to work across all AROS targets. Networking, CAMD and audio are common AROS paths on every target, with no target-specific implementation in MIDIHub or direct hardware dependency for network MIDI. Hardware MIDI transports can be added separately.

An initial investigation of the local AROS MIDI facilities, AppleMIDI protocol,
and the planned `contrib/extras` package layout is recorded in
[docs/initial-investigation.md](docs/initial-investigation.md).
The runtime ownership model is described in
[docs/midihub-architecture.md](docs/midihub-architecture.md). The planned
`MIDIHub.prefs` overview, basic routing, Network MIDI, Synthesizer, and Profiles
are described in [docs/preferences.md](docs/preferences.md).
The SoundFont candidates and distribution decision are recorded in
[docs/soundfonts.md](docs/soundfonts.md).

The project aims to provide a common foundation for MIDI applications on AROS, allowing software to communicate with local devices, networked computers, synthesizers, controllers, and software instruments through multiple MIDI transports.

## Goals

AROS MIDIHub aims to expand the AROS MIDI environment while preserving CAMD as the standard application-facing infrastructure. During incubation it hosts new capabilities; its coherent long-term role is a small persistent service for overview and basic routing between CAMD-visible endpoints, configured through `MIDIHub.prefs`.

Initial areas of development include:

- RTP-MIDI
- AppleMIDI / Network MIDI
- MIDI over UDP/IP
- USB MIDI integration
- Serial MIDI / traditional DIN MIDI
- Virtual MIDI ports
- Software synthesis using SoundFonts
- Interoperability with existing AROS MIDI software
- Basic persistent routing between CAMD endpoints
- Reusable MIDI profiles

The architecture should remain extensible so additional transports and MIDI standards can be incorporated over time.

## Long-term architecture goal

MIDIHub is intended primarily as an **incubation, integration, and validation project** for modern MIDI capabilities on AROS. Its purpose is not to establish a permanent parallel MIDI subsystem. Transport-specific capabilities should still move to their natural AROS homes. A small MIDIHub runtime may remain where it provides a coherent cross-transport function: persistent basic routing and MIDI-environment state above CAMD.

The preferred long-term direction is for every mature capability developed here to find its **most appropriate native home in AROS**. That decision should be made according to AROS architecture rather than according to the current MIDIHub repository layout. Examples may include CAMD for MIDI-facing facilities and virtual endpoints, Poseidon or the appropriate USB components for USB MIDI, the Bluetooth stack and profile classes for BLE MIDI, networking components for network transports, and the appropriate Preferences or system component for user configuration.

This principle also applies to the user interface. `MIDIHub.prefs` is a useful integration target while the facilities are being developed, but it does **not** need to remain an external MIDIHub application. If a unified MIDI Preferences interface belongs naturally in AROS, it should also be considered for upstream integration.

Likewise, synthesis and SoundFont support should not be kept inside MIDIHub merely to preserve the project boundary. Their eventual location should be chosen according to the architecture that best fits AROS, whether that is CAMD-related infrastructure, an audio or synthesis component, a separate system service or package, or another appropriate native location.

```text
                         MIDIHub
              incubation / integration / tests
                            |
        +-------------------+-------------------+
        |                   |                   |
     transports          services              UI
        |                   |                   |
        +-------------------+-------------------+
                            |
                     mature capability
                            |
                            v
                 determine native AROS home
                            |
       +--------------------+--------------------+
       |          |          |         |         |
      CAMD       USB      Bluetooth  Network   Preferences
                  |          |                    |
              USB MIDI   BLE MIDI             MIDI UI
                         btmidi.class
```

During development, MIDIHub may contain implementations, compatibility layers, test programs, patches, services, and Preferences UI that do not yet exist upstream. This is expected. Once a component is mature and has a clear native location, upstreaming it and removing the duplicate MIDIHub implementation is considered progress.

Agents and contributors should therefore avoid designing permanent MIDIHub-specific abstractions solely to preserve MIDIHub as a product. Prefer standard AROS interfaces and architectural boundaries whenever practical, while allowing experimental code to remain local until its upstream destination is understood.

**The architectural task is not to preserve MIDIHub as a subsystem, but to determine the correct AROS home for each capability.**

If AROS later provides another native facility that fully absorbs MIDIHub's routing and management role, MIDIHub may shrink further or disappear. Preserving the project boundary remains less important than placing each capability in the correct AROS architecture.

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

The optional [BLE MIDI transport](docs/ble-midi.md) connects a
registered BLE MIDI peripheral to CAMD through `bluetooth.library`. Its
portable packet codec supports running status, timestamps and multi-packet
SysEx. Upstream AROS now supplies its GATT server, service-record API,
notifications, advertising, and `btgatt.class` Preferences UI. The remaining
[AROS patch](patches/README.md) adds Write Without Response and 128-bit service
UUID advertising, plus service-write snapshots and queued notifications.
`btmidi.class` registers the standard service and bridges it to CAMD. Physical
BLE validation remains.

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
creates its ports. During incubation, MIDIHub's network, BLE, and synth
components create CAMD-visible endpoints directly; mature transport components
should move to their natural AROS subsystems. MIDIHub can then enumerate those
endpoints and preserve simple source-to-destination routes between them. The
[rescan patch](patches/aros-camd-rescan.patch) helps drivers installed after
CAMD opens. See [the CAMD integration design](docs/camd-integration.md) and
[MIDIHub runtime architecture](docs/midihub-architecture.md).

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

- Implement the `MIDIHub.prefs` overview of CAMD-visible endpoints and its UI
  for the resident `MIDIHubRouter` control channel. The route file, forwarding,
  status, reload, and stop contracts are now implemented.
- Add route persistence/reconnection and reusable Profiles without adding
  MIDI transformation or node-processing features.
- Run `MIDIHubSynth` with a native CAMD client and audible AHI output, then
  add SoundFont and preview controls to Preferences.
- Test the Poseidon USB MIDI fixes with a physical device, including SysEx
  and reconnect behavior.
- Test both BLE MIDI roles and peripheral pairing with physical radios and iOS,
  including Legacy Just Works and Secure Connections. Add the `btmidi.class`
  configuration window.
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
count logs support switch controllers 64–69, All Sound Off (120), All Notes
Off (123), Omni Off/On (124/125), and Poly Mode (127). Chapter E now protects
repeated Note On reference counts and
non-default
Note Off release velocities, supplementing Chapter N recovery. Other
alternate logs and extended Chapter X history remain to be implemented. The
first native MIDIHub Preferences application now provides a live CAMD endpoint
overview, persistent routing, Network MIDI settings, SoundFont/backend
selection, and a CAMD synth preview action.
Chapter M now decodes RPN/NRPN logs and protects parameter selection plus
Data Entry MSB/LSB, Data Increment/Decrement operations, and transaction
counts. Parameter transaction identity is retained for up to 128 distinct
channel, type, and parameter combinations per session. The optional
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
Chapter E encodes and recovers repeated Note On reference counts and Note Off
release velocities.
Chapter M encodes and recovers RPN/NRPN selection, pending selector MSBs, and
Data Entry MSB/LSB fields plus Data Increment/Decrement adjustments. The
transaction-count tool identifies missing parameter selections and suppresses
count-only recovery for transactions already observed.
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
Chapter X protects up to four unconfirmed completed non-MTC SysEx commands
within a shared 512-byte data budget, using ordered list-tool logs and COUNT
identities, and recovers them after covered loss. Exceeding either bound
starts a conservative new checkpoint. Larger and unfinished commands remain
outside Chapter X recovery.
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
