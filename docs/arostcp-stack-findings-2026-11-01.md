# AROSTCP findings for MIDIHub (2026-11-01)

This note comes from aros-matter. aros-matter is completing the AROSTCP stack through grouped upstream PRs; the plan is in aros-matter's `AI_context/research/2026-10-31-arostcp-pr-groups-plan.md`. While planning those groups, MIDIHub's network items were checked against the stack.

The result is that **MIDIHub needs one thing new from the stack: `SO_TIMESTAMP`**, now in aros-matter's group 4. The other items are either already there, already planned, or work outside AROSTCP.

Revisions read: AROS upstream master `f04bb2c13e`. That includes AROSTCP 5.20 (#2005) and mdnsd's dual-stack core (#1980, merged 2026-10-31). AROS #2006 (5.21) was still in review.

## Per item

| Item | Finding | Where the work is |
|---|---|---|
| **MD13**: address changes found by looking every 3 s | The stack already signals address changes. A task sets a signal mask with the `SBTC_SIG_ADDRESS_CHANGE_MASK` tag (57, the Roadshow code; `bsdsocket/socketbasetags.h`) through `SocketBaseTags()`. `rt_newaddrmsg()` (`net/rtsock.c:628`) calls `api_sendaddrchange()` (`api/amiga_api.c:929`) on every IPv4 and IPv6 address add and remove, whether or not a routing socket is open. The woken task re-reads the interfaces (`SIOCGIFCONF`). mdnsd does not subscribe yet (`mdns_net.c` uses `SIOCGIFCONF` only). | mdnsd: subscribe and rescan on the signal. A timer can stay as a fallback. aros-matter's group 5 adds a test that the signal fires for both families, SLAAC included. |
| **MD13**: at most eight interfaces | `MDNS_IFS_MAX 8` (`mdnsd.h:26`) is mdnsd's limit. The stack has no such limit. | mdnsd. |
| **MD14 / AM14 / NM15**: IPv6 | mdnsd has had IPv6 since #1980: `ff02::fb` per interface, AAAA records, and the link given by the interface index. AppleMIDI and Network MIDI 2.0 over IPv6 are application work; the stack side (zones, multicast/MLD, `IPV6_RECVPKTINFO`) is in. | Re-check MD14 against #1980. AM14 and NM15 are application work. |
| `SO_BINDTODEVICE` on IPv6 sockets | Still IPv4-only; on an IPv6 socket `setsockopt()` fails with `ENOPROTOOPT`. mdnsd works around it with one wildcard IPv6 socket and `IPV6_PKTINFO` (comment at `mdns_net.c:276`). That is correct, and it doesn't depend on the stack change. | aros-matter's group 5 (item G18i). Once it is in, mdnsd may bind one IPv6 socket per interface, but it doesn't need to. |
| **MD08**: multicast filters on other cards | The stack asks the driver for every IPv4 and IPv6 group through `S2_ADDMULTICASTADDRESS` (`netinet/in.c`, `netinet6/in6.c`). Whether a card honours it is up to each SANA-II driver. | The drivers, one by one. |
| **MD18**: the m68k AROSTCP build needs the `fd.library` headers | A build-dependency fix, not stack behaviour. It doesn't fit aros-matter's groups. | A small PR of its own, as MD18 already says. |
| `SO_TIMESTAMP` | Defined in `sys/socket.h`, but `setsockopt()` fails with `ENOPROTOOPT`, so a program can tell it is missing. AppleMIDI and Network MIDI 2.0 carry their timestamps in the payload, but their clock sync and jitter measurement gain from the time the stack received the datagram: it doesn't include the delay before the MIDIHub task runs. (L12 is about BLE and is unrelated.) | aros-matter's group 4: `SCM_TIMESTAMP` (`struct timeval`) on UDP over IPv4 and IPv6. Until then, take the time right after `recvmsg()` returns, and switch to `SCM_TIMESTAMP` when `setsockopt(SO_TIMESTAMP)` succeeds. |
| **D1**: a library base belongs to its task | Handing sockets over between tasks already exists: `ReleaseSocket()`/`ObtainSocket()` (`api/amiga_generic.c`). | Use the existing API. |

## Stack changes MIDIHub gets anyway

These are planned for other reasons and are useful to MIDIHub:
- **Group 3:** UDP `IPV6_DONTFRAG`/`IP_DONTFRAG` and path-MTU reporting (`IPV6_PATHMTU`, `IPV6_RECVPATHMTU`), plus UDP ECN ancillary data.
- **Group 4:** `SO_TIMESTAMP`; UDP correctness tests (large datagrams, `MSG_TRUNC`, buffers, non-blocking sockets), and `localhost` resolving to `::1` and `127.0.0.1`.
- **Group 5:** RDNSS (DNS servers from Router Advertisements), source-address selection and zones, IPv6 `SO_BINDTODEVICE`, IPv6 address flags (`SIOCGIFAFLAG_IN6`) with the address-change signal when DAD ends, and two-interface multicast tests.

Only one AROSTCP PR group is open upstream at a time. A stack need that MIDIHub finds later should be added to aros-matter's plan rather than sent as a separate AROSTCP PR, so the PRs don't conflict over the stack revision.

## Suggestions for the mDNS/Bonami plan

Read against upstream `workbench/network/mdns` (bonami 41.6, `README.md` §Limits, `DISCOVERY.md`) and `docs/mdns-backlog.md` here. These are suggestions, not edits to the backlog.

1. **Bring the backlog up to date with upstream.** Issue #1559 is closed, so nothing waits for it any longer (MD01, MD14, MU5).
   - **MD01** looks done: since #1695, discovery is queue-only and mdnsd never calls a client hook. A non-NULL hook returns `MDNSERR_NOTREADY`.
   - **MD14**'s core is done by #1980.
2. **Make the extended discovery event the next priority.** The legacy `MDNSDiscoveryEvent` holds one IPv4 address. A service that publishes only AAAA records therefore never completes browse/resolve (`DISCOVERY.md`: "remains for an appended, size-safe event API"). Today it can only be followed with `QueryRecordA()`. This blocks:
   - AppleMIDI and Network MIDI 2.0 over IPv6 (AM14, NM15);
   - Matter, whose devices are IPv6-only;
   - Open Screen.

   One versioned, size-safe event should carry:
   - every address (IPv4 and IPv6), each with its interface index (the scope ID of a link-local address);
   - the SRV target host name (**MD29**).

   Doing these together means one ABI step instead of two.
3. **DNS-SD subtypes** (RFC 6763 §7.1). Bonami has none. Matter needs browse by subtype (`_L<discriminator>._sub._matterc._udp`, `_I<id>._sub._matter._tcp`), and registration with subtypes for an accessory. Worth a backlog item, so that it is designed with the extended event rather than after it. aros-matter's ISSUE-0005 lists what Matter needs.
4. **MD13: interface changes.** Replace the 3 s look with the `SBTC_SIG_ADDRESS_CHANGE_MASK` signal (see above), keeping a slow timer as a fallback. Choosing the IPv6 link-local address needs a stack change too: mdnsd can't tell whether an address is still tentative or is a duplicate (README §Limits), and today the stack doesn't signal when DAD ends. Both are now in aros-matter's group 5: `SIOCGIFAFLAG_IN6`, and the signal when DAD ends. Until then, mdnsd should skip an address that fails to bind or send and try again later.
5. **Tests for IPv6-only links.** Run the Avahi runners with IPv4 off on the veth link: host lookup, registration, and browse/resolve once (2) is in. MD14's own "done when" (Avahi resolves the host over IPv6) is a good first check.

## Who does what

| Work | Owner |
|---|---|
| Stack: everything listed under groups 3–5 above | aros-matter (AROSTCP PR groups) |
| mdnsd/Bonami: extended discovery event, subtypes, using the address-change signal and the address flags, MD07/MD10/MD11, tests | MIDIHub / the Bonami maintainer |
| SANA-II drivers' multicast filters (MD08), the m68k build dependency (MD18) | MIDIHub, as separate small PRs |
| What Matter needs from Bonami | recorded only, in aros-matter's ISSUE-0005; no implementation there |

There is one mDNS responder on the system: mdnsd. Matter uses Bonami, and the SDK's `minimal_mdns` stays a test tool (aros-matter ISSUE-0004).
