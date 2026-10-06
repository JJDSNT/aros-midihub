# MIDIHub runtime and Preferences architecture

## Purpose

MIDIHub is an incubation and integration repository for modern MIDI
capabilities on AROS. Whether a permanent runtime called **MIDIHub** should
remain is now deliberately an open architectural question.

As capabilities mature, they should move to the subsystem that naturally owns
them. BLE MIDI is moving toward the Bluetooth stack, Network MIDI toward native
CAMD-facing devices, and mDNS toward shared system infrastructure. Persistent
routing may ultimately belong to CAMD itself or to a CAMD-owned resident
facility, while software synthesis may become an independent CAMD/AHI
component. If those responsibilities graduate, preserving a MIDIHub runtime is
not a goal by itself.

`MIDIHub.prefs` remains useful during incubation as the integrated management
surface. Its eventual ownership and name should follow the final subsystem
boundaries rather than force the continued existence of a MIDIHub service.

A useful AROS analogy is Trident and Poseidon: the management application does not replace the underlying subsystem. MIDIHub differs in that its view spans several native subsystems through CAMD rather than managing a single hardware stack.

```text
                         MIDIHub.prefs
                    configure / inspect
                              |
                              v
                           MIDIHub
                persistent routing / policy
                              |
                              v
                             CAMD
                              |
          +----------+--------+---------+----------+
          |          |                  |          |
       USB MIDI   BLE MIDI         Network MIDI   Synth
       Poseidon   Bluetooth        AppleMIDI/UMP   |
                                                 AHI
```

## Architectural boundaries

### CAMD

CAMD remains the MIDI infrastructure and application-facing contract. It owns the concepts already provided by `camd.library`, including MIDI nodes, clusters, sender and receiver links, MIDI and SysEx distribution, channel/event masks, link priorities and filters, cluster enumeration/lookup, and cluster add/remove notifications.

MIDIHub must use these facilities rather than introduce a parallel MIDI API.

### MIDIHub

The current MIDIHub runtime incubates **persistent cross-endpoint policy**; this
is not yet a claim that MIDIHub is its permanent system owner. Because CAMD
already owns the MIDI graph, persistent routing should be evaluated as a
possible CAMD or CAMD-owned service capability before the runtime boundary is
frozen.

During incubation its responsibilities may include:



- restoring user-defined routes at startup
- keeping routes active while Preferences is closed
- waiting for configured endpoints that are temporarily absent
- reconnecting routes when CAMD clusters appear again
- forwarding between CAMD clusters when a route requires an active node
- maintaining named routing profiles or studio configurations
- exposing route and activity state to `MIDIHub.prefs`
- exposing enough status for a clear overview of the resulting MIDI topology

MIDIHub should not absorb functionality merely because it is MIDI-related. Bluetooth pairing belongs to Bluetooth, USB binding to Poseidon, IP configuration to the network subsystem, and audio-mode selection to AHI.

### MIDIHub.prefs

`MIDIHub.prefs` configures and observes the MIDI environment. It must not be required to remain open for MIDI routing to continue. Closing Preferences must not destroy active routes.

```text
MIDIHub.prefs
      |
      | configuration / commands / status
      v
   MIDIHub
      |
      | owns runtime MidiNode/MidiLinks
      v
     CAMD
```

## Why a resident router is useful

The current CAMD API already exposes most primitives required by the planned interface:

- `NextCluster()` and `FindCluster()` enumerate and locate clusters.
- `NextClusterLink()` and `NextMidiLink()` inspect connections.
- `AddMidiLinkA()`, `SetMidiLinkAttrsA()`, and `RemoveMidiLink()` manage links.
- `MidiLinkConnected()` reports link state.
- `StartClusterNotify()` and `EndClusterNotify()` notify clients when clusters are added or removed.
- `LockCAMD(CD_Linkages)` protects topology inspection.

However, CAMD links are owned by a `MidiNode`. `DeleteMidi()` removes links attached to that node. A Preferences application therefore cannot own a persistent route and then exit without losing it.

MIDIHub can fill that lifecycle role without changing CAMD's fundamental model:

```text
BLE Piano cluster
       |
       v
  MIDIHub MidiNode
       |
       +-----------------> Synth cluster
       |
       +-----------------> AppleMIDI cluster
```

The service owns the links while the configured route exists. The GUI only changes the desired configuration.

## Dynamic endpoint lifecycle

Persistent routing must tolerate devices and peers that are not present when MIDIHub starts.

For a saved route such as `Arturia MiniLab -> MIDIHub Synth`, MIDIHub loads the route as waiting when the source is absent and subscribes with `StartClusterNotify()`. When Poseidon/CAMD later creates the cluster, MIDIHub resolves the endpoint, creates the required links, and marks the route active.

The same model can be used for BLE MIDI, network MIDI, virtual endpoints, and other CAMD-visible transports.

`StartClusterNotify()` currently reports cluster addition and removal, not all internal link-state changes. MIDIHub should account for that limitation rather than assuming a general CAMD topology-change event.

## Routing model

A route is persistent policy describing how one CAMD-visible source should reach one or more destinations.

```text
source cluster -> MIDIHub -> destination cluster
```

A source may fan out:

```text
                    +--> Synth
BLE Keyboard -> MIDIHub
                    +--> AppleMIDI
```

The MIDIHub routing model is deliberately basic: source, destination, enabled state, and optional automatic reconnection. MIDI transformation, channel remapping, keyboard splits, scripting, and processing graphs are outside the intended scope; specialized CAMD applications can provide those functions.

MIDIHub should avoid forwarding traffic through itself when CAMD already provides an equivalent direct mechanism that satisfies the required lifecycle. It exists to supply persistent routing policy and active bridging where necessary, not to add an unnecessary hop to every MIDI path.

## Preferences model

The exact GUI layout remains a design decision, but the architecture supports a unified MIDI control surface.

### Devices and connections

Present CAMD-visible MIDI endpoints independent of transport: USB MIDI, BLE MIDI, AppleMIDI peers, future Network MIDI 2.0/UMP endpoints, software synthesizers, and virtual MIDI endpoints.

Transport-specific hardware configuration remains in the owning AROS Preferences or class UI. MIDIHub may show status and provide a way to open the relevant configuration surface.

### Routing

Display and edit persistent routes managed by MIDIHub. Source, destination, and state should be clear without requiring the user to understand CAMD internals. A conventional Zune list/table is sufficient; a graphical patchbay is optional future UI, not an architectural requirement.

### Network MIDI

Configure the owning Network MIDI device and expose its MIDI-specific session
and peer state for AppleMIDI/RTP-MIDI and future network MIDI transports. IP
addressing, interfaces, Wi-Fi, and general network configuration remain
outside MIDIHub.

### Synth

Expose MIDI synthesis choices such as SoundFont, supported backend, default bank/program, gain, and MIDI preview. AHI device and audio-mode configuration remain owned by AHI.

## Profiles

A named profile can describe a complete desired routing topology without owning underlying hardware configuration.

```text
Home Studio

BLE Keyboard ------> MIDIHub Synth
USB Controller ----> Bars&Pipes
Mac AppleMIDI -----> CAMD application
```

Profiles are MIDIHub policy. Bluetooth pairing, USB binding, network configuration, and AHI settings remain external dependencies.

## Relationship to transport upstreaming

This architecture does not reverse MIDIHub's upstream-first direction. Experimental implementations should still move to their native AROS homes when mature.

```text
experimental implementation in MIDIHub
                |
                v
       capability becomes mature
                |
                v
      determine native AROS home
                |
       +--------+---------+-----------+
       |                  |           |
   Poseidon           Bluetooth      CAMD / other
   USB MIDI           BLE MIDI       native component
```

After those moves, MIDIHub does not need duplicate transport code. What can legitimately remain is the cross-transport routing and management role:

```text
native AROS transports
         |
         v
        CAMD
         |
         v
      MIDIHub
 persistent routing policy
         |
         v
   MIDIHub.prefs
```

If persistent routing moves into CAMD (or a CAMD-owned service) and synthesis
moves to its own CAMD/AHI component, MIDIHub may cease to exist as a runtime.
The repository can still serve as the incubator in which those capabilities
are developed and validated. Preserving the MIDIHub name or runtime is not an
architectural requirement.

## Relationship to existing documents

This document defines the high-level runtime and ownership model.

- [preferences.md](preferences.md) records current network and synth configuration contracts and detailed Preferences requirements.
- [camd-integration.md](camd-integration.md) describes how individual transports and services appear through CAMD.
- [ble-midi.md](ble-midi.md) describes BLE MIDI transport integration.
- [usb-midi.md](usb-midi.md) describes the Poseidon/CAMD USB path.

Some older wording in those documents reflects the earlier stage where network, BLE, synth, and Preferences functionality were all described as MIDIHub-owned implementations. As capabilities move upstream, their configuration ownership should follow the boundaries defined here.

## Design principle

```text
CAMD              = MIDI infrastructure and runtime graph
Native subsystems = transport, hardware, network, and audio implementation
MIDIHub           = current incubator/integration runtime; long-term role open
MIDIHub.prefs     = current integrated management surface; long-term owner open
```

> **MIDIHub should manage relationships between MIDI endpoints without taking ownership away from the AROS subsystems that implement those endpoints.**

This gives MIDIHub a coherent long-term role while preserving CAMD as the standard MIDI interface for AROS applications.
