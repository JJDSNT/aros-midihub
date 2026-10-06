# mDNS integration with AROSTCP

## Responsibility boundary

AROSTCP already provides the network mechanisms needed by mDNS. Its
`bsdsocket.library` build enables IPv4 multicast, implements IGMP membership,
accepts `IP_ADD_MEMBERSHIP`, and exposes multicast interface, TTL, and loop
options. It also implements `SO_REUSEADDR` and `SO_REUSEPORT`.

mDNS itself should remain above the TCP/IP stack. DNS record processing,
probing, conflict renaming, registration leases, browsing, caching, and
service lifetime are application-service policy. Putting that policy inside
`bsdsocket.library` would couple it to AROSTCP and prevent the same service
from working with another compatible socket provider.

The proposed boundary is:

```text
Network MIDI, printing, file sharing, other clients
                        |
          public Exec message-port API
          (optional library wrapper later)
                        |
             resident mDNS responder
                        |
                bsdsocket.library
                        |
              AROSTCP or another stack
```

The responder should be an AROS network service, for example under
`workbench/network/services/mdns`, rather than part of the AROSTCP stack
directory.

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

An Exec public message port is the smallest native contract for the first
version. A thin `mdns.library` can wrap it later without moving protocol state
into the library. The asynchronous API should provide:

- register, update, and unregister a service instance;
- browse and stop browsing for a service type;
- resolve and cancel resolution of an instance;
- result, removal, rename, and error events delivered to a caller-owned
  reply port;
- opaque handles so registrations and subscriptions are tied to a client;
- leases or a liveness handshake so registrations from a crashed client
  expire, plus goodbye records for an orderly release.

The responder owns UDP sockets, record caches, timers, retransmission,
probing, known-answer suppression, conflict handling, announcements, and
goodbyes. It should eventually handle PTR, SRV, TXT, A, and AAAA records per
active interface, along with interface address changes.

## Startup and availability

The responder depends on `bsdsocket.library`; it is not owned by AROSTCP.
The service may start during the normal system startup and retry while no
socket provider is available. On a system specifically configured for
AROSTCP, startup ordering may place it after `WaitForPort AROSTCP`, but that
is a packaging choice rather than an API dependency.

This avoids changing AROSTCP's `S/startnet`, which currently starts the stack
and waits for its public port. Network MIDI can start independently, register
its services when mDNS becomes available, and keep manually configured peers
working when discovery is unavailable.

## MIDI migration

The current `src/mdns.c` is useful incubation code. It advertises and answers
basic questions for `_apple-midi._udp.local`, but it is not yet a general
responder: it has no shared cache, browse/resolve API, probing and conflict
renaming, arbitrary service registration, or complete multi-interface
handling.

Migration should proceed in three steps:

1. Keep the embedded responder for package testing; use both reuse options if
   it must coexist with another port-5353 process.
2. Implement the resident service and public message-port API using the
   portable DNS encoding and parsing work as a starting point.
3. Change Network MIDI to register and browse `_apple-midi._udp` and
   `_midi2._udp` through that API, then remove its ownership of UDP port 5353.

MIDIHub then consumes the CAMD endpoints created by the native Network MIDI
service. Its Preferences application may configure sessions and show
discovered peers, but discovery state belongs to the mDNS service and session
state belongs to Network MIDI.
