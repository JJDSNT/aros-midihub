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

## Follow-up

When network driver validation reaches external interoperability testing, check release availability and run a minimal bidirectional exchange first. Append results here before expanding the test scope.

Related documents:

- [RTP-MIDI backlog](rtp-midi-backlog.md)
- [Network MIDI 2.0 backlog](network-midi2-backlog.md)
- [MIDI protocol coexistence research](midi-protocol-coexistence-research.md)
- [Future router processing](router-processing-future.md)
