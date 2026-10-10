# Instrument identity, SoundFont mapping and classic Amiga trackers — research backlog

**Status:** Research proposal; no implementation decision or compatibility claim.
**Priority:** Future investigation, relevant to MIDIHub Synth, CAMD, Radium and legacy Amiga music software.

## Motivation

An SF2 file contains multiple **presets**, selected by bank and program. A preset may reference multiple internal SF2 instruments and samples; an SF2 internal "instrument" is not interchangeable with a tracker instrument or a MIDI program. MIDIHub should make GM easy by default without requiring every SoundFont to be GM.

A classic Amiga tracker may define instruments as samples, multisamples, envelopes, tracker-specific synthesis or effects, and pattern commands. A tracker instrument number is generally **not** a MIDI Program Change number. Converting its music to SF2 can lose pitch behavior, sample playback semantics, effects, timing and timbre.

## Research questions

1. **SF2 inventory:** How can we enumerate actual preset names, banks and program numbers independently of the synth engine? Record metadata, duplicate names, empty banks, drum presets and incomplete banks. Check API support and 68k cost in TinySoundFont and optional external FluidSynth.
2. **GM-first, not GM-only:** Detect probable GM/GM partial using bank-0 coverage, program names, percussion conventions and optional metadata. SF2 has no universal authoritative GM compliance flag. Keep results probabilistic; never silently label GM2/GS/XG based only on extra banks.
3. **Bank semantics:** Specify MIDI 1.0 CC0/CC32 + Program Change translation to the SF2 bank index for each engine, including percussion and non-GM banks. Avoid treating raw MIDI bank numbers as automatically identical to SF2 bank IDs. Preserve original MIDI message semantics; examine MIDI 2.0 UMP bank/program representation separately.
4. **Instrument identity:** Define a logical selection including SoundFont identity, bank, program and optional mapping profile. Preserve both requested and resolved selections and make fallback explicit. Consider hashes, catalog cache and missing-file handling.
5. **Trackers and formats:** Investigate ProTracker/NoiseTracker MOD, FastTracker XM, Scream Tracker S3M, Impulse Tracker IT and relevant Amiga-specific formats/players. For each, distinguish native sample playback from optional MIDI-out support, and determine whether a tracker actually exposes CAMD. Do not assume all are supported by AROS or Radium.
6. **Radium 0.55:** Verify how its instrument, patch, channel, bank and program settings are stored and transmitted through legacy CAMD. Test real Program Change / Bank Select into MIDIHub Synth. Avoid assuming its instrument model equals that of modern Radium.
7. **Compatibility strategies:** Compare (a) native tracker playback with original samples/effects, (b) MIDI output to CAMD and MIDIHub Synth, (c) explicit user mapping of tracker instruments to SF2 presets, (d) offline import/conversion, and (e) optional virtual instrument interfaces. Document expected fidelity, limitations and effort.
8. **User experience:** GM default when plausible; show actual SF2 preset names; allow native and custom mapping modes; surface unresolved presets; support per-song/per-track overrides and percussion channels; provide auditable fallback behavior.
9. **System boundaries:** Decide which pieces belong in MIDIHub Synth, a reusable preset-catalog/mapping service, CAMD metadata, Radium integration, or a separate tracker/player application. Avoid expanding CAMD's protocol role into an audio sampler or plugin host.
10. **68k constraints:** Evaluate SF2 loading and index cost, memory, big-endian parsing, latency, CPU and backend sound fidelity. Existing TinySoundFont modulator and endian caveats remain relevant.

## Proposed investigation matrix

| Source | Instrument model | Native playback | MIDI/CAMD output | SF2 mapping | Validation |
| --- | --- | --- | --- | --- | --- |
| Radium 0.55 (AROS) | Inspect source | Inspect | Known CAMD integration; verify behavior | Bank/program test | Build and run pending |
| ProTracker/NoiseTracker MOD | Sample-based tracker instruments | Player dependent | Do not assume | Explicit conversion only, if desired | Research |
| XM / S3M / IT players | Format-specific sample/instrument models | Player dependent | Do not assume | Fidelity analysis needed | Research |
| Standard MIDI File / MIDI sequencer | MIDI channel + bank/program | Via synth | Native MIDI semantics | Natural fit, with profile translation | Research |

## Suggested future deliverables

- Source-grounded comparison of tracker instrument semantics and CAMD support, with concrete AROS applications.
- SF2 preset enumerator and GM-likelihood classifier design (no mandatory GM constraint).
- Engine-independent bank/program mapping contract and explicit fallback policy.
- Radium 0.55 interoperability test plan and small MIDI test fixtures.
- Decision record on whether any tracker-to-SF2 bridge is worthwhile; preserve native sample playback where appropriate.

## Relationship to existing documentation

- [SoundFont investigation](soundfonts.md)
- [Live CAMD synthesizer](synth-camd.md)
- [Radium development integration](radium-development.md)
- [System SoundFont configuration](system-soundfonts-themes-analogy.md)
- [CAMD integration](camd-integration.md)

**Not in scope now:** automatically translating arbitrary MOD/XM/S3M/IT instruments into SF2, implementing a plugin host, or changing CAMD ABI.
