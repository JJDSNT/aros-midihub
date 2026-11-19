# MIDI 2.0 Flex Data — implementation backlog

Status: **proposed, not implemented** (2026-11-19). This note complements [UMP backlog](ump-backlog.md) and does not claim that a codec or tests already exist.

## Current source evidence

- `include/ump/ump.h` defines `UMP_FLEX = 0xd` and `ump_words()`; generic UMP framing exists.
- `src/ump.c` deliberately drops Flex Data in **UMP → MIDI 1.0 conversion** (`not_translated`), which is appropriate where no MIDI 1.0 equivalent exists. Do not change that behavior merely to add Flex Data support.
- `docs/camd-endpoint-abi-draft.md` says Flex Data passes through CAMD's native UMP endpoint path without initiating a special state machine. This is transport, **not** semantic interpretation; the CAMD 43 boundary remains a draft in [AROS PR #2101](https://github.com/aros-development-team/AROS/pull/2101).
- The UMP backlog UP01 explicitly records that Flex Data was not reviewed/implemented semantically. UP12 notes duplicated generic UMP rules between CAMD and the MIDIHub codec.

## Architectural ownership

1. **CAMD 43:** transport and schedule complete native UMP messages, including MT 0xD; do not embed tempo, lyrics or stage application policy in CAMD.
2. **Reusable MIDIHub UMP codec:** validate, decode, assemble where required and encode Flex Data using portable C99, without AROS OS calls. Keep a clear distinction between generic Flex Data wire structures and higher-level musical meanings.
3. **TeleMIDI Band:** opt into decoded events (for example tempo, time signature and text/lyrics where defined by the specification) and choose presentation/session behavior. No dependency on video or stage UI in the generic codec.

## Proposed work (not started)

| ID | Work | Acceptance evidence |
|---|---|---|
| **UF01** | Review the exact applicable MIDI 2.0 UMP Flex Data specification revision and enumerate defined statuses, formats, addressing, payload lengths and any segmentation/reassembly rules. Avoid guessing values from examples. | Normative reference matrix and fixture vectors recorded alongside tests. |
| **UF02** | Design a minimal portable public API, tentatively `include/ump/flex.h`, implemented in `src/ump_flex.c`. Provide validation and decoding of individual MT 0xD packets, with bounded assembly only where required by the standard. | Explicit errors for malformed, unsupported and incomplete input; no allocation or unbounded buffers in hot paths. |
| **UF03** | Implement encoding for supported Flex Data families and byte/word order handling, without inventing MIDI 1.0 translations. | Encode/decode round-trip and normative vectors, including group/channel addressing and boundary cases. |
| **UF04** | Add `tests/ump_flex.c`: malformed lengths/statuses, text and multipart edge cases, state reset/interleaving, fuzz/sanitizers where available. | Portable host suite passes; no regression in existing UMP and Network MIDI 2.0 tests. |
| **UF05** | Audit consumption through CAMD 43 and Network MIDI 2.0 without assuming native-driver hardware validation. Preserve packet timestamps and metadata. | End-to-end test with an actual native UMP endpoint; document what was and was not exercised. |
| **UF06** | Resolve or explicitly document UP12's shared UMP constants/rules before duplicating them in the new codec. | One authoritative definition or a justified compatibility boundary; no competing packet-size/status tables. |

## Integration principles

- A native UMP consumer can receive Flex Data even when the MIDI 1.0 translator drops it. Those are distinct paths.
- Keep MIDIHub's codec independent of the TeleMIDI Band application. Presentation, stage layout and session synchronization remain application concerns.
- Do not claim Flex Data synchronizes remote hosts: transport timestamps and network clock alignment are separate.
- Prefer source-backed tests against the actual specification before making a public API stable.
- Agent: use this file as a discoverable task specification; cross-check the current upstream PR and UMP code before implementation, and update the main UMP backlog when items are actually completed.
