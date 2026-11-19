# MIDIHub Preferences

## Purpose

`MIDIHub.prefs` is the user-facing control surface for the MIDI environment exposed by AROS through CAMD. Its goal is deliberately modest: provide a clear overview of available MIDI endpoints, basic persistent routing between them, configuration surfaces for network MIDI and software synthesis, and reusable profiles.

It is not intended to become a DAW, a MIDI processor, or a sophisticated node-graph environment.

## Current implementation

The native preferences program is installed inside the MIDIHub package,
as `MIDIHUB:Prefs/MIDIHub` (`SYS:Extras/aros-midihub/Prefs`), rather
than in `SYS:Prefs`: whether a MIDI Preferences belongs there is for AROS to
decide. It follows the same application structure as
AROS Bluetooth Preferences and Poseidon Trident. The executable is a small
shell around a custom `Group.mui` class that owns navigation, pages, state,
actions, and live CAMD notifications.

The current pages are:

- **Overview**, with live CAMD endpoint, transport, direction, and state
  columns plus shortcuts to Trident, Bluetooth Preferences, the BLE MIDI
  class settings, and AHI
- **Routing**, with route creation/removal, reconnect policy, and per-route
  active/waiting state from `MIDIHubRouter`
- **Network MIDI**, editing the AppleMIDI/RTP-MIDI session and peer settings
- **Synthesizer**, selecting TinySoundFont or an externally supplied
  FluidSynth backend, a SoundFont path, and sending a preview note through
  CAMD
- **Profiles**, saving, activating, and deleting named snapshots of all
  MIDIHub-owned settings
- **Diagnostics**, one page meant to be photographed when something does not
  work: the CAMD endpoints; a MIDI monitor that decodes what arrives on a
  selected endpoint (notes, controllers, SysEx); a Send test button that
  plays a C major scale and a SysEx Identity Request to a selected
  destination; the BLE MIDI state (class, advertising, port states,
  connected devices); a switch for the 15 ms connection-interval request;
  and the latest Bluetooth log lines, updated as they arrive

CAMD cluster additions and removals refresh the overview through a MUI input
handler.

This describes the copy of `btmidi.class` in this repository. The upstream
class is a CAMD driver whose ports are `btmidi.in.<n>` and `btmidi.out.<n>`
and has no configuration to read; the overview has to be changed for it
(see Open work in [BLE MIDI](ble-midi.md)).

BLE MIDI endpoints are recognised through `bluetooth.library` rather than by
their names, because `btmidi.class` names a bound device's ports after the
device. The overview asks the stack which registered devices are bound to
`btmidi.class` and whether they are connected, and reads the peripheral
role's port names from the class configuration. Their Status column then
shows `Connected` or `Not connected` for a device AROS connects to, and
`Offered` or `Not offered` for the service a phone connects to. Bluetooth
connect, disconnect, binding, and class events refresh the overview too.
The `BLE MIDI` button opens the class's own settings window; MIDIHub.prefs
keeps no copy of those settings, and pairing stays in Bluetooth Preferences.
Without `bluetooth.library` the button is disabled and the rest works as
before. `Use` writes `ENV:MidiHub`; `Save` writes both `ENV:MidiHub` and
`ENVARC:MidiHub`. Route changes are reloaded immediately. The network and
synth services currently read their new settings when restarted.

Profiles are stored under `ENVARC:MidiHub/Profiles/<name>/` using the same
`Routes`, `Network`, `SoundFont`, and `Backend` files as the active
configuration. Activating a profile copies those choices into `ENV:` and
`ENVARC:`, reloads the router, and records its name in
`ENVARC:MidiHub/Profile`. Profile names are limited to letters, numbers,
spaces, underscores, and hyphens so they remain safe directory names on all
AROS filesystems.

The Preferences drawer icon is a two-state 64×64 PNG icon derived from
`images/midihub-icon-states.png`. Its AROS `icOn` metadata and reproducible
generation procedure are documented in `images/README.md`.

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
| AppleMIDI session settings | `applemidi.device`, configured through MIDIHub.prefs |
| Network MIDI 2.0 / UMP settings | `networkmidi2.device`, configured through MIDIHub.prefs |
| SoundFont and MIDI synth settings | Synth service, configured through MIDIHub.prefs |
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

To add a route on the Routing page, pick the endpoint MIDI comes in from in
the **From** list (for example `BLE MIDI In`) and the one it goes to in the
**To** list (for example `MIDIHub Synth`), then **Add Route**. The lists show
the endpoints CAMD has now; for one that is not connected yet, type its name
in the From or To field. `MIDIHUB:Prefs/MIDIHub PAGE=Routing` opens the
window on that page (any page name, or its beginning).

Adding a route only requires a source and destination. An optional **Reconnect automatically** setting may control whether MIDIHub restores the route when an endpoint reappears.

The initial resident `MIDIHubRouter` reads `ENV:MidiHub/Routes`, falling back
to `ENVARC:MidiHub/Routes`. Each UTF-8 line is tab separated:

```text
route<TAB>enabled<TAB>reconnect<TAB>source cluster<TAB>destination cluster
```

For example:

```text
route	1	1	Arturia MiniLab	MIDIHub Synth
route	1	0	iPhone BLE	MIDIHub Out
```

Blank lines and lines beginning with `#` or `;` are ignored. Endpoint names
must match CAMD cluster names and cannot contain tabs or newlines. Direct and
multi-route cycles are rejected to prevent feedback loops. Preferences should
write this same contract; `Use` targets `ENV:` and `Save` targets both `ENV:`
and `ENVARC:`.

The router is a service of its own, so routes keep working with
MIDIHub.prefs closed. The **Router service** box on the Routing page shows
whether it runs, with **Start** and **Stop**, and **Start at boot**
(`ENVARC:MidiHub/RouterAtBoot`, on unless `0`). At boot the package's
`Package-Startup` starts it when a route file exists; **Use** and **Save**
start it too when it is not running and Start at boot is on. By hand:
`MIDIHUB:C/MIDIHubRouter`.

The service owns the CAMD links until it receives Ctrl-C or `STOP`,
forwards channel/system messages and SysEx as they arrive, and reports each
route as `active` or `waiting`. With camd.library 42 it is told when an
endpoint joins or leaves a route's cluster; before 42 it checks the route
states every half second. A reconnecting route keeps its
links attached to the named clusters, so a transport that re-creates its CAMD
endpoint automatically rejoins the route. A route with reconnect disabled is
removed after its first active-to-disconnected transition.

The command line is also the initial Preferences control contract:

```text
MIDIHUB:C/MIDIHubRouter STATUS
MIDIHUB:C/MIDIHubRouter RELOAD
MIDIHUB:C/MIDIHubRouter STOP
```

`STATUS` reports configured, active, and waiting route counts. `RELOAD`
reparses the current `ENV:`/`ENVARC:` file and replaces the live links. These
commands use the public `MIDIHub.Router` Exec message port; a native Preferences
client can use the same message structure without launching a shell command.

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

During incubation, the AppleMIDI/RTP-MIDI implementation reads
`ENV:MidiHub/Network` and falls back to `ENVARC:MidiHub/Network`. Once it moves
to `applemidi.device`, that device owns the AppleMIDI settings contract while
`MIDIHub.prefs` remains its user-facing configuration client. The UTF-8 file
uses one `name=value` option per line. Empty lines and lines beginning with
`#` or `;` are ignored.

```text
local_port=5004
peer_ip=192.168.1.20
peer_port=5004
session_name=AROS MIDIHub
```

`local_port` and `peer_port` are control ports from 1 through 65534; the next port carries data. Omitting both `peer_ip` and `peer_port` allows incoming invitations without initiating a connection. Without a file, the current program listens on port 5004. The session name can be at most 63 bytes.

The Preferences page should expose the relevant MIDI concepts: service enabled state, session name, local control port, discovery, known/discovered peers, connection state, and Connect/Disconnect actions where supported.

Network MIDI 2.0 / UMP configuration can live on this page as the configuration surface for `networkmidi2.device`, without implying that it is the same protocol as AppleMIDI.

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

Persistent MIDIHub routing must not depend on the GUI being open. Network MIDI lifecycle belongs to its native CAMD device runtime.

## Design summary

```text
Overview / Devices  -> What MIDI endpoints exist?
Routing             -> What is connected to what?
Network MIDI        -> How are MIDI network sessions configured?
Synthesizer         -> How is the software MIDI instrument configured?
Profiles            -> Which saved MIDI environment should be active?
```

The guiding principle is **overview and basic control**. Advanced MIDI transformation and processing belong in specialized CAMD applications, not in MIDIHub.prefs.


## Preferences parity audit and recommended next steps (2026-11-18)

**Scope:** source-backed research/backlog only. This section does not assert that the MUI executable was run, does not change the existing six-page navigation, and does not authorize protocol or runtime changes. Inspected: `ports/aros/MIDIHubPrefsClass.c`, `ports/aros/MIDIHubPrefsClass.h`, `include/netmidi/config.h`, `include/netmidi/config_applemidi.h`, `include/netmidi/config_midi2.h`, plus the AppleMIDI/Network MIDI 2.0 backlogs.

### Confirmed UI baseline

The custom MUI class declares **Overview, Routing, Network MIDI, Synthesizer, Profiles, Diagnostics**. Overview displays CAMD clusters with transport, direction and status. Routing shows active/waiting/disabled and service states. Synthesizer, Profiles and Diagnostics already have dedicated controls. **Do not rebuild these pages or introduce a generic MIDI 2.0 tab** merely because new protocol capabilities exist.

The Network MIDI page currently exposes only `network_session`, `network_local_port`, `network_peer_ip` and `network_peer_port` as editable widgets. Its load/save path holds a full `struct netmidi_network_config` and uses `netmidi_config_load/save`, so **unexposed fields can be preserved through a save**; lack of a widget does not mean lack of persistence. Confirm this preservation with round-trip tests.

### Network configuration parity matrix

| Contract field / setting | Supported in configuration source | MUI editable today | Proposed UI treatment |
| --- | --- | --- | --- |
| `session_name` | Shared | Yes | Keep; validate 63-byte maximum and display meaning |
| `applemidi.local_port` (`local_port`) | AppleMIDI | Yes | Keep; port range and data-port implications |
| `applemidi.peer_ip` / `peer_port` | AppleMIDI | Yes | Keep; clarify outgoing invitation versus listening |
| `latency_ms` (0–2000) | Shared | **No** | P0 numeric control with range/help and change-impact warning |
| `applemidi.peer_name` | AppleMIDI when `NETMIDI_BROWSE` | **No** | P0 optional discovery-name control; show only when compiled/supported |
| `applemidi.max_peers` (0 or 1–8) | AppleMIDI | **No** | P0 numeric limit with clear meaning of 0 |
| `applemidi.accept` | AppleMIDI | **No** | P0 incoming connection policy; explicit validation of accepted syntax; **not** a replacement for authentication |
| `applemidi.port_name[16]` | AppleMIDI | **No** | P1 advanced peer-to-driver-port reservation editor, only after safe validation |
| `midi2.port` (`midi2_port`) | Network MIDI 2.0 | **No** | P0 enable/listen-port control; zero/absent semantics and dependencies verified before UI |
| `midi2.peer_ip` / `peer_port` | Network MIDI 2.0 | **No** | P0 separate outgoing Host settings, not mixed with AppleMIDI |
| `midi2.peer_name` | Network MIDI 2.0 when `NETMIDI_BROWSE` | **No** | P0 conditional discovery-name control |
| `midi2.port_name[16]` | Network MIDI 2.0 | **No** | P1 advanced port reservation editor |

**Important distinctions:** AppleMIDI and Network MIDI 2.0 are different protocols; use separate grouped controls under the existing Network MIDI page. Conditional `NETMIDI_BROWSE` members must not be referenced unconditionally. Do not infer a general network enable toggle, peer browser, Connect/Disconnect API or live session list from configuration structs alone; those require a verified runtime/status/control contract.

### P0 — complete Network MIDI parity without changing the protocol

1. Add controls for existing validated configuration fields, grouped as **Shared**, **AppleMIDI**, **Network MIDI 2.0**, with an **Advanced port reservations** section deferred to P1.
2. Check constraints against `netmidi_config_*_check` and existing parsing rules. Show actionable errors; preserve existing values on invalid input.
3. Preserve hidden/conditional fields and unknown future settings as far as the current parser/serializer contract permits; explicitly test any round-trip limitations rather than promising forward compatibility.
4. Preserve Amiga Preferences semantics: **Use** to `ENV:`, **Save** to `ENV:` and `ENVARC:`, **Cancel** without unintended changes; verify profiles restore the complete network configuration.
5. Distinguish **configuration saved** from **configuration applied to a running service**. The AppleMIDI backlog AM16 records disruptive reconfiguration behavior; no promise of seamless updates without a service-level change and test.
6. Keep device pairing, IP interface setup, DNS and AHI configuration in their native AROS owners.

**P0 acceptance:** every exposed field survives Use/Save/reopen/profile activation, invalid input cannot corrupt active configuration, AppleMIDI and MIDI 2.0 settings do not overwrite each other, and conditional builds compile. Exercise network reconfiguration with active sessions and document which changes interrupt them. Test both hosted and target AROS, including m68k when available.

### P1 — improve Overview device identity

The source's `endpoint_transport()` classifies several transports by cluster-name patterns; BLE additionally has `bluetooth.library` integration. A CAMD cluster name alone does not reliably identify the physical device, driver port or remote peer.

- Distinguish **physical device**, **CAMD endpoint/cluster**, **transport**, **direction** and **connection state** where the owning subsystem exposes reliable identity.
- Prefer authoritative driver/Bluetooth/USB/network status over heuristic name matching; retain a neutral `CAMD`/unknown fallback rather than inventing capabilities.
- Explicitly differentiate unavailable device, disconnected peer, advertised service, waiting route and stopped service.
- Verify hotplug/rebind/rename/reconnect refresh without polling loops that burden m68k; avoid implying `StartClusterNotify()` reports every internal link-state change.
- Do not duplicate Poseidon/Trident, Bluetooth Preferences or AHI controls.

**P1 acceptance:** two identical devices and a reconnected BLE/network endpoint remain distinguishable when native identity data permits; unavailable identity is shown as unknown rather than guessed; routing remains functional with the Preferences window closed.

### P1/P2 — preserve the rest and evolve by evidence

- **Routing:** retain basic persistent source→destination policy and existing active/waiting/reconnect UI. Rich processing chains belong to a future specialized application; see `router-processing-future.md`.
- **Diagnostics:** extend to UMP and MIDI-CI only as CAMD 43 APIs expose validated data; distinguish decoded MIDI 1.0, UMP, MIDI-CI discovery and unsupported features. Do not implement MIDI-CI in Preferences.
- **Synthesizer:** improve error reporting and validate SoundFont/backend changes before replacing a working instance; audio device selection remains AHI-owned.
- **Profiles:** verify round-trip of full MIDIHub-owned configuration, explicit active-profile indication and rollback on failed activation.

### Verification matrix (not yet executed)

| ID | Test | Required evidence |
| --- | --- | --- |
| MP01 | Open each of six MUI pages on target AROS | Screenshots or captured UI behavior; no missing widgets or layout clipping |
| MP02 | Edit all supported Network MIDI fields, Use, reopen, Save, reboot | Exact `ENV:NetMIDI`/`ENVARC:NetMIDI` values and retained conditional fields |
| MP03 | Reject invalid ports, peer limits, latency, policy syntax and conflicting settings | No partial/invalid configuration; actionable UI error |
| MP04 | Change network settings with active AppleMIDI and Network MIDI 2.0 sessions | Observed session continuity or explicit disconnect/restart; no misleading success status |
| MP05 | Save/activate a profile containing both protocols and synth/routes | Complete restoration and clear current profile |
| MP06 | Hotplug USB/BLE and disconnect/reconnect a network peer | Accurate endpoint identity/state and route recovery |
| MP07 | Build/run on AROS m68k with and without optional Bluetooth/browse features | Compile success, responsiveness, memory/CPU observation and safe disabled controls |
| MP08 | Close Preferences while routing is active | Router and MIDI event forwarding continue |

### Decision

**First implementation PR:** P0 Network MIDI UI parity only, based on existing configuration contracts; no protocol modifications, no new top-level page, no speculative session-control API. **Second investigation:** Overview identity/status contracts, especially authoritative device↔CAMD endpoint mapping. Perform MUI-on-AROS execution checks before calling either area complete.
