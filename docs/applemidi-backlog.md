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
- The program `MIDIHub` held one session and one peer address until AM05,
  below; it now serves up to eight peers at once through the peer table.
- The program is a CAMD **client** with the clusters `MIDIHub In` and
  `MIDIHub Out`, which is the shape the review of `btmidi.class` turned
  down.
- It advertised with a responder of its own until AM09, below, and did not
  browse until AM10.
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
| AM22 | Found by the first run against another implementation: the RS packet. Its four bytes hold the 16-bit sequence number being acknowledged, and we wrote and read it in the last two, as a 32-bit number. Apple's form has it in the first two (Wireshark's dissector reads it so, node-rtpmidi writes it so); rtpmidid writes it our old way. So a peer of the first kind read every acknowledgement of ours as zero, and we read each of theirs as zero: the journal was never trimmed, and a journal-only packet went out every second for as long as the session lasted. We now send Apple's form and read both, taking the reading nearer behind what we last sent (`applemidi_feedback_acked()`). Seen on the wire: node-rtpmidi reports our acknowledgement at its own sequence number, and after its RS our journal-only packets stop. |
| AM05 | The program keeps `applemidi_peers` and, beside each slot, a stream of its own: clock, sequence numbers both ways, sender and journal, receiver, queue and SysEx assembler. Packets are sorted to a peer by address and port. What a CAMD program sends goes to every connected peer, each with its own sequence numbers and journal; what the peers send is merged into the one cluster `MIDIHub In`, and a peer that leaves has its sounding notes released without touching the others'. `run_two_peers_case` in `tests/network_loopback.py` has two scripted peers connected at once, each losing a packet and repaired from its own journal, one leaving and coming back; `tools/applemidi-node-hosted.sh` has two node-rtpmidi peers on hosted AROS, a scale from a CAMD program reaching both and what each sends reaching CAMD. Merging every peer into one pair of clusters is what a CAMD client can do; a port per peer is the driver's (AM08). |
| AM09 | The program's own mDNS responder is gone: `network/mdns/embedded.c`, its header, its socket on port 5353 and its tests. `ports/aros/midihub/discovery.c` registers the session as `_apple-midi._udp` through `bonami.library`, tries again every ten seconds while `mdnsd` is not there, and withdraws it at exit. `tools/applemidi-discovery-hosted.sh`: Avahi on the host resolves "AROS MIDIHub" to AROS's address and port 5004, and is told when the program ends. The host build used for tests advertises nothing. This is against the service as it is in this repository; what AROS accepts of it upstream (issue #1559, PR #1560) may change the calls. |
| AM23 | Found by the second peer: the RTP marker bit. RFC 6295 has it set when a packet has commands, and so we sent it since R01. The RTP-MIDI class takes no packet with it set, and the sources of rtpmidid and of the Arduino AppleMIDI library say the same of rtpMIDI for Windows and of macOS. We send it clear always and take either. |
| AM10 | The program looks for `_apple-midi._udp` through the system service and reports every session that appears or leaves, with its address and port. `--peer-name <session>` names one to invite wherever it is advertised. The service calls back in its own process, so the callback only copies the event into a ring that the program reads from its loop. `tools/applemidi-discovery-hosted.sh`: the host advertises a session through Avahi with node-rtpmidi behind it, and AROS finds it by name and invites it. There is no list kept for Preferences to show yet: the sessions are reported as they come and go. |
| AM12 | For a peer wanted by name: while it is advertised and we have no session with it, it is invited, at once when it appears and then after 5, 10, 20 seconds up to a minute while it does not answer. When its advertisement goes and comes back, so does the invitation. The same runner takes the host's session and its advertisement away and brings them back, and AROS invites again. A peer given by address is still invited once: nothing says whether it is there. |
| AM07 | `enum applemidi_accept` in the peer table: anyone, nobody, or those of a list of up to eight, each an address or the name the invitation carries. An invitation the rule turns away is answered NO, takes no place and is counted in `refused`; the rule says nothing of whom we invite. `MIDIHub --accept anyone\|nobody\|<list>`. Tested on the host in `applemidi_accept_rule`, and by hand against the RTP-MIDI class, which reports itself refused under "nobody" and under a list it is not on, and connects when listed by name or by address. A name is whatever the peer says it is: this keeps out mistakes, not intent. Not in the configuration file yet (AM16). |
| AM11 | The clock exchange follows what other implementations do, read from their sources: node-rtpmidi 1.0.0 and Benoit Bouchez's RTP-MIDI class both begin with exchanges 1.5 seconds apart and go on with one in ten seconds, and the second gives up a session after about forty seconds without one. We exchanged every fifty: as the inviter of such a peer we would have been dropped. Now three a second apart, then one in ten seconds, and a peer silent for a minute is taken for dead (it was two). The offset is that of the quickest of the last eight exchanges (`APPLEMIDI_CLOCK_KEPT`), at both ends: a CK held up on its way no longer moves every timestamp. `apple_clock_delayed` in `tests/protocol.c`: one answer 30 ms late among eight moves nothing; a first exchange that was late is believed until a better one; a path that became slower for good is followed after eight. Both interoperability scripts pass with it. Not known: what Apple's implementation does, there being no capture; and over eight exchanges, eighty seconds, two clocks drift apart by a few milliseconds that the newest exchange would have shown. |
| AM13 | The waits are stated once, with the reason for each: `APPLEMIDI_INVITE_INTERVAL` and `APPLEMIDI_INVITE_TRIES` (a second, twelve times: the same as Bouchez's class), `APPLEMIDI_DATA_INVITE_TIMEOUT`, `APPLEMIDI_WANTED_FIRST_WAIT` and `APPLEMIDI_WANTED_LONGEST_WAIT` in `include/rtpmidi/peers.h`; `RTPMIDI_GUARD_INTERVAL` in `sender.h`; `RTPMIDI_QUEUE_HORIZON` in `timing.h`; the clock's in `clock.h`. The program has none of its own left. Found on the way: the wait for a data-port invitation was twelve seconds, not the two minutes this backlog said. The host tests name the clock's; the others are used by the program only, whose tests are in Python and cannot. |

## Peers met

| Peer | Where | When | What passed | What it could not show |
|---|---|---|---|---|
| node-rtpmidi 1.0.0 | Linux build over loopback (`tools/applemidi-node-host.sh`) and hosted AROS over the veth pair (`tools/applemidi-node-hosted.sh`) | 2026-10-12 | Invitation and clock exchange in both roles; our Note On and Note Off shown by the peer; its packet of four commands with delta times (Note On, Note Off, Control Change, SysEx) delivered through CAMD to a program on AROS; RS understood both ways; its BY ends the session; `entropy.resource` gave AROS its SSRC and tokens | It has no recovery journal, so no loss was repaired; it sends nothing while it is the invited side; discovery, since its mDNS parts were left out |
| Benoit Bouchez's RTP-MIDI class, commit 7213cdc (the engine of UMP-OX and of KissBox's products) | Linux build over loopback (`tools/interop-bouchez-host.sh`) | 2026-10-12 | Invitation and clock exchange in both roles; its four messages in one packet taken; our notes shown by it, once the marker bit went out clear; its goodbye | It has no recovery journal; not run on hosted AROS |
| rtpmidid | — | — | Not run: no ALSA sequencer here | |
| macOS, iOS | — | — | Not run: no machine at hand | |
| rtpMIDI for Windows | — | — | Not run | |

The two places where the peers showed the documents and the practice apart,
the marker bit and the RS packet, are written up with their evidence in
[AppleMIDI: where practice leaves the documents](applemidi-practice-and-specification.md).

What the first peer showed that our own tests could not:

- Its RTP packets have the marker bit clear with commands in them, against
  RFC 6295. Before R01 of the RTP-MIDI backlog, closed the same day, every
  one of them was dropped without a word: the session connected, clocks
  were exchanged, and nothing was ever received.
- The RS packet's layout (AM22).
- It sends the session name with its terminating zero counted in, which
  our decoder takes as it is.

## P0 — wrong or unsafe as it is

Nothing open.

## P1 — before a first use with a real peer

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| AM06 | Run against other implementations. One has been met: see "Peers met" below. | rtpmidid, which needs a Linux with the ALSA sequencer (this machine's WSL kernel has none, and Ubuntu 24.04 has no package). Then macOS, iOS and rtpMIDI for Windows when the machines are at hand ([the iPhone procedure](iphone-smoke-test.md) is written for Linux and needs redoing for AROS). With a peer that has the recovery journal: packets dropped with `tc netem`, to see a loss repaired in each direction. | Every row of "Peers met" is filled. |

**AM08 is being done in three cuts** (decisions D1, D2 and D4 below).

1. *The service.* Done on 2026-10-12: `applemidid`, the same runtime as
   the program `MIDIHub`, built to be started by the AROSTCP service
   manager from `db/services.d/applemidi`: no console, no arguments, its
   configuration from where the program reads it, stopped with the
   signal the manager sends, and stopped and started again across a
   reconfiguration of the stack (`Policy=restart`).
   `tools/applemidi-service-hosted.sh`: the stack starts it, node-rtpmidi
   invites it, a CAMD program gets the four messages, Avahi sees its
   session, and it stops when told. The file says `Enabled=no`: the
   package's programs and tests still start the runtime by hand on the
   same ports. It still reaches CAMD as a client, prints under the name
   `MIDIHub`, and reads `ENV:MidiHub/Network`: all three go with the next
   cuts and with AM16.
2. *The stub and its link.* Done on 2026-10-12, in the shape the project
   owner chose that day: the driver owns the adapters. `DEVS:Midi/applemidi`
   (`ports/aros/applemidi/applemididriver.c`) has sixteen ports and no
   network code; at `Init()` it makes an adapter for each and puts them on
   the public semaphore list as `applemidi.driver`. The service
   (`ports/aros/midihub/adapters.c`) finds them there, now or whenever the
   driver appears, attaches only to the layout it was built with
   (`APPLEMIDI_ADAPTERS_VERSION`), takes what CAMD clients sent from a
   port's ring and hands CAMD what arrived, under the lock the driver
   closes the port with. It no longer opens `camd.library`. What is sent
   to a port while no service runs is dropped. The driver is installed by
   the build, not written to disk by a program as `btmidi`'s is. Same
   test, on the driver's ports: `applemidi.in.0` receives what the host
   sent and `applemidi.out.0` reaches the host. Every session shares port
   0. Read and not run: a driver of another layout, the service stopping
   while CAMD holds a port open with bytes waiting, and CAMD expunging
   the driver under a running service (the adapters are then left where
   they are, off the list). Not built or run for m68k.
3. *Peers to ports.* Done on 2026-10-12. A peer that connects is given a
   port by `struct applemidi_ports` (`network/rtpmidi/ports.c`, portable):
   the one kept for the name its invitation carries, or the first that is
   free and kept for no one; a peer for whom there is none is told
   goodbye. A port is kept by a line `port.<n> = <name>` in the service's
   configuration, now `ENV:AppleMIDI`. What arrives from a peer goes to
   its port, and what a CAMD client sends to a port goes to its peer
   alone. `apple_ports` in `tests/protocol.c`; on hosted AROS, two
   node-rtpmidi peers, the named one on port 3 and the other on port 0,
   each port receiving its own peer's messages once, and a scale sent to
   `applemidi.out.3` reaching the named peer only. The program `MIDIHub`
   is unchanged: there every peer shares the clusters `MIDIHub In` and
   `MIDIHub Out`, and its tests pass as before.

**AM08 is done by its own measure**: a CAMD program opens a driver's port
and plays to a peer. What it leaves is AM24 to AM26 below, AM16 and AM20.
The time a message was meant for does not cross the driver (decision D4):
a received message is played at its time by the service, and one sent
carries the time the service took it from the port's ring.

## P2 — complete before an upstream proposal

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| AM14 | IPv4 only: `struct applemidi_peer` holds a `uint32_t` address. | An address type that holds either family. Waits for the IPv6 answer in AROS issue #1559; do not start before it. | — |
| AM15 | Eight peers, a constant. | A number from configuration with eight as the default, and a NO with a reason in the log when the table is full. | A test fills a table of two. |
| AM16 | Configuration is one text file with one peer: local port, peer address and port, session name. | Several sessions to invite, the accept rule (AM07), latency (RTP-MIDI backlog R17), a port's binding to a peer (D2). Preferences reads and writes it, and the runtime takes a change without a restart. | The Preferences page edits all of it. |
| AM24 | The service is switched off in its file (`Enabled=no`), because the package's programs and their tests start the same runtime by hand on the same ports. Two ways to CAMD exist side by side: the driver, and the program's clusters. | Move the remaining hosted tests to the service and the driver's ports, make the program a client of nothing (or remove it), and switch the service on. | One runtime in a booted system, started by the stack. |
| AM25 | A CAMD client sees `applemidi.in.3`, not the peer's name, and every port is there whether a peer is or not. A SysEx message from a second port while one is being gathered from a first is left out. | Names and presence wait for the driver-side block in CAMD (its backlog, B30 and B31); a buffer per port for SysEx. | — |
| AM17 | A peer's name is cut at 63 bytes wherever that falls, which can split a UTF-8 character; our own name is refused over 63. | Cut at a character boundary. | A host test with a name of multi-byte characters. |
| AM18 | A packet with a command we do not know (Apple's bit-rate limit, `RL`, is one) fails to decode and is then tried as RTP, which also fails. Nothing is lost, and nothing is counted. | Recognise the command as known and ignored. | A host test; a counter. |
| AM19 | The decoders are tested with packets we wrote. | A fuzz target for `applemidi_decode()` and for the session and peer state machines fed with arbitrary packet sequences, under the sanitizers; captures from AM06 added to the tests. | An hour without a finding; at least one capture per peer. |

## P3 — polish

| # | Limit or defect | What to do |
|---|---|---|
| AM20 | The program's name, its `--probe-note` and `--probe-sysex` arguments and its messages are those of a diagnostic. The service, built from the same source, prints its reports under the name `MIDIHub`. | The probes become a test program; the service's reports name no program and go to counters (RTP-MIDI backlog R18). |
| AM26 | Read and never run: a driver in memory of another layout than the service (`APPLEMIDI_ADAPTERS_VERSION`); the service stopping while CAMD holds a port open with bytes waiting; CAMD expunging the driver under a running service. Never built for m68k. | A test of each on hosted AROS; a build in the m68k tree. |
| AM21 | `applemidi_peers_receive()` sets the "left" event through two branches that end the same way. | Simplify when the function is next touched. |

## Decisions needed

| # | Question | Leaning, or what was decided |
|---|---|---|
| D1 | Who owns the sockets. A CAMD driver is loaded by `camd.library` and never unloaded, and each task needs its own `bsdsocket.library` base. Either the driver starts a process of its own on `Init()`, or a service under the AROSTCP service manager (as `mdnsd` is) owns the sessions and the driver in `DEVS:Midi` is a stub that attaches to it, the way `btmidi`'s stub attaches to the class. | **Decided on 2026-10-12 by the project owner: the service.** It follows the network going up and down, which a driver loaded once cannot, and it is the shape the reviewer has already accepted twice. |
| D2 | How peers map to ports. A fixed pool (`btmidi` has 16); a port bound to a named session by configuration, or the first free port to whoever connects. | **Decided on 2026-10-12 by the project owner:** bound by configuration where a name is configured, first free otherwise, so a route to "the iPad" survives a reconnection. Identity beyond that waits for CAMD backlog B29. |
| D3 | Whether one port can be several peers at once (what a Mac's session does: every participant hears every other). | One peer per port first. A shared port is a later option of the pool, not a second design. |
| D4 | What the driver boundary costs. A driver hands over bytes and is given bytes: the time a message was meant for cannot cross it (CAMD backlog B32). Either accept that for now (the runtime plays a received event at its due time, and a sent one carries the time it left CAMD), or hold the driver until CAMD has a timed path for drivers. | **Decided on 2026-10-12 by the project owner: accept it** and say so in the limits; holding the driver would put the whole transport behind an extension that has not been proposed to the maintainer yet. |

D1, D2 and D4 are taken; D3 is open and nothing waits for it. AM08, which
needed the three, has nothing in front of it.

## Not scheduled here

- The driver-side block in CAMD (B29 to B32 in
  [the CAMD backlog](camd-backlog.md)). This backlog must not depend on it.
- Anything Apple's implementation is not known to do: authentication,
  encryption, sessions over TCP.
