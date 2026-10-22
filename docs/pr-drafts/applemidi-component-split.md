Separate AppleMIDI control/session handling from the RFC 6295 payload
library introduced by #1587 and extended by #1678. AppleMIDI uses the RTP
payload, but its control codec, peers, sessions, RS feedback and CK clock
exchange should have their own public headers and build target.

Draft for review of the component boundaries and coordinated migration
of the pending service proposals. Do not merge independently of their
consumer updates.

This change introduces three components below `workbench/network/netmidid`:

- `rtpmidi/`: the payload codec, journal, sender, receiver and RTP timestamp
  conversion, built as `rtpmidi`.
- `applemidi/`: the control codec, session state, peers and CK exchange,
  built as `applemidi`, with public headers under `applemidi/`.
- `common/`: neutral port ownership, bounded text/UTF-8 helpers and timed
  MIDI events, built as `netmidi-common`. The service core and MIDI 2.0
  transport can use these without linking an unrelated protocol.

The implementations, packet formats and working-buffer sizes are
preserved. The CK offset estimator moves out of payload timing to
`applemidi_clock_offset()`. Neutral helpers acquire `netmidi_*` names.

This is a coordinated source migration of static libraries: callers must
update their includes, helper names and link lists. The service in #1679
and its dependents #1693, #1742 and #1743 need the corresponding consumer
updates before this migration can be merged with those proposals. No
already-merged runtime consumer of these library headers was found in
upstream `workbench/network` outside the library itself.

Validation in the companion repository:

- Separate payload and AppleMIDI regression suites; the payload links
  without AppleMIDI. Configuration tests remain separate and pass.
- Isolated-header builds of the protocol suites and of core-only and
  MIDI-2-only runtimes; the latter two have no RTP/AppleMIDI headers.
- Host unit and network loopback suites; ASan/UBSan fuzz, 100000 rounds.
- All-features AROS service and selftest builds for hosted x86-64 and
  amiga-m68k, with separate static archives.
- Generated MetaMake builds of core, AppleMIDI, MIDI 2.0 and advertisement
  cuts for both architectures, using matching Bonami headers.
- Hosted node-rtpmidi suite: 8/8, including CAMD traffic in both
  directions, SysEx, CK and reconnection; Avahi sees the advertisement.
- Hosted MIDI 2.0 suite: 3/3, including bidirectional UMP/CAMD, SysEx,
  All Notes Off and advertisement.

The [source mapping and validation record](https://github.com/JJDSNT/aros-midihub/blob/main/docs/applemidi-component-split-2026-10-22.md)
is in the companion repository.
