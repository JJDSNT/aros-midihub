# netmidid: a core and its transports — 2026-10-21

netmidid was one event loop with AppleMIDI built into it: the runtime
structure held the AppleMIDI peer table and streams unconditionally, the
loop read the two AppleMIDI sockets by name, the CAMD output path walked
the AppleMIDI peers, and `network.h` had an AppleMIDI port pair and a
separate Network MIDI 2.0 socket. Network MIDI 2.0 was some 25 fields and
476 lines added to that loop under `NETMIDI_MIDI2`. A MIDI 2.0 service
could not be built without AppleMIDI, and its pull request (#1693) had to
edit the heart of the AppleMIDI one (#1679).

It is now a core and transports, in `ports/aros/midihub/`:

| file | what it is |
|---|---|
| `netmidid.h` | the interface: `struct netmidid` (the core's state) and `struct netmidid_transport` (open, start, receive, next_due, periodic, discovered, send_midi, flush, counts, stop, release), and what the core offers a transport |
| `main.c` | the core: configuration and its watch, arguments of the host program, sockets, the ports of the CAMD drivers, the counts it publishes, the system mDNS service, the loop |
| `transport_applemidi.c` | AppleMIDI (RTP-MIDI): peers, streams, clocks, journals, feedback, invitations, the host program's probes |
| `transport_midi2.c` | Network MIDI 2.0 (UDP): Host and Client sessions, UMP translation, JR timestamps, the Product Instance Id |
| `network.c`/`.h` | numbered UDP sockets and one wait over all of them; no protocol is known there |

What the core offers transports: `netmidid_socket` (a socket whose
datagrams go to the transport), `netmidid_port_keep`/`_take`/`_release`
(ports of a driver by name, shared mode handled in one place),
`netmidid_browse` (an mDNS search whose results go to the transport),
`netmidid_now`/`_elapsed`/`_random`/`_ipv4`, `d->camd` to deliver, and the
options, latency and session name. The core validates SysEx once
(`NETMIDID_SYSEX_MAX`) before offering a CAMD message to each transport;
each transport takes what was sent to a port of its own driver.

A new marker, `NETMIDI_APPLEMIDI`, selects the AppleMIDI transport as
`NETMIDI_MIDI2` selects the other. Every existing build and cut defines it
(Makefile, package mmakefile, `tools/cut-netmidi-service.sh` marks); a
build with `NETMIDI_MIDI2` alone is now possible and compiles, with and
without discovery and browse. With neither transport running the core
says "the configuration asks for no transport" and ends.

## What the cuts look like now

Generated over the published heads and compared:

| N7 (MIDI 2.0) over N4 (AppleMIDI) | before | after |
|---|---|---|
| `main.c` | +476 | 1 line, the transport table's entry |
| `network.c`, `network.h` | +52 | nothing |
| `netmidid.h` | — | 1 line, the transport's declaration |
| transport | — | `transport_midi2.c`, a new file (571 lines) |
| `config.c`, `config.h` | +33 | +33, the MIDI 2.0 keys |

Against the published #1679, #1693 and #1743 heads the cuts change only
`main.c`, `network.c`, `network.h` and the build file, and add
`netmidid.h` and the transport files; `adapters.c`, `camd.h`, `config.c`
and `discovery.c` come out byte for byte as published. No AROS branch was
changed.

## Behaviour kept

Every message the program and the service print is unchanged (the host
tests and the hosted runners match them). Order within a turn of the loop
moved in three places, none observable in the tests: the service's
re-invitation and the host program's "session over, so are we" are decided
in the AppleMIDI transport's periodic turn, before CAMD is polled instead
of after; Network MIDI 2.0 releases its held events at the end of its
periodic turn; CAMD is opened before the sockets, so with several faults at
once a different one is reported first.

## Remaining dependency on the rtpmidi library

The core uses `applemidi_ports` (ports kept by name) and the MIDI 2.0
transport uses `rtpmidi_event_queue`, `applemidi_text_length`/`_name_fit`
and the invitation back-off constants. That code is generic but lives in
the merged rtpmidi library under AppleMIDI names. It is a link dependency
only: no transport calls the other's code. Moving it to a neutral place
would change a merged library and is left as its own proposal.

## Validation

- `make test` and `make fuzz` pass. `make test-network` passed 4 of 6 runs
  after, 1 of 4 before; every failure, before and after, is in the cases
  that time feedback and note spacing (WSL stalls, see the memory note).
- Marker builds on Linux with `-Werror`: AppleMIDI alone, with discovery,
  with MIDI 2.0, MIDI 2.0 alone, MIDI 2.0 alone with browse.
- Hosted AROS (`contrib-aros-midihub-netmidid`, all features):
  `tools/applemidi-service-hosted.sh` 8/8 and `tools/netmidi2-hosted.sh`
  3/3, the same as the baseline run just before the change.
- amiga-m68k: `contrib-aros-midihub-netmidid` builds and links in
  `~/aros-build-amiga-m68k` with all features, browse included. (The
  stand-alone `make m68k` still fails as before, on the emu68 SDK's headers:
  no `proto/bonami.h`, no `CamdTime` for `camd_bridge.c`.)
- The N4, N7 and N8-AppleMIDI cuts compile with the hosted cross compiler
  and `-Werror` against the headers of their published trees.

Not done yet: a full build of each cut in the clean upstream tree, and
replacing the published branches. Both wait for the owner, since #1679 is
under review (see the handoff).
