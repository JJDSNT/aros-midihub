# AROS PR #1845: gather legacy unicast service answers

[AROS #1845](https://github.com/aros-development-team/AROS/pull/1845) is
ready for review, not a draft. Branch `review/mdns-legacy-unicast`, commit
`e69e9a8030`, is one commit directly on AROS `master` `412963d55f`.

## Scope

RFC 6762 section 6.7 treats a query received from a source port other than
5353 as a legacy unicast DNS query. `mdnsd` already returned the query ID and
question, capped TTLs at ten seconds, omitted the cache-flush bit and sent the
reply only to the requester. When a PTR question matched several local
services, however, it sent one complete DNS response per service.

This change builds one response while walking all matching services. It emits
another response only if the bounded 1400-byte response cannot hold the next
service. Services on this host share one A record in a gathered response;
proxied services retain their own address records. Multicast and QU response
paths are unchanged.

No public API, ABI, library version or imported copyright changes.

## Executable evidence

The host fixture registers two services of one type and sends a query from a
non-5353 port. On AROS `master` it observes two packets and eight records. On
this branch it observes one packet and seven records: two PTR/SRV/TXT sets and
one shared host A record.

The hosted AROS runner independently checks the wire contract: one packet,
both PTR answers, the original ID and question, TTL no greater than ten,
class without cache flush, unicast to the requester's port and no multicast
copy.

## Validation

- responder fixture under ASan/UBSan: 27/27;
- discovery regressions under ASan/UBSan: 38/38;
- queued record-query regressions under ASan/UBSan: 12/12;
- generated `mdnsd` builds: linux-x86_64 and amiga-m68k;
- Linux-hosted AROS wire test with two registered services;
- repository `make test`.

The test does not claim IPv6, multi-packet known-answer completion or DNS over
unicast DNS; those remain separate backlog items.
