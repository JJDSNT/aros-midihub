# RTP-MIDI backlog — from a diagnostic program to a CAMD driver

**Scope:** RTP-MIDI (RFC 6295) and the AppleMIDI session protocol around it:
the portable code in `src/` (`rtpmidi.c`, `sender.c`, `applemidi.c`,
`session.c`, `peers.c`, `timing.c`), the AROS program in
`ports/aros/midihub/`, their tests, and the way to the AROS tree. Network
MIDI 2.0 (`src/netmidi2.c`) and the mDNS service have their own documents:
[network MIDI in AROS](network-midi-integration.md) and
[mDNS and AROSTCP](mdns-arostcp.md).

**State this starts from** (read from the code on 2026-10-12):

- The codecs are complete for what a session needs: AppleMIDI IN, OK, NO, BY,
  CK and RS; the RTP-MIDI command section with delta times, running status
  and segmented SysEx; the recovery journal's chapters P, C, M, W, N, E, T
  and A for a channel and D, V, Q, F and X for the system, decoded and
  encoded. `tests/protocol.c` covers them on the host.
- `network/rtpmidi/peers.c` keeps up to eight sessions, either role, and is tested, but
  nothing on AROS calls it.
- The AROS program `MIDIHub` talks to **one** peer. It answers an invitation
  or invites the address in its configuration, synchronises clocks, sends
  and receives with journals and RS feedback, and delivers timed events
  through a queue. `tests/network_loopback.py` runs it on Linux against
  scripted peers.
- It reaches CAMD as a **client**, through the clusters `MIDIHub In` and
  `MIDIHub Out`. The upstream review of `btmidi.class` turned that shape
  down: a transport is a `DEVS:Midi` driver (see
  [the CAMD integration design](camd-integration.md)).
- It advertises `_apple-midi._udp` with an embedded responder of its own,
  which cannot run beside `mdnsd`, and it never browses for peers.
- It has never exchanged a packet with an implementation that is not
  itself: no iOS, macOS, rtpMIDI for Windows or rtpmidid.

Priorities: **P0** is wrong or unsafe as it is. **P1** stands between the
program and a first use with a real peer. **P2** completes it before an
upstream proposal. **P3** is polish. Within a priority the order is the
suggested order of work. When an item is done, move it to "Done" with the
commit and the test that shows it.

## Done

| # | What was done |
|---|---|
| R03 | The receiver's state and the repair from a journal are `struct rtpmidi_receiver` in `network/rtpmidi/receiver.c`, the counterpart of `struct rtpmidi_sender`: output through three callbacks, the clock handed in, no OS calls. `ports/aros/midihub/main.c` went from 1907 lines to 930 and keeps sockets, clock and CAMD. `receiver_recovery` in `tests/protocol.c` sends a stream to two receivers, one of which loses packets, and compares them after chapters N, P, W, C, M, T, A, the system chapters and X; switching off any one of the six repair steps fails it. The text of the reports is unchanged, so `tests/network_loopback.py` passes as before. Not covered on the host: chapter E's counts, the enhanced form of chapter C, MTC, and a Note On queued for later. |

Found while doing it: `tests/network_loopback.py` failed about once in forty
runs, before the change as after, because a journal-only guard packet could
arrive before the answer to a clock exchange. The test now takes the guard
packet in either order (60 runs without a failure).

## P0 — wrong or unsafe as it is

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| R01 | `rtpmidi_decode()` accepts only the two header bytes `0x80 0xe1`. A packet with the marker bit clear, padding, a CSRC list or a header extension is dropped without a word. RFC 6295 ties the marker bit to a non-empty command section, so a peer's journal-only packet may come with it clear, and our own empty guard packet goes out with it set. | Read the RFC text for the marker bit and write down the rule. Decode by fields: version 2, payload type 97, CSRC count and extension skipped by length, padding removed. Set the marker on sending by the same rule. | Host tests for each header variant; a guard packet from another implementation is acknowledged. |
| R02 | A message is lost when its journal does not fit. `send_midi()` builds packet and journal in 1100 bytes and returns an error when `rtpmidi_sender_journal()` runs out of room, and the caller has no way to try again. A long stretch without RS from the peer makes the journal grow. | Bound the journal: when it would pass the room left, send the command without the chapters that do not fit, oldest first, and count it. Decide the packet size from the path's MTU instead of a constant. | A host test sends with no feedback until the history is full and loses no command. |
| R04 | A SysEx message going out carries no journal and clears the sender's history when chapter X cannot hold it (more than 512 bytes, or a fifth log), so everything sent before it loses its protection at that moment. | Keep the history when a SysEx message is not journalled; mark chapter X as not covering it. | A host test: note on, large SysEx, lose the note's packet, the next journal still recovers the note. |

## P1 — before a first use with a real peer

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| R06 | Never run against another implementation. | First rtpmidid on Linux, which needs no hardware: a runner in `tools/` beside the mDNS ones, hosted AROS on the veth pair, notes and SysEx in both directions, a session opened from each side, packets dropped with `tc netem` to make the journal work. Then macOS, iOS and rtpMIDI for Windows when the machines are at hand ([the iPhone procedure](iphone-smoke-test.md) is written for Linux and needs redoing for AROS). | `tools/rtpmidi-rtpmidid-hosted.sh` passes; a table in this file says which peer was tried, when, and what failed. |
| R07 | One peer on AROS. | Put `network/rtpmidi/peers.c` under the event loop: sender, receiver (R03), clock offset, queue and SysEx assembler per peer. | Two scripted peers connected at once, each recovering its own losses. |
| R05 | An invitation from anyone is accepted while there is room; the only rule is the table's size. The SSRC of RTP, CK and RS is already checked against the session's. | An accept rule in the configuration, as Apple's "who may connect to me": anyone, a list of addresses or names, nobody. Goes with R07, where a second peer first becomes possible. | Host tests for an accepted and a refused invitation under each rule. |
| R08 | A CAMD client, not a driver. | A `DEVS:Midi` driver with a fixed number of ports, each bound to a peer while the session lasts, as `btmidi` does. Needs D1 and D2 below. | A CAMD program opens `applemidi.out.0` and a peer hears it; `MIDIHub In`/`Out` are gone. |
| R09 | The embedded mDNS responder. | Register `_apple-midi._udp` through the system service and remove `network/mdns/embedded.c` from this program. Waits for the registration half of the service to exist upstream (AROS issue #1559, PR #1560). | `avahi-browse` sees the session with `mdnsd` running and no second socket on port 5353. |
| R10 | Peers are found only by typing an address. | Browse `_apple-midi._udp` and keep a list of sessions seen, for Preferences to show and for a configured name to be invited when it appears. | A session announced by Avahi on the host shows up and can be invited by name. |
| R11 | One command per packet, each with the time of sending and a delta of zero. What CAMD hands over in a burst goes out as that many packets, and a time given by the sender is not carried. | Gather what is ready into one command section with delta times, up to the packet size from R02. | A host test: a chord leaves as one packet; the receiver's times are the sender's. |
| R12 | Clock exchange every 50 seconds once three have been done, and a session is dropped after 120 seconds without CK. Apple's implementation sends every 10 seconds and gives up after about a minute. No exchange is thrown away for a long round trip, so one delayed CK moves every timestamp. | Take Apple's intervals. Keep the offset from the exchange with the shortest round trip among the last few. | A host test with one delayed CK among eight: the offset does not move. |
| R13 | Reconnection is nobody's job: after a timeout or a BY the program goes back to idle and stays there unless the peer invites again. | For a peer we invited: invite again, backing off, while it is configured or still advertised. | A scripted peer that goes away and comes back is connected again without a restart. |
| R14 | A received SysEx message over 4096 bytes is dropped (`RTPMIDI_SYSEX_MAX`); the same limit refuses one going out. | Size by configuration up to what the CAMD side accepts; count what is dropped. | A 16 KiB dump crosses both ways. |

## P2 — complete before an upstream proposal

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| R15 | IPv4 only, in the sockets and in `struct applemidi_peer` (`uint32_t address`). | An address type that holds either family. Waits for the IPv6 answer in AROS issue #1559; do not start before it. | — |
| R16 | Configuration is one text file with one peer: local port, peer address and port, session name. | Several sessions to invite, the accept rule (R05), latency (R17), the port's binding to a peer (D2). Preferences reads and writes it. | The Preferences page edits all of it and the runtime takes a change without a restart. |
| R17 | A timed event is delivered at the sender's time exactly, or at once when that time has passed. There is no added latency to absorb jitter, so on a wireless path most events are late and played at once, in bursts. | A latency setting per session, added to the due time; count late events. | With 20 ms of jitter from `tc netem`, intervals between notes at the receiver are those of the sender. |
| R18 | Nothing is counted. Losses, recoveries, late and dropped events appear only as lines on standard output. | Counters per peer, read by Preferences and by a shell command. Remove the per-packet `printf`. | The loopback tests assert on counters, not on text. |
| R19 | RS feedback is sent for every RTP packet received. | Once a second while packets arrive, and at once after a gap. | A host test counts RS packets for a run of 100. |
| R20 | The `P` flag (phantom status) is decoded and never used, and never set. | Read RFC 6295 section 3.2 again and either handle it or state why a receiver may ignore it. | A sentence here or a test. |
| R21 | Decoders are tested with packets we wrote. | A fuzz target for `rtpmidi_decode()`, the reader, each journal chapter and `applemidi_decode()`, run under the sanitizers; captures from real peers (R06) added to the tests as they are made. | An hour of fuzzing without a finding; at least one capture per peer in `tests/`. |
| R22 | Big-endian and 16-bit `int` have not been looked at since the code was written. | Build and run `protocol-test` for m68k in the cross tree (`~/aros-build-amiga-m68k`). | It passes there. |

## P3 — polish

| # | Limit or defect | What to do |
|---|---|---|
| R23 | The sender always writes the default form of chapter C; the enhanced form is only decoded. | Nothing unless a peer needs it; note which peers send it (R06). |
| R24 | Chapter X keeps four logs of 512 bytes; "other extended Chapter X history" in the README is not specified further. | Say exactly which tools of chapter X are and are not written, then decide. |
| R25 | The receiver recovers at most what the journal's checkpoint covers and reports the rest as lost, without telling CAMD. | After an uncovered gap, release the notes and send the usual resets, as at the end of a session. |
| R26 | The program's name, its `--probe-note` and `--probe-sysex` arguments and its messages are those of a diagnostic. | Goes with R08: the driver has no command line; the probes become a test program. |

## The way upstream

Not started, and not to be started before P0 and R06 to R08 are done. The
rules are the ones the earlier pull requests taught us
([upstream PRs](upstream-prs.md)):

| # | What |
|---|---|
| U1 | Where it lives: under `workbench/devs/midi/` as the driver, with the session code beside it, or in contrib first. A question for the maintainers, to ask with working code to show. |
| U2 | Names and layout by AROS convention. Done on 2026-10-12: `rtpmidi_*` and `applemidi_*`, sources in `network/rtpmidi/`, headers in `include/rtpmidi/`, no program name in the code or its text. `config.c` and the embedded responder (`netmidi_*`) belong to the program and stay with it. |
| U3 | The Codacy gate: no new finding. Variables declared at first use, no `strcpy`/`strncpy`/`strlen`, `SocketBase` passed as a parameter. `main.c` uses `strcpy` and `printf` freely today. |
| U4 | Origin of the code. The README names MIDIKit and librtpmidid as references. Confirm that nothing was copied from either and say so in the pull request; if anything was, its licence and notice go with it. |
| U5 | A first cut small enough to review: the codecs and their tests alone, then the session layer, then the driver. |

## Decisions needed

| # | Question | Leaning |
|---|---|---|
| D1 | Who owns the sockets. A CAMD driver is loaded by `camd.library` and never unloaded, and each task needs its own `bsdsocket.library` base. Either the driver starts a process of its own on `Init()`, or a service under the AROSTCP service manager (as `mdnsd` is) owns the sessions and the driver in `DEVS:Midi` is a stub that attaches to it, the way `btmidi`'s stub attaches to the class. | The service: it follows the network going up and down, which a driver loaded once cannot, and it is the shape the reviewer has already accepted twice. |
| D2 | How peers map to ports. A fixed pool (`btmidi` has 16); a port bound to a named session by configuration, or the first free port to whoever connects. | Bound by configuration where a name is configured, first free otherwise, so a route to "the iPad" survives a reconnection. Identity beyond that waits for CAMD backlog B29. |
| D3 | Whether one port can be several peers at once (what a Mac's session does: every participant hears every other). | One peer per port first. A shared port is a later option of the pool, not a second design. |
| D4 | What the driver boundary costs. A driver hands over bytes and is given bytes: the time a message was meant for cannot cross it (CAMD backlog B32). Until then the runtime plays a received event at its due time, and a sent one carries the time it left CAMD. | Accept it and say so in the limits; R11 and R17 are still worth doing on the network side. |

## Not scheduled here

- Network MIDI 2.0 and UMP over the network: `src/netmidi2.c`, after this
  transport works through a driver.
- The driver-side block in CAMD (B29 to B32 in
  [the CAMD backlog](camd-backlog.md)), which would give a peer's name and
  identity to a port. This backlog must not depend on it.
- RTP over TCP, RTCP, and session description by SDP (RFC 6295 sections 6
  and appendix C). No AppleMIDI peer uses them.
