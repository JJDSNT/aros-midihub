# AROS NIPC / Envoy and Bonami integration

## Status

**Upstream capability available; integration and interoperability remain future work.**

AROS now contains a clean-room `nipc.library` implementation of the Envoy NIPC
API over `bsdsocket.library`, added upstream in commit
`fbc2e274d880868c3fe447dffdfc9226fae0bd66`. The same change installs public
`<envoy/nipc.h>`, `<envoy/nipclowlevel.h>`, and `<envoy/errors.h>` headers
and adds a `NipcTest` command. A subsequent upstream change,
`06d56a768fa31bb1487e70a8dae11cd7667425ea`, extends this into a broader
Envoy subsystem: AROS now has `services.library`, a Services Manager and
configuration tool, an Echo test service, `accounts.library`, and an Accounts
Server backed by the system account store and `pam.library`. `nipc.library`
also answers service inquiries from the Services Manager's service list.

This removes the previously identified API/header blocker for Bonami's optional
Envoy/NIPC bridge. NIPC is still not a requirement for MIDIHub, AppleMIDI, or
Network MIDI 2.0; it is an additional integration opportunity exposed by the
Bonami port.

## Why this appeared during MIDIHub work

MIDIHub needs a system-wide mDNS/DNS-SD responder for AppleMIDI
(`_apple-midi._udp`) and Network MIDI 2.0 (`_midi2._udp`). Bonami is being
evaluated as the Amiga-native basis for that service.

Bonami also contains an Envoy/NIPC compatibility layer. Its inquiry bridge
includes `<envoy/nipc.h>`, understands NIPC inquiry tags such as
`MATCH_ENTITY`, `QUERY_ENTITY`, `QUERY_HOSTNAME`, and `QUERY_IPADDR`,
and maps NIPC discovery onto DNS-SD/mDNS. Bonami uses `_nipc._tcp` for that
discovery path.

The new AROS NIPC implementation provides the public API surface that this
bridge expects, so the AROS Bonami port should now attempt to build and test
the bridge instead of disabling it merely because NIPC is unavailable.

## What AROS now provides

The upstream implementation covers the main NIPC programming model:

- public Envoy-compatible headers and error definitions;
- entity creation, lookup, attributes, ownership, and lifetime;
- synchronous and asynchronous transactions;
- transaction wait, abort, reply, and completion;
- NIPC scatter/gather buffers;
- resolver and inquiry support;
- RDP transport over `bsdsocket.library`;
- low-level NIPC/RDP configuration;
- a native `NipcTest` exercising local and network behavior, inquiry, ping,
  Accounts Server access, and Services Manager lookup.

The implementation deliberately leaves IP, ARP, fragmentation, and interface
ownership to the AROS TCP/IP stack where appropriate. This matches the desired
system boundary: NIPC supplies the Envoy programming/transaction model while
`bsdsocket.library` supplies IP networking.

## Native Envoy discovery and Bonami

The AROS implementation now has native inquiry/resolution responsibilities of
its own. In particular, `nipc.library` can answer service inquiries from the
Services Manager's configured service list. Bonami's DNS-SD bridge must
therefore not be documented as a replacement for NIPC inquiry.

The integration question is narrower: determine whether Bonami's mDNS/DNS-SD
discovery can complement native NIPC inquiry for zero-configuration discovery
across hosts, and where translation between DNS-SD results and NIPC entities
should live. The two mechanisms may coexist, but that boundary must be proven
against the current AROS Envoy implementation.

## Bonami integration target

The useful target is now:

```text
Envoy/NIPC-aware application
          |
          v
     nipc.library
          |
   +------+------------------+
   |                         |
entities / transactions     discovery
   |                         |
RDP over bsdsocket       Bonami bridge
                             |
                        bonami.library
                             |
                          mDNS/DNS-SD
                             |
                         _nipc._tcp
```

Bonami does not replace `nipc.library`, the Services Manager, or their native
inquiry path. It may complement them by providing DNS-SD/mDNS discovery of
NIPC entities across hosts. Conversely, NIPC must not become a
dependency of Bonami's core register/browse/resolve API: AppleMIDI, Network
MIDI 2.0, printing, and other mDNS clients should work without Envoy.

## Remaining validation

The next work is integration rather than implementing NIPC from scratch:

1. build the pinned Bonami revision against the upstream AROS Envoy headers;
2. identify any source/ABI assumptions in Bonami that still require AROS port
   changes;
3. enable and test Bonami's NIPC inquiry bridge;
4. verify publication and browsing of `_nipc._tcp`;
5. compare Bonami discovery results with AROS `NIPCInquiryA()` semantics and
   the Services Manager service list;
6. determine the ownership boundary between native NIPC inquiry and Bonami
   DNS-SD discovery, avoiding duplicate or conflicting discovery state;
7. test entity discovery, service lookup, Accounts Server access, and
   transactions between two AROS systems;
8. separately test interoperability with classic Envoy releases;
9. upstream generally useful Bonami fixes and keep AROS-specific build/service
   glue in the AROS tree.

## Classic Envoy interoperability

The presence of AROS `nipc.library`, `services.library`, and
`accounts.library` changes the implementation problem substantially but does
not by itself prove compatibility with every historical Envoy release.

API compatibility, discovery compatibility, and wire-protocol compatibility
should be tested separately. The upstream `NipcTest` already contains useful
paths for entity transactions, ping, inquiry, Accounts Server, and Services
Manager. These provide a practical basis for interoperability testing.

Future testing should determine which Envoy releases are compatible with the
AROS RDP/resolver implementation and whether Bonami's `_nipc._tcp` discovery
can coexist with or complement historical Envoy discovery.

## Relationship to MIDIHub

The dependency direction remains deliberately loose:

```text
                    AROS network infrastructure
                    /                       \
          nipc.library                 bonami.library
               |                         /       \
          Envoy/NIPC              _nipc._tcp   mDNS/DNS-SD
                                                |
                                  +-------------+-------------+
                                  |                           |
                           _apple-midi._udp               _midi2._udp
                                  |                           |
                              AppleMIDI              Network MIDI 2.0
                                  \                           /
                                   +--------- MIDIHub --------+
```

MIDIHub does not need NIPC to provide MIDI networking. The project merely
surfaced Bonami as a useful mDNS implementation and therefore exposed the
opportunity to connect Bonami's existing Envoy bridge to the new AROS NIPC
infrastructure.

## Future decision points

Before treating the integration as complete, answer:

- Does Bonami compile unchanged against AROS's new `<envoy/nipc.h>`?
- Which Bonami NIPC publish operations are complete versus still stubs?
- How should Bonami DNS-SD discovery complement native `NIPCInquiryA()` and
  the Services Manager service list?
- Should integration be direct, optional, or provided through a separate
  adapter, and which component owns deduplication/lifetime of discovered
  entities?
- Does `_nipc._tcp` interoperate with classic Bonami/Envoy environments?
- Which classic Envoy version(s) should be explicit compatibility targets?
- Which changes belong in Bonami upstream and which belong in AROS?

None of this should delay the MIDI networking roadmap, but the former missing
NIPC dependency is no longer a reason to disable Bonami's Envoy bridge.
