# mDNS/DNS-SD integration: agreements between areas

**Status:** agreed by the project owner on 2026-11-10. Each section has an
owner; an owner may refine its section, and a change that crosses sections
is agreed with the project owner first and dated here.

This records what the network stack, the system resolver, `mdnsd` and
`dnssd.library` may expect of each other. It complements the architecture
decision on the Codex branch `review/mdns-modernization-plan`,
`docs/dnssd-native-architecture-decision.md` (the ADR), which is the
baseline for this work; it does not repeat it.

## Decisions

1. The ADR is the baseline. `dnssd.library` replaces `bonami.library` as the
   public API; `mdnsd` owns all mDNS state and may be redesigned.
2. Three layers:
   - clients: applications, tools, and **the system resolver**;
   - `mdnsd`: policy and shared state (names, probing, conflicts, cache,
     registrations, browse/resolve/query);
   - the network stack: mechanisms only (interfaces, addresses, UDP,
     multicast, notifications).
3. The system resolver in `bsdsocket.library` resolves `.local` names as a
   client of `dnssd.library` (section "System resolver").
4. A capability the stack lacks is added to the stack, in a stack change of
   its own, and consumed afterwards (the ADR's dependency rule). It is not
   emulated in `mdnsd` or `dnssd.library`.

## Owners

| Area | Owner | Where |
|---|---|---|
| Network stack (AROSTCP): the binding below, and anything it lacks | Claude | AROS `workbench/network/stacks/AROSTCP`; tracked in aros-matter `AI_context` |
| System resolver (`gethostbyname`, `gethostbyname2`, `gethostbyaddr`, `getaddrinfo` in `bsdsocket.library`/`miami.library`) | Claude | same |
| `mdnsd`, `dnssd.library`, migration of consumers | Codex | `review/mdns-modernization-plan` and the ADR |
| IPP printer discovery (`workbench/devs/ipp/dnssd.c`) | its author (terminills) | proposals only |

## Functional contract and its AROSTCP binding

What `mdnsd` needs from a stack (left), and how AROSTCP provides it (right).
Another stack that provides the left column can run `mdnsd`.

| # | Capability | AROSTCP binding |
|---|---|---|
| F1 | Enumerate interfaces: index, name, flags, link type | `SIOCGIFCONF`, `SIOCGIFFLAGS`, `SIOCGIFINDEX`, `if_nameindex()` |
| F2 | Every address of an interface, several per family, with its usability | `SIOCGIFCONF` / `SIOCGLIFADDR`; state by `SIOCGIFAFLAG_IN` (IPv4: `IN_IFF_TENTATIVE`, `IN_IFF_DUPLICATED`; RFC 5227) and `SIOCGIFAFLAG_IN6` (tentative, duplicated, detached, deprecated) |
| F3 | Be told that interfaces or addresses changed | `SBTC_SIG_ADDRESS_CHANGE_MASK`: on add and delete, and when an address's probing ends (IPv4 conflict detection, IPv6 DAD), passed or duplicated; also `RTM_NEWADDR`/`RTM_DELADDR` on a routing socket |
| F4 | Join 224.0.0.251 and `ff02::fb` on a chosen interface | `IP_ADD_MEMBERSHIP` with `struct ip_mreqn` (by index); `IPV6_JOIN_GROUP` (by index) |
| F5 | Learn, per datagram, the interface it came in by and its destination | `IP_RECVIF` + `IP_RECVDSTADDR`; `IPV6_RECVPKTINFO` |
| F6 | Choose, per datagram, the output interface and source | `IP_MULTICAST_IF` (`ip_mreqn` index), `IP_SENDSRCADDR`; `IPV6_PKTINFO`, `sin6_scope_id` |
| F7 | Several sockets on port 5353 | `SO_REUSEADDR`, `SO_REUSEPORT` |
| F8 | Multicast TTL / hop limit and loopback control | `IP_MULTICAST_TTL`, `IP_MULTICAST_LOOP`; `IPV6_MULTICAST_HOPS`, `IPV6_MULTICAST_LOOP` |
| F9 | Conventional (any-source) multicast delivery in both families | As now. After the multicast group (IGMPv3/MLDv2, AROS issue #2220) a socket receives only the groups it joined on that interface; `mdnsd` joins, so it is unaffected, and it must not depend on source-specific multicast internals |
| F10 | Be told when the stack reconfigures | `netservices.library` reconfigure-begin/end (close and reopen `bsdsocket.library`) |

F2's IPv4 state and F4-F6's IPv4 options are in AROS PR #2218 (in review).

## Interface tracking in `mdnsd`

- **Snapshot** of interfaces and addresses at start and after each reopen
  (F10).
- **Events** (F3) drive the immediate reaction: probe, announce, withdraw.
- **Reconciliation** at an interval compares the snapshot with the stack
  and repairs differences, against a lost event or a change between the
  snapshot and the first event. With F3 available the interval may be long;
  the value is `mdnsd`'s (its owner's) choice, documented with it.

## System resolver

- **Names:** those ending in `.local` (any case, final dot optional), and the
  reverse names of the link-local ranges (`254.169.in-addr.arpa`,
  `8.e.f.ip6.arpa` through `b.e.f.ip6.arpa`), RFC 6762 sections 3 and 4.
  The host's own `<name>.local` too: `mdnsd` owns it.
- **Order:** an address literal or a localhost name (RFC 6761 section 6.3)
  is answered by the resolver itself, as now; then the hosts file; then a
  `.local` name goes to mDNS only (RFC 6762 section 3), any other name to
  DNS, as now.
- **Mechanism:** the resolver opens `dnssd.library` when it first needs it,
  as `miami.library` opens `usergroup.library` on demand. There is no build
  or link dependency. Without the library or `mdnsd`, a `.local` lookup
  fails with "host not found"; nothing else is affected.
- **What `dnssd.library` must offer for it:** an address lookup usable
  synchronously from any task, with a timeout and cancellation, returning
  IPv4 and IPv6 addresses with the interface index of each (the scope of a
  link-local address).
- **No cycle:** `mdnsd` never resolves a `.local` name through the system
  resolver.

## Open

- `dnssd.library` names and ABI (the ADR's gate).
- The resolver's timeout for a `.local` lookup.
- `mdnsd`'s reconciliation interval.
