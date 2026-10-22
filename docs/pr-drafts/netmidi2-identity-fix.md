# netmidi2: reject invalid identity outputs and malformed acceptances

Reject invalid output buffers in `netmidi2_identity()` before writing either
output. The public contract now requires both outputs to be non-NULL with
nonzero capacity; invalid arguments or encoded lengths return -1 without
modifying either buffer. This fixes the one-byte out-of-bounds terminator
write with zero capacity. Existing session callers use nonzero internal
arrays; this is a public C API boundary defect, not a demonstrated remote
overflow in netmidid.

For an inviting Client, validate an Invitation Reply Accepted into temporary
buffers before publishing peer identity or starting the session. A malformed
length, empty name or empty product ID gets `NAK / Command Malformed`, and
the pending invitation and previous peer metadata remain intact. Valid
acceptance and duplicate acceptance behavior are preserved.

The response follows [M2-124-UM v1.0, section 6.15 and table 25](https://amei-music.github.io/midi2.0-docs/amei-pdf/M2-124-UM_v1-0_Network-MIDI-2-0-UDP.pdf):
NAK reports an unusable command while leaving a pending session alive.
The fix does not broaden authentication or protocol support.

Based directly on upstream `9bf9977d8dd65786165731e276b302a9bc035a0d`.
No dependency on the pending AppleMIDI sessions/service or on Bonami.

Validation against exact upstream/library revisions:

- Before: zero-capacity name and product outputs trigger ASan overflow;
  malformed acceptance incorrectly establishes a session (three failures).
- After: all nine regression/suite cases pass with ASan/UBSan: each output
  at capacity 0/1/8, malformed acceptance, Network MIDI 2.0 and UMP suites.
- Additional suite cases cover NULL/zero and NULL/nonzero outputs, no
  partial output modification, malformed/empty accepted identities,
  unchanged peer metadata, invitation retry, valid and duplicate acceptance.
- The library builds through generated makefiles with the hosted x86-64 SDK.
- UDP interoperability against NetUMP `f74b0b8` / BEBSDK `022d0f2` passes as
  Host and Client: notes, controller, SysEx and goodbye in both directions.

Regression harnesses remain in the companion MIDIHub workspace, following
the existing library PR test arrangement. These checks do not establish
complete specification compliance, real hardware coverage, or independent
loss-recovery interoperability. A separate service integration is proposed
as a dependent draft.

Service consumer: dependent draft #1693.
