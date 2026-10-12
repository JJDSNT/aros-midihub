# UMP-OX: testing candidate and future reference

Recorded: 2026-10-12.

Reference: https://github.com/bbouchez/ump-ox

## Initial purpose: interoperability testing

Evaluate UMP-OX as an external counterpart for Midihub network MIDI tests. Its README describes integrated RTP-MIDI and NetworkUMP drivers, message generators and a routing matrix.

Before relying on it, confirm availability of a working release and compatibility with the protocol versions implemented by our drivers.

Suggested tests:

- Establish sessions and exchange messages in both directions over RTP-MIDI and NetworkUMP.
- Exercise MIDI 1.0 and MIDI 2.0 messages supported by each path, checking channels, groups and values.
- Verify behavior at translation boundaries, including documented unsupported messages or precision loss.
- Test reconnects, session teardown and sustained traffic; compare received data with expected messages using our own diagnostics or packet captures.
- Record application version, OS, configuration, driver revision and results so failures are reproducible.

A successful exchange is evidence of interoperability with that application, not proof of complete specification conformance.

## Possible future reference

Study port configuration, routing matrix interactions, saved configuration profiles, message generators and Lua processing per output as references for Midihub diagnostics and future router processing.

The README describes a UMP-only internal representation with translation at MIDI 1.0/RTP-MIDI interfaces. Evaluate the tradeoffs while preserving Midihub's intended native MIDI 1.0 and UMP paths; UMP transport of MIDI 1.0 messages is distinct from conversion to MIDI 2.0 Channel Voice messages.

## Source availability

At the time of inspection, the repository's main branch contained only README.md, with no application source or code license available there. Treat it initially as a testing candidate and behavioral reference. A port or code reuse would require access to the source and clarification of licensing.

## Results, 2026-10-12

UMP-OX itself was not run: its releases (1.9.1 at the time) are for Windows
and macOS, it has a graphical interface only, and its repository holds no
source. This machine is Linux under WSL.

Its two engines are published by the same author with their source, under
the MIT licence, and build on Linux: the `RTP-MIDI` class (commit 7213cdc)
and the `NetUMP` class (commit f74b0b8), both over his `BEBSDK` (022d0f2).
`tools/interop-bouchez-host.sh` fetches the three, builds a small program
around each (`tests/interop/bb_rtpmidi_peer.cpp`,
`tests/interop/netump_peer.cpp`) and runs four cases over the loopback
interface, all of which pass:

| Case | Result |
|---|---|
| AppleMIDI, MIDIHub inviting the class | Session and clocks; our Note On and Note Off shown by the class; its four messages taken here |
| AppleMIDI, the class inviting MIDIHub | The same in the other role, and its goodbye |
| Network MIDI 2.0, the class as Client of our Host | Names and ids exchanged; UMPs both ways word for word; its Bye frees our slot |
| Network MIDI 2.0, our Client of the class as Host | The same, and our Bye understood with its reason |

What it took: for Network MIDI 2.0, nothing. For AppleMIDI, one change: the
class takes no RTP packet with the marker bit set, which RFC 6295 requires
of a packet with commands; see
[AppleMIDI: where practice leaves the documents](applemidi-practice-and-specification.md).

What it does not show: neither class implements error recovery, so no loss
was repaired; one session at a time; MIDI 1.0 messages only; the host
build only, not AROS. Running UMP-OX on a Windows or macOS machine against
AROS on the same network remains worth doing: it would add its generators,
MIDI 2.0 channel voice messages and a human's eye on the result.

## Follow-up

When network driver validation reaches external interoperability testing, check release availability and run a minimal bidirectional exchange first. Append results here before expanding the test scope.

Related documents:

- [RTP-MIDI backlog](rtp-midi-backlog.md)
- [Network MIDI 2.0 backlog](network-midi2-backlog.md)
- [MIDI protocol coexistence research](midi-protocol-coexistence-research.md)
- [Future router processing](router-processing-future.md)
