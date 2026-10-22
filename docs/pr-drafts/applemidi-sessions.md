# rtpmidi: add AppleMIDI sessions, peers and clock sync

Add AppleMIDI sessions to the RTP-MIDI link library merged in #1587:
invitation exchange on control/data ports, peer/session state, driver-port
assignment, clock synchronization and timestamp conversion. The portable C
library owns no sockets or tasks; callers supply network I/O and the clock.

Sources and headers live in `workbench/network/netmidid/rtpmidi`. The service
consumer is separate in #1679. There is no Bonami or Network MIDI 2.0 dependency.
The session protocol follows Apple's MIDI Network Driver Protocol around
RFC 6295's MIDI payload. The session commit is based on upstream master
`eee73b61566cd27644b8c46efdb90d3a2313bb73`, after the prerequisite merges.

### Changes following review

Addresses [Kalamatee's review](https://github.com/aros-development-team/AROS/pull/1678#issuecomment-6250940105):

- An older `now` sample no longer makes a clock/peer expire immediately.
  Clock polling likewise ignores a sample older than its last send.
- Peer expiry uses unsigned half-range ordering, preserving forward progress
  across the low 32-bit clock wrap while rejecting small backward differences.
  The header specifies the monotonic 1/10000-second unit and the supported
  maximum elapsed interval of `INT32_MAX` ticks (about 59.7 hours).
- Renamed peer parameters/member to `now`, `timeout` and `last_seen`, removing
  the misleading millisecond suffixes. Field sizes/layout are unchanged.
- Collapsed the redundant terminal peer-event assignment to `PEER_LEFT`.
- Repeated the `text[limit]` readable-byte requirement at both ports.c call
  sites and documented the external name's requirement in ports.h.

The optional CK0 role-tolerance suggestion is retained as interop follow-up;
this update preserves the existing role behavior and adds no claim of a
reproduced failure with such a peer.

### Validation

The new stale-clock regression fails on the original PR head with `now=994`,
`heard=1000`. The complete protocol suite passes under ASan/UBSan with the
updated exact PR sources and with the local implementation. Regressions cover
backdated samples, the exact timeout boundary, updates through peers_seen,
32-bit wrap and old samples from the previous counter cycle.

All nine library sources cross-compile with `-Wall -Wextra -Werror` for hosted
x86-64 and amiga-m68k (the m68k SDK needs its existing volatile-register warning
exclusion). Both #1679 and #1693 service consumers compile/link for both targets
with this update propagated. Their drafts include the parent correction.

Earlier generated-make and hosted UP AROS checks against node-rtpmidi exercised
invitations both ways, clocks and MIDI through CAMD. Those runtime exchanges
were not repeated for this small timeout/units update. These checks are not a
fresh full upstream AROS rebuild, native m68k/SMP execution, or proof of complete
RFC 6295/recovery-journal interoperability. Regression harnesses remain in the
local companion aros-midihub workspace, outside this AROS source slice.
