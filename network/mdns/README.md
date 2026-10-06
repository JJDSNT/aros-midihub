# System mDNS incubation

This directory contains the code and references used to close AROS's missing
system-wide mDNS/DNS-SD service. The intended upstream home is an AROS network
service above `bsdsocket.library`; the code remains here while its API, AROS
port, and interoperability are validated.

## Contents

- `embedded.c` is MIDIHub's current minimal `_apple-midi._udp` advertiser and
  query responder. It remains in the normal MIDIHub build until the system
  service can replace it.
- `bonami/` pins the upstream
  [amigazen Bonami](https://github.com/amigazen/Bonami) repository as a Git
  submodule. It is the primary implementation and API reference for the AROS
  port. Bonami uses the BSD 2-Clause license.

Clone this repository with submodules, or initialize this reference after an
ordinary clone:

```sh
git submodule update --init network/mdns/bonami
```

The submodule is not yet a MIDIHub build dependency.

## Design decision

Bonami is a better base than creating another AROS-specific API. It already
has the relevant Amiga structure:

- `bonami.library` with TagItem-based register, browse, and resolve calls;
- a single shared `mdns.task` engine;
- private Exec message-port IPC;
- opaque registration, browse, and resolve handles;
- DNS-SD cache and Hook notifications;
- `bsdsocket.library` ownership in the engine task;
- BSD 2-Clause licensing suitable for an AROS contribution.

[mjansson/mdns](https://github.com/mjansson/mdns) is a useful portable packet
and socket reference. It has an unrestricted license, avoids allocations in
the library, and supports IPv4 and IPv6 helpers. It does not provide the shared
daemon, cache lifecycle, repeated browsing, complete probing/conflict policy,
client ownership, or an Amiga library API, so it is not included as another
submodule. Individual implementation ideas can be used where they improve the
Bonami port and their origin can be recorded in the affected files.

## Work required for AROS

The pinned Bonami revision is a working classic-Amiga implementation rather
than a drop-in AROS component. The AROS port needs:

1. an AROS MetaMake library/service definition and generated public headers;
2. compiler and library-entry conversion from the classic SAS/C ABI;
3. native AROSTCP validation with UDP 5353 as the normal bind port;
4. full RFC 6762 probing, conflict detection, automatic rename, defensive
   announcements, known-answer suppression, and goodbyes;
5. interface enumeration and address-change handling, then IPv6 mDNS on
   `ff02::fb`; current AROSTCP already supplies scoped link-local/multicast
   output (`sin6_scope_id` / `IPV6_PKTINFO`) and `SO_BINDTODEVICE`, so this is
   primarily Bonami/service integration rather than a missing socket-stack
   prerequisite;
6. working update and resolve cancellation operations;
7. optional message-port event delivery so clients need not do work in an
   engine-task Hook;
8. client lifetime cleanup and tests with Bonjour and Avahi;
9. Network MIDI registration and browsing for `_apple-midi._udp` and
   `_midi2._udp`;
10. build and validate Bonami's optional Envoy/NIPC bridge against the
    `nipc.library` and public `<envoy/nipc.h>` API added upstream to AROS in
    commit `fbc2e274d880868c3fe447dffdfc9226fae0bd66`, including
    `_nipc._tcp` publication and browsing.

AROSTCP upstream commit `28dd8854eae4ccc1325d600689790938dd0f7fba` also
records the multi-interface primitives needed by the responder: scoped IPv6
link-local/multicast output and per-interface UDP binding. This removes a
socket-stack uncertainty from the IPv6/multi-interface roadmap; Bonami still
needs to enumerate interfaces, track address/link changes, join the mDNS group
per interface, and select the correct scope for announcements and replies.

AROS now goes beyond the initial NIPC addition. Upstream commit
`06d56a768fa31bb1487e70a8dae11cd7667425ea` adds `services.library`, a
Services Manager, `accounts.library`, and an Accounts Server, and teaches
`nipc.library` to answer service inquiries from the configured services list.
Bonami's `_nipc._tcp` bridge should therefore be evaluated as an optional
DNS-SD discovery complement to native NIPC/Services inquiry, not assumed to
replace it.

The AROS NIPC addition removes the former reason to disable Bonami's Envoy
bridge merely because the platform lacked NIPC headers or a `nipc.library`.
The bridge remains optional: the core mDNS/DNS-SD service and its MIDI clients
must not depend on Envoy/NIPC.

Changes that are useful to classic Amiga should be proposed to Bonami upstream.
The AROS build glue and integration can then move into the AROS tree with the
BSD notices retained.
