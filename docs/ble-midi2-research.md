# BLE MIDI 2.0 / UMP research for AROS

Status: research and architecture recommendation, 2026-10-20. Not an implementation commitment or an upstream-ready specification.

## Executive decision

Retain the existing `btmidi.class` as the Bluetooth owner and extend it, when justified, to recognize the UMP GATT characteristic alongside the existing BLE MIDI 1.0 characteristic. Keep a native MIDI 1.0 path and avoid mandatory MIDI 1.0 -> UMP -> MIDI 1.0 round trips. Do not create two independent Bluetooth classes that race for the same MIDI GATT service. Do not claim full BLE MIDI 2.0 conformance while the transport specification is unpublished.

## Standards and evidence

- Published BLE MIDI 1.0: MMA/AMEI RP-052.
- Published UMP/MIDI 2.0: M2-104-UM v1.1.2, https://midi.org/universal-midi-packet-ump-and-midi-2-0-protocol-specification .
- BLE MIDI 2.0 transport: MIDI Association draft TSB item #274, **not yet published** as of this research. Public technical evidence is Windows MIDI Services preview architecture, https://microsoft.github.io/MIDI/kb/ble-midi-transport-architecture/ . Treat its implementation description as provisional evidence, not the normative draft text.
- Windows Bluetooth API description: https://microsoft.github.io/MIDI/sdk-reference/Transports/Bluetooth/ .
- Windows setup cautions that BLE MIDI 2.0 is experimental: https://microsoft.github.io/MIDI/tools/midibluetoothsetup/ .

## On-wire design reported by the reference implementation

Both formats use GATT MIDI Service UUID `03B80E5A-EDE8-4B33-A751-6CE34EC4C700`.
- MIDI 1.0 characteristic: `7772E5DB-3868-4112-A1A9-F2669D106BF3`.
- UMP characteristic: `C3B10ECF-88F5-4F7D-BFFA-8AD2C91FBAFE`.

The reference says a Central discovering both must prefer the UMP characteristic, and a Peripheral must not permit two active MIDI-characteristic subscriptions. UMP notifications carry whole 32/64/96/128-bit messages in big-endian words, potentially concatenated; an individual UMP must not be fragmented across notifications. Endpoint discovery and protocol negotiation are required for the UMP endpoint, but not for legacy BLE MIDI 1.0. Connection loss resets UMP stream and translation state; re-subscription and discovery/negotiation are required on reconnection. Jitter Reduction Timestamps are optional in the reference. These are draft-derived details and must be rechecked against the final published specification.

## AROS architecture assessment

Existing repository structure:
- `ports/aros/btmidi/`: older companion class implementation.
- `docs/ble-midi.md`, `docs/ble-midi-backlog.md`: BLE MIDI 1.0 design and open defects.
- `docs/camd-integration.md`: the Bluetooth class owns Bluetooth GATT and exposes a `DEVS:Midi` CAMD driver.
- `docs/midi-protocol-coexistence-research.md`: explicitly recommends dual-native MIDI 1.0 and UMP paths and conversion only at mismatched boundaries.

No confirmed structural need to replace `bluetooth.library` or the class model. However, this is an architectural inference, **not proof that the current AROS GATT API supports every necessary operation**. Audit Central discovery of multiple characteristics, CCCD subscription management, Peripheral characteristic publication, MTU/notification sizing, ATT error responses, connection parameter negotiation, disconnection callbacks and multi-device lifetime.

Proposed conceptual flow:

Bluetooth LE MIDI service -> btmidi.class selects GATT characteristic
  - MIDI 1.0 characteristic -> existing BLE MIDI codec -> native CAMD 1.0 driver path
  - UMP characteristic -> UMP validation/packet queue -> future native CAMD UMP endpoint path
  - explicit translation adapter only where the endpoint/application protocols differ

The current CAMD application API does not by itself provide native UMP event delivery; do not mistake UMP-over-BLE transport support for end-to-end MIDI 2.0. Keep protocol capabilities and loss of fidelity visible to clients.

## Alternatives evaluated

1. **Extend btmidi.class (recommended)**: single service owner, shared discovery, pairing, security, connection lifecycle and CAMD integration; least duplication. Requires careful isolation of the two codecs and API audit.
2. Separate btmidi2.class: simpler source separation but two independent owners can race for the same service and characteristic subscription; reject unless Bluetooth core explicitly arbitrates ownership.
3. Translate all BLE MIDI 1.0 to UMP internally: technically possible but violates the project's dual-native direction and adds unnecessary conversion to legacy paths; reject as a mandatory design.

## Research and implementation gates

1. Obtain published TSB #274 or authorized draft and confirm normative UUIDs, mandatory/optional behavior, security and error rules. Do not present Windows preview as the standard.
2. Audit current upstream AROS Bluetooth class and GATT APIs against characteristic discovery, subscription exclusivity, writes without response, notification handling, MTU, Peripheral behavior and teardown.
3. Define native UMP CAMD endpoint ABI and capability advertisement separately from BLE transport work; ensure legacy MIDI 1.0 CAMD clients continue unchanged.
4. Build portable UMP payload codec tests: 32/64/96/128-bit boundaries, concatenation, invalid lengths, endianness, no fragmentation, MTU constraints, disconnect/reconnect and stream reset.
5. Test Central and Peripheral independently, including legacy-only, UMP-only and dual-characteristic peers; confirm preference, rejection of concurrent subscriptions, pairing/privacy, multiple devices, timeouts and teardown.
6. Interoperate with independent implementations (Windows preview if available); do not count a self-to-self test as independent validation.
7. Only after spec/API stabilization, consider a focused upstream PR. Keep experimental implementation behind explicit opt-in until then.

## Scope and ownership

Bluetooth transport and GATT lifecycle belong to AROS `btmidi.class` / Bluetooth subsystem, not MIDIHub Preferences. MIDIHub may expose capabilities, status and routing. This document records research, not a promise to ship MIDI 2.0 BLE before the CAMD native UMP API exists.
