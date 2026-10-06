# mDNS integration with AROSTCP

## Responsibility boundary

AROSTCP already provides the network mechanisms needed by mDNS. Its
`bsdsocket.library` build enables IPv4 multicast, implements IGMP membership,
accepts `IP_ADD_MEMBERSHIP`, and exposes multicast interface, TTL, and loop
options. It also implements `SO_REUSEADDR` and `SO_REUSEPORT`. Current AROSTCP
also provides the socket mechanisms needed for a multi-interface IPv6 mDNS
implementation: scoped link-local/multicast output follows `sin6_scope_id`,
`IPV6_PKTINFO`, or the interface owning the selected source address, and
`SO_BINDTODEVICE` can pin a UDP socket to a particular interface. These
capabilities are documented in upstream commit
`28dd8854eae4ccc1325d600689790938dd0f7fba`.

mDNS itself should remain above the TCP/IP stack. DNS record processing,
probing, conflict renaming, registration leases, browsing, caching, and
service lifetime are application-service policy. Putting that policy inside
`bsdsocket.library` would couple it to AROSTCP and prevent the same service
from working with another compatible socket provider.

The proposed boundary, refined after reviewing Bonami, is:

```text
Network MIDI, printing, file sharing, other clients
                        |
                 bonami.library API
                        |
             private Exec message-port IPC
                        |
              shared mDNS engine task
                        |
                bsdsocket.library
                        |
              AROSTCP or another stack
```

AROS now also provides a clean-room Envoy-compatible `nipc.library` over
`bsdsocket.library`. Bonami's optional NIPC bridge can therefore form a
parallel discovery path without changing this responsibility boundary:

```text
Envoy/NIPC applications
          |
     nipc.library
       /      \
     RDP      discovery
      |           |
bsdsocket     Bonami / _nipc._tcp
                  |
             shared mDNS engine
                  |
           bsdsocket.library
```

AROS has since expanded this native Envoy layer with `services.library`, a
Services Manager, `accounts.library`, and an Accounts Server. Native
`nipc.library` service inquiry can consult the Services Manager's configured
service list. Bonami's `_nipc._tcp` path should therefore be treated as an
optional zero-configuration discovery integration to validate, not as the
owner or replacement of native Envoy service inquiry.

NIPC remains a client/integration of the mDNS service rather than part of the
TCP/IP stack, and Bonami's core API must remain usable without NIPC.

The responder should be an AROS network service, for example under
`workbench/network/services/mdns`, rather than part of the AROSTCP stack
directory. Keeping Bonami's public API also allows Amiga applications to share
source across AROS and classic systems.

## Why one responder

Every mDNS participant uses UDP port 5353. AROSTCP can deliver multicast UDP
to multiple matching sockets when the reuse flags permit it, but that does
not remove the duplicated cache, name ownership, probing, and conflict state
created by one embedded responder per application.

There is also a concrete bind detail in AROSTCP. In
`bsdsocket/netinet/in_pcb.c`, `SO_REUSEADDR` is treated like `SO_REUSEPORT`
for a socket bound to a multicast address. MIDIHub's current responder binds
`INADDR_ANY:5353`, so its use of `SO_REUSEADDR` alone does not guarantee that
another process can bind the same wildcard address and port. Adding
`SO_REUSEPORT` is useful for an interim embedded implementation, but one
resident responder is the intended system design.

## Public service contract

Bonami's BSD 2-Clause API and engine are the preferred baseline. It already
provides the Amiga-native library, TagItem calls, opaque handles, Hooks, private
IPC, cache, registration, browsing, and resolution. The AROS port should keep
that public contract and extend it where required rather than introduce a
second incompatible mDNS API.

The service contract should provide:

- register, update, and unregister a service instance;
- browse and stop browsing for a service type;
- resolve and cancel resolution of an instance;
- result, removal, rename, and error events; an optional caller-owned message
  port should avoid requiring application work inside the engine task's Hook
  callback;
- opaque handles so registrations and subscriptions are tied to a client;
- leases or a liveness handshake so registrations from a crashed client
  expire, plus goodbye records for an orderly release.

The responder owns UDP sockets, record caches, timers, retransmission,
probing, known-answer suppression, conflict handling, announcements, and
goodbyes. It should eventually handle PTR, SRV, TXT, A, and AAAA records per
active interface, along with interface address changes.

Bonami is already much closer to this target than the embedded MIDIHub code,
but its current tree documents important gaps: one interface, IPv4 only,
simplified probing/conflict handling, a no-op resolve cancellation path, and
an incomplete update operation. Multi-interface and IPv6 support are therefore
Bonami/service work rather than a missing AROSTCP transport primitive: the
stack can scope IPv6 link-local and multicast output per interface, and can
bind UDP explicitly to an interface. An IPv6 mDNS port can use `ff02::fb`
with the appropriate interface scope while IPv4 continues to use
`224.0.0.251`. Its default ephemeral-port mode is a useful
workaround for hosted classic-Amiga socket emulation; the native AROS build
should bind 5353 by default and keep the override only for diagnostics.

## Startup and availability

The responder depends on `bsdsocket.library`; it is not owned by AROSTCP.
Following Bonami's model, the first `OpenMDNSEngine()` starts the shared engine
and later clients attach to it. The engine remains alive while clients are
attached. A persistent consumer such as Network MIDI therefore keeps discovery
available without adding an AROSTCP startup command. A future system-startup
holder may keep the cache warm even with no service clients, but it should use
the same library API.

This avoids changing AROSTCP's `S/startnet`, which currently starts the stack
and waits for its public port. If the socket provider is not ready,
`OpenMDNSEngine()` should return a useful error and permit a later retry.
Network MIDI can start independently and keep manually configured peers working
while discovery is unavailable.

## MIDI migration

The current `network/mdns/embedded.c` is useful incubation code. It advertises
and answers basic questions for `_apple-midi._udp.local`, but it is not yet a
general responder: it has no shared cache, browse/resolve API, probing and
conflict renaming, arbitrary service registration, or complete multi-interface
handling.

Migration should proceed in three steps:

1. Keep the embedded responder for package testing; use both reuse options if
   it must coexist with another port-5353 process.
2. Port Bonami's library and shared engine to AROS, then complete probing,
   conflict handling, interface tracking, IPv6 (`ff02::fb`) using AROSTCP's
   scoped multicast facilities, cancellation, and safe asynchronous event
   delivery. The pinned source and port notes live under
   [`network/mdns`](../network/mdns/README.md).
3. Change Network MIDI to register and browse `_apple-midi._udp` and
   `_midi2._udp` through that API, then remove its ownership of UDP port 5353.

MIDIHub then consumes the CAMD endpoints created by the native Network MIDI
service. Its Preferences application may configure sessions and show
discovered peers, but discovery state belongs to the mDNS service and session
state belongs to Network MIDI.
