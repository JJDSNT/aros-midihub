# mDNS and AROSTCP

How the system mDNS service relates to the TCP/IP stack: what each side is
responsible for, what the stack had to gain, and how the service starts. The
service itself is described in
[`network/mdns/aros/README.md`](../network/mdns/aros/README.md).

## Responsibility boundary

```text
Network MIDI, printing, file sharing, Envoy, tools
                        |
                 bonami.library            thin client, Bonami's API
                        |
              message port "BonAmi"
                        |
                      mdnsd                one per host; owns names, cache,
                        |                  timers and UDP port 5353
                bsdsocket.library
                        |
              AROSTCP or another stack
```

mDNS stays above the stack. Record processing, probing, renaming on
conflict, browsing, caching and the lifetime of a registration are the
service's policy. Inside `bsdsocket.library` they would tie the service to
AROSTCP and keep it from running on another socket provider.

The stack supplies mechanisms, and mdnsd uses only these:

- UDP sockets, `SO_REUSEADDR` and `SO_REUSEPORT`;
- `SO_BINDTODEVICE`, to tie a socket to one interface;
- `IP_ADD_MEMBERSHIP`, `IP_MULTICAST_IF`, `IP_MULTICAST_TTL` and
  `IP_MULTICAST_LOOP`;
- `SIOCGIFCONF` and `SIOCGIFFLAGS`, to find the interfaces;
- `gethostname()`.

For IPv6, which mdnsd does not speak yet, AROSTCP already has scoped
link-local and multicast output (`sin6_scope_id`, `IPV6_PKTINFO`), recorded
in upstream commit `28dd8854eae4ccc1325d600689790938dd0f7fba`.

## What AROSTCP lacked

The first native run showed that IPv4 multicast did not work on a SANA-II
interface, although the options were accepted. Three patches in
`patches/net/series` fix what was found; none is specific to mDNS.

| Patch | Fault |
|---|---|
| `net/aros-arostcp-ipv4-multicast.patch` | `IFF_MULTICAST` was never set and its bit carried a private flag; a group had no Ethernet address; `ip_input()` had no case for a joined group; a sender that had chosen its interface with `IP_MULTICAST_IF` still needed a route. |
| `net/aros-eth-unix-multicast.patch` | The Linux-hosted `eth.device` dropped received multicast frames and refused the SANA-II multicast commands. |
| `net/aros-arostcp-netdb-unknown-keyword.patch` | An unknown keyword in a netdb file crashed the stack. |

One more fault is not patched: on amiga-m68k the stack does not build until
`fd.library`'s headers exist, because its build file does not ask for them.
`make workbench-libs-fd-includes` first works around it.

Other SANA-II drivers were not looked at. A driver that filters multicast in
hardware has to honour `S2_ADDMULTICASTADDRESS`, which AROSTCP sends when a
group is joined; the hosted driver needs no filter because its host
interface is promiscuous.

## Why one responder

Every mDNS participant uses UDP port 5353. The stack can deliver a multicast
datagram to several sockets bound with the reuse options, but that does not
remove what several responders in one host would each keep for themselves:
the cache, the ownership of names, and the probing and conflict state. Two
responders in one host can also answer the same question twice, or contest
each other's names.

mdnsd therefore refuses to start twice, and MIDIHub's own embedded responder
(`network/mdns/embedded.c`) has to go once Network MIDI uses the service:
mdnsd ignores packets from the host's own addresses, so the two would not
see each other.

## Startup and availability

mdnsd is a managed service of the stack. `db/services.d/mdns` names it; the
AROSTCP service manager starts it with the stack and stops it with the
stack. It registers with `netservices.library` and is signalled around a
reconfiguration: at the start it says goodbye and closes
`bsdsocket.library`, at the end it opens it again, finds the interfaces, and
announces what applications had registered.

Nothing else needs to be started, and no client keeps the engine alive: a
program that opens `bonami.library` while mdnsd is not running gets
`MDNSERR_NOENGINE` from its calls and can try again later. Network MIDI can
start on its own and work with manually configured peers while discovery is
unavailable.

This is where the port leaves Bonami's model, in which the first
`OpenMDNSEngine()` starts the engine and the last `CloseMDNSEngine()` stops
it. A daemon was chosen because the cache and the host's names outlive any
one client, and because the stack needs to tell the responder when it goes
away.

`<libraries/netservice.h>` used to carry a GNU GPL version 2 banner, which
left open whether a BSD-licensed service that only registers was bound by
it. AROS settled that on 2026-10-11: commit `43328bc8f2` gives the header
the plain AROS Development Team notice that public SDK headers carry, saying
the GPL banner had come from the stack sources it was written beside, and
`95ed7cd53c` puts the BSD-derived network headers back under Berkeley's
notice. No header in `workbench/network/common/include` has a GPL banner
since. The stack's own sources remain GPL; the daemon includes only the
public headers, and `netservice.h` in one file.

## Public service contract

Bonami's API is the contract; the port extends it instead of adding a second
one. It gives a client:

- register, update and withdraw a service instance, also for another host;
- browse a type, and enumerate the types present;
- resolve an instance and look up a host's address;
- ask for any record;
- publish and inquire in Envoy's terms.

What a client makes belongs to its task and goes with the task, whether it
closes the library or not.

Not in the contract yet:

- telling a client that its service was renamed after a conflict;
- delivering events to a client's own message port. Events come through a
  Hook that runs in mdnsd's process, which obliges every client to copy and
  signal. The tools share one helper for that.

## MIDI migration

1. Done: the service runs and is tested on hosted AROS.
2. Next: Network MIDI registers and browses `_apple-midi._udp` and
   `_midi2._udp` through `bonami.library`, and resolves peers with
   `ResolveServiceA()` and `LookupHostA()`.
3. Then `network/mdns/embedded.c` and its use of UDP port 5353 are removed.

MIDIHub then consumes the CAMD endpoints exposed by the Network MIDI device.
Its Preferences program may show discovered peers, but discovery state
belongs to mdnsd and session state behind the Network MIDI device boundary.
