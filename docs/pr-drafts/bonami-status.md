Title: bonami.library: implement daemon status snapshot

## Scope

`GetMDNSStatusA()` already occupies Bonami ABI slot 16, and the public header
already defines its six `MDNS_Status*` output tags. AROS currently leaves the
entry as a compatibility stub that returns `MDNSERR_NOTREADY` without touching
the outputs. The daemon already counts accepted RX and TX datagrams; it does
not expose them, count parsed answer records, or retain the three discovery
diagnostics.

Implement the established operation through one synchronous private IPC
message. mdnsd replies with one daemon-wide snapshot, and the library writes
recognized `ULONG *` tag outputs only after a successful reply. Unknown tags
and NULL output pointers remain harmless. The counters reset with mdnsd and
are read in its single task, so the returned values belong to one coherent
dispatch point.

The original Bonami diagnostics described a browse handler and Hook delivery.
AROS intentionally uses daemon-owned event queues and never runs client Hooks
in mdnsd. The existing debug tags therefore count records offered to AROS
discovery, PTR records among them, and attempts to deliver events into the
browse/resolve queues. The packet counters retain their original meanings:
successful `recvfrom()`/`sendto()` calls and answer-section records parsed from
responses.

This is an implementation of an existing ABI, not a new RFC or interface
proposal. The minimum integration change adds the status command and six
private reply fields, advances the private IPC from 4 to 5, and advances
`bonami.library` from 41.4 to 41.5. The daemon and library must be installed
together.

The #1916 identity is 41.4 because it implements the TXT helpers. `dnssd`
keeps the 41.3 minimum required by its older record-query calls locally. This
status proposal advances the library to 41.5 because it turns the status stub
into a working operation and requires the matching IPC-v5 daemon, not merely
because it follows another revision.

The upstream commit is `be461e3`, published as AROS PR
[#1942](https://github.com/aros-development-team/AROS/pull/1942), directly on
the current master containing #1916 merge `62249bcc`. It is a standalone
proposal, not a stacked PR.

## Reproduction and validation

The source baseline demonstrates the gap directly: slot 16 returns
`MDNSERR_NOTREADY` for every input and never sends an IPC message. The external
native probe in `tests/review/bonami_status_probe.c` requests all six outputs,
checks ignored unknown/NULL-output tags, a second monotonic snapshot and NULL
tag-list reachability. It passes 8/8 on hosted x86-64 with the generated
library 41.5 and IPC-v5 mdnsd installed together.

The sanitizer-backed host fixture runs the canonical daemon parser, discovery
queue and IPC dispatcher. `status-counters` seeds RX/TX, parses a four-answer
DNS-SD response containing one PTR, creates one queue delivery and receives
the exact `7, 11, 4, 4, 1, 1` snapshot through `MDNS_IPC_STATUS`.

Current local validation:

- responder fixture: 34/34 pass under ASan/UBSan;
- discovery fixture: 40/40 pass, including the new status case and the local
  reflection behavior merged through #1915;
- generic-query fixture: 13/13 pass;
- version-check matrices compile with `-Werror` for x86-64 and m68k: `dnssd`'s
  record requirement rejects 41.2 and accepts 41.3, 41.4 and later versions;
- every mdnsd and `bonami.library` source compiles with `-Werror` for
  amiga-m68k; the hosted daemon sources are compiled by the sanitizer fixture;
- complete generated `bonami.library` and mdnsd compile/link passes for hosted
  x86-64 and amiga-m68k with `-Werror`;
- the native hosted probe passes 8/8 through the public ABI and real daemon
  IPC, including both snapshots and NULL-tag reachability;
- cppcheck warning/performance/portability checks pass on all changed C files;
- companion repository `make test` passes;
- `git diff --check` passes.

The m68k link uses the current master `symbolsets.h` and rebuilt `libautoinit`
objects over the installed target SDK because that SDK predates upstream core
commit `c9b64995`. No physical m68k or big-endian runtime was used.
