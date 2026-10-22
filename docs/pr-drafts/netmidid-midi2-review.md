AROS PR: [#1796](https://github.com/aros-development-team/AROS/pull/1796), branch `review/netmidid-midi2-review` (`build/netmidid-midi2-review`), sent 2026-10-22.

Follow-up to the review of #1693 (Network MIDI 2.0 transport of netmidid).
Only `transport_midi2.c` changes.

**Re-invite wait.** `midi2_periodic()` doubled `midi2_wait` on every
invitation, up to 60 s, and never reset it, so after a few invitations any
later disconnect from the Host waited a minute before the next one. While a
session with the Host is established the wait now goes back to the first
one, as the AppleMIDI transport does for its configured peer.

**Bad `midi2_peer_ip`.** If it is not an IPv4 address, netmidid now prints
"MIDI 2.0 host must be an IPv4 address" and the transport fails to open, as
AppleMIDI does for its peer. Before, the Host was silently never invited.
The check comes before the socket is bound.

**SysEx drops.** `midi2_counts()` now adds `netmidi_camd_sysex_dropped()`,
which #1793 added to `applemidi_counts()` only, so outgoing SysEx dropped
on networkmidi2 ports is published too.

Validation: netmidid (all sources, with the rtpmidi and netmidi2 link
libraries) compiles and links for x86-64 and amiga-m68k with `-Werror`. The
same change in the companion repository passes its host tests and network
loopback test, and a bad `midi2_peer_ip` stops the service with the
message above.
