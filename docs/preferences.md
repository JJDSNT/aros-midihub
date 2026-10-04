# MIDIHub Preferences

## Purpose

`MIDIHub.prefs` is the user-facing control surface for the MIDI environment exposed by AROS through CAMD. Its goal is deliberately modest: provide a clear overview of available MIDI endpoints, basic persistent routing between them, configuration of MIDIHub-managed network MIDI and software synthesis, and reusable profiles.

It is not intended to become a DAW, a MIDI processor, or a sophisticated node-graph environment.

The five primary areas are:

1. **Overview / Devices**
2. **Routing**
3. **Network MIDI**
4. **Synthesizer**
5. **Profiles**

A small MIDI monitor or test facility may be available as a diagnostic action, but diagnostics do not need to be a primary page.

## Ownership boundaries

MIDIHub presents MIDI information without taking ownership away from the AROS subsystem that provides an endpoint.

| Concern | Owner |
| --- | --- |
| MIDI endpoint graph and message distribution | CAMD |
| Basic persistent MIDI routes | MIDIHub |
| USB device binding and USB hardware | Poseidon / Trident |
| Bluetooth adapter, pairing, trust and security | Bluetooth Preferences |
| IP, Ethernet and Wi-Fi configuration | Network Preferences |
| Audio device and mode | AHI Preferences |
| AppleMIDI session settings | MIDIHub |
| SoundFont and MIDI synth settings | MIDIHub |
| MIDI profiles | MIDIHub |

Where useful, MIDIHub.prefs may show status and provide a button to open the owning Preferences application or class configuration. It must not maintain a second copy of those settings.

## Overview / Devices

The first page should answer a simple question: **what MIDI endpoints are available now?**

It should enumerate CAMD-visible endpoints and present a compact view such as:

```text
Endpoint             Transport      Direction      Status
Arturia MiniLab      USB            In / Out       Online
iPhone               BLE MIDI       In / Out       Online
MacBook              AppleMIDI      In / Out       Available
MIDIHub Synth        Software       In             Running
```

Useful summary state includes CAMD availability, MIDIHub runtime state, active/waiting route counts, endpoint transport, direction, and connection state.

The page is a system overview, not a replacement for transport-specific configuration.

## Routing

Routing is intentionally **basic**. MIDIHub.prefs should let the user see, create, remove, and persist connections between CAMD-visible endpoints.

A conventional Zune list/table is sufficient and is the preferred initial UI:

```text
From                 To                  Status
Arturia MiniLab      MIDIHub Synth       Active
iPhone               MacBook             Active
MacBook              USB MIDI Out        Waiting

[ Add Route ] [ Remove Route ]
```

Adding a route only requires a source and destination. An optional **Reconnect automatically** setting may control whether MIDIHub restores the route when an endpoint reappears.

The initial routing scope explicitly does **not** require:

- note or velocity transformation
- keyboard splits
- transpose
- channel remapping
- event-processing graphs
- scripting
- arbitrary processing nodes

CAMD already provides the MIDI graph primitives. MIDIHub's role is to remember the desired basic connections and keep them alive independently of the Preferences process.

A graphical patchbay may be investigated later as an alternative view, but it is not an implementation requirement and the routing model must not depend on one.

## Network MIDI

This page owns MIDI-specific network-session configuration, not general network configuration.

The current AppleMIDI/RTP-MIDI implementation reads `ENV:MidiHub/Network` and falls back to `ENVARC:MidiHub/Network`. The UTF-8 file uses one `name=value` option per line. Empty lines and lines beginning with `#` or `;` are ignored.

```text
local_port=5004
peer_ip=192.168.1.20
peer_port=5004
session_name=AROS MIDIHub
```

`local_port` and `peer_port` are control ports from 1 through 65534; the next port carries data. Omitting both `peer_ip` and `peer_port` allows incoming invitations without initiating a connection. Without a file, the current program listens on port 5004. The session name can be at most 63 bytes.

The Preferences page should expose the relevant MIDI concepts: service enabled state, session name, local control port, discovery, known/discovered peers, connection state, and Connect/Disconnect actions where supported.

Future Network MIDI 2.0 / UMP support can live on this page without implying that it is the same protocol as AppleMIDI.

IP addresses may be used where a MIDI peer requires them, but interface, Wi-Fi, DNS, gateway, and general TCP/IP configuration remain in Network Preferences.

## Synthesizer

The Synthesizer page configures the software MIDI instrument exposed through CAMD.

It should provide:

- enable/status
- SoundFont `.sf2` selection
- available backend
- gain
- default bank/program where supported
- a simple Test Note / preview action

The optional `MIDIHubSynth` currently reads a SoundFont path from `ENV:MidiHub/SoundFont`, falling back to `ENVARC:MidiHub/SoundFont`; an explicit command-line path overrides both. It loads the path at startup and does not yet implement live bank switching.

The backend is read from `ENV:MidiHub/Backend`, then `ENVARC:MidiHub/Backend`. Valid values are `tiny` and `fluid`; `--backend` overrides them. The normal AROS package provides TinySoundFont. Preferences should only offer FluidSynth when an external FluidSynth-enabled build is available.

A new SoundFont should be validated before replacing the active one so a failed load does not interrupt current playback.

Preview should send Note On/Off through the same CAMD/synth/audio path used by applications. A selectable note/velocity or a small test control is sufficient.

### AHI boundary

AHI owns audio device and audio-mode configuration. MIDIHub.prefs should not duplicate it.

`SoundFontPlay` and `MIDIHubSynth` currently use AHI's device API on unit 0. AHI Preferences already has a Music unit for applications using AHI's low-level API; moving the synth to that path would require a separate implementation and test.

## Profiles

Profiles preserve a useful MIDI environment without turning MIDIHub into a processing workstation.

A profile may contain MIDIHub-owned choices such as:

- the set of persistent routes
- route reconnect state
- Network MIDI settings
- synth/SoundFont settings

For example:

```text
Home Studio

Arturia MiniLab  -> MIDIHub Synth
iPhone BLE       -> MIDIHub Synth
MacBook          -> USB MIDI Out

AppleMIDI session: AROS MIDIHub
Synth SoundFont: GeneralUser GS
```

Profiles must not copy configuration owned by other subsystems. Bluetooth pairings, USB bindings, network-interface configuration, and AHI modes remain external.

Useful actions are `New`, `Duplicate`, `Rename`, `Delete`, and `Activate`, plus selection of the profile loaded at startup.

## Runtime separation

The Preferences application is not the router.

CAMD links are associated with their owning `MidiNode`; deleting that node removes its links. Therefore routes that must survive after the GUI closes need a resident owner.

```text
MIDIHub.prefs
      |
      | desired configuration
      v
   MIDIHub
      |
      | persistent route ownership
      v
     CAMD
```

MIDIHub should restore configured routes at startup, mark routes as waiting when an endpoint is absent, and reconnect them when the relevant CAMD cluster appears.

The detailed runtime rationale is documented in [midihub-architecture.md](midihub-architecture.md).

## Bluetooth MIDI ownership

The BLE MIDI implementation belongs with the AROS Bluetooth architecture. The peripheral implementation is intended as `btmidi.class` under `SYS:Classes/Bluetooth`, following existing Bluetooth profile classes.

Bluetooth Preferences should continue to manage adapters, devices, pairing, trust, security, and class loading. A Bluetooth MIDI class may expose its own profile-specific settings/status.

MIDIHub.prefs should show the resulting BLE MIDI endpoint as part of the MIDI environment. It may link to Bluetooth configuration, but must not duplicate pairing or security controls.

## AROS Preferences semantics

The GUI should follow normal AROS Preferences behavior. `Use` applies the selected MIDIHub configuration for the current environment, `Save` persists it, and `Cancel` discards unapplied changes.

The runtime service must not depend on the GUI being open.

## Design summary

```text
Overview / Devices  -> What MIDI endpoints exist?
Routing             -> What is connected to what?
Network MIDI        -> How are MIDI network sessions configured?
Synthesizer         -> How is the software MIDI instrument configured?
Profiles            -> Which saved MIDI environment should be active?
```

The guiding principle is **overview and basic control**. Advanced MIDI transformation and processing belong in specialized CAMD applications, not in MIDIHub.prefs.
