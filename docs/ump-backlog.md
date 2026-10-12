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

- Added in one commit on 2026-10-06. **There is no record that any of it
  was compared with the text** of *UMP Format and MIDI 2.0 Protocol*
  (M2-104-UM, 1.1.2 is the version the CAMD documents name), and the CAMD
  documents say outright that it was not. None of it has met another
  implementation.
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

Nothing yet under this backlog.

## P0 — wrong or unsafe as it is

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| UP01 | Nothing was checked against the specification. The table of packet sizes by type, the scaling of values, the bit positions in the stream messages and the rules of translation have no source written beside them. | Obtain M2-104-UM 1.1.2 and the default translation document that goes with it and go through the file against them, one section at a time, writing the section number beside each rule. This is the same work as CAMD's gates U02 to U14 and should be done once, for both. | Every table and rule in `ump.c` cites its section; the differences found are listed here as items. |
| UP02 | `ump_to_midi1()` passes the undefined system status bytes F4 and F5 on as one-byte MIDI 1.0 messages. | Drop them. | A host test. |
| UP03 | A SysEx7 message reaches the sink in pieces, one per packet: `F0` and up to six bytes, then more bytes, then the rest and `F7`. A sink that expects whole messages, as every one we have does, gets fragments. Nothing bounds the length either. | Decide the contract and say it in the header: either the translator gathers the message, with a limit and a count of what was dropped, or the sink is told start, continue and end. | The header states it; a host test with a 100-byte message through the path a caller uses. |
| UP04 | A continuation packet of another SysEx stream, a short message of the same group in the middle of one, and a start while one is open are not thought through: only "a continuation without its start" is refused. | Read the specification's rules for interleaving (UP01) and follow them; a start inside an open message ends the first one as broken. | Host tests for each case. |

## P1 — before a first use with a real peer

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| UP05 | What is dropped disappears: per-note messages, SysEx8, flex data, messages of other groups. A caller cannot tell a quiet stream from an unsupported one. | Counters in the translator's state, by reason. | The host tests assert on them. |
| UP06 | One group per translator; the others are ignored. An endpoint with several groups in use would need one translator each, and nothing says which groups a peer uses. | Keep it, but give the caller the group of what was ignored (UP05), so a runtime can open a port for it. | A host test with two groups. |
| UP07 | Endpoint Info declares no function blocks. Whether an endpoint may have none is to be read in the specification (UP01); a host that asks for function blocks gets no answer, because Function Block Discovery is not recognised. | Declare one function block covering the groups in use and answer its discovery with Function Block Info and Name. | A host test of the exchange; the values follow the cited sections. |
| UP08 | Device Identity is asked for in Endpoint Discovery (filter bit 2) and never answered. | Write the notification, with the manufacturer and model identifiers from the caller. Which identifiers AROS may use is a question to answer first: a SysEx ID is assigned, not chosen. | The message is written; the question has an answer here. |
| UP09 | Utility messages are dropped whole. That includes jitter-reduction timestamps, which are the only time a UMP stream carries. | Decode JR Clock and JR Timestamp and hand the time to the sink with the message that follows. Depends on a caller that can use it (Network MIDI 2.0 backlog, NM09). | A host test; the time reaches the sink. |
| UP10 | Translating a MIDI 2.0 registered or assignable controller down writes the selection and the data and leaves the parameter selected. A later Data Entry from another source then changes it. | Follow the default translation document for what comes after (UP01); if it says nothing, close with the null selection as the RTP-MIDI receiver does. | A host test. |

## P2 — complete before an upstream proposal

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| UP11 | MIDI 1.0 is never translated up to MIDI 2.0 channel voice. An endpoint that has agreed the MIDI 2.0 protocol cannot be sent to. | The upward translation of the default translation document: scaling, and the gathering of bank select, RPN and NRPN sequences into single messages. | Host tests round the trip for every message type. |
| UP12 | `camd.library` has its own table of packet sizes and its own rules for which message may pass (CAMD backlog B17, B18), and so does this file. Two copies of the same facts will drift. | Decide where the one copy lives. A library cannot link a contrib static library, so the likely answer is a header of constants and inline functions under `compiler/include/` or beside CAMD's, used by both. A question for the maintainers, to ask with UP01 done. | One table in the tree. |
| UP13 | The tests use values we chose. | The worked examples of the specification as test vectors (UP01), and a fuzz target for `ump_to_midi1()` and `ump_from_midi1_byte()` under the sanitizers. | The examples pass; an hour of fuzzing without a finding. |
| UP14 | `ump_scale_down()` shifts and `ump_scale_up()` repeats bits. Whether down-scaling should round, and whether the centre of a 14-bit pitch bend survives a trip up and down, is not tested at the edges. | Property tests: every 7-bit and 14-bit value survives up and down; minimum, centre and maximum map to minimum, centre and maximum. | The tests pass or the differences are items here. |
| UP15 | Big-endian has not been looked at: words are in host order here and the callers convert. | Run `ump-test` built for m68k in the cross tree. | It passes there. |

## P3 — polish

| # | Limit or defect | What to do |
|---|---|---|
| UP16 | The sources are in `src/`, beside MIDIHub's own programs, though this is system code. | Move them when their home is decided (UP12): this file serves the network, USB and CAMD alike, so `network/` is not it. |
| UP17 | A MIDI 2.0 Note On with velocity 0 becomes velocity 1, and the note's attribute is ignored. | Confirm the first against the translation document; say the second in the header. |

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
