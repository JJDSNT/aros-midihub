# AppleMIDI backlog — sessions, peers and the driver

**Scope:** the AppleMIDI session protocol and everything that makes
RTP-MIDI a thing that runs: the control packets (`applemidi.c`), one
session (`session.c`), the peer table (`peers.c`), clock synchronisation,
discovery of `_apple-midi._udp`, the AROS runtime in `ports/aros/midihub/`
and the `DEVS:Midi` driver it has to become. The payload those sessions
carry is in [the RTP-MIDI backlog](rtp-midi-backlog.md).

There is no standard for this protocol. What is known of it comes from
Apple's behaviour and from other implementations, so an item that says
"what Apple does" means: observe a real peer before changing the code.

**State this starts from** (read from the code on 2026-10-12):

- The codec reads and writes IN, OK, NO, BY, CK and RS, protocol version 2.
  Anything else is rejected as a whole packet.
- `applemidi_session` is one session in either role, five phases, tokens and
  SSRC checked on every step. `applemidi_peers` keeps eight of them, found
  by IPv4 address and control port, with accept or refuse, a name per peer,
  and expiry by silence. Both are tested on the host.
- Nothing on AROS uses the peer table. The program `MIDIHub` holds one
  `applemidi_session` and one peer address; an invitation from a second
  peer is not answered at all.
- The program is a CAMD **client** with the clusters `MIDIHub In` and
  `MIDIHub Out`, which is the shape the review of `btmidi.class` turned
  down.
- It advertises with a responder of its own and never browses.
- It has met no implementation but itself.

Priorities as in the other backlogs: **P0** wrong or unsafe as it is, **P1**
before a first use with a real peer, **P2** before an upstream proposal,
**P3** polish.

## Done

On 2026-10-12, with host tests in `tests/protocol.c`; the whole host suite,
the network loopback and the build for hosted AROS pass. None of it has run
inside AROS or against another implementation.

| # | What was done |
|---|---|
| AM01 | An invitation on the control port with another token or SSRC, in a session the peer had reached (waiting for its data port, or connected), ends that session and begins the new one. `restarts` in the session counts it, the peer table reports `APPLEMIDI_PEER_RESTARTED`, and the program then drops what it kept for the old stream and releases its notes. Not done for a session we are still inviting: two invitations crossing is another case. The old session's BY arriving late is ignored. Tested in `session_restart_and_collision`; the program's side of it (releasing notes) is not in any test. |
| AM02 | `struct applemidi_clock` in `network/rtpmidi/clock.c` holds one session's exchange: when to start one, the answer to each packet, the offset, and the silence after which the invited side gives up. The program passes packets and time and keeps none of it. `apple_clock` carries five exchanges between two clocks five hundred seconds apart over a path of known delay, checks the offset at both ends to the tick, and feeds each side the packets that belong to no exchange. The intervals are the ones the program had; AM11 is still open. |
| AM03 | `applemidi_decode()` takes the fields it uses and no more: a name without its terminator, bytes after the terminator, and any value in a CK packet's three unused bytes are accepted. What it refuses it refuses with a reason (`enum applemidi_decode_result`): not AppleMIDI, unknown command, truncated, wrong version, malformed. Nothing counts the reasons yet: that is with the counters of RTP-MIDI backlog R18. |
| AM04 | SSRC, tokens and the first sequence number come from `entropy.resource` on AROS and `/dev/urandom` on the host, through `netmidi_network_random()`; with neither, the program says so once and falls back to the clock. A peer with our SSRC is refused, NO for its invitation and BY for its acceptance, `ssrc_collisions` counts it, and the program takes another SSRC and invites again. The AROS branch compiles and has not run. |

## P0 — wrong or unsafe as it is

Nothing open.

## P1 — before a first use with a real peer

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| AM05 | One peer on AROS. | Put `applemidi_peers` under the event loop, with a sender, a receiver, a clock, a queue and a SysEx assembler per peer. | Two scripted peers connected at once, each recovering its own losses. |
| AM06 | Never run against another implementation. | First rtpmidid on Linux, which needs no hardware: a runner in `tools/` beside the mDNS ones, hosted AROS on the veth pair, notes and SysEx in both directions, a session opened from each side, packets dropped with `tc netem` to make the journal work. Then macOS, iOS and rtpMIDI for Windows when the machines are at hand ([the iPhone procedure](iphone-smoke-test.md) is written for Linux and needs redoing for AROS). | `tools/applemidi-rtpmidid-hosted.sh` passes; a table in this file says which peer was tried, when, and what failed. |
| AM07 | An invitation from anyone is accepted while there is room; the only rule is the table's size. | An accept rule, as Apple's "who may connect to me": anyone, a list of addresses or names, nobody. | Host tests for an accepted and a refused invitation under each rule. |
| AM08 | A CAMD client, not a driver. | A `DEVS:Midi` driver with a fixed number of ports, each bound to a peer while the session lasts, as `btmidi` does. Needs D1 and D2 below. | A CAMD program opens `applemidi.out.0` and a peer hears it; `MIDIHub In`/`Out` are gone. |
| AM09 | The embedded mDNS responder. | Register `_apple-midi._udp` through the system service and take `network/mdns/embedded.c` out. The service's registration half is not upstream yet (AROS issue #1559, PR #1560), but `bonami.library` and `mdnsd` work in this repository, so this can be done against them now and adjusted to what upstream accepts. | `avahi-browse` sees the session with `mdnsd` running and no second socket on port 5353. |
| AM10 | Peers are found only by typing an address. | Browse `_apple-midi._udp` and keep a list of sessions seen, for Preferences to show and for a configured name to be invited when it appears. | A session announced by Avahi on the host shows up and can be invited by name. |
| AM11 | Clock exchange every 50 seconds once three have been done, and a session is dropped after 120 seconds without CK. The intervals other implementations use are, from memory and to be confirmed on a capture, 10 seconds and about a minute. No exchange is thrown away for a long round trip, so one delayed CK moves every timestamp. | Take the observed intervals. Keep the offset from the exchange with the shortest round trip among the last few. In `network/rtpmidi/clock.c`. | A host test with one delayed CK among eight: the offset does not move. |
| AM12 | Reconnection is nobody's job: after a timeout or a BY the program goes back to idle and stays there unless the peer invites again. | For a peer we invited: invite again, backing off, while it is configured or still advertised. | A scripted peer that goes away and comes back is connected again without a restart. |
| AM13 | Retries and timeouts are constants scattered in the program: an invitation every second, twelve times; two minutes for a data-port invitation that never comes. | State them once, beside the peer table, with the reason for each number. | One place to read them; the host tests name them. |

## P2 — complete before an upstream proposal

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| AM14 | IPv4 only: `struct applemidi_peer` holds a `uint32_t` address. | An address type that holds either family. Waits for the IPv6 answer in AROS issue #1559; do not start before it. | — |
| AM15 | Eight peers, a constant. | A number from configuration with eight as the default, and a NO with a reason in the log when the table is full. | A test fills a table of two. |
| AM16 | Configuration is one text file with one peer: local port, peer address and port, session name. | Several sessions to invite, the accept rule (AM07), latency (RTP-MIDI backlog R17), a port's binding to a peer (D2). Preferences reads and writes it, and the runtime takes a change without a restart. | The Preferences page edits all of it. |
| AM17 | A peer's name is cut at 63 bytes wherever that falls, which can split a UTF-8 character; our own name is refused over 63. | Cut at a character boundary. | A host test with a name of multi-byte characters. |
| AM18 | A packet with a command we do not know (Apple's bit-rate limit, `RL`, is one) fails to decode and is then tried as RTP, which also fails. Nothing is lost, and nothing is counted. | Recognise the command as known and ignored. | A host test; a counter. |
| AM19 | The decoders are tested with packets we wrote. | A fuzz target for `applemidi_decode()` and for the session and peer state machines fed with arbitrary packet sequences, under the sanitizers; captures from AM06 added to the tests. | An hour without a finding; at least one capture per peer. |

## P3 — polish

| # | Limit or defect | What to do |
|---|---|---|
| AM20 | The program's name, its `--probe-note` and `--probe-sysex` arguments and its messages are those of a diagnostic. | Goes with AM08: the driver has no command line; the probes become a test program. |
| AM21 | `applemidi_peers_receive()` sets the "left" event through two branches that end the same way. | Simplify when the function is next touched. |

## Decisions needed

| # | Question | Leaning |
|---|---|---|
| D1 | Who owns the sockets. A CAMD driver is loaded by `camd.library` and never unloaded, and each task needs its own `bsdsocket.library` base. Either the driver starts a process of its own on `Init()`, or a service under the AROSTCP service manager (as `mdnsd` is) owns the sessions and the driver in `DEVS:Midi` is a stub that attaches to it, the way `btmidi`'s stub attaches to the class. | The service: it follows the network going up and down, which a driver loaded once cannot, and it is the shape the reviewer has already accepted twice. |
| D2 | How peers map to ports. A fixed pool (`btmidi` has 16); a port bound to a named session by configuration, or the first free port to whoever connects. | Bound by configuration where a name is configured, first free otherwise, so a route to "the iPad" survives a reconnection. Identity beyond that waits for CAMD backlog B29. |
| D3 | Whether one port can be several peers at once (what a Mac's session does: every participant hears every other). | One peer per port first. A shared port is a later option of the pool, not a second design. |
| D4 | What the driver boundary costs. A driver hands over bytes and is given bytes: the time a message was meant for cannot cross it (CAMD backlog B32). Either accept that for now (the runtime plays a received event at its due time, and a sent one carries the time it left CAMD), or hold the driver until CAMD has a timed path for drivers. | Accept it and say so in the limits; holding the driver puts the whole transport behind an extension that has not been proposed to the maintainer yet. |

None of the four is taken. AM08 needs D1, D2 and D4; nothing else here
needs any of them.

## Not scheduled here

- The driver-side block in CAMD (B29 to B32 in
  [the CAMD backlog](camd-backlog.md)). This backlog must not depend on it.
- Anything Apple's implementation is not known to do: authentication,
  encryption, sessions over TCP.
