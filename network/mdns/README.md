# System mDNS for AROS

This directory holds what closes AROS's missing system-wide mDNS/DNS-SD
service. Its intended home is the AROS tree, as a network service above
`bsdsocket.library`, and Bonami upstream for what is useful to classic Amiga
too; it stays here while that is prepared.

## Contents

- `aros/` is the service: `mdnsd`, `bonami.library`, the command-line tools
  and the commodity, ported from Bonami. Its
  [README](aros/README.md) is the reference for how it works, what it does of
  the protocol, how it is tested and what it lacks.
- `bonami/` pins [amigazen Bonami](https://github.com/amigazen/Bonami) as a
  Git submodule: the source the port derives from and the reference for its
  API. Bonami uses the BSD 2-Clause license. The AROS build does not read the
  submodule; initialize it only to compare:

  ```sh
  git submodule update --init network/mdns/bonami
  ```

- `embedded.c` is MIDIHub's own small `_apple-midi._udp` advertiser and
  responder. MIDIHub still builds with it. It goes when Network MIDI
  registers and browses through `bonami.library` instead.

## Where it stands

On 2026-10-11 the service runs on Linux-hosted AROS and
`tools/bonami-hosted.sh` checks it against Avahi: registering, browsing and
resolving in both directions over two links, name conflicts, clients that
go away, the tools, and the Envoy bridge. It compiles for amiga-m68k and
raspi-aarch64 and has not been run on either.

Three things had to change in AROS first, all in `patches/net/series`: a
crash in AROSTCP's netdb parser, multicast in the hosted `eth.device`, and
IPv4 multicast on SANA-II interfaces, which AROSTCP did not deliver.

## Why Bonami

Bonami already had the Amiga shape of the service: a library with
TagItem-based register, browse and resolve calls, opaque handles, Hook
notifications, a private message-port protocol to one shared engine, and a
license AROS can take. Porting it keeps one API for AROS and classic
systems instead of adding an AROS-only one.

What the port changed, and why, in the order it mattered:

1. **Byte order.** Upstream was only right on a big-endian machine.
2. **A daemon instead of a task in the library.** `mdnsd` is managed by the
   AROSTCP service manager; the library became a thin client; what a client
   registers belongs to that client.
3. **The protocol.** Probing and renaming, defending a name, goodbyes, cache
   lifetimes, answers to every kind of question about a service, backing off.
4. **Every interface**, each with its own socket and its own address records,
   following address changes.
5. **API additions** for what `dnssd` could not do: host lookup, a query for
   any record, registering for another host. And `PublishNIPCEntitiesA()`,
   a stub upstream, now publishes.

[mjansson/mdns](https://github.com/mjansson/mdns) was considered as a base. It
is a good packet and socket reference with an unrestricted license, but it
has no shared daemon, cache lifetime, client ownership or Amiga library API,
so it is not included.

## What is left

For the service itself, the open items are listed under "Known limits" in
[`aros/README.md`](aros/README.md): probing the host name, known answers and delayed responses in mdnsd's own traffic,
and IPv6. AROSTCP already has what IPv6 needs from the stack (scoped
link-local multicast and `SO_BINDTODEVICE`).

Around it:

1. Run it on m68k and AArch64, against Apple's Bonjour, and between two AROS
   systems.
2. Move Network MIDI to it: register and browse `_apple-midi._udp` and
   `_midi2._udp` through `bonami.library`, and remove `embedded.c`.
3. Join AROS's `nipc.library` to the Envoy bridge, if that is wanted; see
   [`docs/nipc-envoy-future.md`](../../docs/nipc-envoy-future.md).
4. Send it upstream: the three AROS patches to AROS (merged on 2026-10-11 as
   AROS PR #1543 and #1558), the byte-order fixes,
   protocol work and API additions to Bonami, and then the AROS-specific
   parts (the daemon, the service entry, the build files) to the AROS tree
   with Bonami's notices kept. The license of AROS's `netservice.h`, once
   open for the last of these, was settled upstream on 2026-10-11 (see
   [the AROS port](aros/README.md)).

How the service relates to the TCP/IP stack is described in
[`docs/mdns-arostcp.md`](../../docs/mdns-arostcp.md).
