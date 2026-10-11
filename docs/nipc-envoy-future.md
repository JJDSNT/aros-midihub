# AROS NIPC / Envoy and Bonami integration

## Status

**Bonami's side of the bridge works (2026-10-11); `nipc.library` does not use it yet.**

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

## What works now

Tested by `tools/bonami-hosted.sh` on hosted AROS against Avahi, which plays
the other Envoy host:

- `PublishNIPCEntitiesA()` announces this host as an instance of
  `_envoy-nipc._tcp` whose TXT record lists its entities, services and owner;
  Avahi resolves it.
- `InquiryFromNIPCTagsA()` with `MATCH_ENTITY` finds the host that announces
  the entity and hands the hook the tag list `NIPCInquiryA()` would, then
  ends with the NULL call. A host without the entity is not reported.

The type is `_envoy-nipc._tcp`, as Bonami's headers have it; earlier notes
here said `_nipc._tcp`.

## Joining nipc.library to it

Nothing in AROS calls the bridge. Two changes in `nipc.library` would, both
optional at run time (no `bonami.library` or no mdnsd: nothing happens):

1. **Publishing.** When the set of public entities or offered services
   changes, the library's own process calls `PublishNIPCEntitiesA()` with
   the current lists. It is the process that owns the publication, so it
   lasts as long as the library runs. mdnsd may start later than
   `nipc.library`, so the call is repeated from the inquiry heartbeat until
   it succeeds.
2. **Inquiry.** `NIPCInquiryA()` of the plain kind (not through a Realm
   Server) also starts a Bonami inquiry. Bonami's hook runs in mdnsd's
   process; it must only copy the answer and send it to the library's
   process, which treats it as one more reply to the same `struct Inquiry`.
   That keeps what the application sees unchanged: one hook, called in the
   library's process, one NULL call at the end, one response count. The
   library must drop an mDNS answer for an address that already answered by
   broadcast, and the reverse, or an AROS host that speaks both is reported
   twice.

Open points for that work: which of the two answers wins when they differ
(the broadcast reply carries more, such as versions and memory); whether a
host name from mDNS (`amiga1`) and one from Envoy (`realm:amiga1`) are the
same host; and whether a classic Envoy host can be published by proxy
(`MDNS_HostName` with `MDNS_HostAddr`) by an AROS host that hears it.

## Remaining validation

1. make the two `nipc.library` changes above and test them between two AROS
   systems, with and without mdnsd;
2. compare what an application sees from `NIPCInquiryA()` before and after;
3. test entity discovery, service lookup, Accounts Server access, and
   transactions between two AROS systems found through mDNS only (different
   broadcast domains are out: mDNS is link-local too);
4. separately test interoperability with classic Envoy releases and with
   amigazen's Bonami and nipc on a classic system;
5. offer the tag definitions and the TXT layout to Bonami upstream, which
   has `PublishNIPCEntitiesA()` as a stub.

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
