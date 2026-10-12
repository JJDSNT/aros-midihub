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
- Added in one commit on 2026-10-06. **There is no record that it was
  compared with the text of M2-124-UM**, and it has met no other
  implementation.

Priorities as in the other backlogs.

## Done

Nothing yet under this backlog.

## P0 — wrong or unsafe as it is

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| NM01 | Nothing was checked against the specification: the command codes, the reason codes, the meaning of the two command-specific bytes, the times (first idle command after 100 ms, a ping after 5 s of silence, ten retries a second apart) and the limits (64 words in a command, 1400 bytes in a packet). | Obtain M2-124-UM 1.0 and go through the file against it, writing the section beside each constant and each state change. | Every constant cites its section; the differences found are listed here as items. |
| NM02 | The session does not know who its peer is. `netmidi2_receive()` takes any packet with the signature and treats it as the peer's: an Invitation from a second client is "accepted again" into the running session, a Bye from anyone ends it, and any packet at all resets the silence timer. | The caller gives the source with each packet; the session keeps its peer's and answers a stranger with Bye, reason "too many sessions" for an invitation. Or a table of sessions as AppleMIDI has (NM06), which then owns this. | Host tests: an invitation, a bye and UMP data from a second address leave the first session as it was. |
| NM03 | When a reply does not fit the packet being written, the commands already added for error correction stay in it and the function returns an error. The caller holds a packet that is neither what it asked for nor empty. | Check the room first, or cut the writer back to where it was. | A host test with a writer too small by one word. |
| NM04 | A loss longer than the two repeats is counted in `rx_lost` and nothing else happens: no retransmit request (we answer one with an error and never send one), no session reset, no bye for "too many missing". What was playing keeps playing. | Tell the caller there is a gap, so it can release notes; then what the specification asks for (NM01), at least the reset. | A host test loses four commands in a row and the sink is told. |
| NM05 | A Session Reset from the peer clears only what we expect to receive. Whether the sequence we send must start again too is to be read in the specification. | Follow the text (NM01). | A host test of a reset in both directions. |

## P1 — before a first use with a real peer

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| NM06 | One session. A Host is one endpoint that several Clients connect to. | A table of sessions by address and port, as `applemidi_peers` is, with a limit and the "too many sessions" reply. | Two clients at once in a host test. |
| NM07 | No runtime. | A host program first, so the protocol can be tried on Linux before AROS: one UDP socket, the clock, a session table, UMP in and out through the translators. Then the same loop on AROS beside AppleMIDI's. | Two instances on the host exchange notes; then on hosted AROS over the veth pair. |
| NM08 | No discovery. A Host advertises `_midi2._udp` with the TXT keys `UMPEndpointName` and `ProductInstanceId`; a Client browses for it. | Through the system mDNS service, after AppleMIDI has done the same for its own name (AppleMIDI backlog AM09, AM10). Confirm the key names and the service name in the text (NM01). | `avahi-browse -r _midi2._udp` shows the endpoint and its TXT record. |
| NM09 | No time. A UMP is delivered when its packet arrives. The jitter-reduction timestamps that would say when it was meant are dropped by the translator (UMP backlog UP09). | Use them once UP09 hands them over, with the same latency setting as RTP-MIDI (its backlog, R17). | With 20 ms of jitter from `tc netem`, intervals between notes at the receiver are those of the sender. |
| NM10 | Never run against another implementation. | Find one that runs without hardware, as rtpmidid does for AppleMIDI. Candidates to check for availability, none confirmed: Windows MIDI Services' network transport, the MIDI Association's reference tools, an ALSA bridge on Linux. | A table here says which peer was tried, when, and what failed. |
| NM11 | A peer reaches CAMD only as MIDI 1.0: the endpoint declares the MIDI 1.0 protocol, and a `DEVS:Midi` driver is given and gives bytes. | Keep it for the first version and say so. A port per session in the driver that AppleMIDI builds, or a second driver: decision D1. | A CAMD program plays to a Network MIDI 2.0 peer. |

## P2 — complete before an upstream proposal

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| NM12 | No authentication. An invitation that carries it is answered with Bye, "no matching authentication method"; a Host that requires it ends our attempt. | Shared-secret and user authentication as the specification defines them, behind a setting. Needs a SHA-256, which AROS has to provide or this code has to carry: a question for the maintainers. | Host tests of both forms, with the specification's examples if it has them. |
| NM13 | Retransmission is refused. | Keep a longer history than the two error-correction repeats and answer a Retransmit Request from it; ask for one on a gap before giving up (NM04). | A host test recovers a loss of five commands. |
| NM14 | The MIDI 2.0 protocol is never agreed: a Stream Configuration Request for it is answered with MIDI 1.0. | After the upward translation exists (UMP backlog UP11) and a client could use it (decision D1 there). | — |
| NM15 | IPv4 or IPv6 is the caller's business, and there is no caller. What the specification requires of each is to be read (NM01). | Follow the answer on IPv6 in AROS issue #1559. | — |
| NM16 | The ping identifier is the time XORed with a constant; an Invitation Reply's and a Ping Reply's origin is not checked beyond that. | With NM02 the source is checked; then a random identifier. | Host tests of a reply with a wrong identifier and of one from a wrong source. |
| NM17 | The tests are one session talking to itself, with packets we wrote. | The specification's examples as vectors (NM01); a fuzz target for `netmidi2_parse()` and `netmidi2_receive()` under the sanitizers; captures from NM10. | The examples pass; an hour of fuzzing without a finding. |
| NM18 | Big-endian has not been looked at. | Run `netmidi2-test` built for m68k in the cross tree. | It passes there. |

## P3 — polish

| # | Limit or defect | What to do |
|---|---|---|
| NM19 | The sources are in `src/`, beside MIDIHub's own programs. | Move them to `network/netmidi2/`, as RTP-MIDI's went to `network/rtpmidi/`. |
| NM20 | Sixty-four commands in a packet and sixty-four words in a command are constants with no reason written beside them. | Derive them from the packet size, or cite the section (NM01). |

## Decisions needed

| # | Question | Leaning |
|---|---|---|
| D1 | One driver or two. A Network MIDI 2.0 peer could take a port of the driver that AppleMIDI sessions use, or have a driver of its own (`networkmidi2`, as [the target architecture](aros-network-midi-target-architecture.md) names it). | Two drivers over one runtime: a user chooses a peer by what it is, and the two protocols share sockets handling, discovery and configuration but not names. |
| D2 | When to start. Everything here is after AppleMIDI in the agreed order, but NM01 to NM05 are host work on portable code and need nothing from AROS. | NM01 first whenever the specification is at hand, because it may change the rest; the others after AppleMIDI's P0 and P1. |

## Not scheduled here

- Native UMP to CAMD clients through a driver: CAMD backlog B32.
- MIDI-CI over this transport.
- Anything the specification marks optional and no peer has been seen to
  use; to be listed when NM01 is done.
