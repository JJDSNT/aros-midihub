# CAMD 43 PR #2101 — driver endpoint gaps for TeleMIDI

Status: **follow-up / pending** (2026-11-19). Scope: [AROS PR #2101](https://github.com/aros-development-team/AROS/pull/2101), currently draft. This is a source-based backlog note, **not** evidence of merge or hardware validation.

## Evidence and remaining work

| Existing backlog ID | Observed in PR #2101 | Remaining action / acceptance |
|---|---|---|
| **B29 — physical identity** | `MidiUMPDriverPortInfoV1` exposes `ProductInstance`; endpoint identity persistence exists, but driver endpoint naming/identity still uses driver name and port index. | **Partial/open:** verify whether one physical USB/BLE device retains the same endpoint ID when reconnected on another port. If not, add optional per-port stable physical identity evidence and confidence, with collision/privacy policy. Validate with `btmidi` and CAMD 41/42 regressions. Do not equate `ProductInstance` with a proven stable device ID. |
| **B30 — port presence** | `QueryPort()` returns port `State`; host `SetPortState()` supports AVAILABLE/OFFLINE transitions. | **API implemented; integration pending:** test an unbound port, bind/unbind, endpoint watch events and reconnect with a real driver. |
| **B31 — friendly name per port** | `MidiUMPDriverPortInfoV1` has no per-port display-name field; `driver_endpoints.c` constructs `<driver>.<index>` names. | **Open:** introduce an optional presentation name independent of stable identity and legacy cluster name; expose through `GetEndpointInfo()`. Test name updates, truncation and stable ID across reconnects. |
| **B32 — native UMP and receive timing** | Optional `MDD_F_UMP_EXTENSION`, `SubmitUMP`/`SendUMP` and timed `CAMDUMPEventV1` exist. The PR reports in-process loopback tests. | **API implemented; integration pending:** verify source timestamp provenance, clock domain, real UMP delivery with physical/Network MIDI 2.0 drivers, backpressure and m68k callback ABI. Do not claim actual hardware-arrival timestamps are proven. |

## Priority and constraints

- **Functional gaps to resolve:** B29 and B31.
- **Real-driver validation:** B30 and B32.
- Preserve the optional, sized/versioned driver extension and CAMD 41/42 ABI. Do not duplicate implemented B30/B32 APIs.
- No CAMD implementation change or test execution is claimed here.

## Source locations

- [PR #2101](https://github.com/aros-development-team/AROS/pull/2101)
- `compiler/include/midi/camddevices.h` — `MidiUMPDriverPortInfoV1`, `QueryPort`, `SetPortState`, UMP extension
- `workbench/libs/camd/driver_endpoints.c` — driver-port naming, publication and initial state
- `workbench/libs/camd/camdendpoint.h` — endpoint metadata, identity kinds and timed events
- [Existing backlog](camd-backlog.md) — B29–B32 original definitions

**Agent instruction:** treat this as a current PR #2101 audit supplement to the original B29–B32 descriptions in `camd-backlog.md`; re-check the latest PR diff before implementation or declaring an item closed.
