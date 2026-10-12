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
- **Nothing runs it.** No program on AROS or on the host opens a socket for
  it, advertises `_midi2._udp` or looks for it.
- Added in one commit on 2026-10-06, with two of the specification's packet
  examples already in its tests, and read against M2-124-UM 1.0 in full on
  2026-10-12: see NM01 under "Done". It has met no other implementation.

Priorities as in the other backlogs.

## Done

On 2026-10-12, with `tests/netmidi2.c` (also under the address and
undefined-behaviour sanitizers) and the whole host suite passing. Still
nothing runs this code outside its tests.

| # | What was done |
|---|---|
| NM01 | `src/netmidi2.c` was read against M2-124-UM 1.0, and each rule and constant now cites its section. **Agreed as it was:** the signature and the command header (5.2 to 5.4), every command code (table 8), the NAK and Bye reason codes it used (tables 25 and 27), the Invitation's layout and limits (table 10), 64 words to a UMP Data command (7.1), 1400 bytes to a packet (5.1.1), the first sequence number being zero, forward error correction with two repeats in the order sent (7.2.2), the first idle command within 300 ms (7.2.1), repeats between 300 ms and 2 s (6.2), a Bye answered in every state (6.16). The appendix's packet examples A.1.1 to A.1.4 are in the tests, byte for byte; the first two already were. **Differed:** the five items below, and these, corrected with them: an Invitation nobody answers has to end with Bye, "invitation canceled" (6.4), not in silence; a Retransmit Request we cannot serve is answered with NAK, "not supported" (7.2.3), not with Retransmit Error; an Invitation Reply: Pending out of place gets Bye or NAK (6.6); an Invitation with Authentication at a Host that never asked for one gets Bye 0x41, not 0x45; a Client asked to authenticate withdraws with Bye 0x80, because 0x45 is a Host's reason; the commands of a session, outside one, get Bye, "not established" (6.11, 6.12, 7.2.3, 7.2.4). **Not read against code**, there being none: authentication (6.7 to 6.10), discovery (section 4), devices in both roles (8). |
| NM02 | A session is with one address and port (3.2, 6.4). `netmidi2_receive_from()` takes where a packet came from; a packet from elsewhere leaves the session as it is. A second Client's Invitation used to be "accepted again" into the running session, overwriting the peer's name, and a Bye from anyone ended it. Now the Invitation gets Bye, "too many sessions"; UMP Data and the session's commands get Bye, "not established"; a Bye and a Ping are answered and nothing more; and none of it counts as the peer being alive. |
| NM03 | A UMP Data command and its repeats go into a packet together or not at all: when the new command does not fit, what was already written is taken back. |
| NM04 | A loss longer than the repeats cover is counted in `gaps`, which a caller compares before and after to know that something may hang. Nothing more is done by itself: we ask for no retransmission (NM13), and whether to reset or end the session after many losses is the caller's choice, with `netmidi2_reset()` and `netmidi2_bye()`. |
| NM05 | A reset is a new beginning for both directions (6.11): sequence numbers at zero, the repeats flushed, `resets` counted for the caller. It can be asked for (`netmidi2_reset()`): no UMP Data goes out until the reply, the command is repeated, and a reset never answered ends the session with Bye, "timeout". A reply to a reset never asked for is answered by asking for one (6.12). |
| NM16 | The specification lets a Ping Id be any value (6.13), and a reply from another address no longer reaches the session (NM02). Nothing left to do. |
| NM20 | The limits cite their sections: 64 words (7.1), 1400 bytes (5.1.1). The 64 commands a packet is parsed into is ours, and more than a 1400-byte packet can hold of anything but empty commands. |

## P0 — wrong or unsafe as it is

Nothing open.

## P1 — before a first use with a real peer

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| NM06 | One session. A Host is one endpoint that several Clients connect to, each known by its address and port (3.2). | A table of sessions by address and port, as `applemidi_peers` is, with a limit and the "too many sessions" reply, which a single session already gives. | Two clients at once in a host test. |
| NM07 | No runtime. | A host program first, so the protocol can be tried on Linux before AROS: one UDP socket, the clock, a session table, UMP in and out through the translators. Then the same loop on AROS beside AppleMIDI's. | Two instances on the host exchange notes; then on hosted AROS over the veth pair. |
| NM08 | No discovery. Section 4 has a Host advertise `_midi2._udp` with a TXT record holding `UMPEndpointName` and `ProductInstanceId`, the same two strings as in the Invitation Reply, and keep the records' time to live at a minute or less (4.6). A Client browses for it. | Through the system mDNS service, after AppleMIDI has done the same for its own name (AppleMIDI backlog AM09, AM10). The TXT value has spaces in it (mDNS backlog MD02), and the minute needs the service to take a time to live from its client. | `avahi-browse -r _midi2._udp` shows the endpoint and its TXT record. |
| NM09 | No time. A UMP is delivered when its packet arrives. The jitter-reduction timestamps that would say when it was meant are dropped by the translator (UMP backlog UP09). | Use them once UP09 hands them over, with the same latency setting as RTP-MIDI (its backlog, R17). | With 20 ms of jitter from `tc netem`, intervals between notes at the receiver are those of the sender. |
| NM10 | Never run against another implementation. | Find one that runs without hardware, as rtpmidid does for AppleMIDI. Candidates to check for availability, none confirmed: Windows MIDI Services' network transport, the MIDI Association's reference tools, an ALSA bridge on Linux. | A table here says which peer was tried, when, and what failed. |
| NM11 | A peer reaches CAMD only as MIDI 1.0: the endpoint declares the MIDI 1.0 protocol, and a `DEVS:Midi` driver is given and gives bytes. | Keep it for the first version and say so. A port per session in the driver that AppleMIDI builds, or a second driver: decision D1. | A CAMD program plays to a Network MIDI 2.0 peer. |

## P2 — complete before an upstream proposal

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| NM12 | No authentication. It is optional (6.7): a Host may ask for it only of a Client that says it can, and we say we cannot. A Host that insists ends our invitation with Bye 0x45. | Shared-secret and user authentication (6.7 to 6.10), behind a setting. Needs a SHA-256, which AROS has to provide or this code has to carry: a question for the maintainers. | Host tests of both forms. |
| NM13 | Retransmission is refused, with the NAK the specification gives for that (7.2.3); it calls the mechanism optional and recommended. We never ask for it either. | Keep a longer history than the two repeats and answer a Retransmit Request from it; ask for one, ten milliseconds after a gap, before counting it lost. | A host test recovers a loss of five commands. |
| NM14 | The MIDI 2.0 protocol is never agreed: a Stream Configuration Request for it is answered with MIDI 1.0. | After the upward translation exists (UMP backlog UP11) and a client could use it (decision D1 there). | — |
| NM15 | IPv4 or IPv6 is the caller's business, and there is no caller. Section 3.1: every device shall have an IPv4 address and should support IPv6 as well. | IPv4 first, then by the answer on IPv6 in AROS issue #1559. | — |
| NM17 | The appendix's four packet examples are in the tests; its examples of forward error correction and of going idle (A.2, A.3) are figures not yet turned into tests, and nothing fuzzes the parser. | Those two as sequences of packets; a fuzz target for `netmidi2_parse()` and `netmidi2_receive_from()` under the sanitizers; captures from NM10. | The examples pass; an hour of fuzzing without a finding. |
| NM18 | Big-endian has not been looked at. | Run `netmidi2-test` built for m68k in the cross tree. | It passes there. |

## P3 — polish

| # | Limit or defect | What to do |
|---|---|---|
| NM19 | The sources are in `src/`, beside MIDIHub's own programs. | Move them to `network/netmidi2/`, as RTP-MIDI's went to `network/rtpmidi/`. |

## Decisions needed

| # | Question | Leaning |
|---|---|---|
| D1 | One driver or two. A Network MIDI 2.0 peer could take a port of the driver that AppleMIDI sessions use, or have a driver of its own (`networkmidi2`, as [the target architecture](aros-network-midi-target-architecture.md) names it). | Two drivers over one runtime: a user chooses a peer by what it is, and the two protocols share sockets handling, discovery and configuration but not names. |
| D2 | When to start the runtime. NM01 to NM05 are done; what is left of P1 begins with the table of sessions (NM06) and a program on the host (NM07), neither of which waits for anything. | After the AppleMIDI driver's decisions are taken, or now if they are slow: nothing here depends on them until NM11. |

## Not scheduled here

- Native UMP to CAMD clients through a driver: CAMD backlog B32.
- MIDI-CI over this transport.
- Anything the specification marks optional and no peer has been seen to
  use; to be listed when NM01 is done.
