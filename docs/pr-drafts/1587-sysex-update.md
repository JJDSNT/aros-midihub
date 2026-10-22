# network: RTP-MIDI as a link library

The second part of MIDI over the network, after the drivers of #1586: the payload of RTP-MIDI (RFC 6295) as a link library, `rtpmidi`, in `workbench/network/netmidid/rtpmidi`. The dependent #1678 adds the AppleMIDI sessions to this library; #1679 supplies its AROS service consumer and attaches to the drivers.

### What is in it

- `rtpmidi.c`: the RTP header and the MIDI command section, read and written: delta times, running status, SysEx in segments, and the recovery journal's chapters with their structures (P, C, M, W, N, E, T, A for a channel; D, V, Q, F, X for the system).
- `sender.c`: what a sender has to remember to write a journal, the checkpoint a receiver's feedback moves, and packets of several commands, each at its time.
- `receiver.c`: what a receiver does with a journal after a loss, so that no note is left sounding and controllers end where the sender has them.
- `timing.c`: a queue that holds a received event until its time.

It is portable C that calls nothing of the system, and of the C library only `malloc()` for the SysEx messages its queue holds: whoever links it owns the sockets and the clock. That is why it is a link library and can be tested on the build machine.

It is 4,300 lines, most of them the journal. I could not find a smaller piece of it that stands: a sender without a journal is easy, but a receiver that ignores one hangs notes at the first lost packet.

### Where it goes

Under its consumer: `workbench/network/netmidid/rtpmidi`, as requested in review. Coordinate merge with the service #1679 and its AppleMIDI session prerequisite #1678.

### Tested

- Builds by its own target (`linklibs-rtpmidi`) on today's `master`, for `linux-x86_64`, and together with the sessions for `amiga-m68k`. It needs the C library's headers and nothing else.
- Unit tests, which stay in my repository since I do not know where such tests would live in the tree: packets of the RFC's forms, every chapter written and read back, loss and recovery, and a fuzzer over the readers (nine million rounds, no finding).
- Through the service, on hosted AROS against node-rtpmidi, an implementation that shares no code with this one: notes, controllers and SysEx both ways.
- cppcheck 2.13 (`style,warning,portability`) has no finding in it, and there is no `strcpy`, `strncpy`, `strlen` or `sprintf`.

What it has **not** met: macOS, iOS or rtpMIDI for Windows, and node-rtpmidi has no recovery journal, so the journal has so far been read only by this code and checked against the RFC.

### Origin

Written for AROS from RFC 6295 and RFC 3550. MIDIKit and rtpmidid are named in my repository as implementations to learn the protocol's use from; no code was taken from either. I compared these sources with both trees mechanically before sending: no line in common, and no run of tokens beyond `#include` lists and loop headers.



### Segmented SysEx correction (2026-10-17)

A complete, contiguous RTP stream can contain a SysEx start, a MIDI Clock packet, and a SysEx end. Comparing only the sequence numbers of SysEx-bearing packets incorrectly discards that message. Empty command sections between segments expose the same problem.

Commit caa66d00b30dc0f6abb1d158bac2901fcab8bfd5 adds `rtpmidi_sysex_packet()`. Call it for every validated, fresh RTP packet before feeding its commands, after filtering duplicates and old packets. Contiguous packets preserve assembly, including sequence wrap; an actual sequence gap abandons partial SysEx. The service follow-up includes that call. No structure layout changes.

Validation of this correction: the protocol suite passed against the patched published library sources under ASan/UBSan. A runtime regression fails before the correction and passes after it, covering intervening MIDI Clock and empty packets, sequence wrap, an old packet, and actual loss. A new 100,000-round sanitizer mutation run passed (200,000 RTP inputs).

These checks do not establish complete RFC 6295 conformance or recovery-journal interoperability with another implementation. The node-rtpmidi peer used in the transport tests does not implement journals.



### Layout review addressed (2026-10-18)

Moved the library beneath `netmidid` and removed both `workbench-network` / `workbench-network-netmidi` metatarget additions. Only `linklibs-rtpmidi : includes` remains in this library's source build metadata; the service's mmakefile owns the network build dependency.

This relocation changes no C/header contents. The relocated N3 library passes the protocol suite under ASan/UBSan, and the generated library/service makefiles compile and link with the hosted x86-64 SDK in an isolated build. This is not a full current-master or m68k rebuild.
