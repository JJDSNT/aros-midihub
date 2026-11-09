# Bonami V2 DNS-SD tool adoption — 2026-11-05

## Verified baseline

The canonical implementations are in AROS `workbench/network/mdns/tools/`.
Current `dnssd -B`, `dnssd -L`, Nearby and Handshake request
`MDNS_EVENT_QUEUE` and call `ReadMDNSEvent()`, so they cannot see or resolve an
AAAA-only service. `dnssd -Z` also uses the V1 browse result and starts a
second generic SRV query to recover the target discarded by the legacy event;
it prints one A record and no AAAA record.

`dnssd -q` already supports AAAA through `QueryRecordA()` and `dnssd -G` is an
explicitly IPv4-only `LookupHostA()` command. Service registration and Share
do not consume discovery events. These paths do not need to change.

The public interface required by the tools is the G2 proposal in AROS #2123:
Bonami 41.8, `MDNS_EVENT_QUEUE_V2`, `ReadMDNSEvent2()` and the fixed
`MDNSDiscoveryEvent2`. Calling the appended vector against an older revision
is unsafe even though the library major version remains 41, so each affected
tool must check revision 8 before starting a V2 operation.

## Minimum change

- add shared printing for the V2 address list, including uncompressed DNS
  target display and IPv4/IPv6 formatting without opening bsdsocket.library;
- move `dnssd -B`, `-L` and `-Z` to V2; `-Z` uses the event's complete SRV,
  TXT and bounded A/AAAA data instead of a second SRV query;
- move Nearby and Handshake to V2 so IPv6-only services are visible;
- reject only the affected modes with a clear Bonami 41.8 requirement when
  an older library is installed; retain unrelated dnssd modes;
- do not change Bonami ABI, daemon code, registration tools or generic query
  behavior.

This branch depends on #2123 and must remain local until that API is accepted.

## Validation criteria

- full AROS build/link of all mDNS tools;
- host tests for V2 formatting, multiple addresses, IPv6 and malformed bounds;
- hosted runtime against A-only, AAAA-only and dual-stack services;
- `dnssd -Z` output contains the real Avahi SRV target plus A and AAAA owners;
- Nearby sees an IPv6-only service and Handshake resolves it;
- older-revision gating is exercised without calling LVO 26;
- existing `dnssd -q`, `-G`, registration and Share behavior remains intact.
