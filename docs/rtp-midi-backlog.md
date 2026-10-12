# RTP-MIDI backlog — the payload and its recovery journal

**Scope:** the RTP-MIDI payload (RFC 6295): the command section, the
recovery journal, the sender's and the receiver's state, and the timing of
delivery. It is the code in `network/rtpmidi/` named `rtpmidi_*`. The
session protocol around it, the peers, discovery, the AROS runtime and the
driver are in [the AppleMIDI backlog](applemidi-backlog.md); items R05 to
R10, R12, R13, R16 and R26 moved there on 2026-10-12 and kept no number
here. Network MIDI 2.0 and UMP have
[their](network-midi2-backlog.md) [own](ump-backlog.md).

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
| R01 | `rtpmidi_decode()` reads the header by fields (RFC 3550 section 5.1): version 2, payload type 97, contributing sources and a header extension skipped by their lengths, padding removed, the marker bit accepted either way. On sending, the marker is set when the command section has commands and clear when it is empty, as RFC 6295 section 2.1 requires; our journal-only guard packet used to go out with it set. `rtp_header_variants` in `tests/protocol.c` decodes the same Note On in six forms and refuses nine broken ones. Seen on the wire the same day: node-rtpmidi sends the marker clear with commands in the packet, and before this change all of its packets were dropped (AppleMIDI backlog, "Peers met"). |
| R02 | `rtpmidi_sender_packet()` builds the packet for a message: the command, the journal if it fits, the entry in the history. A journal that does not fit is left out and counted in `journals_omitted`; the command goes. The program's send path is that one call. `sender_journal_limits` sends 400 messages with no feedback and loses none, and sends one into a buffer a byte short for the journal. Not done: the packet size is still the constant 1100 bytes, not the path's MTU, and a journal that does not fit is dropped whole rather than cut chapter by chapter; both are with R11 now. |
| R04 | A SysEx message too long for chapter X is counted and the history is kept; when chapter X is full, its oldest logs give way instead of everything sent before. A message other than SysEx that the journal cannot describe no longer clears the history either. `unjournalled` counts what no journal covers. The same test: a note sent before a 700-byte SysEx, before six small ones, and before two that pass the limit together is still in the journal afterwards, with the checkpoint where it was. |
| R27 | Found while closing R04: a SysEx message could be delivered twice. The receiver replayed every log of chapter X whose count differed from its own tally, so after any loss it repeated each SysEx still unacknowledged in the journal except the last. It now takes the count as RFC 6295 appendix B.5.3 defines it, a number modulo 256: a log is replayed when its number is ahead of the last the receiver has, and the receiver takes up the count from the chapter's final log. A SysEx too long for chapter X has no number at either end. `receiver_recovery` covers three received and one unrelated loss, a long one lost for good, and six lost in a row followed by a seventh received; the old receiver fails the first. This agreement on what is too long holds between two of us; what another implementation counts is to be seen (AppleMIDI backlog AM06). |

Found while doing it: `tests/network_loopback.py` failed about once in forty
runs, before the change as after, because a journal-only guard packet could
arrive before the answer to a clock exchange. The test now takes the guard
packet in either order (60 runs without a failure).

## P0 — wrong or unsafe as it is

Nothing open.

## P1 — before a first use with a real peer

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| R11 | One command per packet, each with the time of sending and a delta of zero. What CAMD hands over in a burst goes out as that many packets, and a time given by the sender is not carried. The packet is 1100 bytes by constant, and a journal that does not fit is left out whole. | Gather what is ready into one command section with delta times, up to a packet size taken from the path's MTU; when the journal does not fit, leave out chapters, oldest first, before leaving out all of it. | A host test: a chord leaves as one packet; the receiver's times are the sender's; a journal one chapter too long goes without that chapter. |
| R14 | A received SysEx message over 4096 bytes is dropped (`RTPMIDI_SYSEX_MAX`); the same limit refuses one going out. | Size by configuration up to what the CAMD side accepts; count what is dropped. | A 16 KiB dump crosses both ways. |

## P2 — complete before an upstream proposal

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| R15 | IPv4 only, in the sockets and in `struct applemidi_peer` (`uint32_t address`). | An address type that holds either family. Waits for the IPv6 answer in AROS issue #1559; do not start before it. | — |
| R17 | A timed event is delivered at the sender's time exactly, or at once when that time has passed. There is no added latency to absorb jitter, so on a wireless path most events are late and played at once, in bursts. | A latency setting per session, added to the due time; count late events. | With 20 ms of jitter from `tc netem`, intervals between notes at the receiver are those of the sender. |
| R18 | Nothing is counted. Losses, recoveries, late and dropped events appear only as lines on standard output. | Counters per peer, read by Preferences and by a shell command. Remove the per-packet `printf`. | The loopback tests assert on counters, not on text. |
| R19 | RS feedback is sent for every RTP packet received. | Once a second while packets arrive, and at once after a gap. | A host test counts RS packets for a run of 100. |
| R20 | The `P` flag (phantom status) is decoded and never used, and never set. | Read RFC 6295 section 3.2 again and either handle it or state why a receiver may ignore it. | A sentence here or a test. |
| R21 | Decoders are tested with packets we wrote. | A fuzz target for `rtpmidi_decode()`, the reader, each journal chapter and `applemidi_decode()`, run under the sanitizers; captures from real peers (AM06) added to the tests as they are made. | An hour of fuzzing without a finding; at least one capture per peer in `tests/`. |
| R22 | Big-endian and 16-bit `int` have not been looked at since the code was written. | Build and run `protocol-test` for m68k in the cross tree (`~/aros-build-amiga-m68k`). | It passes there. |
| R28 | The MIDI 1.0 rules this code leans on (which status bytes exist and how long their messages are, running status, what cancels it, System Real Time inside other messages, the channel mode messages and what they reset, RPN and NRPN) were written from memory. The Complete MIDI 1.0 Detailed Specification 96.1 is in `docs/` since 2026-10-12. | Read `rtpmidi.c`'s command reader, the sender's and the receiver's state tracking against it, and write the page beside each rule. The same reading serves `src/ump.c`'s byte-stream side and the BLE MIDI codec. | Each rule cites the text; differences are items here. |

## P3 — polish

| # | Limit or defect | What to do |
|---|---|---|
| R23 | The sender always writes the default form of chapter C; the enhanced form is only decoded. | Nothing unless a peer needs it; note which peers send it (AM06). |
| R24 | Chapter X keeps four logs of 512 bytes; "other extended Chapter X history" in the README is not specified further. | Say exactly which tools of chapter X are and are not written, then decide. |
| R25 | The receiver recovers at most what the journal's checkpoint covers and reports the rest as lost, without telling CAMD. | After an uncovered gap, release the notes and send the usual resets, as at the end of a session. |

## The way upstream

Not started, and not to be started before P0 here and the first interoperability run (AM06 in the AppleMIDI backlog) are done. The
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

None of its own. The decisions about the driver (who owns the sockets, how
peers map to ports, what the driver boundary costs in timing) are D1 to D4
in [the AppleMIDI backlog](applemidi-backlog.md), because the session layer
is what a driver is built around.

## Not scheduled here

- Network MIDI 2.0 and UMP over the network: `src/netmidi2.c`, after this
  transport works through a driver.
- The driver-side block in CAMD (B29 to B32 in
  [the CAMD backlog](camd-backlog.md)), which would give a peer's name and
  identity to a port. This backlog must not depend on it.
- RTP over TCP, RTCP, and session description by SDP (RFC 6295 sections 6
  and appendix C). No AppleMIDI peer uses them.
