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
| R02 | `rtpmidi_sender_packet()` builds the packet for a message: the command, the journal if it fits, the entry in the history. A journal that does not fit is left out and counted in `journals_omitted`; the command goes. The program's send path is that one call. `sender_journal_limits` sends 400 messages with no feedback and loses none, and sends one into a buffer a byte short for the journal. Not done: the packet size is still the constant 1100 bytes, not the path's MTU, and a journal that does not fit is dropped whole rather than cut chapter by chapter; both are with R32 now. |
| R04 | A SysEx message too long for chapter X is counted and the history is kept; when chapter X is full, its oldest logs give way instead of everything sent before. A message other than SysEx that the journal cannot describe no longer clears the history either. `unjournalled` counts what no journal covers. The same test: a note sent before a 700-byte SysEx, before six small ones, and before two that pass the limit together is still in the journal afterwards, with the checkpoint where it was. |
| R27 | Found while closing R04: a SysEx message could be delivered twice. The receiver replayed every log of chapter X whose count differed from its own tally, so after any loss it repeated each SysEx still unacknowledged in the journal except the last. It now takes the count as RFC 6295 appendix B.5.3 defines it, a number modulo 256: a log is replayed when its number is ahead of the last the receiver has, and the receiver takes up the count from the chapter's final log. A SysEx too long for chapter X has no number at either end. `receiver_recovery` covers three received and one unrelated loss, a long one lost for good, and six lost in a row followed by a seventh received; the old receiver fails the first. This agreement on what is too long holds between two of us; what another implementation counts is to be seen (AppleMIDI backlog AM06). |
| R17 | `MIDIHub --latency <ms>` adds that much to the time every received event is due; an event still past its time is played at once and counted, and the count is printed when the session ends ("N of M timed events came after their time"). `run_latency_cases` in `tests/network_loopback.py`: a scripted peer stamps twelve notes 50 ms apart and sends each up to 20 ms late, unevenly. With no latency all twelve are late and their spacing is the network's, off by up to 11 ms; with 40 ms none is late and no interval is more than half a millisecond off. The unevenness is made by the test's own sender, not by `tc netem`, and the latency is one number for the program, not one per session. On the host only: what AROS's timer does to those half milliseconds is not measured. |
| R28 | The MIDI 1.0 rules were read against the Complete MIDI 1.0 Detailed Specification 96.1 (version 4.2.1) in the four places that hold them: the command reader of `rtpmidi.c`, the state tracking of `sender.c` and `receiver.c`, the byte-stream side of `src/ump.c`, and the BLE MIDI codec. The places now cite the page. **Agreed as it was:** the length of a message by its status (tables II, V, VI); running status for Voice and Mode messages only, stopped by any other status and untouched by System Real Time (pages 5, 30, A-4); Real Time anywhere, also inside SysEx (page 30); data without a status ignored (page 6); 120 and 123 to 127 turning notes off, 121 and 122 not (pages 20, 24, table IV); parameter numbers by 101/100 and 99/98 with Data Entry, Increment and Decrement (page 17). **Differed, corrected, in `src/ump.c`:** a second F0 inside a SysEx dropped the first without ending it, where page 29 has any status but Real Time end it; an EOX with no SysEx before it did not stop running status. `test_midi1_to_ump` has both, and a message cut short by a new status. **Differed, left:** R29, R30. **Not ours to follow:** what a receiver does on Reset All Controllers and on the mode messages beyond notes is in RFC 6295's chapters, which the state tracking follows; Omni, Mono and Poly as modes of an instrument concern a sound generator, not a transport. BLE MIDI departs from page 5 on purpose: its own text keeps running status across System Common. |
| R31 | The two rare failures of `tests/network_loopback.py` seen on 2026-10-12 are this machine's, not the program's or the test's. The two cases were run 150 times with the program's output kept: 14 failed, every 33.5 seconds by the clock, and the log of each shows about 1.3 seconds in which nothing moved. An idle process that only sleeps ten milliseconds at a time is held for 1.3 seconds at the same period on this WSL host. A run that crosses such a moment fails; nothing was changed for it. |
| R11 | Several commands leave in one packet, each at its time. `rtpmidi_sender_packet_list()` takes messages with their times and writes the first at the packet's timestamp and each later one with a delta time (RFC 6295 section 3.1); it takes as many as the packet holds and says how many, a time that goes backwards is sent as the one before it, and every message goes into the history under the packet's number. `MIDIHub` gathers what CAMD hands over in one turn of its loop, up to 64 messages, and sends it so to each peer; a SysEx message still has packets of its own, after what came before it. `sender_packet_list` in `tests/protocol.c`: a chord is one packet and the receiver reads the sender's times; its journal, were the packet lost, has the three notes; 400 messages leave in as many packets as hold them, none lost or doubled. On hosted AROS the scale a CAMD program plays still reaches two node-rtpmidi peers (`tools/applemidi-node-hosted.sh`); that it left in fewer packets was looked at later and is not so: see R32. The time of a message is the time the program took it from CAMD, since a CAMD client is given none. Left for R32: the packet's size, the journal by chapters, running status. |
| R29 | An undefined status no longer costs a packet, and the three codecs treat it alike: it is left out and what is around it is taken. The command reader steps over F9 and FD as single octets and over F4 and F5 with their data up to the F7 that closes them, the two forms RFC 6295 section 3.2 gives, keeps their delta times and counts them in `undefined`; one with data and nothing to close it still ends the packet, since nothing says where it stops. The BLE MIDI codec takes F9 and FD off the stream in both directions (BLE MIDI backlog L18); the UMP translator already left all four out. `rtp_command_reader` in `tests/protocol.c`, `undefined_statuses` in `tests/blemidi_class.c`. The count is in the reader and nobody reads it yet (R18). |
| R14 | A SysEx message of up to 32 KiB is taken in either direction (`RTPMIDI_SYSEX_MAX`, which was 4096: less than a DX7's voice bank of 4104 bytes). What is too long or broken is counted per session and reported when it ends. `sysex_dump` in `tests/protocol.c` gathers 16 KiB from seventeen segments and refuses one past the limit, taking the next; in `tests/network_loopback.py` one instance of the program sends another a dump of 16384 bytes, which arrives whole. The limit is a constant, not a setting; a session is lent a buffer of it while it has a peer (R22). Not run: a dump through CAMD on AROS in either direction, and against another implementation. The Network MIDI 2.0 path has the same limit for what it gathers from SysEx7 packets. |
| R20 | The `P` flag needs nothing done, and the code says why. RFC 6295 section 3.2: it tells that the first channel command's status was not in the sender's own source stream, which had it by running status; the list has the status regardless ("the P bit describes the MIDI source data stream, not the MIDI list encoding"). A receiver that hands on whole messages has no use for it, and we send it clear because what we send reached us as whole messages. Closed by reading, on 2026-10-12. |
| R30 | The journal's state treats a parameter number with only its MSB sent as no parameter until the LSB comes, and that is right. RFC 6295 appendix A.4 calls such a number pending, carries it in chapter M's PENDING field, and begins a transaction only at the LSB; MIDI 1.0's rule that a receiver take either half alone is for an instrument, not for the journal. Closed by reading, on 2026-10-12; the code cites it. |
| R25 | After a loss that no journal covers, the notes the peer has on are ended: the end of one may have been in what was lost, and it would sound for ever. Its controllers are left as they are, where the item had asked for the resets of a session's end too: nothing is known to be wrong with them, and a peer that sends no journal, as node-rtpmidi and Bouchez's class do, loses a packet now and then. `run_uncovered_gap_case` in `tests/network_loopback.py`: two notes on, three packets lost, and both notes are ended before the next packet's own note, the controller untouched. |
| R19 | Receiver feedback goes out once a second while packets arrive (`RTPMIDI_FEEDBACK_INTERVAL`), at once for the first packet, after a gap and when a packet already had comes again, and once more after a stream has stopped; it went out for every packet. `run_feedback_rate_case` in `tests/network_loopback.py`: a hundred packets in a second and a half get between two and five RS, the last saying how far all of it came, and a gap of three is answered within the moment. Both interoperability scripts and the hosted test pass with it. A sender keeps up to a second more of history than before, which is what its journal then carries. |

Found while doing it: `tests/network_loopback.py` failed about once in forty
runs, before the change as after, because a journal-only guard packet could
arrive before the answer to a clock exchange. The test now takes the guard
packet in either order (60 runs without a failure).

## P0 — wrong or unsafe as it is

Nothing open.

## P1 — before a first use with a real peer

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| R32 | Of what R11 left, one part was done on 2026-10-12: a channel command whose status is that of the channel command before it in the packet goes without its status (RFC 6295 section 3.2); `sender_packet_list` in `tests/protocol.c`. Both other implementations met read such a list, by their sources (node-rtpmidi's `MidiMessage.js`, Bouchez's `RTP_MIDI_Input.cpp`); neither was seen to, because nothing here sends them one: **on AROS the gathering of R11 does not happen in practice.** A chord of three notes put to CAMD at once left as three packets, by the service's own count (23 messages in 23 packets, `tools/applemidi-service-hosted.sh`): the service is woken by the first byte in a port's ring and sends what it finds, which is one message. Open: whether to wait a moment for more (a fraction of a millisecond buys fewer packets and costs that much delay on every note); a packet's size by the path's MTU instead of the constant 1100 bytes. **Not to do**, having thought it through: leaving out chapters of a journal that does not fit. A receiver takes a journal as covering everything since its checkpoint, so one with a chapter missing would have it believe a loss repaired that was not; a packet with no journal at all, which is what is sent now, cannot mislead it. | The moment's wait, if it is wanted; the interface's MTU from the stack. | A chord leaves AROS as one packet. |

## P2 — complete before an upstream proposal

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| R15 | IPv4 only, in the sockets and in `struct applemidi_peer` (`uint32_t address`). | An address type that holds either family. Waits for the IPv6 answer in AROS issue #1559; do not start before it. | — |
| R18 | Most of it was done on 2026-10-12: the service counts, for each port of its two drivers, the packets and messages in and out, packets lost, messages timed and late, SysEx messages dropped, journals left out and undefined statuses stepped over, and makes the counts public with the peer's name about once a second (`struct NetMidiStatus`, a public semaphore named `netmidi.status`). `C:NetMIDIStatus` prints them; on hosted AROS it shows four messages in from each of two peers and seventeen out on the port a scale and a SysEx message were played to (`tools/applemidi-service-hosted.sh`). The service no longer writes a line for every packet. Left: Preferences does not read the counts; the Linux build of the program still prints every packet, and its tests read that text; recoveries from the journal are told in text only; and a reader that is waiting for the semaphore at the moment the service stops could read memory just freed, a window that is narrow and was not closed. | Preferences shows the counts; the journal's recoveries counted; the host tests read counts. | The loopback tests assert on counters, not on text. |
| R21 | Decoders are tested with packets we wrote. | A fuzz target for `rtpmidi_decode()`, the reader, each journal chapter and `applemidi_decode()`, run under the sanitizers; captures from real peers (AM06) added to the tests as they are made. | An hour of fuzzing without a finding; at least one capture per peer in `tests/`. |
| R22 | Big-endian and 16-bit `int` have not been run. Built on 2026-10-12 for amiga-m68k, with warnings as errors, in `~/aros-build-amiga-m68k`: the service `netmidid`, the two drivers and `NetMIDIStatus`. Measured there: the service took 882 KB of memory that is never given back, most of it buffers for SysEx messages of sessions that may never be; they are now lent while a session has a peer, 32 KiB each, and the drivers' rings are a quarter of what they were. It takes 358 KB, the queues of timed events and the journals' state of eight and eight sessions. Nothing of it was run on m68k: there is no emulator set up here for it. | Run `protocol-test` and the service on an m68k AROS; allocate a session's queue and journal state with the session. | They pass there. |

## P3 — polish

| # | Limit or defect | What to do |
|---|---|---|
| R23 | The sender always writes the default form of chapter C; the enhanced form is only decoded. | Nothing unless a peer needs it; note which peers send it (AM06). |
| R24 | Chapter X keeps four logs of 512 bytes; "other extended Chapter X history" in the README is not specified further. | Say exactly which tools of chapter X are and are not written, then decide. |

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
