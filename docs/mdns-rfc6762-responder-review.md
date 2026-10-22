# PR #1591: RFC 6762 responder review

**Historical revision:** the [2026-10-18 follow-up](bonami-review-followup-2026-10-18.md)
records fixes and 25 passing fixtures at the updated #1591 head. The failures
below describe the original revision.

Date: 2026-10-17. Published head checked again on GitHub:
`24f476235e7ed6babaad84a44e90242948a029f9`; no reviews/comments were present.
This pass examines **the open registration PR**, without assuming acceptance
of Bonami's public API. Production sources and upstream branches were not changed.

## Result and provenance

Recommendation remains **changes requested**. Five additional findings are
recorded below; BA-013 is inherited from the PR's parent, rather than newly
introduced. Earlier BA-005 and MD09 now also have executable packet-path
evidence. Findings are functional/protocol issues, not API/style objections.

The tests compile exact PR copies of `mdns_dns.c`, `mdns_host.c`, `mdns_reg.c`
and `mdns_packet.c`, with exact PR public/private headers. Hand-built DNS
fixtures enter `mdnsHandlePacket()`; outgoing packets are captured instead
of sent. Mocked facilities are Exec lists/allocation, time and network I/O.
The input encoder is in the test and does not call the implementation's
packet-writing helpers. This provides deterministic protocol evidence,
not proof of native ABI, scheduling or independent network interoperability.

The parent used to distinguish inherited code is
`334116075a52306b69358e51200ae33b6e7d602b`. `git show` and the complete
`mdns_packet.c` diff confirm that header OPCODE/RCODE filtering was already
absent there. New service matching also repeats an existing host-class gap.

## Governing requirements

| Reference | Rule used by the tests |
|---|---|
| RFC 6762 §§6.6, 9 | Identical record data is not a conflict. |
| RFC 6762 §7.1 | Suppression depends on the matching answer and its remaining TTL relative to the responder's actual TTL. |
| RFC 6762 §6 | Match question class, allowing class ANY and masking the QU bit. |
| RFC 6762 §§18.3, 18.11 | Ignore received nonzero OPCODE/RCODE. |
| RFC 6762 §§8.3–8.4 | Changed record data repeats the announcement procedure, including at least two announcements. |
| RFC 6762 §8.2.1 | Resolve simultaneous probes using the ordered proposed record sets. |

These are requirements or algorithmic conditions in
[RFC 6762](https://www.rfc-editor.org/rfc/rfc6762.html), rather than preferences
inherited from Bonami. The update/announcement, suppression, class matching
and header rejection requirements are mandatory; record-set tie-breaking
specifies the procedure for deciding ownership. Timing recommendations and
optional capabilities must continue to be identified separately.

## BA-010 — identical SRV responses cause false conflict and rename

**Severity:** medium. **Category:** confirmed protocol defect introduced in
the new registration implementation.

**Location:** `workbench/network/mdns/mdns_reg.c:482–503`,
`mdnsRegHandleRecord()`; called by `mdns_packet.c:134`.

The service announces `Session._midi2._udp.local`, port 5673, target
`arosbox.local`. A response from a different sender contains an identical
SRV, class IN with cache-flush, TTL 60. The service becomes `Session (2)`
and returns to probing. The handler matches the owner string/type but never
compares the received record data to its own. A cooperating responder or
proxy publishing the same data therefore disrupts the endpoint's discovery
identity. This is not a real name conflict.

**Executed evidence:** `identical-srv` fails; `different-srv` passes the
positive control by detecting genuinely changed port data.

**Fix direction:** compare name, type, class and canonical record data before
starting conflict handling. Use existing DNS/probe helpers where appropriate;
SRV target compression needs packet context, so a raw comparison of the copied
`rdata` array is insufficient. Preserve the required response to genuinely
conflicting data. Add identical/different TXT, compressed SRV, class and low-TTL
cooperating-response cases. The current test reproduces the uncompressed SRV case.

## BA-011 — known-answer suppression loses TTL and record identity

**Severity:** medium. **Category:** confirmed protocol defect in new code.

**Location:** `mdns_packet.c:63–80`; `mdns_reg.c:785–792`.

Two failures share the same information-loss problem:

- A registered service has TTL 60. A query already knows its PTR with 40
  seconds remaining. It still gets a full response: the packet walker compares
  against `MDNS_TTL_OTHER / 2` (2250), not half the registered TTL (30).
- A query includes a known PTR owned by `_unrelated._udp.local`, whose target
  happens to be `Session._midi2._udp.local`, TTL 3000. The legitimate
  `_midi2._udp.local` browse response is suppressed. The walker preserves
  only the target string, so the service matcher cannot check the PTR owner,
  class or TTL.

**Executed evidence:** `known-short` and `known-wrong-owner` fail. Default
TTL suppression (`known-default`) and a correctly expiring TTL-20 answer
(`known-expiring`) pass as controls. One failure causes redundant traffic;
the other can hide an answer. Do not describe them solely as an optimization.

**Fix direction:** preserve/compare full known-answer identity and TTL, or
evaluate suppression against registrations while parsing each record. The
existing parsed `MDNSRecord` already has fields that are currently discarded.
Use each actual publication TTL. Test threshold boundaries, record class and
multiple types in the same query; keep bounded memory and packet validation.

## BA-012 — service replies ignore the question class

**Severity:** low. **Category:** confirmed protocol defect in the new service
matcher; an analogous gap exists in the pre-existing host matcher.

**Location:** `mdns_reg.c:738–824`, `mdnsRegHandleQuery()`.

A question for `Session._midi2._udp.local` SRV, class CH (3), gets a packet
with the service's four class-IN records. The matcher checks owner/type and
the QU bit but not the remaining question class.

**Executed evidence:** `class-ch` fails. `class-in` and `class-any` both pass.

**Fix direction:** accept a matching IN class or class ANY after masking QU;
avoid an IN-only check that breaks valid ANY queries. Apply the same scrutiny
to host/type-enumeration/proxy paths. Additional convenience records do not
justify answering an entirely unmatched question.

## BA-013 — invalid OPCODE/RCODE packets are processed

**Severity:** low for the demonstrated query case. **Category:** confirmed
pre-existing protocol defect exposed through the new service path.

**Location:** `mdns_packet.c:46–58`, `mdnsHandlePacket()`.

Queries with OPCODE 1 (`flags=0800`) or RCODE 1 (`flags=0001`) receive the
ordinary service response. The packet path uses QR to choose processing but
does not reject these invalid header fields.

**Executed evidence:** `opcode` and `rcode` fail. The same missing validation
is visible in the parent source. **Do not claim #1591 introduced it.**

**Fix direction:** reject these headers before processing questions/records;
also test invalid response headers with conflicting data, ensuring no rename
or state mutation. Decide whether to repair the common responder in a small
independent change or alongside the touched parser. This is separate from
the new-registration defects above.

## BA-014 — service updates emit only one announcement

**Severity:** medium. **Category:** confirmed protocol defect in new code.

**Location:** `mdns_reg.c:905–924`, `mdnsRegUpdate()`.

Changing the live SRV port from 5673 to 6000 sends one immediate announcement.
After advancing the fake clock through two further seconds, the scheduler
has emitted no second announcement. `mdnsRegUpdate()` neither restarts the
announcement step counter nor schedules its next step.

**Executed evidence:** `update-port` fails with `port=6000, announcements=1`.
`announce-initial` passes with two announcements through the same scheduler;
the second-announcement mechanism exists but is not used for updates.

**Fix direction:** reuse the existing bounded announcement scheduling on data
changes, keeping record identity and cache-flush semantics. Test port-only,
TXT-only and combined updates, the second-send interval, repeated updates,
and termination during a pending second announcement. The current test
demonstrates the missing send; it is not a packet-loss interoperability run.

## Previous findings strengthened by this pass

**BA-005 — individual deregistration withdraws the live host A record.**
`withdraw-service` removes the only registration while the host remains in
announced state; the captured goodbye includes `arosbox.local A` with TTL 0.
This executes the original PR's registration/removal and packet construction,
upgrading the earlier source-only observation. Client cache impact still
needs an independent peer run; a captured host goodbye itself is proven.

**MD09 — TXT is omitted from the local simultaneous-probe comparison.**
The local service owns TXT `v=a`. The peer proposes the same SRV and TXT
`v=z`, which would win when comparing both services' complete sets. The local
code compares its SRV against the peer's first TXT, keeps `step=1` and a
13-tick retry, instead of yielding with a fresh probe delayed by a second.
`probe-txt` fails. This is executable evidence for the existing backlog item,
not a newly invented architecture requirement. Fix complete proposal and
comparison together; retest the opposite ordering and identical sets.

## Reproduction and current limits

```sh
rtk proxy python3 tools/review-mdns-responder.py --aros-tree /home/jaime/AROS-net
```

Results: **15 cases, 6 pass, 9 fail** under ASan/UBSan. Failures above are
semantic assertions, not sanitizer memory reports. Exit 1 means findings
remain; per-case logs are saved in `build/mdns-responder-review/<commit>/`.
`--revision` permits repeating the checks against a corrected revision.

The previously added discovery-contract suite was rerun because its host
type shim gained list/message definitions: its original result is unchanged
(2 pass, 5 semantic failures). No product code was changed and no native
build, scheduler test, hardware test, or independent peer exchange was run.

Prioritize BA-010/011/014 and the already reproduced BA-005/007 for the next
registration corrections; include MD09's ownership test and negative parser
tests in the acceptance set. Keep BA-013's inherited provenance visible.
This pass does not approve the rest of the PR or finish the other four PRs.
