# Telemidi and WAN MIDI performance — research and architectural boundaries

**Recorded:** 2026-10-13  
**Status:** Future research; no implementation commitment.  
**Scope:** Musical collaboration over WAN using existing MIDI transports, timing and application-level latency acceptance.

## Findings

Matt Bray's Telemidi connects two near-identical Ableton Live environments using bidirectional RTP-MIDI and local sound generation. The 2017 project documents performances at 7.5 km and 2,730 km, and ten *Latency Accepting Solutions* (LAS): one-bar launch quantization, direct P2P connectivity, master/follower tempo, pulse-oriented material, responsive improvisation, note-record quantization, incoming MIDI quantization, tempo reduction, fallback kick loops and per-voice stops. These are **musical strategies**, not a new wire protocol or evidence of guaranteed 10–30 ms WAN latency.

Bray's 2026 paper *Pulse Before Sound* further frames musical synchrony around coordinated pulse, gesture and intentional actions across locally rendered nodes rather than streaming audio. It should be examined before any product decision; availability of a reusable implementation or source license is **not verified**.

RFC 6295 already provides RTP-MIDI command timestamps, delta times, recovery journals and receiver playout-buffer considerations. Apple's Network MIDI driver protocol separately provides invitation/session handling and clock synchronization. Neither guarantees synchronized musical interaction over arbitrary WAN paths. Network MIDI 2.0/UMP is a distinct transport and representation; do not assume its packet-loss recovery or timestamp semantics are interchangeable with RFC 6295.

## Existing MIDIHub capabilities and gaps

- [RTP-MIDI backlog](rtp-midi-backlog.md): the repository already documents a timed receive queue, configurable receive latency and journal recovery. **Do not duplicate these in a generic router filter.**
- [Network MIDI 2.0 backlog](network-midi2-backlog.md): distinct UMP network session and loss-handling mechanisms; audit its actual event-time representation before suggesting scheduling.
- [MIDI 1.0 / UMP coexistence](midi-protocol-coexistence-research.md): preserve native paths, converting only at incompatible boundaries.
- [Future router processing](router-processing-future.md): optional timestamp-aware MIDI event processors remain future work, not current scope.
- [CAMD integration](camd-integration.md): endpoints and clock facilities are shared infrastructure; application musical timing policy must not become mandatory CAMD behavior.

## Proposed responsibility split (not yet an implementation decision)

| Layer | Responsibility | Avoid |
|---|---|---|
| RTP-MIDI / AppleMIDI driver | Transport timestamps, clock mapping, journal repair, bounded receive scheduling, connection state | Bar quantization or DAW tempo policy |
| Network MIDI 2.0 driver | Native UMP transport, loss handling, session lifecycle, protocol-appropriate timing | Pretending UMP uses RTP timestamp semantics |
| CAMD | Endpoint identity, native MIDI 1.0 and UMP delivery, clock metadata where supported | Imposing a global WAN jitter buffer or tempo grid |
| Resident Router (optional future processors) | Explicitly configured per-route transformations, order/timestamp preservation | Default latency on all routes |
| Collaboration/performance application | Session-wide musical pulse, tempo authority, bar launch, predictive or delayed scheduling, fallback behavior | Reimplementing wire protocols |

### Central design question: receive compensation or destination scheduling?

Prefer **preserving original event time and clock-domain provenance as long as possible**, with an explicit conversion into a destination clock domain and a configurable presentation deadline at the scheduling boundary. A transport may already need a bounded playout queue; a higher-level application may add *musical* lookahead for quantized bar transitions. These are different delays and must not be silently stacked.

Late-event policy must be explicit per event class: immediate delivery, drop, quantize to next musical boundary, or reset/reconcile state. Note Off, sustain, clock, SysEx and UMP messages require different care. Clock drift, timestamp wrap, reconnect and queue overflow must be measurable. Avoid blocking the CAMD delivery path.

## Research questions before implementation

1. Obtain Bray's thesis and the 2026 paper; verify precise algorithms, observed latency distributions, availability/licensing of code, and which LAS ideas generalize beyond Ableton/EDM.
2. Compare native MIDI collaboration with audio-centric Jamulus/JackTrip only as alternative product models, not as interchangeable MIDI transport implementations.
3. Determine how AppleMIDI and Network MIDI 2.0 expose sender event time, clock offset/uncertainty, loss and recovery to CAMD without losing MIDI 1.0 native compatibility.
4. Audit actual AROS scheduling precision and existing RTP `--latency` behavior under jitter, burst loss, clock drift and multiple peers.
5. Decide whether a standalone performance application is justified. Prefer a demonstrable user scenario before adding an ABI or resident service feature.

## Suggested validation experiment

Use two hosts (later AROS plus macOS/iOS or a third-party peer), inject controlled RTT/jitter/loss and clock drift, and record send time, receive time, mapped event time, scheduled time, actual CAMD delivery and local synth onset separately. Compare (A) no buffer, (B) transport buffer, (C) musical bar quantization, and (D) combined configuration. Include Note On/Off, CC sustain, SysEx, tempo changes, reconnect and MIDI 1.0/UMP native routes. Report p50/p95/p99 lateness, dropped/stuck notes and musical alignment; never equate network RTT with one-way latency.

## Decision / trigger

**Document only.** No new mandatory dependency, CAMD ABI, transport modification or router processor now. Revisit after interoperable AppleMIDI and Network MIDI 2.0 tests and baseline timing measurements; open a dedicated implementation issue only if the experiment demonstrates a gap not already handled by transport scheduling.

## Sources

- MIDI Association, Telemidi: https://midi.org/telemidi-creating-music-over-the-internet-in-real-time
- Matt C. Bray, *Pulse Before Sound* (2026): https://commons.library.stonybrook.edu/jonma/vol8/iss1/2/
- RFC 6295, RTP Payload Format for MIDI: https://www.rfc-editor.org/rfc/rfc6295
- Apple, MIDI Network Driver Protocol: https://developer.apple.com/library/archive/documentation/Audio/Conceptual/MIDINetworkDriverProtocol/MIDI/MIDI.html
