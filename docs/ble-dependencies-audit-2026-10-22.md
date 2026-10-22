# Bluetooth dependency audit — 2026-10-22

This is a dependency/source audit, not a new radio test, live PR review or
claim that the changes below are implemented. It corrects the earlier
recommendation to make L10 wait for CAMD metadata.

## Ownership rule

Bluetooth must expose the state it owns independently of consumers:
device/binding, assigned port, name and distinct connection/readiness
states. A future CAMD adapter can publish this information through a
generic interface. That publication does not block the Bluetooth capability.
Implement follow-ups in the canonical AROS class, not a second local class.

## Work that can advance independently

| Item | What can advance now | What is separate |
|---|---|---|
| L10 | Public, lifetime-safe device/port query in the class; test association and removal independently of Preferences | Preferences adoption and future CAMD publication |
| L02 | Expose the device's existing name with its port association; review how name changes are reflected | Generic CAMD display-name queries require B31. Editable aliases and renaming legacy clusters are different features, not automatically covered by this query. |
| L03 | Expose binding occupancy, radio connection and service readiness as distinct states | Making CAMD's graph or legacy participation signals reflect them requires CAMD changes. An open port alone is not proof of a connected device. |
| L01 | Design and implement persistent class-side device/port assignment, exhaustion handling and safe reuse | This does not give every CAMD application a stable device ID. B29 remains the generic identity integration. A Bluetooth address or display name must not be assumed to be an immutable identity. |
| L06 | Report exhaustion of the bound-device port pool | No requirement to finish L01 first just to report failure; increasing the pool needs its own compatibility/resource assessment. |
| TX failures | Check writer/transport failures, account for them and define bounded handling for known pre-acceptance refusal | End-to-end delivery cannot be inferred from local acceptance; do not introduce blind retransmission after uncertain acceptance. No CAMD metadata dependency. |
| W05 | Audit port-close, shared-state and callback lifetime rules, including the proposed query | SMP runtime evidence requires a suitable environment; expertise is useful but not a prerequisite to begin analysis. |
| L09 | Package the matching driver in the image and validate the already-present-driver path on read-only DEVS | Diskless resident registration and replacing/unloading an active driver are different lifecycle contracts. |
| L16/L17 | Review security and connection-interval policy and available stack APIs | Real-phone/peripheral tests remain necessary for interoperability and timing claims; they do not prevent design/source work. |
| L15 | Continue host vectors/fuzz against the canonical class source | Upstream test placement is a repository/review decision; it does not block running or extending the tests. |

L01 and most class-local fixes were already listed as having no CAMD
dependency. The correction is to stop describing B29 as a prerequisite
for every useful identity improvement, or hardware tests as a prerequisite
for all preparatory work. L02/L03 require an explicit split between the
Bluetooth capability and its generic CAMD integration.

## Dependencies that remain real

- **L03 in CAMD:** a Bluetooth query alone does not change CAMD driver
  cluster participation, `MidiLinkConnected()` or application notifications.
  B30 or another agreed CAMD change is needed for that behavior.
- **W02:** losses inside CAMD cannot be counted reliably by Bluetooth
  without feedback from CAMD. This is a real driver-boundary dependency,
  but acknowledgements/overflow reporting need not wait for UMP or timed
  receive simply because they were grouped under B32.
- **L12:** preserving parsed BLE timestamps through the current byte-only
  driver receive interface needs a timed handoff. Class-side timestamp
  analysis can precede it; timestamp delivery through CAMD cannot.
- **L04:** accepting an open on an unbound port changes behavior explicitly
  requested in review. It requires agreement on behavior, not B29–B31.
- **W04:** a common identity model for USB/BLE across CAMD requires a common
  contract. It does not block completing BLE's own state/query interface.
- **T01/T02 and hardware-specific observations:** real radio, pairing,
  subscription, timing and disconnect evidence requires the corresponding
  devices. Host vectors and cross-builds cannot replace that evidence.

L05/W03 and the unloading portion of L09 contain historical descriptions
of older CAMD/USB code. The status register records subsequent merged scan
and lifecycle work. Revalidate those exact behaviors against a current
upstream checkout before declaring them still blocked or already solved;
neither is a reason to defer the class query.

## Source checks

The inspected class checkout is the active codec proposal at
`3918469bab111a3d633cc6261c654a2ebf6ae918`:

- `btmidi.c:btcGetAttrsA()` exposes binding device/service/task but no
  public assigned-port query.
- `btmidi_camd.c:btmidi_camd_reserve()` chooses a free port and stores its
  name; `btmidi_camd_release()` clears ownership/name. The driver-install
  path already checks whether the installed bytes match before writing.
- `btmidi_gui.c:show_ports()` displays the internally stored association.
- Both `send_to_device()` and `send_to_ble()` drain the ring while ignoring
  writer put/flush results. This confirms that TX error handling can be
  investigated locally; it does not prove a particular radio failure.

No normative protocol change is approved by this audit. L07/L14 and security
or interval behavior still need their specific protocol and compatibility
review before implementation.

See [the corrected L10 plan](ble-l10-plan.md) and
[BLE backlog](ble-midi-backlog.md).
