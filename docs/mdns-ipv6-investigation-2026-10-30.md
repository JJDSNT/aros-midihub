# AROS mDNS IPv6 investigation — 2026-10-30

## Scope and source of truth

The implementation reviewed is AROS upstream `master` at
`33a5178c2e322981fe0270dbf5d43e2fa59af7a7`, under
`workbench/network/mdns/`. The old `network/mdns/aros/src/` copy in this
repository is not an implementation base and was not modified.

The upstream daemon already has one shared DNS-SD engine, per-link caches,
registration ownership, probing, browse/resolve queues and generic record
queries. The IPv4 link key is the interface IPv4 address. Each link has one
IPv4 UDP/5353 socket, and public discovery results contain only one IPv4
address and one IPv4 interface address.

Those two IPv4 values are the important compatibility boundary. Packet
parsing itself is mostly address-family neutral; replacing the daemon or
duplicating its cache for IPv6 would add state and make cross-family cache
coherence harder.

## AROSTCP capability audit

Current AROSTCP provides the mechanisms the daemon needs:

- `SIOCGLIFADDR` enumerates IPv6 addresses and `SIOCGIFINDEX` supplies a
  stable link identity;
- `IPV6_JOIN_GROUP`, `IPV6_MULTICAST_IF`, `IPV6_MULTICAST_HOPS` and
  `IPV6_MULTICAST_LOOP` are implemented;
- `SO_BINDTODEVICE`, `sin6_scope_id`, link-local source selection and
  interface-specific UDP/IPv6 output are implemented;
- local multicast delivery and socket membership fan-out are implemented.

The relevant stack work is now on upstream master, including the changes
originally reviewed as #1945, #1947, #1970, #1973, #1974 and #1975. IPv6
mDNS therefore has no missing socket primitive.

One public API gap remains: `SIOCGLIFADDR` returns an `in6_ifreq` containing
the name and address, but not the address flags. The stack internally tracks
`IN6_IFF_TENTATIVE`, `IN6_IFF_DUPLICATED`, `IN6_IFF_DETACHED` and
`IN6_IFF_DEPRECATED`; user code cannot retrieve those flags for each
enumerated address. The first implementation can select a link-local address,
but cannot formally reject a tentative/deprecated one. Extending the ioctl
result or adding an address-flags ioctl is a small AROSTCP follow-up.

The existing IPv4 daemon also does not validate received TTL 255. The newer
ancillary-data support makes hop-limit validation possible for IPv6, but
adding it consistently for both families is a separate RFC-hardening change,
not a prerequisite for opening the IPv6 socket.

## Chosen core design

[`aros-mdns-ipv6-core.patch`](../patches/aros-mdns-ipv6-core.patch) is an
incremental patch against the audited upstream commit. It keeps one logical
`MDNSInterface` and one cache namespace per link, keyed internally by the
AROSTCP interface index. A link may hold an IPv4 socket/address, an IPv6
socket/link-local address, or both.

The proposal is published as AROS
[#1980](https://github.com/aros-development-team/AROS/pull/1980), commit
`eeb75e39b4`. It is non-draft, mergeable and has all four upstream checks
passing; maintainer review remains required.

The patch:

- joins `ff02::fb` on UDP 5353 with hop limit 255 and the interface scope;
- receives and sends through both address families while keeping replies on
  the link and family that received the question;
- publishes, probes, defends and withdraws A and AAAA host RRsets;
- includes A and AAAA additionals in service announcements and answers;
- accepts and caches AAAA records in the existing per-link discovery cache;
- asks for both A and AAAA when an SRV target is missing;
- supports AAAA through the existing generic record-query API;
- preserves all old IPv4 event layouts and behavior.

Only one IPv6 link-local address is selected per interface in this first
change. That is sufficient for link-local mDNS and avoids inventing address
preference policy before AROSTCP exposes address state. Publishing every
global/privacy address should be a later policy decision backed by tests.

## Public API compatibility

`MDNSRecordEvent` already has a reserved `UWORD`. The core patch names it
`re_InterfaceIndex` and fills it with the link scope while keeping
`re_Interface` as the IPv4 interface address (zero on an IPv6-only link).
Existing binaries retain the same structure size and ignore the reserved
value. New callers can query AAAA records and construct a scoped IPv6
destination.

`MDNSDiscoveryEvent` cannot represent an IPv6 service endpoint: it has one
`ULONG de_Address`, no address family, no interface index and no SRV target
hostname. Growing that structure would make a new library overwrite the
smaller buffer supplied by an old binary. The core patch therefore caches
AAAA but deliberately leaves legacy browse/resolve completion dependent on
an A record. This preserves old-client semantics but means the patch alone is
not the final IPv6-only browse/resolve API.

The compatible follow-up is an appended library vector such as
`ReadMDNSEvent2()`, with a new fixed-size event containing the SRV target,
IPv4/IPv6 presence flags, a 32-bit interface index and address storage. The
daemon should keep one rich internal snapshot and down-convert it for the
old `ReadMDNSEvent()` vector. Browse and resolve creation, handles, wakeups
and cancellation need not change.

## Practical sequence

1. Land the core socket/wire/cache change after native dual-stack validation.
   It is independently useful for AAAA publication and `QueryRecordA()` and
   does not add a second implementation.
2. Add the rich discovery event and one appended read vector. Make IPv6-only
   snapshots complete when SRV/TXT and at least one A or AAAA record exist;
   retain the legacy A-only down-conversion.
3. Update `dnssd`, `Nearby`, `Handshake` and Network MIDI consumers to prefer
   the new event when running against the library revision that adds it
   (41.7 if the core lands as 41.6), with an IPv4
   fallback for older libraries.
4. Add address-state exposure in AROSTCP, then filter tentative, duplicate,
   detached and deprecated addresses and decide whether global addresses are
   published.
5. Run native packet tests: IPv4 regression, IPv6-only, dual stack, two links
   with identical `fe80::/64` addresses, interface removal/re-addition, and
   both directions against Linux/Avahi. Capture hop limit, scope ID, probes,
   announcements, answers and goodbyes.

## Validation completed for the core patch

- responder fixtures: 38/38, including AAAA query/announcement, conflict and
  delayed-response address-family confinement;
- generic-query fixtures: 14/14, including scoped AAAA on an IPv6-only link;
- discovery/cache fixtures: 41/41, including A/AAAA coexistence and two links;
- all 93 fixtures ran under AddressSanitizer and UndefinedBehaviorSanitizer;
- the nine changed daemon translation units compile with `-Wall -Wextra
  -Werror` using the m68k-AROS cross-compiler;
- the patch applies in reverse to the exact audited worktree and has no
  whitespace errors.

Not yet validated: a complete AROS image, real multicast sockets, Avahi
interoperability, AArch64, or the rich IPv6-only browse/resolve API. These are
explicit acceptance conditions, not assumed successes.
