# Future integration research: resident CAMD drivers, AHI synth output and BLE MIDI 2.0

Status: tracked research / future design; no implementation or upstream approval implied. Recorded 2026-10-21.

## 1. Resident CAMD drivers (upstream architecture)

- Discussion: https://github.com/aros-development-team/AROS/issues/1736
- Accepted AROS precedent: https://github.com/aros-development-team/AROS/pull/1727 (Bluetooth A2DP class, merged 2026-10-20), and Poseidon usbaudio.class.
- CAMD currently discovers driver files under DEVS:Midi using LoadSeg. Issue #1736 asks whether classes could register existing MidiDeviceData directly from memory (illustrative AddMidiDevice/RemMidiDevice), retaining Init, OpenPort/ClosePort, CAMD ports/clusters and legacy file loading.
- Motivation: avoid duplicate embedded/file drivers, writable SYS: and DOS requirements, stale files per USB device, and discovery timing problems. This is not a request to replace CAMD's driver model.
- Design gate: Kalamatee's response; define driver/class ownership, unload and unregister semantics, active clients, ABI/versioning, duplicate names, hot-plug and compatibility fallback before coding.
- Review lessons from #1727: use established OS conventions; avoid lock-order inversions, ensure safe reconnect and device removal, separate unrelated API changes, add host tests to existing suites. Note that #1727's review explicitly rejected out-of-scope changes to AHI's public API.
- Decision: track for a future CAMD evolution; do not block current upstream PRs.

## 2. AHI output from the SoundFont synthesizer

- Existing implementation: [synth-camd.md](synth-camd.md). MIDIHubSynth is an **AHI client**, generating PCM from MIDI events and writing it through ahi.device; it is not an AHI driver.
- **Reference relevance correction:** AROS [#1727](https://github.com/aros-development-team/AROS/pull/1727) implements **btaudio.audio, an AHI driver**, not an AHI client. Its mode registration, hardware callbacks and A2DP transport are not a useful primary reference for MIDIHubSynth's output path. Do not plan a synth AHI redesign around this PR.
- Only incidental lessons (timing, buffering, resource lifetime) may be consulted where directly applicable. For future synth improvements, seek examples of AHI **client** applications and compare their usage of ahi.device to MIDIHubSynth.
- No implementation change or new AHI work is implied by this reference.

### Cross-Amiga AHI client and MIDI synth references (research backlog)

These are **candidate references, not yet source-audited**. Compare the actual client-side audio implementation to MIDIHubSynth before adopting any pattern. The btaudio.audio driver (#1727) remains a CAMD resident-driver architectural precedent, **not** the primary AHI client example.

| Project | Platform / role | Research target |
| --- | --- | --- |
| [WildMIDI](https://github.com/Mindwerks/wildmidi) | AmigaOS / MorphOS / AROS ports; open-source MIDI synth | Locate and audit AHI backend, output buffers, timing, shutdown and platform-specific code; verify backend availability in the exact revision |
| FrozenLiquid | MorphOS; CAMD + FluidLite SoundFont synthesizer | Study CAMD-to-synth-to-Reggae/audio.output architecture, event scheduling and reported ~20 ms rendering granularity; locate primary source/docs before drawing conclusions |
| [MorphOS Reggae audio.output](https://morphos-team.net/sdk/Reggae/audio.output.html) | MorphOS media output class using AHI client functionality | Async PCM output, buffer ownership, scheduling, latency and error handling; compare the abstraction boundary, **not** a proposal to port Reggae |
| TiMidity / AmigaOS 4 port (Alfred Faust) | AmigaOS 4 MIDI synthesis | Locate port sources and verify AHI playback implementation, buffering and output selection |
| SoftSyn | AmigaOS 4 MIDI software synthesizer | Investigate CAMD routing, synth lifecycle and audio-output implementation; verify actual APIs and source availability |
| [ScummVM](https://github.com/scummvm/scummvm) AmigaOS 4 port | Game audio/MIDI engines | Examine MIDI synth backends and their relation to host audio output; avoid conflating CAMD MIDI I/O with AHI PCM playback |
| MorphOS CAMD implementation | MorphOS MIDI subsystem | Investigate its hardware-driver model and USB MIDI integration as a **separate CAMD architecture topic**, not an AHI client example |
| AmigaOS AHI client programming examples | AmigaOS family | Locate official examples for ahi.device, async SendIO, double buffering and cleanup; compare against MIDIHubSynth |

Questions to answer with code/evidence: Does the reference actually use AHI directly or via an intermediate layer? How are PCM buffers, mix rate, MIDI event timestamps, latency, output device choice, reconnection and resource teardown handled? Which design applies to AROS without changing AHI public APIs?

## 3. Bluetooth MIDI 2.0 / UMP

- Existing detailed research: [ble-midi2-research.md](ble-midi2-research.md).
- Preserve native BLE MIDI 1.0 support while researching UMP over BLE. Prefer extending btmidi.class with protocol-aware characteristics and negotiation, rather than creating two classes competing for the same service.
- BLE MIDI 2.0 transport details are provisional until the relevant specification is published; do not claim conformance based only on Windows MIDI Services preview documentation.
- Investigate capability detection, subscription arbitration, UMP message boundaries, endpoint discovery, reconnection, dual native MIDI 1.0/MIDI 2.0 CAMD integration and test hardware.
- This is separate from A2DP audio in #1727: the transferable lessons concern Bluetooth class lifecycle, integration patterns and testing, not the audio transport itself.

## Follow-up

Review these three independent topics when planning the next CAMD/driver/synth architecture milestone. Do not combine them into one upstream PR. Keep references and acceptance criteria synchronized with the existing detailed documents.
