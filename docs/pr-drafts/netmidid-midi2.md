Add Network MIDI 2.0 over UDP as a second transport of the netmidid
service proposed in #1679, using the merged `netmidi2`/UMP library and `DEVS:Midi/networkmidi2`.
Accept Clients as a Host, or invite a configured Host by IPv4 address,
assign sessions to CAMD driver ports, translate UMP/MIDI traffic, and
handle peer departure and the existing session recovery mechanisms.

This remains a **dependent draft against master**. Its only open merge
prerequisite is #1679, the base netmidid service. #1678 (AppleMIDI sessions)
and #1692 (MIDI 2.0 library corrections) are now merged into master.

Rebased on upstream master `a1b8aa6fd8b7ed6e0cae1ecbe83c82ecb905c343`
plus the #1679 commits: the netmidid core, its AppleMIDI transport and the
review fix.
GitHub's aggregate diff includes #1679; the separate MIDI 2.0 commit is
[`d8e6c5056e`](https://github.com/JJDSNT/AROS/commit/d8e6c5056e05a45738df1410cec573e7dfc06550).
It is a transport of its own: `transport_midi2.c` and `config_midi2.c` are
new files, and the core gains one line where transports and their keys
are registered. The behaviour is that of the earlier revision. Sources live in
`workbench/network/netmidid`; shared driver headers use the maintainer's
`workbench/devs/camd/networkmidi2` layout.

Configuration in `ENV:NetMIDI`, for example:

```ini
midi2_port=5673
session_name=AROS Net MIDI
```

To invite a Host, additionally set:

```ini
midi2_peer_ip=192.168.166.1
midi2_peer_port=5683
```

Network MIDI 2.0 remains disabled when its local port is zero; the installed
service entry remains `Enabled=no`. This cut does not advertise or browse
services, include Bonami, or require #1591. CAMD traffic is MIDI 1.0, carried
as UMP and translated on receive; this does not introduce a native UMP CAMD
application API or add authentication support.

Validation of the proposed cut:

- Both protocol libraries, all four CAMD drivers and netmidid compile/link
  through generated makefiles with the existing hosted x86-64 SDK in an
  isolated build. Direct service/driver cross-builds pass too.
- The RTP-MIDI protocol suite passes under ASan/UBSan. The nine MIDI 2.0
  regression/suite cases pass against the exact #1692 library revision.
- Fresh service/driver binaries on hosted UP AROS, as a Host: a peer's
  notes, controller and SysEx reach CAMD; CAMD's eight notes and SysEx reach
  the peer; its Bye produces All Notes Off on all sixteen channels.
- Hosted AROS as a Client by IP: it invites the configured Host, preserves
  its persisted product identity, receives the Host's four MIDI messages
  through CAMD, and sends the CAMD scale/SysEx back to the Host.
- The two hosted serial logs contain no detected native fault signatures.
- Separately, the corrected portable library exchanges notes/controller/
  SysEx and goodbye with independent NetUMP `f74b0b8` / BEBSDK `022d0f2`, as
  Host and Client over Linux loopback. The hosted AROS tests use our own
  peer, so they are not independent on-wire interoperability evidence.

The native tests reuse the existing hosted image with newly compiled
service/driver binaries, not a fresh full current-master AROS build.
Real hardware, SMP, simultaneous AppleMIDI/MIDI 2.0 stress, driver
unload/restart under MIDI 2.0 load and independent loss-recovery
interoperability remain follow-up validation. The separate netinfo
worker-lifetime proposal #1677 remains open for review; this change does not resolve
that concurrency review or claim full M2-124-UM conformance.

### AppleMIDI review follow-up (2026-10-18)

Includes the correction requested in #1678: backdated timestamps no longer
expire peers/clocks immediately; peer time names use the documented 10 kHz
unit and 32-bit wrap is preserved. Also includes the terminal-event and
buffer-contract documentation refinements.

The updated library and both dependent services compile/link for hosted
x86-64 and amiga-m68k with warnings treated as errors (the existing m68k SDK
volatile-register exclusion applies). The full protocol suite with stale-time
and wrap regressions passes under ASan/UBSan against the parent sources.
This remains a dependent draft; this update adds no service feature and does
not claim fresh native/hardware/SMP or interop execution.


### Base refresh (2026-10-20)

The exact rebased service builds and links for x86-64 and amiga-m68k with
warnings treated as errors (the existing m68k SDK exclusion applies).
This refresh adds no runtime behavior and does not add fresh MIDI 2.0
interop/hardware/SMP evidence. It remains draft until #1679 merges.
