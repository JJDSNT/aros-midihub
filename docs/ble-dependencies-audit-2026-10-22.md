# Bluetooth dependency audit — 2026-10-22

This is a dependency/source audit, not a new radio test, live PR review or
claim that the changes below are implemented. It corrects the earlier
recommendation to make L10 wait for CAMD metadata.

**Maintainer correction, 2026-10-24:** the review of #1789 rejected
`BCBA_MidiPort`. A MIDI application would have to use Bluetooth's internal
service/binding traversal to consume it, and profile-specific tags are not the
generic `BCBA_` pattern. The device-to-port gap remains real, but its next
proposal needs a concrete consumer and belongs with CAMD port naming or
identity. The source facts below remain evidence; the former interface
conclusion is superseded.

**Source-research correction:** name and connection state are already
exposed by `BCBA_Device` plus `BDA_Name` / `BDA_IsConnected`, as in other
Bluetooth classes. The binding internally stores its MIDI port, but the
review established that exposing it through a MIDI-specific Bluetooth tag is
not an accepted public interface. The initial copied-snapshot design and the
later tag design were both withdrawn.
See [the verified L10 facts](ble-l10-plan.md). The table below separates
technical dependencies; it does not certify every row as implementation-ready.

## Ownership rule

The source confirms a device/binding reference, name, connection state and an
internally assigned port. Ownership alone does not select a public interface.
Do not add a Bluetooth tag without an accepted Bluetooth consumer contract.
For MIDI applications, start from the concrete CAMD consumer and the existing
CAMD port/name interfaces. Implement any class follow-up in canonical AROS
sources, not a second local class.

## Current work and interface boundaries

| Item | What can advance now | What is separate |
|---|---|---|
| L10 | Document the concrete Preferences/CAMD consumer and audit existing CAMD port naming/identity interfaces | `BCBA_MidiPort` was rejected; the minimum accepted interface is not established |
| L02 | Demonstrate where a CAMD consumer needs a device-readable port name and define fallback behavior | Generic CAMD display-name queries remain B31 research; Bluetooth's internal attributes are not an application API |
| L03 | Use the existing public binding and device connection data; investigate any additional service-readiness gap before proposing a new state API | Making CAMD's graph or legacy participation signals reflect them requires CAMD changes. An open port alone is not proof of a connected device. |
| L01 | Investigate the existing class configuration/identity lifecycle, then define persistent assignment, exhaustion and safe reuse if the gap remains | This does not give every CAMD application a stable device ID. B29 remains the generic identity integration. A Bluetooth address or display name must not be assumed to be an immutable identity. |
| L06 | The narrowed #1789 reports exhaustion in the Bluetooth error log; exercise the full pool when hardware permits | Increasing the pool needs its own compatibility/resource assessment. |
| TX failures | Investigate writer return semantics and where acceptance becomes uncertain before defining counters or retries | End-to-end delivery cannot be inferred from local acceptance; do not introduce blind retransmission after uncertain acceptance. No CAMD metadata dependency. |
| W05 | Audit port-close, shared-state and callback lifetime rules | SMP runtime evidence for the existing data paths requires a suitable environment. |
| L09 | Revalidate current packaging and the already-present-driver path before changing it | Diskless resident registration and replacing/unloading an active driver are different lifecycle contracts. |
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

The original dependency audit inspected the active codec proposal at
`3918469bab111a3d633cc6261c654a2ebf6ae918`. The L10 follow-up was then
checked directly against upstream `43e2e698c4075f8b9c19b05702ceca17b1ff5442`
so it does not depend on that codec branch:

- `btmidi.c:btcGetAttrsA()` exposes binding device/service/task but no
  public assigned-port query. The attempted #1789 attribute was removed after
  review.
- `btmidi_camd.c:btmidi_camd_reserve()` chooses a free port and stores its
  name; `btmidi_camd_release()` clears ownership/name. The driver-install
  path already checks whether the installed bytes match before writing.
- `btmidi_gui.c:show_ports()` displays the internally stored association.
- Both `send_to_device()` and `send_to_ble()` drain the ring while ignoring
  writer put/flush results. This confirms that TX error handling can be
  investigated locally; it does not prove a particular radio failure.

No normative protocol change is approved by this audit. L01, L03, L07/L14,
L09 and security or interval behavior remain investigations until their
specific source, protocol and compatibility questions are answered.

See [the corrected L10 plan](ble-l10-plan.md) and
[BLE backlog](ble-midi-backlog.md).
