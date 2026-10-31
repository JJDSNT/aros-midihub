# Shared upstream work-in-progress index

This is a lightweight cross-project coordination index, **not** a second backlog. Canonical facts remain in AROS master, PRs, project issues and source branches. Update on shared PR/contract events, not on every task. Do not infer that a missing record means no local work exists.

| Shared topic | Canonical references | Published state to verify | Unpublished/local-only inventory | Next reconciliation |
| --- | --- | --- | --- | --- |
| IPv6 in `mdnsd` | AROS PR #1980; MIDIHub MD14; Matter ISSUE-0005 | Recheck PR and upstream master before changing code | **Unknown — local agent must inspect own checkout/worktrees** | Identify canonical upstream implementation and consumer tests |
| AROSTCP IPv6, UDP6 multicast | Matter ISSUE-0008; MIDIHub AM14/NM15/MD14 | Recheck merged commits, open PRs and actual socket behavior | **Unknown — local agent must inspect own checkout/worktrees** | Separate generic stack fixes from MIDI transport integration |
| Bonami/DNS-SD API and event lifecycle | MIDIHub MD01/MD03, N8/N9, discovery contract audit; Matter ISSUE-0004 | Recheck upstream API and any open changes | **Unknown — local agent must inspect own checkout/worktrees** | Ensure callbacks, restarts, names and TXT contracts survive |
| DNS-SD CLI tools | MIDIHub `docs/mdns-backlog.md`; AROS PRs #1803/#1836 | Verify accepted API before further tool changes | **Unknown — local agent must inspect own checkout/worktrees** | Check `dnssd -q/-G/-Z`, Nearby, Share, Handshake |

## Agent inventory instructions
For each relevant *own* checkout, inspect `git status`, local and remote branches, unpushed commits relative to the intended upstream base, and unpublished patches/worktrees. Add only actionable shared entries with: topic; owning project; affected AROS paths; local branch or commit (if safely shareable); upstream PR/issue; status (local-only, published, PR open, merged, superseded, unknown); evidence/tests; next action. Never mark another project's local work as absent merely because it is not on GitHub.

When an upstream change merges, remove obsolete duplicate implementation plans and link the accepted commit in the canonical backlog. Preserve project-specific tests and integration work.
