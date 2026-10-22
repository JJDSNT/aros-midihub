# Network MIDI refactor handoff — 2026-10-21

The review work left local companion sources, cut scripts, fixtures and
reports uncommitted even though the corresponding AROS changes had been
published. This consolidation preserves that work before another session
changes the transport structure. It does not implement the refactor.

## Source and publication boundary

- RTP-MIDI packet observation, session clock units and stale-time handling,
  and Network MIDI 2.0 identity validation belong to the completed protocol
  review work. Keep their regression tests.
- The change in `main.c` calls `rtpmidi_sysex_packet()` after filtering old
  and duplicate packets. Keep that ordering during the refactor.
- The change in `discovery.c` checks registration handles periodically so
  an mdnsd restart cannot leave a permanently stale registration.
- Cut scripts and templates follow the reviewed AROS directory layout:
  protocol libraries below `workbench/network/netmidid`, drivers below
  `workbench/devs/camd`, with the shared header in `networkmidi2`.
- `n8-applemidi` registers AppleMIDI without MIDI 2.0 or browse dependencies.
  It is distinct from the combined `n8` and the unfinished `n9` consumer.
- Review fixtures and dated reports are retained as evidence and tools;
  several probes intentionally reproduce old failures. They are not all
  part of `make test`, nor all AROS deliverables.

On this consolidation, generated N4, N7 and N8-AppleMIDI output was compared
byte for byte with these already published local AROS branch revisions:

| Cut | AROS revision | Files compared | Differences |
| --- | --- | ---: | ---: |
| N4 / #1679 | `f541409f06cea0f76b0d1d1f3065f598d7319356` | 30 | 0 |
| N7 / #1693 | `6e71764ba2c999c9c1ce4c1fa4237a42bf7be6ec` | 35 | 0 |
| N8-AppleMIDI / #1743 | `989227ceaca9d9f4b8d10caf59563a25479f265a` | 33 | 0 |

This is a comparison to pinned revisions, not a fresh GitHub review-status
query. No AROS branches were changed during the consolidation.

The netinfo patch snapshot is updated through #1677 revision
`a8684ee0f08bd9b294b5a35342cafdcaa2c2f1a7`. The old top-level
`.camd-v42-reject-fix.patch` was already present before this review work;
it is preserved under `build/handoff-2026-10-21/` as an unclassified local
experiment, not applied or published. Do not discard it based on this handoff.

## Verification and remaining work

Fresh `make test` and `make test-network` pass. The netinfo waiting-open
fixture passes 32 checks with ASan/UBSan against the published source.

The full `make -B m68k` was attempted with the existing m68k AROS SDK and
fails in discovery: the source still uses `MDNSBrowseMsg`, `MEVENT_ADD`,
`MEVENT_REMOVE` and `MDNS_BrowseRef`, while the available Bonami public
headers use the queued discovery API. The SDK also has inconsistent old
generated prototypes (`MDNSBrowseRef`, `MDNSResolveRef`, `MDNSQueryRef`).
Thus the default all-features m68k build is **not green**. Adapting N9 to
the queued API and building against consistent generated Bonami headers
remains necessary; a host test pass does not cover this AROS-only path.

The prior native node-rtpmidi and Avahi evidence is in the dated review
reports, especially `mdns-advertisement-avahi-2026-10-20.md`. These suites
were not rerun merely to consolidate unchanged source. Run them again
after the refactor, together with the full m68k build.

Preserve the other session's generic/AppleMIDI mapping and transport table
design. First refactor and validate this repository. Only afterward
regenerate the dependent cuts and inspect each PR diff. Coordinate with
the user before any public maintainer message or structural replacement
of the reviewed #1679 branch. Use one writer for the shared source files.

The local snapshot before consolidation, including all untracked files,
is under `build/handoff-2026-10-21/` (ignored, not a remote backup).
