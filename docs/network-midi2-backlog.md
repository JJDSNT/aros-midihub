# Network MIDI 2.0 backlog — from a tested codec to something that runs

**Scope:** `src/netmidi2.c` and `include/netmidi2/netmidi2.h`: the UDP
transport of the Universal MIDI Packet (MMA/AMEI M2-124-UM, version 1.0),
its sessions as Host and as Client, and what it would take to run on AROS.
The packets it carries are in [the UMP backlog](ump-backlog.md). It follows
AppleMIDI in the order of work
([network MIDI in AROS](network-midi-integration.md)): the driver, the
runtime and the discovery it needs are the ones
[the AppleMIDI backlog](applemidi-backlog.md) builds first.

**State this starts from** (read from the code on 2026-10-12):

- A codec for the command packets (signature, header, payload in words) and
  one session with one peer, in either role: invitation, accepted and
  pending replies, ping, bye with its reasons, session reset, NAK.
- UMP Data with a sequence number; the two previous commands are sent again
  in front of each new one as forward error correction, and the receiver
  delivers each once. Zero-length commands when idle; a ping to a quiet
  peer and a bye after three unanswered.
- Endpoint Discovery and Stream Configuration requests are answered inside
  the session; the endpoint declares the MIDI 1.0 protocol only.
- `tests/netmidi2.c` covers the codec and one session against itself.
- It ran nowhere until NM07, below, advertised nothing until NM08, and
  looked for nobody until NM22.
- Added in one commit on 2026-10-06, with two of the specification's packet
  examples already in its tests, and read against M2-124-UM 1.0 in full on
  2026-10-12: see NM01 under "Done". It met its first other implementation the same day: see NM10.

Priorities as in the other backlogs.

## Done

On 2026-10-12, with `tests/netmidi2.c` (also under the address and
undefined-behaviour sanitizers) and the whole host suite passing.

| # | What was done |
|---|---|
| NM01 | `src/netmidi2.c` was read against M2-124-UM 1.0, and each rule and constant now cites its section. **Agreed as it was:** the signature and the command header (5.2 to 5.4), every command code (table 8), the NAK and Bye reason codes it used (tables 25 and 27), the Invitation's layout and limits (table 10), 64 words to a UMP Data command (7.1), 1400 bytes to a packet (5.1.1), the first sequence number being zero, forward error correction with two repeats in the order sent (7.2.2), the first idle command within 300 ms (7.2.1), repeats between 300 ms and 2 s (6.2), a Bye answered in every state (6.16). The appendix's packet examples A.1.1 to A.1.4 are in the tests, byte for byte; the first two already were. **Differed:** the five items below, and these, corrected with them: an Invitation nobody answers has to end with Bye, "invitation canceled" (6.4), not in silence; a Retransmit Request we cannot serve is answered with NAK, "not supported" (7.2.3), not with Retransmit Error; an Invitation Reply: Pending out of place gets Bye or NAK (6.6); an Invitation with Authentication at a Host that never asked for one gets Bye 0x41, not 0x45; a Client asked to authenticate withdraws with Bye 0x80, because 0x45 is a Host's reason; the commands of a session, outside one, get Bye, "not established" (6.11, 6.12, 7.2.3, 7.2.4). **Not read against code**, there being none: authentication (6.7 to 6.10), discovery (section 4), devices in both roles (8). |
| NM02 | A session is with one address and port (3.2, 6.4). `netmidi2_receive_from()` takes where a packet came from; a packet from elsewhere leaves the session as it is. A second Client's Invitation used to be "accepted again" into the running session, overwriting the peer's name, and a Bye from anyone ended it. Now the Invitation gets Bye, "too many sessions"; UMP Data and the session's commands get Bye, "not established"; a Bye and a Ping are answered and nothing more; and none of it counts as the peer being alive. |
| NM03 | A UMP Data command and its repeats go into a packet together or not at all: when the new command does not fit, what was already written is taken back. |
| NM04 | A loss longer than the repeats cover is counted in `gaps`, which a caller compares before and after to know that something may hang. Nothing more is done by itself: we ask for no retransmission (NM13), and whether to reset or end the session after many losses is the caller's choice, with `netmidi2_reset()` and `netmidi2_bye()`. |
| NM05 | A reset is a new beginning for both directions (6.11): sequence numbers at zero, the repeats flushed, `resets` counted for the caller. It can be asked for (`netmidi2_reset()`): no UMP Data goes out until the reply, the command is repeated, and a reset never answered ends the session with Bye, "timeout". A reply to a reset never asked for is answered by asking for one (6.12). |
| NM16 | The specification lets a Ping Id be any value (6.13), and a reply from another address no longer reaches the session (NM02). Nothing left to do. |
| NM20 | The limits cite their sections: 64 words (7.1), 1400 bytes (5.1.1). The 64 commands a packet is parsed into is ours, and more than a 1400-byte packet can hold of anything but empty commands. |
| NM06 | `struct netmidi2_table`: eight sessions on one port, each found by the address and port its packets come from, as Host for those who invite and as Client for those we invite. One more Client than there is room for gets Bye, "too many sessions"; a packet from someone with no session is answered as an idle Host would and takes no slot. `test_table` in `tests/netmidi2.c`. |
| NM07 | The path runs. `tests/interop/netmidi2_peer.c` is an endpoint on a UDP socket for the host; `tools/netmidi2-host.sh` runs a Host and a Client, then a Host and two Clients at once, each sending a note, a controller and a SysEx message as MIDI 1.0 bytes and printing what the other end's became. On AROS, `MIDIHub --midi2 <port>` opens a third socket beside AppleMIDI's two: UMPs received are translated for the cluster `MIDIHub In`, what a CAMD program sends to `MIDIHub Out` goes out as UMPs to every established session, and a session that ends, is reset or loses data is followed by All Notes Off on every channel. `tools/netmidi2-hosted.sh` has the host as Client of hosted AROS over the veth pair, with a CAMD program at the AROS end of each direction. Both ends are our own code. |
| NM10 | Met another implementation: Benoit Bouchez's NetUMP class (commit f74b0b8, MIT, the engine of his UMP-OX), built on this host around `tests/interop/netump_peer.cpp`. `tools/interop-bouchez-host.sh`: as Client of our Host and as Host of our Client, sessions open with names and product ids exchanged, UMPs cross both ways word for word (three MIDI 1.0 channel voice messages and a SysEx7 pair), and Bye is understood in each direction with its reason. A twelve-second capture showed no NAK from either side: it takes our idle commands and our repeats, and answers our ping. Nothing had to change in our code. Not shown: loss and its repair, a reset, more than one session, MIDI 2.0 channel voice, and UMP-OX itself, which is released for Windows and macOS only and has no source. Not run on hosted AROS. |
| NM08 | A Host is advertised. `MIDIHub --midi2 <port>` registers `_midi2._udp` on that port through the system mDNS service, with a text record of `UMPEndpointName` and `ProductInstanceId`, the two strings its Invitation Reply gives (section 4.4), and a time to live of 60 seconds for the service's records (4.6). The instance is named as the endpoint is; the specification would rather have a name not meant for display, built from the product id (4.2). `tools/netmidi2-hosted.sh`: the host's Avahi finds it on the port, with both strings, the name's space kept. Nothing looks for Hosts, because the program has no way to be a Client yet: NM22. |
| NM09 | A message is played at the time its sender meant. An endpoint says in Endpoint Info that it takes JR Timestamps when its caller uses them, and agrees to a Stream Configuration Request for them; it sends none, and answers a request that it send them with what it does (UMP specification 7.1.6). `struct ump_jr_clock` follows the sender's clock from its JR Clock messages: of the last eight, the one that arrived soonest after its own time is the measure, and a second of silence starts the count anew (7.2.2.1). `MIDIHub` holds each message that came with a timestamp until that time plus `--latency`, plays at once what is already late and counts it, and plays at once what has no timestamp or comes before any clock (7.2.2.3). `test_jr_clock` in `tests/ump.c`, the request in `tests/netmidi2.c`, and `tools/netmidi2-host.sh`, case "timed": a Client sends twelve notes meant 50 ms apart, each up to 20 ms late; with no latency all twelve are late and the spacing is off by up to 9.8 ms, with 40 ms none is late and no interval is off by more than half a millisecond. On the host only, and against our own peer: the NetUMP class sends no timestamps. One queue serves all sessions, and what it still holds when any session ends is dropped. The unevenness is made by the test's sender, not by `tc netem`. |
| NM11 | A Network MIDI 2.0 peer has a port of a driver of its own, `DEVS:Midi/networkmidi2` (decision D1, taken on 2026-10-12: two drivers over one service). The driver is the source of `applemidi`'s built a second time (`ports/aros/netmidi/netmididriver.c`), sixteen ports and no network code; the one service, now `netmidid`, attaches to both and numbers their ports through. A session that is established is given a port by the rule AppleMIDI peers have, by its endpoint's name (`networkmidi2.port.<n> = <name>`) or the first free; what it sends reaches its port alone, what a CAMD client sends to the port goes to it alone, and each session holds its timed messages in a queue of its own. Network MIDI 2.0 is switched on by `midi2_port` in `ENV:NetMIDI`. `tools/netmidi2-hosted.sh` runs both directions on `networkmidi2.in.0` and `networkmidi2.out.0`, with the stack starting the service; the program `MIDIHub` is no longer built for AROS. Still MIDI 1.0 at the port: native UMP to a CAMD client is CAMD backlog B32. Not run: two sessions on two ports (the rule is the one tested for AppleMIDI, the path through it is not), and a session for which no port is left, which stays open and unheard. |
| NM22 | The runtime is a Client too. Its configuration names a Host by address (`midi2_peer_ip`, `midi2_peer_port`) or by the name of its endpoint (`midi2_peer_name`); for the second it browses `_midi2._udp` through the system mDNS service and takes the Host whose text record's `UMPEndpointName`, or whose instance name, is the one wanted. It invites the Host when it has no session with it, and again after five seconds, then twice as long each time up to a minute, as for an AppleMIDI peer wanted by name. `tools/netmidi2-host.sh`, case "client", by address on the host; `tools/netmidi2-client-hosted.sh` on hosted AROS: a Host advertised by Avahi under an instance name that is not its endpoint's is found by the endpoint's, invited, and messages cross both ways through `networkmidi2.in.0` and `networkmidi2.out.0`. One Host at a time. Not run: the Host going away and coming back, and a Host that asks for authentication (NM12), which ends our invitation. |
| NM21 | The Product Instance Id is the machine's own. The service makes one of sixty-four random bits the first time it needs it (`AROS-` and sixteen hexadecimal digits), writes it to `ENVARC:NetMIDI.id`, and uses what it finds there from then on; the same string is in the Invitation, the Invitation Reply, the Product Instance Id notification and the text record (section 4.4). On hosted AROS the id advertised, the one in the session and the one in the file are the same (`tools/netmidi2-hosted.sh`), and an id put in the file beforehand is the one used (`tools/netmidi2-client-hosted.sh`). Not shown: two machines, which would have two by the odds of sixty-four bits and no more than that. The Linux build of the program keeps the constant `aros-midihub`. |
| NM17 | The appendix's examples are all in the tests: the four packets of A.1, and since 2026-10-12 figure 16 (five packets with two repeats, and nothing lost when the second and third are dropped) and figure 18 (going idle with a new Zero Length command in each packet), as sequences in `test_appendix_fec`. Our sender goes idle as figure 18 draws; figure 17's way, one Zero Length command repeated, is the other, and a receiver that goes by sequence numbers takes both. `tests/fuzz.c` has two tables talk, with a third of the packets changed on their way, stream messages and requests for retransmission among them: no finding (RTP-MIDI backlog R21). No captures: NetUMP's packets were looked at with tcpdump once and not kept. |

## P0 — wrong or unsafe as it is

Nothing open.

## P1 — before a first use with a real peer

| # | Limit or defect | What to do | Done when |
|---|---|---|---|

## P2 — complete before an upstream proposal

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| NM12 | No authentication. It is optional (6.7): a Host may ask for it only of a Client that says it can, and we say we cannot. A Host that insists ends our invitation with Bye 0x45. | Shared-secret and user authentication (6.7 to 6.10), behind a setting. Needs a SHA-256, which AROS has to provide or this code has to carry: a question for the maintainers. | Host tests of both forms. |
| NM13 | Half of retransmission was done on 2026-10-12: a peer that asks is served. A session keeps the last 32 UMP Data commands it sent, of up to eight words each, and answers a Retransmit Request with those from the one asked for to the newest, as they were (7.2.3); for one it no longer has, or one too long to have been kept, it sends a Retransmit Error naming it (7.2.4, table 32), and what follows is still sent. `test_appendix_fec` in `tests/netmidi2.c`. The other half is not done: we never ask. After a gap that the repeats do not cover we count it and go on with what came, as before. | Ask: hold what comes after a gap for ten milliseconds, request the missing commands, and only then count them lost. That delays delivery by design and needs the order of delivery kept. | A host test recovers a loss of five commands. |
| NM14 | The MIDI 2.0 protocol is never agreed: a Stream Configuration Request for it is answered with MIDI 1.0. | After the upward translation exists (UMP backlog UP11) and a client could use it (decision D1 there). | — |
| NM15 | IPv4 or IPv6 is the caller's business, and there is no caller. Section 3.1: every device shall have an IPv4 address and should support IPv6 as well. | IPv4 first, then by the answer on IPv6 in AROS issue #1559. | — |
| NM18 | Big-endian has not been looked at. | Run `netmidi2-test` built for m68k in the cross tree. | It passes there. |
| NM23 | Upstream advertises only the AppleMIDI session (#1743). The `_midi2._udp` advertisement (`UMPEndpointName` and `ProductInstanceId` in TXT, 60 s TTL; M2-124-UM 4.4, 4.6) is in `transport_midi2.c` here under `NETMIDI_DISCOVERY`, but the cut left it out, and with it `netmidi_discovery_add_text()` and `netmidi_discovery_item()`, which the #1743 review asked to drop while unused. | After #1743 merges: a pull request that brings back the two helpers with the TXT and TTL fields (as in `ports/aros/midihub/discovery.c`) and the registration in `midi2_open()`. | A browse from Avahi or macOS finds the AROS Network MIDI 2.0 Host with its endpoint name. |

## P3 — polish

| # | Limit or defect | What to do |
|---|---|---|
| NM19 | The sources are in `src/`, beside MIDIHub's own programs. | Move them to `network/netmidi2/`, as RTP-MIDI's went to `network/rtpmidi/`. |

## Decisions needed

| # | Question | Leaning |
|---|---|---|
| D1 | One driver or two. A Network MIDI 2.0 peer could take a port of the driver that AppleMIDI sessions use, or have a driver of its own (`networkmidi2`, as [the target architecture](aros-network-midi-target-architecture.md) names it). | **Decided on 2026-10-12 by the project owner: two drivers over one runtime.** A user chooses a peer by what it is, and the two protocols share sockets handling, discovery and configuration but not names. |
| D2 | Whether Network MIDI 2.0 stays an option of the program `MIDIHub` (`--midi2`), off unless asked for, until the driver exists. | Settled by NM11: on AROS it is the service's, switched on by `midi2_port` in its configuration. The option remains in the Linux build of the program, which the host tests run. |

## Not scheduled here

- Native UMP to CAMD clients through a driver: CAMD backlog B32.
- MIDI-CI over this transport.
- Anything the specification marks optional and no peer has been seen to
  use; to be listed when NM01 is done.
