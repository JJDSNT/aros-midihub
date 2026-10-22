<!-- Comment for #1679, posted with the push of 37915bd98f. -->

I have added one commit, `37915bd98f`, that separates the service's core
from the AppleMIDI transport. The two reviewed commits are unchanged, so
the new structure can be read as its own diff.

Until now, Network MIDI 2.0 (#1693) could only be added inside the
AppleMIDI event loop: the runtime held the AppleMIDI peers unconditionally,
the loop read the AppleMIDI sockets by name, and `network.c` knew the
AppleMIDI port pair. With this commit:

- `netmidid.h` defines a transport (open, start, receive, next_due,
  periodic, send_midi, flush, counts, stop, release, and the size of its
  state) and what the core offers it: sockets, CAMD driver ports kept by
  name, clock and random.
- `main.c` is the core: configuration and its watch, sockets, CAMD, the
  published counts and the loop.
- `transport_applemidi.c` is the AppleMIDI code, moved with its behaviour
  and output unchanged.
- `network.c` opens numbered UDP sockets and knows no protocol.
- The configuration keeps the shared keys (`session_name`, `latency_ms`)
  and reads AppleMIDI's own in `config_applemidi.c`. The keys and the file
  format of `ENV:NetMIDI` are unchanged.

On top of this, #1693 adds `transport_midi2.c` and `config_midi2.c` as new
files plus one line in the transport table. Before, it was 476 lines in
`main.c` and changes to `network.c`. #1743 now touches `main.c` by 13
lines. I have rebased #1693, #1742 and #1743 onto this commit.

Validation of the exact revision: it compiles and links for hosted x86-64
and amiga-m68k with `-Werror`. The hosted node-rtpmidi suite passes as
before (7 checks; discovery is skipped because this cut has none).

If you would rather review the service in its earlier form first, I can
move this commit to a follow-up PR instead.
