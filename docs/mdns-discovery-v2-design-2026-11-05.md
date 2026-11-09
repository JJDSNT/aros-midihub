# Bonami dual-stack discovery event design — 2026-11-05

> **Disposition, 2026-11-08:** retained as implementation evidence and a
> Bonami compatibility proposal, not as the native AROS DNS-SD ABI. The new
> library is intentionally free of Bonami vector, tag and structure
> constraints; see the
> [native DNS-SD ADR](dnssd-native-architecture-decision.md).

## Verified baseline

The implementation source is AROS `workbench/network/mdns/`, with the DNS-SD
subtype proposal from AROS PR #2118 as the chronological base.  The historical
copy under `network/mdns/aros/src/` is not an implementation source.

The existing public discovery contract consists of:

- `BrowseServiceTypeA()` and `ResolveServiceA()` with
  `MDNS_EventMode=MDNS_EVENT_QUEUE`;
- task-owned opaque handles and the existing stop/request-wakeup calls;
- `ReadMDNSEvent()` returning the fixed `MDNSDiscoveryEvent` layout;
- a daemon queue containing that same IPv4-only layout.

`MDNSDiscoveryEvent` has one IPv4 endpoint and one IPv4 interface address.  It
has no size/version field, address family, interface index or SRV target.
Growing it would overwrite an old caller's allocation.  Current daemon
snapshots therefore become complete only after an A record arrives, even
though the shared per-link cache already accepts AAAA records and asks for A
and AAAA.

The cache can retain several A and AAAA records for one SRV target.  The
current snapshot selects only the newest A record.  The pending-address cache,
used when an address arrives before its SRV, currently replaces another
address of the same family/owner/link and therefore also needs a small
correction before multi-address delivery is complete.

## Minimum compatible extension

Add one new queue mode and one appended library vector:

- `MDNS_EVENT_QUEUE_V2`, selected through the existing `MDNS_EventMode` tag;
- `ReadMDNSEvent2(ref, event)`, appended after all existing vectors;
- a fixed, pointer-free `MDNSDiscoveryEvent2` containing the event code,
  32-bit interface index, SRV priority, weight and port, uncompressed DNS-wire
  SRV target, counted TXT, and a bounded array of IPv4/IPv6 addresses;
- explicit address-family values and an address-truncation flag.

The existing start/stop/notification vectors, handle ownership, cancellation,
daemon-incarnation checks and bounded queue semantics remain shared.  The IPC
message and daemon queue carry the rich event once; `ReadMDNSEvent()` keeps its
old layout and is accepted only for a V1 queue, while `ReadMDNSEvent2()` is
accepted only for a V2 queue.  This prevents an IPv6-only snapshot from being
silently converted to `0.0.0.0` for an old client.

V1 and V2 sessions may browse the same cached view concurrently:

- V1 receives an ADD/RESOLVED only while the snapshot contains IPv4;
- V2 receives an ADD/RESOLVED while it contains at least one IPv4 or IPv6
  address;
- type-enumeration events remain addressless and are valid in both modes;
- if the last IPv4 address disappears while IPv6 remains, V1 receives REMOVE,
  while V2 receives the normal changed-snapshot REMOVE followed by ADD;
- no parallel daemon, cache, browse engine or resolver is introduced.

Addresses are sorted deterministically by family and bytes before they enter
the event.  A bounded event reports truncation instead of making queue size or
IPC allocation unbounded.  Every address belongs to the event's per-link
interface index, which supplies the scope needed for link-local IPv6.

The subtype feature is not a functional dependency.  It is only the chosen
branch base because both changes touch the public header, Bonami vector table,
IPC structure, discovery engine and documentation; stacking avoids knowingly
creating a large textual conflict while #2118 is reviewed.

## Implemented local commits

The unpublished AROS branch `review/mdns-discovery-v2`, based on the subtype
proposal, contains two commits:

1. `f540abb163 bonami: add dual-stack discovery events` adds the public V2
   event, queue mode, appended vector and AutoDoc, bumps Bonami from 41.7 to
   41.8 and private IPC from 6 to 7, and implements the shared rich snapshot
   plus V1 projection. Keeping the API and engine in one commit avoids an
   intermediate revision that advertises a non-functional public interface.
2. `f95e151555 docs: describe dual-stack Bonami discovery` documents the
   version, ownership, address ordering/truncation, link scope and V1/V2
   transition behavior.

DNS-SD tool adoption remains a following consumer change; it is not needed
to establish the public interface. The branch has not been pushed and no PR
has been opened.

## Required validation

Host fixtures must demonstrate the missing capability before the candidate
passes:

- an AAAA-only service resolves through V2 but remains absent through V1;
- A-only, AAAA-only and dual-stack services do not repeatedly query the
  intentionally absent family after the existing one-time follow-up;
- several A and AAAA records survive both record orderings (addresses before
  and after SRV), are deterministic and remain isolated by interface index;
- V1 and V2 sessions can consume the same service concurrently without queue
  cross-talk;
- address removal produces the correct V1/V2 transitions;
- event overflow/truncation is explicit and does not corrupt adjacent fields;
- old structure sizes, existing vector offsets and `ReadMDNSEvent()` results
  remain unchanged.

The proposal then needs complete hosted AROS compilation/linking, m68k
cross-compilation/linking, and hosted runtime against an independent Linux
peer for IPv4-only, IPv6-only and dual-stack publication.  Hosted fixtures
and cross-builds are not hardware runtime and must be reported separately.

## Verification completed

- The host discovery fixture compiled the current canonical AROS proposal
  sources with ASan/UBSan and passed 47/47 cases. New cases cover AAAA-only
  V2 discovery and resolution, addresses before SRV, link isolation, bounded
  truncation, and V1/V2 address-family transitions.
- The unchanged query and responder fixtures passed 14/14 and 38/38 cases,
  respectively, with ASan/UBSan. These are host fixtures, not AROS runtime.
- The persistent x86-64 AROS hosted build completed and linked both `mdnsd`
  and `bonami.library` from the proposal source.
- A native AROS probe linked against Bonami 41.8 and ran under AROS hosted.
  An independent Linux packet generator published A-only, AAAA-only and
  multi-address dual-stack services over IPv6 multicast from UDP/5353 with
  hop limit 255. V2 received all three; V1 received A-only and dual-stack but
  no AAAA-only event; V2 also resolved the AAAA-only service. Wrong V1/V2
  read vectors returned `MDNSERR_BADPARAM` without consuming an event. The
  run ended with `BONAMI-DISCOVERY-V2: OK (0 failures)` and no AROS trap.
- The m68k AROS target completed cross-compilation and linking. Fresh mDNS
  objects, `mdnsd` and `bonami.library` were produced; `nm` shows the original
  `Bonami_20_ReadMDNSEvent` and appended `Bonami_26_ReadMDNSEvent2`. This was
  cross-build/link evidence only: no m68k runtime was executed. Regenerating
  that copied build exposed an unrelated missing `partition_support.h`
  include path, so a symlink was added only inside the build tree to complete
  the target; no partition source change is part of G2.

Still outstanding are hardware runtime and interoperability with a real
Avahi-published service. The hosted peer was an independent DNS encoder, not
Avahi, and its announcements used IPv6 transport even for the A-only record;
therefore it proves the Bonami A-only result path but is not a separate
IPv4-only network-transport run.

