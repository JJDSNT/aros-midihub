# mDNS backlog — from a service that runs here to one in the AROS tree

**Scope:** the system mDNS and DNS-SD service ported from Bonami:
`mdnsd`, `bonami.library`, the tools and the commodity in
`network/mdns/aros`, the cut of it that went to AROS as PR #1560 (worktree
`~/AROS-net`, branch `mdnsd-hostname`), and the way of the rest into the
AROS tree. The target is acceptance in AROS; staying compatible with the
original Bonami is not a constraint. Background:
[the port](../network/mdns/aros/README.md), whose "Known limits" this file
orders, [mDNS and AROSTCP](mdns-arostcp.md) and
[upstream pull requests](upstream-prs.md).

**State this starts from** (2026-10-12):

- The service runs on Linux-hosted AROS over two veth links against the
  host's Avahi: `tools/bonami-hosted.sh` checks 24 things, and six more
  runners check the host name (21), the tie-break (2), known answers (5),
  delayed answers (3), unicast answers (5) and the responder alone (22).
- It registers, updates and withdraws services, browses, resolves, looks up
  hosts, asks for any record, and follows interfaces coming and going and
  the stack being reloaded.
- What AROSTCP lacked for multicast is merged upstream (PR #1543, #1558).
- The first cut, a responder for the host's own name, is PR #1560: open,
  checks passing, no review yet. Issue #1559 asks six questions about the
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

## P0 — wrong or unsafe as it is

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| MD01 | A client's Hook is called in mdnsd's process. Every client has to know it may only copy and signal there; one that prints, calls the library or crashes does it inside the daemon, for every other client. | Deliver events outside the daemon: a message to the client's own port, or a dispatcher per client in the library. Which one is question 3 of issue #1559; do not build before the answer. | A client that misbehaves in its callback harms only itself; the tools no longer share a helper to copy and signal. |

## P1 — before another program depends on it

| # | Limit or defect | What to do | Done when |
|---|---|---|---|
| MD03 | A client is not told the final name of its service after a conflict renamed it. | An event for it, in the form MD01 settles. Until then a client can ask with a browse for its own type. | The test that takes "Name (2)" sees the client told. |
| MD04 | A question about one of this host's own services gets no answer through `QueryRecordA()`: mdnsd does not hear itself. | Answer from the daemon's own records before asking the network. | `dnssd -q` for a local service prints it. |
| MD05 | Never run outside hosted x86-64. | m68k in the emulator and raspi-aarch64 on a board, with the runners' checks done by hand or adapted. Byte order and alignment are the point on m68k. | A table here: target, date, what passed. |
| MD06 | Never met Apple's Bonjour, Windows' resolver, or a second AROS. | Each when the machine is at hand; Bonjour's conformance test if it can be run against AROS. | The same table. |
| MD07 | The number in a host name changed after a conflict (`name-2`) is not kept across a restart of mdnsd, so every restart probes the taken name again and loses again. | Keep it in `ENV:` for the session. | The host-name runner restarts mdnsd and sees one probe series, not two. |
| MD08 | Other SANA-II drivers were not looked at. A card that filters multicast in hardware has to honour `S2_ADDMULTICASTADDRESS`; only the hosted driver was checked, and its host interface is promiscuous. | Read the drivers in `workbench/devs/networks` for the command and list the ones that ignore it; test on any real card available. | The list exists; one real card answers a browse. |
| MD20 | Seen once in twenty-five runs of `tools/applemidi-discovery-hosted.sh` on 2026-10-12: Avahi receives the PTR record of a service registered on AROS and then fails to resolve the instance ("Timeout reached"), so the SRV, TXT or address question went unanswered or the answer was not taken. The failing run was not captured. It first appeared after MIDIHub began to browse the same type it registers, which may or may not be related: `tools/bonami-hosted.sh` registers and browses one type too and has not shown it. | Do not chase it blind. The runner now keeps every mDNS packet of the link; when a run fails, read which question Avahi asked and what mdnsd sent back, and how long after the announcement. | A cause named from a capture, and a runner case that provokes it. |

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

## The way upstream

| # | What | Waits for |
|---|---|---|
| MU1 | PR #1560, the responder for the host's name: answer its review. | The reviewer |
| MU2 | The second cut: registering, updating and withdrawing a service through the library. | #1560 merged; the answers on location, split and the library's name |
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
