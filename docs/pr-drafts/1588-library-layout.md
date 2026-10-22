# network: the Universal MIDI Packet and Network MIDI 2.0 as a link library

The MIDI 2.0 side of MIDI over the network, as a link library, `netmidi2`, in `workbench/network/netmidid/netmidi2`. It stands by itself: it needs none of #1586 or #1587, and nothing links it yet. Its user is the service `netmidid`, which comes after those two and gives each Network MIDI 2.0 peer a port of the driver `networkmidi2` of #1586.

### What is in it

- `ump.c`, the Universal MIDI Packet (M2-104-UM): the packet's formats and sizes, and the translation between the MIDI 1.0 byte stream CAMD carries and UMPs, both ways. What CAMD sends goes out as MIDI 1.0 channel voice messages in UMPs; a peer's MIDI 2.0 channel voice messages are scaled down to MIDI 1.0. SysEx goes as SysEx7 in as many packets as it takes, Jitter Reduction clocks and timestamps are read and written, and the answers an endpoint owes to discovery are made: Endpoint Info, its name and product id, one function block.
- `netmidi2.c`, Network MIDI 2.0 over UDP (M2-124-UM): the session protocol as a Host and as a Client: invitation and its replies, ping, Bye with its reasons, the UMP Data command with its sequence numbers, the repeats of what was sent, and Retransmit Request and Error for what was lost.

It is portable C that calls nothing of the system and no `malloc()`: whoever links it owns the socket and the clock, and that is why it can be tested on the build machine.

It does **not** do authentication: as a Host it never asks for one, and as a Client it says it cannot give one and ends an invitation that is answered with a request for it. Nor does it send MIDI 2.0 channel voice messages, since what it carries comes from CAMD.

### Where it goes

Under its service consumer, beside #1587: `workbench/network/netmidid/netmidi2`, as requested in review.

### Tested

- Builds by its own target (`linklibs-netmidi2`) on today's `master` for `linux-x86_64` and for `amiga-m68k`. It needs the C library's headers and nothing else.
- Unit tests, which stay in my repository: every packet of the appendix of M2-124-UM, loss and retransmission, the translation both ways, and a fuzzer over the readers.
- Against another implementation, Benoit Bouchez's NetUMP class (the engine of his UMP-OX), on Linux: as Client of this code's Host and as Host of its Client, sessions open with names and product ids exchanged, UMPs cross both ways word for word, and Bye is understood in each direction. Nothing had to change here for it.
- Through the service, on hosted AROS: UMPs from a peer on the host reach a CAMD program as notes, a controller and a SysEx message, and what a CAMD program plays reaches the peer; as a Host and as a Client.
- cppcheck 2.13 (`style,warning,portability`) has no finding in it, and there is no `strcpy`, `strncpy`, `strlen` or `sprintf`.

What it has **not** met: loss repaired between two implementations (only between two of this one), more than one session with NetUMP, MIDI 2.0 channel voice messages from another implementation, and any product that speaks the protocol; UMP-OX is released for Windows and macOS only.

### Origin

Written for AROS from the two specifications. I compared these sources mechanically with NetUMP, BEBSDK, AM_MIDI2.0Lib and ni-midi2 before sending: no line in common, and one run of tokens, the table of sixteen packet sizes by message type, which is the specification's.




### Layout review addressed (2026-10-18)

Removed the `workbench-network` and `workbench-network-netmidi` metatarget additions. The library retains its explicit `linklibs-netmidi2` target; the Network MIDI 2.0 service build owns the dependency.

C/header contents are unchanged byte for byte. The current service draft #1679 is deliberately AppleMIDI-only: it does not yet consume this library. Coordinate this library's merge with the subsequent Network MIDI 2.0 service extension; relocating it does not add that consumer to #1679. MIDIHub's generation template for that extension has been updated to the same layout.
