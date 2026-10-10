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


## Existing projects and port status (checked 2026-10-10)

Distinguish **upstream existence**, **AROS source-tree presence**, **confirmed AROS build**, and **confirmed runtime operation**. These are different claims.

| Project | Relevant capabilities / precedent | AROS status verified here | MIDIHub relevance |
| --- | --- | --- | --- |
| [Radium 0.55](https://github.com/aros-development-team/contrib/tree/master/MultiMedia/radium) | Historical tracker/editor with CAMD integration | **Present in AROS contrib** at `MultiMedia/radium`; build/runtime still unverified | Primary legacy CAMD / bank-program interoperability test |
| [PT2 Clone](https://github.com/8bitbubsy/pt2-clone) | ProTracker 2-style MOD editor/player; C/SDL2, BSD-3-Clause | **No port confirmed**; not listed in AROS contrib `MultiMedia` | Reference for authentic MOD semantics; possible separate port evaluation |
| [Schism Tracker](https://github.com/schismtracker/schismtracker) | Impulse Tracker-style sample-based composition; GPL-2.0 | **No port confirmed**; not listed in that directory | Instrument/sample mappings and effect compatibility |
| [OpenMPT / libopenmpt](https://github.com/OpenMPT/openmpt) | Tracker instrument and sample handling; SoundFont import/reference behavior; reusable module playback library | **No port confirmed**; not listed in that directory | Strong reference for SF2-to-tracker instrument import and fidelity limitations; distinguish editor from playback library |
| [MilkyTracker](https://github.com/milkytracker/MilkyTracker) | FastTracker II-compatible editor; sample-to-note mapping | **AROS support reported historically, not independently confirmed in current AROS source/build**; not listed in that directory | Candidate second testbed after locating exact port/version |
| Classic Amiga ProTracker / NoiseTracker | Original sample-based MOD workflows | Historical Amiga software, **not a verified native AROS port here** | Compatibility target, not automatically a MIDI client |

**Evidence boundary:** The AROS contrib `MultiMedia` directory currently lists `AMP2`, `camira`, `cdxlplay`, `libs`, `mad`, `play`, `playcdda`, `radium`, `shellplayer`, `sox`, and `tinysid`. Absence from this directory does **not** prove absence from all AROS repositories, third-party distributions, Aminet, or historical ports. Follow up with repository-wide searches and build/runtime checks before upgrading status.

### What existing solutions actually bridge

- **Samples → tracker instruments:** Trackers such as MilkyTracker and OpenMPT can map samples across note ranges; this is **not** equivalent to MIDI Program Change.
- **SoundFont → tracker instruments:** OpenMPT provides useful prior art for SoundFont import. Investigate exact current code path and which SF2 features are flattened or lost; do not claim full SF2 synthesis fidelity.
- **Tracker instruments → MIDI/SF2 presets:** Requires an explicit mapping table and a player/editor that emits MIDI events. This is **not** an automatic capability of MOD/XM/S3M/IT or a function that CAMD can infer.
- **MIDI → SoundFont:** Already matches MIDIHub Synth's intended role. Bank Select / Program Change choose SF2 presets through the selected engine, subject to bank-convention translation and available presets.
- **Native tracker playback:** Preserve tracker sample/effect semantics rather than routing every module through MIDI.

### Scope and ownership proposal

| Responsibility | Proposed owner | Status |
| --- | --- | --- |
| MIDI 1.0 / MIDI 2.0 messages, endpoint routing, compatibility | CAMD + MIDIHub Router | Existing architecture; no new tracker-specific CAMD ABI |
| Enumerate SF2 presets; GM-likelihood detection; mapping profiles; explicit fallback | MIDIHub Synth / reusable instrument-catalog component | **Future design**, not implemented by this document |
| Render SF2 via backend to AHI | MIDIHub Synth | Existing experimental implementation; AROS runtime validation pending |
| Parse MOD/XM/S3M/IT and faithfully apply tracker effects | Tracker/player or dedicated playback library | **Outside MIDIHub core** |
| Export tracker notes to MIDI or bind a tracker instrument to SF2 preset | Optional tracker integration / bridge application | **Research only**, depends on source application |
| Host general-purpose VST/LV2-style plugins or implement a new tracker | Separate future project | **Out of scope** |

### Recommended investigation order and acceptance criteria

1. **Verify existing AROS software:** Search the full AROS tree, historical contrib/distributions and third-party port archives for MilkyTracker and other trackers; record exact paths, versions, build targets, licenses and runtime evidence. Do not equate AmigaOS binaries with native AROS ports.
2. **Radium first:** Build/run the pinned 0.55 port, inspect CAMD instrument/channel/program behavior, then verify a Program Change selects the expected MIDIHub Synth preset and that bank select works without breaking MIDI 1.0 clients.
3. **SoundFont inventory:** Prototype preset enumeration and display of real names; test full GM, partial GM and deliberately non-GM SF2 files. Report classification confidence, never enforce GM.
4. **Mapping contract:** Define source instrument ID → selected SF2 identity/bank/program with per-song overrides, explicit missing-preset behavior, drum handling and reversible project persistence.
5. **Tracker comparison:** Use at least one simple MOD and one instrument-rich XM/IT fixture; document which tracker effects cannot be represented by MIDI or SF2. Verify native playback independently.
6. **Decision gate:** Only propose a bridge/port if a concrete user workflow, acceptable fidelity and reasonable 68k resource use are demonstrated. Prefer an optional application over increasing CAMD or MIDIHub core complexity.

**Key decision to preserve:** The goal is *interoperability and optional instrument mapping*, **not** replacing the Amiga tracker sound engine or pretending that SF2 presets are identical to tracker instruments.


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
