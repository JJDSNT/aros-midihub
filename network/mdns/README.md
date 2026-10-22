# System mDNS for AROS

This directory holds the proposed system-wide mDNS/DNS-SD service for AROS,
currently derived from Bonami and running above `bsdsocket.library`. The
priority is protocol conformance and functional AROS integration. The proposed
Bonami API, name and architecture have no assumed maintainer acceptance;
compatibility with original Bonami is not a requirement. Contributions to
original Bonami can be considered separately.

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

- Network MIDI uses the system service through
  [`ports/aros/midihub/discovery.c`](../../ports/aros/midihub/discovery.c).
  The former `embedded.c` responder has been removed.

The [discovery contract audit](../../docs/network-midi-mdns-contract-audit.md)
distinguishes RFC/MIDI protocol requirements from Bonami API choices, lists
the calls MIDI actually uses, and records current reproducible defects.
The dated status sections below describe the earlier integration snapshot.

## Where it stands

On 2026-10-11 the service runs on Linux-hosted AROS and
`tools/bonami-hosted.sh` checks it against Avahi: registering, browsing and
resolving in both directions over two links, name conflicts, clients that
go away, the tools, and the Envoy bridge. It compiles for amiga-m68k and
raspi-aarch64 and has not been run on either.

Three things had to change in AROS first: a
crash in AROSTCP's netdb parser, multicast in the hosted `eth.device`, and
IPv4 multicast on SANA-II interfaces, which AROSTCP did not deliver. Those
changes were merged in #1543/#1558; the old patch series is no longer needed.

## Why Bonami

Bonami already had the Amiga shape of the service: a library with
TagItem-based register, browse and resolve calls, opaque handles, Hook
notifications, a private message-port protocol to one shared engine, and a
license AROS can take. That was the initial rationale for trying the port.
The current review judges the resulting code against the RFCs and AROS
requirements; keeping the original API is optional.

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
[`aros/README.md`](aros/README.md): known answers and delayed responses in mdnsd's own traffic,
and IPv6. AROSTCP already has what IPv6 needs from the stack (scoped
link-local multicast and `SO_BINDTODEVICE`).

Around it:

1. Run it on m68k and AArch64, against Apple's Bonjour, and between two AROS
   systems.
2. Finish upstream delivery and validation of Network MIDI's existing
   system registration/browse integration (N8/N9), including the restart,
   name and TXT findings in the contract audit.
3. Join AROS's `nipc.library` to the Envoy bridge, if that is wanted; see
   [`docs/nipc-envoy-future.md`](../../docs/nipc-envoy-future.md).
4. Send it upstream, in parts. The first part, the responder for the
   host's name alone, is AROS PR #1560; the questions about the rest are
   in AROS issue #1559. Before that: the three AROS patches to AROS (merged on 2026-10-11 as
   AROS PR #1543 and #1558), the byte-order fixes,
   protocol work and API additions to Bonami, and then the AROS-specific
   parts (the daemon, the service entry, the build files) to the AROS tree
   with Bonami's notices kept. The license of AROS's `netservice.h`, once
   open for the last of these, was settled upstream on 2026-10-11 (see
   [the AROS port](aros/README.md)).

How the service relates to the TCP/IP stack is described in
[`docs/mdns-arostcp.md`](../../docs/mdns-arostcp.md).
