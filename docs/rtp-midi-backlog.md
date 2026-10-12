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
- It advertised `_apple-midi._udp` with an embedded responder of its own,
  since replaced by the system service (AppleMIDI backlog, AM09), and it
  never browses for peers.
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
| R01 | `rtpmidi_decode()` reads the header by fields (RFC 3550 section 5.1): version 2, payload type 97, contributing sources and a header extension skipped by their lengths, padding removed, the marker bit accepted either way. On sending, the marker was first made to follow RFC 6295 section 2.1 (set with commands, clear without) and then, the same day, left clear always: that is what AppleMIDI implementations do and what some require ([the evidence](applemidi-practice-and-specification.md)). `rtp_header_variants` in `tests/protocol.c` decodes the same Note On in six forms and refuses nine broken ones. Seen on the wire the same day: node-rtpmidi sends the marker clear with commands in the packet, and before this change all of its packets were dropped (AppleMIDI backlog, "Peers met"). |
| R02 | `rtpmidi_sender_packet()` builds the packet for a message: the command, the journal if it fits, the entry in the history. A journal that does not fit is left out and counted in `journals_omitted`; the command goes. The program's send path is that one call. `sender_journal_limits` sends 400 messages with no feedback and loses none, and sends one into a buffer a byte short for the journal. Not done: the packet size is still the constant 1100 bytes, not the path's MTU, and a journal that does not fit is dropped whole rather than cut chapter by chapter; both are with R11 now. |
| R04 | A SysEx message too long for chapter X is counted and the history is kept; when chapter X is full, its oldest logs give way instead of everything sent before. A message other than SysEx that the journal cannot describe no longer clears the history either. `unjournalled` counts what no journal covers. The same test: a note sent before a 700-byte SysEx, before six small ones, and before two that pass the limit together is still in the journal afterwards, with the checkpoint where it was. |
| R27 | Found while closing R04: a SysEx message could be delivered twice. The receiver replayed every log of chapter X whose count differed from its own tally, so after any loss it repeated each SysEx still unacknowledged in the journal except the last. It now takes the count as RFC 6295 appendix B.5.3 defines it, a number modulo 256: a log is replayed when its number is ahead of the last the receiver has, and the receiver takes up the count from the chapter's final log. A SysEx too long for chapter X has no number at either end. `receiver_recovery` covers three received and one unrelated loss, a long one lost for good, and six lost in a row followed by a seventh received; the old receiver fails the first. This agreement on what is too long holds between two of us; what another implementation counts is to be seen (AppleMIDI backlog AM06). |
| R17 | `MIDIHub --latency <ms>` adds that much to the time every received event is due; an event still past its time is played at once and counted, and the count is printed when the session ends ("N of M timed events came after their time"). `run_latency_cases` in `tests/network_loopback.py`: a scripted peer stamps twelve notes 50 ms apart and sends each up to 20 ms late, unevenly. With no latency all twelve are late and their spacing is the network's, off by up to 11 ms; with 40 ms none is late and no interval is more than half a millisecond off. The unevenness is made by the test's own sender, not by `tc netem`, and the latency is one number for the program, not one per session. On the host only: what AROS's timer does to those half milliseconds is not measured. |
| R28 | The MIDI 1.0 rules were read against the Complete MIDI 1.0 Detailed Specification 96.1 (version 4.2.1) in the four places that hold them: the command reader of `rtpmidi.c`, the state tracking of `sender.c` and `receiver.c`, the byte-stream side of `src/ump.c`, and the BLE MIDI codec. The places now cite the page. **Agreed as it was:** the length of a message by its status (tables II, V, VI); running status for Voice and Mode messages only, stopped by any other status and untouched by System Real Time (pages 5, 30, A-4); Real Time anywhere, also inside SysEx (page 30); data without a status ignored (page 6); 120 and 123 to 127 turning notes off, 121 and 122 not (pages 20, 24, table IV); parameter numbers by 101/100 and 99/98 with Data Entry, Increment and Decrement (page 17). **Differed, corrected, in `src/ump.c`:** a second F0 inside a SysEx dropped the first without ending it, where page 29 has any status but Real Time end it; an EOX with no SysEx before it did not stop running status. `test_midi1_to_ump` has both, and a message cut short by a new status. **Differed, left:** R29, R30. **Not ours to follow:** what a receiver does on Reset All Controllers and on the mode messages beyond notes is in RFC 6295's chapters, which the state tracking follows; Omni, Mono and Poly as modes of an instrument concern a sound generator, not a transport. BLE MIDI departs from page 5 on purpose: its own text keeps running status across System Common. |

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
| R18 | Nothing is counted. Losses, recoveries, late and dropped events appear only as lines on standard output. | Counters per peer, read by Preferences and by a shell command. Remove the per-packet `printf`. | The loopback tests assert on counters, not on text. |
| R19 | RS feedback is sent for every RTP packet received. | Once a second while packets arrive, and at once after a gap. | A host test counts RS packets for a run of 100. |
| R20 | The `P` flag (phantom status) is decoded and never used, and never set. | Read RFC 6295 section 3.2 again and either handle it or state why a receiver may ignore it. | A sentence here or a test. |
| R21 | Decoders are tested with packets we wrote. | A fuzz target for `rtpmidi_decode()`, the reader, each journal chapter and `applemidi_decode()`, run under the sanitizers; captures from real peers (AM06) added to the tests as they are made. | An hour of fuzzing without a finding; at least one capture per peer in `tests/`. |
| R22 | Big-endian and 16-bit `int` have not been looked at since the code was written. | Build and run `protocol-test` for m68k in the cross tree (`~/aros-build-amiga-m68k`). | It passes there. |
| R29 | An undefined status (F4, F5, F9, FD) in a packet makes the program drop the whole packet, with a line of text. MIDI 1.0 has a receiver ignore such a byte "without causing any problems" (page 6). RFC 6295 section 3.2 forbids them in a stream not set up for them, and says how they are coded in one that is: F9 and FD as System Real Time, F4 and F5 with their data closed by F7. The three codecs differ: the BLE MIDI decoder passes F9 and FD on and refuses a packet with F4 or F5; the UMP translator leaves all four out. | In the reader, step over both coded forms and count them; make the BLE decoder leave F9 and FD out. | A host test: the commands around an undefined one arrive. |

## P3 — polish

| # | Limit or defect | What to do |
|---|---|---|
| R23 | The sender always writes the default form of chapter C; the enhanced form is only decoded. | Nothing unless a peer needs it; note which peers send it (AM06). |
| R24 | Chapter X keeps four logs of 512 bytes; "other extended Chapter X history" in the README is not specified further. | Say exactly which tools of chapter X are and are not written, then decide. |
| R25 | The receiver recovers at most what the journal's checkpoint covers and reports the rest as lost, without telling CAMD. | After an uncovered gap, release the notes and send the usual resets, as at the end of a session. |
| R30 | A parameter number of which only the MSB (101 or 99) has come is no parameter to the sender's and the receiver's state until the LSB comes: a Data Entry in between is a plain controller to the journal. MIDI 1.0, page 17, rule 4, has a receiver take either half alone, keeping the other. | Read RFC 6295 appendix A.4 on what chapter M means by a pending parameter before changing anything: the journal's model may be the narrower one on purpose. |

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
