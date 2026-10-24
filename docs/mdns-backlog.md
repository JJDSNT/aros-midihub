# mDNS backlog — from a service that runs here to one in the AROS tree

**2026-10-18:** see the [#1591 fixes](bonami-review-followup-2026-10-18.md)
and [browse/resolve draft](mdns-discovery-followup-2026-10-18.md). The owner
authorized preparing a polling proposal in draft; the maintainer's event-API
decision is still open. Full local Hook delivery and N9 remain separate.

**Scope:** the system mDNS and DNS-SD service ported from Bonami:
`mdnsd`, `bonami.library`, the tools and the commodity in
`network/mdns/aros`, the cut of it that went to AROS as PR #1560 (worktree
`~/AROS-net`, branch `mdnsd-hostname`), and the way of the rest into the
AROS tree. The target is acceptance in AROS; staying compatible with the
original Bonami is not a constraint. Background:
[the port](../network/mdns/aros/README.md), whose "Known limits" this file
orders, [mDNS and AROSTCP](mdns-arostcp.md) and
[upstream pull requests](upstream-prs.md).

The [2026-10-17 discovery contract audit](network-midi-mdns-contract-audit.md)
records the current MIDI integration and additional findings BA-007–009 and
DI-001–003. The starting state below is historical; Network MIDI now uses
the local system registration/browse API and its embedded responder is gone.

The [#1591 RFC responder review](mdns-rfc6762-responder-review.md) adds
BA-010–014 and packet-path reproductions of BA-005 and MD09. See the #1591
follow-up for fixes; the historical runners below did not cover these cases.

**State this starts from** (2026-10-12):

- The service runs on Linux-hosted AROS over two veth links against the
  host's Avahi: `tools/bonami-hosted.sh` checks 24 things, and six more
  runners check the host name (21), the tie-break (2), known answers (5),
  delayed answers (3), unicast answers (5) and the responder alone (22).
- It registers, updates and withdraws services, browses, resolves, looks up
  hosts, asks for any record, and follows interfaces coming and going and
  the stack being reloaded.
- What AROSTCP lacked for multicast is merged upstream (PR #1543, #1558).
- The first cut, a responder for the host's own name, is PR #1560: merged
  on 2026-10-13. Issue #1559 asks six questions about the
  rest: where it lives, how to split it, how clients get events, which
  protocol limits block it, where tests go, what to call the library. No
  answer yet.
- It builds for amiga-m68k and raspi-aarch64 and has run on neither, nor
  against Apple's Bonjour, nor between two AROS systems.
- No program but its own tools uses it. Network MIDI still carries its own
  responder.

Priorities as in the other backlogs.

## Done

What was done before this backlog is in the port's README and in the
history of `tools/mdns-*-hosted.sh`. Under it:

| # | What was done |
|---|---|
| MD02 | A value in a service's text may have spaces. In the string a client gives and gets, a space that belongs to an item has a backslash before it and a backslash is written twice; the items go into the TXT record, and come out of it, as the strings they are (`mdnsTxtNext()`, `mdnsTxtAppend()`). Text without backslashes means what it meant before. `dnssd -R` marks its arguments so. `tools/mdns-txt-hosted.sh`: Avahi reads four items registered on AROS, two with spaces and one with a backslash, as written, and AROS is given the host's `name=Host With Spaces` with its spaces marked. The whole text is still at most 256 characters, and a record's item that does not fit is left out whole. Whether a string with backslashes is the form AROS wants for this, or counted strings, is one more question for the review. |
| MD19 | A client says how long its service's records may live: `MDNS_TimeToLive` in `RegisterServiceA()`, 1 to 4500 seconds, anything else refused. The service's PTR, SRV and TXT records are announced and answered with it; the address record is the host's and keeps its 120 seconds. Without the tag nothing changes: 75 minutes, and 2 for the SRV record (RFC 6762 section 10). `dnssd -R ... -t seconds`. `tools/mdns-ttl-hosted.sh` reads the announcements off the wire: 60 seconds where asked, the defaults where not, nothing announced for 0 and for 4501. `UpdateServiceA()` does not change it. The message between library and daemon grew a field, and its version went from 4 to 5. |
| MD21 | `dnssd -Z` prints a service's host as it is, `arosbox.local.`, not with the domain a second time. Seen on hosted AROS in the output of `tools/mdns-txt-hosted.sh`, for a service of AROS's own and for one of the host's. |
| MD04 | A client that asks, through `QueryRecordA()`, for a record this host itself holds is given it: the daemon, which does not hear itself, writes every announced service's records and the host's address as they would go out and reads them back for the query that has just begun, once, before it asks the link (`mdnsRegAnswerLocally()`). `tools/mdns-txt-hosted.sh`: `dnssd -q` for a local service's SRV record and for the host's own address prints each, once. Not covered: a service that is still probing when the question comes, which is answered when it is announced only if the question is asked again. |

## P0 — wrong or unsafe as it is

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| MD01 | A client's Hook is called in mdnsd's process. Every client has to know it may only copy and signal there; one that prints, calls the library or crashes does it inside the daemon, for every other client. | Deliver events outside the daemon: a message to the client's own port, or a dispatcher per client in the library. Which one is question 3 of issue #1559; do not build before the answer. | A client that misbehaves in its callback harms only itself; the tools no longer share a helper to copy and signal. |

## P1 — before another program depends on it

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| MD03 | A client is not told the final name of its service after a conflict renamed it. | An event for it, in the form MD01 settles. Until then a client can ask with a browse for its own type. | The test that takes "Name (2)" sees the client told. |
| MD05 | Never run outside hosted x86-64. | m68k in the emulator and raspi-aarch64 on a board, with the runners' checks done by hand or adapted. Byte order and alignment are the point on m68k. | A table here: target, date, what passed. |
| MD06 | Never met Apple's Bonjour, Windows' resolver, or a second AROS. | Each when the machine is at hand; Bonjour's conformance test if it can be run against AROS. | The same table. |
| MD07 | The number in a host name changed after a conflict (`name-2`) is not kept across a restart of mdnsd, so every restart probes the taken name again and loses again. | Keep it in `ENV:` for the session. | The host-name runner restarts mdnsd and sees one probe series, not two. |
| MD08 | Other SANA-II drivers were not looked at. A card that filters multicast in hardware has to honour `S2_ADDMULTICASTADDRESS`; only the hosted driver was checked, and its host interface is promiscuous. | Read the drivers in `workbench/devs/networks` for the command and list the ones that ignore it; test on any real card available. | The list exists; one real card answers a browse. |
| MD20 | Seen once in twenty-five runs of `tools/applemidi-discovery-hosted.sh` on 2026-10-12: Avahi receives the PTR record of a service registered on AROS and then fails to resolve the instance ("Timeout reached"), so the SRV, TXT or address question went unanswered or the answer was not taken. The failing run was not captured. It first appeared after MIDIHub began to browse the same type it registers, which may or may not be related: `tools/bonami-hosted.sh` registers and browses one type too and has not shown it. | Do not chase it blind. The runner now keeps every mDNS packet of the link; when a run fails, read which question Avahi asked and what mdnsd sent back, and how long after the announcement. | A cause named from a capture, and a runner case that provokes it. |
| MD22 | #1743 changed after its 2026-10-22 review and was checked only by host fixtures and cross builds: mdnsd no longer announces an `UpdateServiceA()` that changes nothing (`update-unchanged` in `tests/review/mdns_responder_rfc.c`), netmidid keeps its registration across a change of `ENV:NetMIDI` that leaves name and port alone, and a refused registration prints its reason once. The native run with packet capture listed in the PR's earlier validation was not repeated. | On hosted AROS with the capture runner: no announcement every ten seconds while registered; a change of latency in `ENV:NetMIDI` sends no goodbye; a second service of the same name logs `MDNSERR_DUPLICATE` once; a restart of mdnsd is still followed by a new registration. | The capture shows each, at revision `5666327816` or later. Partly done 2026-10-22: `tools/applemidi-discovery-hosted.sh` on hosted x86-64 with master's mdnsd plus the #1743 mdnsd commit and bonami.library 41.2, netmidid from this repository (registration code as #1743's): the capture shows the two announcements at registration and none more in the 21 s that followed, with handle checks every 10 s; the renamed session got its goodbye and its own probe; the stopped service its goodbye. Left: a latency change sending no goodbye, and the `MDNSERR_DUPLICATE` message. |

## P2 — the protocol, before calling it complete

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| MD09 | The tie-break compares the SRV record only. RFC 6762 section 8.2 compares all the records proposed for the name, TXT among them. | Propose and compare both, in the order the RFC gives. | The tie-break runner has a case decided by the TXT record. |
| MD10 | Known answers that do not fit one packet are left out; the truncation bit and a second packet are not used, and only browse questions carry known answers. | Sections 7.1 and 7.2 in full: the bit when sending, and waiting for the rest when receiving. | A runner with more known answers than fit. |
| MD11 | The answer to "which service types are there" goes at once, not after the random delay, and a record that was multicast less than a second ago is sent again all the same. | The delay for every shared record; the one-second rule of section 6. | The delay runner covers both. |
| MD12 | Several services of one type go to a plain DNS resolver as one packet each, not as one answer. | Gather them. | The unicast runner counts one packet. |
| MD13 | At most eight interfaces; an address that changes is noticed within three seconds, by looking. | A number from configuration; a notification from the stack if it has one to give, which is a question for AROSTCP. | A test with nine interfaces, or the limit stated as chosen. |
| MD14 | IPv4 only. AROSTCP has what IPv6 needs. | AAAA records, the group `ff02::fb`, a socket per interface. Asked in issue #1559; do not start before the answer. | Avahi resolves the host over IPv6. |
| MD15 | Byte order is written three ways: `htons()` on aligned fields, shifts by hand in about 25 places for fields inside a packet, and `AROS_BE2LONG` in two. | Decision D1 below, then one way. | One style; the hand-written shifts are four functions. |

## P3 — polish

| # | Limit or defect | What to do |
|---|---|---|
| MD16 | The Envoy bridge publishes and finds hosts but does not match what DNS-SD cannot tell (memory, versions, realms), and AROS's `nipc.library` does not call it. | Nothing until someone wants Envoy discovery; see [the notes](nipc-envoy-future.md). |
| MD17 | The commodity has no automatic test; its Share panel, hot key and Exchange were never tried, and a label overlaps a gadget with the default font. | Try them by hand; lay the row out by the font. |
| MD18 | On amiga-m68k AROSTCP does not build until `fd.library`'s headers exist, because its build file does not ask for them. | A one-line dependency, as a pull request of its own. |
| MD25 | What #1695 left out of discovery and the Bonami port still had (its "Limits and follow-up"), now the rest of the port to present: (1) **service types on the link**, RFC 6763 section 9: #1802, review fixes applied 2026-10-24 (no SRV/TXT for type targets; cached types replayed to a second browse); (2) **host lookup and record queries** (`LookupHostA`, `QueryRecordA`/`StopQuery`, vectors 16–18 in the prototype, now taken by `ReadMDNSEvent`/`RequestMDNSEvent`): new vectors, and an event that can carry a record; (3) **this host's own registrations in the discovery cache**: a browse on AROS does not see what AROS advertises; (4) `MakeTXTRecordA`/`FreeTXTRecord` and `GetMDNSStatusA`, stubs that answer `MDNSERR_NOTREADY`; (5) the commodity; (6) the Envoy bridge (`InquiryFromNIPCTagsA`, `PublishNIPCEntitiesA`, MD16); (7) NSEC and known answers over more than one packet (MD10). | One pull request each, in that order; (2) proposed on issue #1559 first, as the queue was. | Each merged or declined. |
| MD23 | The mdnsd sources build only with AROS's own `-Wno-pointer-sign -Wno-unused-parameter`: under plain `-Wall -Wextra -Werror` they stop at `mdnsd.c:84` (a `char *` to `OpenLibrary()`) and `mdns_dns.c:102` (`pktbase` unused). Seen on 2026-10-22 building #1743; upstream builds and Codacy are not affected. | Casts and a `(void)`, as a cleanup pull request of its own, only if wanted. |
| MD24 | `network/mdns/aros/src/` here is an early prototype of mdnsd and no longer what upstream has (on 2026-10-22 its `mdns_reg.c` differed from master's in some 1,980 diff lines). The upstream source is the AROS tree (`~/AROS-net`, the PR worktrees under `build/`); a fix made only here does not reach upstream. | Say so in `network/mdns/README.md`, or remove the copy once nothing reads it. |
| MD26 | **Bonami API AutoDocs in AROS style.** The original [amigazen Bonami SDK](https://github.com/amigazen/Bonami/blob/main/SDK/Autodocs/bonami.doc) includes `SDK/Autodocs/bonami.doc`, but the AROS port currently lacks equivalent API AutoDocs. Do not simply copy the old document: AROS's public API and queued discovery contract have evolved (#1695), and further vectors may be added under MD25. | Audit all exported `bonami.library` functions against the current AROS headers, `bonami.conf`, implementation and `DISCOVERY.md`. Use the **AROS source-embedded AutoDoc convention** (`/*  NAME`, `SYNOPSIS`, `FUNCTION`, `INPUTS`, `RESULT`, `NOTES`, `EXAMPLE`, `BUGS`, `SEE ALSO`, as appropriate), not just a standalone SDK `.doc`; follow upstream precedents [kernel API AutoDocs #1756](https://github.com/aros-development-team/AROS/commit/c4454e6b58758ed2f3232a023d7d41dd6f880881) and [rexxsyslib C API AutoDocs #1721](https://github.com/aros-development-team/AROS/commit/8e7c8e3e1caf4110568996c959adf875afd37ecd). Also use [pl011bt.resource Bluetooth UART API AutoDocs #1808](https://github.com/aros-development-team/AROS/commit/0491d0ad4ca776b317c17d116383eaee612937b4) as a concrete reference: its eight source-embedded function blocks describe precise inputs/results, ownership, task context, concurrency, failure modes and known BUGS without concealing limitations. Document ownership/lifetime, message-port and queued request semantics, cancellation, thread/task safety, error values, version requirements and examples. Keep this a **documentation-only upstream PR**, separate from MD25's API changes; revisit after new vectors merge. | Every exported AROS Bonami function has accurate source AutoDocs matching its actual implementation and AROS documentation tooling/conventions; no ABI or behavior changes. |
| MD27 | **Evaluate the original Bonami SDK for AROS-native developer support.** The [original SDK](https://github.com/amigazen/Bonami/tree/main/SDK) contains C examples (`service_browser`, `dnssd`, `mdns_status`, `nearby`, `share`, `handshake`, Envoy/NIPC), headers, SFD, LVO includes, an Amiga E module and a binary library. These are not all suitable for copying into AROS. | Compare the SDK with the actual AROS public API and build conventions. Prioritize **adapted C examples** for register, browse, resolve, queued events, cancellation and cleanup, with hosted/native tests where feasible. Audit headers/SFD for missing public declarations or useful API documentation only; do **not** replace AROS-generated bindings or ship the original binary. Keep Amiga E support optional and demand-driven. Coordinate with MD25 (API gaps) and MD26 (source AutoDocs). | A short SDK-to-AROS compatibility matrix and selected examples that compile and exercise AROS's current contract, including the #1695 queued discovery API; anything intentionally omitted has a reason. |
| MD28 | **Consider selective backport of AROS Bonami/mDNS improvements to the original amigazen project.** The AROS port has diverged from the original and includes changes in the responder, discovery queue, lifetime/error handling and integrations; not every change belongs upstream in the original project's API or target environment. | Audit AROS commits and implementation against [amigazen/Bonami](https://github.com/amigazen/Bonami) and identify self-contained fixes, RFC-conformance improvements, robustness tests, documentation and examples that are portable. Propose these individually to the original maintainers, preserving their compatibility and API decisions; do not mechanically copy AROS-specific `mdnsd`/Exec IPC, queued contracts, build glue or NetMIDI integration. Confirm licensing/attribution and run tests on the original targets. This is **optional collaboration**, not a blocker for AROS upstream; follow the original-project communication decision D2 and MU6. | A categorized candidate list (portable / needs adaptation / AROS-only), with links to originating AROS commits, and upstream issues or PRs only after agreement; no unsupported claims that changes have already been accepted upstream. |

## The way upstream

| # | What | Waits for |
|---|---|---|
| MU1 | PR #1560, the responder for the host's name. **Merged 2026-10-13.** | Nothing |
| MU2 | The second cut: registering, updating and withdrawing a service through the library. **Sent 2026-10-13 as PR #1591**, from the branch `mdns-register` in `~/AROS-net`, built and tested in the clean tree; see the graph of pull requests. | Its review; it assumes the answers on location, split and the library's name |
| MU3 | The third cut: browse, resolve, host lookup and queries, which is where clients get events. | MU2 and MD01 |
| MU4 | The tools, the commodity, the Envoy bridge. | MU3; whether AROS wants them at all |
| MU5 | Where the tests go: seven runners need sudo, veth and Avahi, which AROS's own tests do not have. | The answer in issue #1559 |
| MU6 | The original Bonami: its issue #1 offers our fixes back and describes the port as it was before the host name, the tie-break and the delayed answers. | Decision D2 below |

Each cut has to pass the tree's Codacy gate with no new finding, as the
first did: variables declared at first use, `SocketBase` passed as a
parameter, none of `strcpy`, `strncpy`, `strncat` and `strlen`.

## Decisions needed

| # | Question | Leaning |
|---|---|---|
| D1 | Byte order: AROS's own macros (`AROS_BE2WORD` and the rest) or the socket ones. The original reason to avoid AROS's, staying close to Bonami's source, is gone. In the AROS tree, code that talks to `bsdsocket.library` uses `htons()` (218 uses in AROSTCP, none of the AROS macros), and network card drivers use the AROS macros. Fields inside a DNS packet are at odd offsets, where neither kind may read a word directly on a 68000. | Functions that read and write packet fields byte by byte; `htons()` on aligned values, by the directory's custom. Asked on 2026-10-12 and not answered. |
| D2 | Whether to tell the original Bonami that the work now goes to AROS and may leave their API. | A short comment on their issue: they should hear it from us. Drafted in conversation on 2026-10-12, not posted. |

## Not scheduled here

- Network MIDI's move to the service and the end of its own responder:
  AM09 and AM10 in [the AppleMIDI backlog](applemidi-backlog.md), NM08 in
  [the Network MIDI 2.0 backlog](network-midi2-backlog.md).
- DNS-SD over unicast DNS, wide-area discovery, sleep proxy.
