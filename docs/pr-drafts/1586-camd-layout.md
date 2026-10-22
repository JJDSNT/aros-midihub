# midi: CAMD drivers for MIDI over the network

The first, small part of MIDI over the network for AROS: the CAMD side of it, and nothing else. Two drivers in `DEVS:Midi`, with separate build directories under `workbench/devs/camd` and one shared source in `common`.

- `applemidi`, for AppleMIDI (RTP-MIDI) sessions, what macOS, iOS and rtpMIDI on Windows speak;
- `networkmidi2`, for Network MIDI 2.0 (UDP) sessions.

Each has sixteen ports, so a CAMD program sees `applemidi.in.0` to `.15`, `applemidi.out.0` to `.15` and the same of `networkmidi2`: a peer on the network is to be one port, and the port's name says what kind of peer it is.

### The shape

It is the shape the review of #1507 asked of a transport: a driver in `DEVS:Midi` like `camdusbmidi`, not a program that makes clusters of its own as a CAMD client. The drivers hold **no network code**. Each makes its adapters public (`struct NetMidiAdapters` in `netmidi_camd.h`, found through a named semaphore), and a service of the network stack attaches to them: what a client sends to a port goes into that port's ring and the service is signalled; what the service receives for a port it hands to CAMD through the port's receive function. The adapters carry a version, since the driver in memory and the service that finds it can be of different builds, and a driver that is unloaded marks its adapters gone so that the service lets go of them.

### Alone, it is inert

Without the service a port takes what is sent to it and drops it, and gives nothing. The service is proposed in #1679, with its RTP-MIDI and AppleMIDI library under `workbench/network/netmidid/rtpmidi` (#1587 and #1678).

### Tested

- Builds by its own targets (`workbench-devs-camd-applemidi`, `workbench-devs-camd-networkmidi2`) for `linux-x86_64` and `amiga-m68k`; it includes `proto/exec.h` and `midi/camddevices.h` and nothing that is not in the tree.
- `linux-x86_64` hosted, a clean tree of today's `master` with this, #1585 and the service on top: two peers running node-rtpmidi, each given a port of `applemidi`, and CAMD programs on those ports receiving each its own peer's messages and playing to it.

On 64-bit targets the drivers are found only with #1585; that is so for every driver in `DEVS:Midi`, and not about these.




### Layout review addressed (2026-10-18)

The first commit moves the existing debug driver into `workbench/devs/camd/debugdriver` and Unix hostmidi into `arch/all-unix/devs/camd/hostmidi`, updates their metatargets and generated-file ignores, and adds the CAMD parent aggregator. Their C sources are unchanged.

The second commit adds `applemidi/` and `networkmidi2/`, each with its own build target and object directory. They compile `../common/netmididriver.c`; the adapter header also lives in `common/`. The parent only aggregates the subsystem. No `workbench-devs-midi` metatarget remains. Installed driver names remain `DEVS:Midi/applemidi` and `DEVS:Midi/networkmidi2`.

Validation of this layout: all four drivers compile with the existing hosted x86-64 SDK; generated makefiles also compile/link them in an isolated build. All moved C/header contents match the previous sources byte for byte. This update was not cross-built for m68k and does not add new hardware or SMP evidence.
