# netmidid merged-service follow-up

AROS PR: [#1793](https://github.com/aros-development-team/AROS/pull/1793)

The service merged in #1679 still left four review findings in its runtime
paths: its adapter error named a library it does not open, two option
overrides had no producer, an outgoing SysEx lost to the shared gather
buffer was absent from the status counters, and the transport reached into
byte 12 of an RTP-MIDI packet to mark a recovery journal.

The follow-up:

- reports failure to initialize the CAMD adapters without claiming that
  `camd.library` was opened;
- removes the unused service-only option structure and reads the configured
  acceptance rule directly;
- counts one outgoing SysEx loss per rejected message, both when another
  port owns the shared buffer and when the message exceeds it, and publishes
  that count for the affected port;
- adds `rtpmidi_sender_guard()`, so the RTP-MIDI library constructs the empty
  command list and recovery journal while the transport only requests a
  guard packet.

The corresponding changes remain in the local component-split proposal
because that is active work intended for a later upstream PR. Its real
command-line overrides remain present and are not the unused service-only
fields removed by #1793.

Validation:

- generated x86_64 and amiga-m68k builds passed with warnings treated as
  errors for rtpmidi, netmidi2 and netmidid;
- a host fixture compiling the canonical `adapters.c` passed under ASan and
  UBSan for cross-port contention, repeated bytes after rejection and buffer
  overflow;
- a host fixture compiling the canonical rtpmidi sources decoded the guard
  as an empty command list with a valid recovery journal and rejected a short
  destination buffer;
- the complete companion `make test` suite passes.
