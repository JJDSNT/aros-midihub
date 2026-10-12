# RealTime and FastForward: future research

Recorded: 2026-10-12.

Status: research candidates, not implementation commitments or required dependencies.

## References

- [AmigaOS RealTime Library documentation](https://wiki.amigaos.net/wiki/RealTime_Library)
- [FastForward source repository](https://github.com/amigazen/FastForward)

Additional FastForward documentation: https://amigazen.com/fastforward/

## realtime.library

Investigate Conductor/Player timing contexts, alarms, transport states and external synchronization as a possible shared timing layer for MIDI sequencers, synth playback and other media applications.

Questions for a future investigation:

- What does the current AROS implementation support, and which applications already use it?
- How should Conductor time map to CAMD timestamps and the independent BLE, RTP-MIDI and network UMP clock domains?
- What resolution, jitter, wraparound behavior and callback restrictions apply on each target?
- Can existing CAMD timing remain compatible while applications optionally join a shared transport?

The AmigaOS documentation describes 1,200 Hz ticks (approximately 0.83 ms). This is not a guarantee of sample-accurate scheduling or measured AROS performance. Verify implementation and behavior before adopting it.

## fastforward.library

Explore FastForward as optional multimedia infrastructure for AROS: media pipelines, plugin loading, decoding, filters, output and shared clocks. Potential Midihub uses include synth-side PCM processing and synchronized media playback; CAMD message routing and MIDI transport remain separate responsibilities.

The project describes a classic Amiga focus, welcomes feasible AROS ports and licenses its original work under BSD 2-Clause. Third-party codec licenses must be reviewed separately. Verify current source, build status and working features; documentation lists both implemented and planned components.

Questions for a future investigation:

- Is there an existing AROS port, branch, issue or maintainer effort to collaborate with?
- How portable are library entry points, calling conventions, LoadSeg plugin loading, BPTR handling, pointer widths and endian assumptions?
- Are the required realtime.library and AHI facilities available and reliable on the intended targets?
- Does its graph model help future audio/DSP processing without imposing unnecessary overhead on live MIDI routing?

Suggested first experiment: port only the core and a WAV -> PCM -> AHI playback pipeline. Measure CPU and memory use, playback continuity, shutdown/resource cleanup and timing behavior before expanding codec or video coverage. Evaluate m68k separately from native AROS architectures.

## Follow-up

Start with an AROS realtime.library implementation review and a FastForward source/build feasibility review. Record findings here before deciding whether to open a dedicated porting workstream.

Related project documents:

- [CAMD integration](camd-integration.md)
- [Synth and CAMD](synth-camd.md)
- [Future router processing](router-processing-future.md)
