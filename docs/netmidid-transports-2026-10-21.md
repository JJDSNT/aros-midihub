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

## Second step: each transport's configuration and state

The same day the two couplings the first step left were taken out.

**Configuration.** `struct netmidi_network_config` had every transport's
keys as fields of its own, under markers, and one parser for all. It now
has the keys all transports share (`session_name`, `latency_ms`) and
embeds `struct netmidi_applemidi_config` and `struct netmidi_midi2_config`,
each in a header of its own (`include/netmidi/config_applemidi.h`,
`config_midi2.h`) with its reader, check and writer (`src/config_applemidi.c`,
`config_midi2.c`). `src/config.c` reads the file and offers every key it
does not know to each transport's reader; a key none takes still makes the
file invalid, and each key is validated as before. The keys and the file
format are unchanged; only the order in which `netmidi_config_save` writes
them changed (shared keys first, then each transport's). The MIDIHub
Preferences use `network.applemidi.local_port` and so on.
`NETMIDI_APPLEMIDI` now decides fields of the structure, so the package's
`USER_CPPFLAGS`, with which the link library is built, define it too.

**State.** A transport no longer keeps its state in a variable of its
module. `struct netmidid_transport` begins with `state_size`; the core gives
each transport that many zeroed bytes for a run and passes them as `self`
to every call, and frees them at the run's end. The two states are about
290 KiB and 86 KiB (they were in BSS before; the memory used is the same).

What N7 adds over N4 is now additive only:

| N7 over N4 | first step | now |
|---|---|---|
| `config.c` | +23 inside the parser | 6 lines, a call in each hook |
| `config.h` | +10 fields | 2 lines, the include and the member |
| keys | — | `config_midi2.c` and `config_midi2.h`, new files |
| `main.c`, `netmidid.h` | 1 + 1 | 1 + 1 |
| transport | `transport_midi2.c` | `transport_midi2.c` |

Validated again after this step: `make test` (configuration round trip
included), the marker builds, hosted AROS AppleMIDI 8/8 and MIDI 2.0 3/3,
the hosted and amiga-m68k builds of netmidid and of the MIDIHub
Preferences, and the three cuts compiled against their published trees.
`make test-network` was compared with the binary from before the refactor,
alternating the two: 6 of 8 against 1 of 8 in one series, 0 of 5 against 4
of 5 in another, every failure in either a timeout waiting for feedback or
a timing assertion, with the same signatures in both binaries.

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

Not done yet (after both steps): a full build of each cut in the clean upstream tree, and
replacing the published branches. Both wait for the owner, since #1679 is
under review (see the handoff).

## Prepared for publication (not pushed)

#1679 is restructured so that it is about netmidid, with AppleMIDI as its
first transport; it had no review yet. Local branches in
`build/upstream-pr-prep`, author Jaime Dias, no other attribution:

| PR | local branch | commits over master `a1b8aa6fd8` |
|---|---|---|
| #1679 | `netmidid/service` | `aa6ca46a01` the core and its transport interface (cut `n4-core`); `60f807ef96` the AppleMIDI transport (cut `n4`) |
| #1693 | `netmidid/midi2` | the two, then `4776ebc8f4` Network MIDI 2.0 (cut `n7`) |
| #1743 | `netmidid/advertise` | the two, then `c1d710e74e` advertisement (cut `n8-applemidi`) |
| #1742 | `netmidid/netmidistat` | the two, then `ad4ce857f2`, `669a30346a` picked unchanged |

The AppleMIDI commit and the MIDI 2.0 commit have one shape: a transport
file, a configuration module, one line in each registry. The final #1679
tree equals the validated refactor apart from the build file's comment.
Every point, the core alone included, compiles and links for hosted
x86-64 and amiga-m68k against the real SDKs with `-Werror`, and so does
`NetMIDIStatus`.

```sh
cd build/upstream-pr-prep
git push --force-with-lease=review/netmidid-service:f541409f06 \
    fork netmidid/service:review/netmidid-service
git push --force-with-lease=review/netmidid-midi2:6e71764ba2 \
    fork netmidid/midi2:review/netmidid-midi2
git push --force-with-lease=review/netmidid-advertise:989227ceac \
    fork netmidid/advertise:review/netmidid-advertise
git push --force-with-lease=review/netmidistat:669a30346a \
    fork netmidid/netmidistat:review/netmidistat
```

Then the #1679 title ("netmidid: a network MIDI service, with AppleMIDI as
its first transport"), the comment
`docs/pr-drafts/netmidid-transports-comment.md`, and the bodies in
`docs/pr-drafts/` (`netmidid-service.md`, `netmidid-midi2.md`,
`netmidid-advertise.md`, `netmidistat.md`). `gh pr edit` fails on this
repository (Projects classic), so title and body go through `gh api -X
PATCH repos/aros-development-team/AROS/pulls/N -f title=... -F
body=@file`.
