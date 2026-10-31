# Cross-project upstream coordination contract

Applies reciprocally to [aros-midihub](https://github.com/JJDSNT/aros-midihub) and [aros-matter](https://github.com/JJDSNT/aros-matter). Both projects contribute to [AROS upstream](https://github.com/aros-development-team/AROS).

## Principles
1. **Independent agents and backlogs.** Neither repository governs the other's priorities, schedules or milestones. An agent may activate, reprioritize and interleave its own work without routine approval from the other project.
2. **Upstream is canonical.** Accepted AROS code and interfaces outrank historical plans. Before proposing shared changes, inspect current upstream master, open PRs and applicable branches. A merged fix should be consumed rather than maintained as a parallel implementation.
3. **Coordinate changes, not calendars.** Check for overlap when touching shared source, APIs, protocols, build integration or tests. Do not make entire project milestones depend on unrelated work.
4. **No presumed access to unpublished work.** GitHub cannot reveal unpushed commits, untracked files or local-only worktrees. Each agent must inventory its own local checkout and publish a concise pointer or explicitly mark the state unknown.
5. **No duplicated ownership or silent ABI changes.** Prefer one upstream PR per coherent generic change, with separate consumer integration and validation where appropriate. Material ABI/API, security, licensing or architectural changes follow the repository's existing maintainer-approval rules.

## Shared areas and initial coordination lead
| Area | Initial lead / consumer | Coordination trigger |
| --- | --- | --- |
| `mdnsd`, `bonami.library`, DNS-SD and discovery contract | MIDIHub lead; Matter consumer or contributor | Shared daemon, library, hooks, TXT, IPv6 or DNS-SD API changes |
| AROSTCP IPv6, UDP6 multicast, interface scoping and sockets | Matter lead; MIDIHub consumer or contributor | Network-stack or public socket contract changes |
| TCP reliability and congestion work (PMTUD, RTO, ECN, PRR, RACK-TLP) | Matter Backlog 2 | Changes to shared network behavior; not an automatic MIDIHub blocker |
| CAMD, AppleMIDI, RTP-MIDI, Network MIDI 2.0, UMP, BLE, synth/router | MIDIHub | Only when changing shared networking/discovery contracts |
| Matter SDK, commissioning, SPAKE2+ and crypto integration | Matter | Only when introducing reusable shared AROS APIs |

Leads are *coordination defaults*, not exclusive code ownership. Either agent can fix a generic defect after checking existing work and recording the decision.

## Required lightweight workflow
1. **Scope:** identify concrete AROS paths, interfaces, test suites and upstream PR/issue IDs affected by the planned task.
2. **Check overlap only when relevant:** inspect upstream master, open PRs, the other project's published WIP manifest, and relevant project documents. Check own local branches/worktrees and uncommitted changes; request confirmation if the other checkout is inaccessible.
3. **Classify:** independent; shared prerequisite; overlapping implementation; incompatible contract. Record a one-line rationale and links in the local WIP manifest only for shared work.
4. **Proceed:** independent work proceeds freely. Reuse merged changes. For overlap, nominate a single upstream implementation/PR and let the other project supply consumer tests or complementary changes. A dependency blocks only the specific affected task, not an entire project.
5. **Reconcile on events:** when a shared PR is opened, updated, merged or closed, or when an API/ownership decision changes, update the local WIP entry and affected backlog links. Mark obsolete snapshots as historical rather than silently treating them as current.

## Known intersections to reconcile (not claims of current PR status)
- MIDIHub **MD14** vs Matter **ISSUE-0005**: IPv6 in `mdnsd`; inspect AROS **#1980** and merged master before any new implementation.
- MIDIHub **AM14/NM15/MD14** vs Matter **ISSUE-0008**: IPv6 multicast/socket foundations are shared; MIDI transport integration and interoperability remain separate work.
- MIDIHub **MD01/MD03**, N8/N9 and discovery audit findings vs Matter **ISSUE-0004**: preserve existing Bonami consumer semantics when changing event delivery or DNS-SD contracts. Matter's SDK `minimal_mdns` is an alternative, not a mandatory dependency on full Bonami IPv6.
- MIDIHub DNS-SD tools: revisit `dnssd -q` / `QueryRecordA()`, `-G` / `LookupHostA()`, `-Z` SRV/A and Nearby/Share/Handshake after upstream Bonami API merges; avoid redoing already accepted work.

## Evidence and limits
The per-repository [WIP manifest](upstream-work-in-progress.md) is an **index**, not a second backlog or source of truth. Each record should link to the canonical issue, PR, branch, commit or code path, state what is verified and distinguish *unpublished/unknown* from *absent*. Do not claim a PR is open or merged without checking. Do not expose private local paths, credentials or sensitive checkout details unnecessarily.

## Escalation
Agents decide ordinary task ordering and small implementation changes autonomously. Escalate only genuine competing designs, incompatible public APIs/ABI, ownership disputes, broad invasive refactors or other changes already requiring maintainer approval.