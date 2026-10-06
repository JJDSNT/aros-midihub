# AROS NIPC / Envoy and Bonami integration

## Status

**Upstream capability available; integration and interoperability remain future work.**

AROS now contains a clean-room `nipc.library` implementation of the Envoy NIPC
API over `bsdsocket.library`, added upstream in commit
`fbc2e274d880868c3fe447dffdfc9226fae0bd66`. The same change installs public
`<envoy/nipc.h>`, `<envoy/nipclowlevel.h>`, and `<envoy/errors.h>` headers
and adds a `NipcTest` command.

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

Bonami does not replace `nipc.library`. It can complement it by providing
DNS-SD/mDNS discovery of NIPC entities. Conversely, NIPC must not become a
dependency of Bonami's core register/browse/resolve API: AppleMIDI, Network
MIDI 2.0, printing, and other mDNS clients should work without Envoy.

## Remaining validation

The next work is integration rather than implementing NIPC from scratch:

1. build the pinned Bonami revision against the upstream AROS Envoy headers;
2. identify any source/ABI assumptions in Bonami that still require AROS port
   changes;
3. enable and test Bonami's NIPC inquiry bridge;
4. verify publication and browsing of `_nipc._tcp`;
5. compare Bonami discovery results with AROS `NIPCInquiryA()` semantics;
6. test entity discovery and transactions between two AROS systems;
7. separately test interoperability with classic Envoy releases;
8. upstream generally useful Bonami fixes and keep AROS-specific build/service
   glue in the AROS tree.

## Classic Envoy interoperability

The presence of an AROS `nipc.library` changes the implementation problem but
does not by itself prove compatibility with every historical Envoy release.

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
- Should AROS NIPC use Bonami discovery directly, optionally, or through a
  separate system integration layer?
- Does `_nipc._tcp` interoperate with classic Bonami/Envoy environments?
- Which classic Envoy version(s) should be explicit compatibility targets?
- Which changes belong in Bonami upstream and which belong in AROS?

None of this should delay the MIDI networking roadmap, but the former missing
NIPC dependency is no longer a reason to disable Bonami's Envoy bridge.
