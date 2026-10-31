@/home/jaime/.codex/RTK.md

Commits and pull requests must not contain references to Claude Code,
including co-author trailers, generated-by footers or session links.

Merged AROS components must use the upstream implementation. Do not keep a
parallel local runtime implementation unless it contains identified work
being prepared for a subsequent upstream proposal. Record that work and its
blockers in the backlog. Tests should compile the canonical upstream source
or the active proposal branch. Unrelated UI improvements must not block
retirement of an obsolete implementation.

Before calling a backlog item implementation-ready, identify the existing
public interfaces and source behavior, demonstrate the missing capability,
and record the minimum change, real dependencies and validation criteria.
No external dependency does not mean the design is already established.


Cross-project AROS upstream coordination: before changing shared AROSTCP, mdnsd, Bonami or DNS-SD code/contracts, follow [docs/upstream-coordination.md](docs/upstream-coordination.md) and reconcile relevant entries in [docs/upstream-work-in-progress.md](docs/upstream-work-in-progress.md). Inventory local-only branches/commits in your own checkout; GitHub absence does not prove absence of unpublished work. Preserve autonomous project prioritization and do not introduce blanket cross-project blockers.
