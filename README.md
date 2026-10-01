# AROS MIDIHub

**Modern MIDI connectivity and software synthesis for AROS**

AROS MIDIHub is an open-source project to expand MIDI support on the **AROS Research Operating System**, providing modern MIDI transports, network MIDI interoperability, and software synthesis using SoundFonts.

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

The initial implementation will investigate **TinySoundFont (TinySF)**.

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

MIDIKit by Jonas Pommerening contains a C implementation of RTP-MIDI and is distributed under the BSD 2-Clause license.

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

AROS MIDIHub is currently in the **research and initial implementation phase**.

The first development target is establishing RTP-MIDI / AppleMIDI communication between AROS and another system over a local IP network.

## License

The final project license will be selected to remain compatible with the AROS ecosystem and with the licenses of any source code incorporated into the project.

Third-party components retain their respective licenses and copyright notices.

## Project

**AROS MIDIHub**

Modern MIDI connectivity and software synthesis for AROS.
