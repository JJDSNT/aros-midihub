# UMP backlog — the packet format and its translation to MIDI 1.0

**Scope:** `src/ump.c` and `include/ump/ump.h`: the size of a Universal MIDI
Packet, scaling between resolutions, the translation of a MIDI 1.0 byte
stream to UMP and back, and the UMP Stream messages an endpoint answers
discovery with. It is system code: Network MIDI 2.0 uses it today, and USB
MIDI 2.0 and a UMP path for CAMD drivers would. What `camd.library` itself
checks in UMP is in [the CAMD backlog](camd-backlog.md) and its gates U02 to
U14; MIDI over the network is in
[the Network MIDI 2.0 backlog](network-midi2-backlog.md).

**State this starts from** (read from the code on 2026-10-12):

- Added in one commit on 2026-10-06 and read against *UMP Format and MIDI
  2.0 Protocol* (M2-104-UM 1.1.2) on 2026-10-12: see UP01 under "Done".
  None of it has met another implementation.
- MIDI 1.0 bytes to UMP: channel voice as type 2, system as type 1, SysEx as
  a series of SysEx7 packets, with running status and realtime bytes inside
  SysEx handled. It never produces MIDI 2.0 channel voice (type 4).
- UMP to MIDI 1.0 bytes, one group at a time: types 1 and 2 directly, SysEx7
  reassembled, type 4 translated down. Per-note and relative controllers,
  per-note pitch bend and per-note management have no MIDI 1.0 form and are
  dropped. Utility, SysEx8 and mixed data, and flex data are dropped too.
- Stream messages: Endpoint Info (version 1.1, no function blocks, static),
  Endpoint Name, Product Instance Id and Stream Configuration are written.
- `tests/ump.c` covers sizes, scaling, both translations and the stream
  messages, with values we chose.
- The only caller is `src/netmidi2.c`. Nothing on AROS runs it.

Priorities as in the other backlogs.

## Done

On 2026-10-12, with `tests/ump.c` (also under the address and
undefined-behaviour sanitizers) and the whole host suite passing:

| # | What was done |
|---|---|
| UP01 | `src/ump.c` was read against M2-104-UM 1.1.2, text and figures, and each rule now cites its section. **Agreed with the text as it was:** the packet sizes by type (2.1.4, table 4), scaling up and down (appendix D.1.3 and D.1.4, whose code ours matches line for line), the filter bits of Endpoint Discovery (figure 12), Endpoint Info (figure 13), the text notifications (figure 16), Stream Configuration (figure 18), the MIDI 2.0 controller, program change and channel pressure formats (figures 56, 64, 65), and the translation of controllers and program change to MIDI 1.0 (D.2.3 with figure 97, D.2.4). **Differed:** UP02 and UP04 below, and the lengths and characters of the text notifications, now checked (7.1.4, 7.1.5). **Not read**, because nothing here implements it: the translation upward (D.3), jitter-reduction timestamps (7.2.2), the function block messages (7.1.7 to 7.1.9), flex data, SysEx8. The Bit Scaling and Resolution specification, which appendix D names as the authority over its own text, was not at hand. |
| UP02 | Section 7.6, table 17: F4, F5, F9 and FD are reserved, and F0 and F7 are not system messages in a UMP. All six are refused in both directions. F9 and FD used to pass both ways, F4 and F5 one way. |
| UP03 | A SysEx message reaches the sink whole, once, as appendix D.2.7 puts it. The caller lends the translator a buffer (`ump_to_midi1_buffer()`); a message longer than it is dropped and counted, and the next one is taken. |
| UP04 | Section 7.7.1: inside a SysEx message of a group only its further pieces and System Real Time may come. Anything else of that group ends it, and what was gathered is dropped; a start inside a message does the same and begins anew; a piece without a start is dropped. Messages of other groups and groupless ones do not disturb it. |
| UP05 | `struct ump_dropped` in the translator's state counts what was left out, by reason: another group, no MIDI 1.0 form, a type not translated, a reserved status, a broken SysEx, a SysEx too long. |
| UP10 | Appendix D.2.3: a registered or assignable controller becomes exactly four MIDI 1.0 messages. The parameter stays selected, and the specification asks for nothing after them; the code is left as it was. |
| UP14 | Every 7-bit value survives the trip to 16 and to 32 bits and back, every 14-bit value the trip to 32 and back, and minimum, centre and maximum keep their places, as D.1.2 requires. The appendix's eight numerical examples are in the tests. |
| UP17 | A translated Note On velocity of zero becomes one: appendix D.2.1 says so in as many words. The note's attribute is not carried, and has no MIDI 1.0 form to carry it in. |

## P0 — wrong or unsafe as it is

Nothing open.

## P1 — before a first use with a real peer

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| UP06 | One group per translator; the others are counted and nothing more. An endpoint with several groups in use would need one translator each, and nothing says which groups a peer uses. | Give the caller the group of what was passed over, so a runtime can open a port for it. | A host test with two groups. |
| UP07 | Endpoint Info declares no function blocks. Section 7.1.2 allows that, and with none there is nothing to answer Function Block Discovery with (7.1.8), so this is no defect. A host that shows function blocks to its user shows nothing for us. | Declare one function block covering the groups in use and answer its discovery with Function Block Info and Name (7.1.7 to 7.1.9, not yet read against the code because there is none). | A host test of the exchange. |
| UP08 | Device Identity is asked for in Endpoint Discovery (filter bit 2) and not answered. Section 7.1.3 says an endpoint may answer, not that it shall. | Write the notification (figure 14), with the manufacturer and model identifiers from the caller. Which identifiers AROS may use is a question to answer first: a SysEx ID is assigned, not chosen. | The message is written; the question has an answer here. |
| UP09 | Utility messages are dropped whole. That includes jitter-reduction timestamps, which are the only time a UMP stream carries. | Read section 7.2.2, then decode JR Clock and JR Timestamp and hand the time to the sink with the message that follows. Depends on a caller that can use it (Network MIDI 2.0 backlog, NM09). | A host test; the time reaches the sink. |

## P2 — complete before an upstream proposal

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| UP11 | MIDI 1.0 is never translated up to MIDI 2.0 channel voice. An endpoint that has agreed the MIDI 2.0 protocol cannot be sent to. | Appendix D.3, not yet read against anything: scaling, and the gathering of bank select, RPN and NRPN sequences into single messages. | Host tests round the trip for every message type. |
| UP12 | `camd.library` has its own table of packet sizes and its own rules for which message may pass (CAMD backlog B17, B18), and so does this file. Two copies of the same facts will drift. Both can now be checked against the same text: CAMD's gates U02 to U14 are no longer waiting for it. | Decide where the one copy lives. A library cannot link a contrib static library, so the likely answer is a header of constants and inline functions under `compiler/include/` or beside CAMD's, used by both. A question for the maintainers. | One table in the tree. |
| UP13 | The specification's numerical examples are in the tests; its message examples are not, and nothing fuzzes the translators. | The worked examples of the figures as vectors, and a fuzz target for `ump_to_midi1()` and `ump_from_midi1_byte()` under the sanitizers. | The examples pass; an hour of fuzzing without a finding. |
| UP15 | Big-endian has not been looked at: words are in host order here and the callers convert. | Run `ump-test` built for m68k in the cross tree. | It passes there. |

## P3 — polish

| # | Limit or defect | What to do |
|---|---|---|
| UP16 | The sources are in `src/`, beside MIDIHub's own programs, though this is system code. | Move them when their home is decided (UP12): this file serves the network, USB and CAMD alike, so `network/` is not it. |

## Decisions needed

| # | Question | Leaning |
|---|---|---|
| D1 | Where UMP reaches a CAMD client. Today only through translation to MIDI 1.0 at a driver's edge. Native UMP exists in CAMD's provisional version 43 for endpoints a program publishes, not for drivers (CAMD backlog B32). | Translation at the edge until B32 is agreed with the maintainer; do not build a second way in. |
| D2 | Whether MIDI-CI (profiles, property exchange, protocol negotiation by SysEx) belongs here. | Not now. It is a layer above the packet format and needs a client that wants it. |

## Not scheduled here

- MIDI-CI, as above.
- MIDI Clip File and the delta clockstamp messages: a file format, not a
  transport.
- Mixed Data Set and SysEx8 beyond counting them as dropped: no MIDI 1.0
  form exists and no client here could take them.
