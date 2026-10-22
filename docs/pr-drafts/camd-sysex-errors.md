SysEx error reporting in camd.library did not follow its own interface.

- **`MIDI_ErrFilter` was ignored.** It was stored and every error still
  reached `WaitMidi()` and `GetMidiErr()`. Errors now go through one function
  that masks them with the node's filter.
- **Oversized SysEx was reported as a full queue.** A message longer than a
  receiver's `MIDI_SysExQueueSize` gave `CMEF_SysExFull`, as if the queue
  were only busy, and the receiver could get the part that fitted.
  `PutSysEx()` now reports `CMEF_SysExTooBig` and delivers nothing to that
  receiver. From a driver, byte by byte: a message that overflows an empty
  queue is `CMEF_SysExTooBig`, and the rest of it is discarded up to its F7
  instead of being taken for a new message.
- **Off by one.** The queue held one byte less than `MIDI_SysExQueueSize`
  (its buffer is already allocated with one byte more), so a message of
  exactly that size did not fit. `GetSysXLen()` left out the closing F7,
  which also made the length passed to receive hooks one short.
- After a SysEx queue is reallocated its parsing state starts afresh.

Validation on hosted x86-64 against an unmodified upstream system, with
the CAMD 41 contract suite (60 checks):

| | master | this PR |
|---|---|---|
| contract checks | 54 pass, 5 fail, 1 skip | 59 pass, 0 fail, 1 skip |

The five that now pass: a SysEx of exactly `MIDI_SysExSize` fits; oversized
SysEx reports `CMEF_SysExTooBig`, not `CMEF_SysExFull`, both through
`PutSysEx()` and byte-wise; `MIDI_ErrFilter` hides an error from `WaitMidi()`
and from `GetMidiErr()`. No new compiler warnings; builds for x86-64 and
amiga-m68k. Independent of #1730 and #1732, and merges cleanly with both.


### Discard recovery review follow-up (2026-10-20)

Addresses [the maintainer's review](https://github.com/aros-development-team/AROS/pull/1734#issuecomment-6290031862).
A fresh F0 clears discard state and starts a new message even when the
previous `PutSysEx()` stopped on a full queue without passing F7.
`FlushMidi()` resets discard and queue parsing state as well.
The upfront oversized-message check skips receivers without a SysEx queue
or with SysEx excluded by `MLINK_EventMask`, avoiding spurious errors.

The native hosted recovery/filter probe passes all 16 checks, including
queue-full recovery, flush recovery, exact received bytes and both ineligible
receiver cases. The CAMD 41 contract suite passes 59 checks, with 0 failures
and 1 skipped case requiring a device. The runs finish `Avail FLUSH` using
the separate netinfo fix. Complete CAMD builds and links pass for x86-64 and
amiga-m68k; no hardware or SMP execution is claimed.
