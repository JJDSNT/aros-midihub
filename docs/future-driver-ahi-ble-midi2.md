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

## 3. Bluetooth MIDI 2.0 / UMP

- Existing detailed research: [ble-midi2-research.md](ble-midi2-research.md).
- Preserve native BLE MIDI 1.0 support while researching UMP over BLE. Prefer extending btmidi.class with protocol-aware characteristics and negotiation, rather than creating two classes competing for the same service.
- BLE MIDI 2.0 transport details are provisional until the relevant specification is published; do not claim conformance based only on Windows MIDI Services preview documentation.
- Investigate capability detection, subscription arbitration, UMP message boundaries, endpoint discovery, reconnection, dual native MIDI 1.0/MIDI 2.0 CAMD integration and test hardware.
- This is separate from A2DP audio in #1727: the transferable lessons concern Bluetooth class lifecycle, integration patterns and testing, not the audio transport itself.

## Follow-up

Review these three independent topics when planning the next CAMD/driver/synth architecture milestone. Do not combine them into one upstream PR. Keep references and acceptance criteria synchronized with the existing detailed documents.
