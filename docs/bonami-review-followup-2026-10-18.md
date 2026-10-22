# Bonami #1591: maintainer review follow-up

Date: 2026-10-18. This follows [Kalamatee's review](https://github.com/aros-development-team/AROS/pull/1591#issuecomment-6250825444)
and the owner's authorization to address it. The original published head was
`24f476235e7ed6babaad84a44e90242948a029f9`; the replacement is
`55e7f21ec2ac29f774de27eada5524b8d01c1c54` on `mdns-register`.
The working branch is `review/bonami-review` in `build/upstream-pr-prep`.

## Changes for the review

- Preserved all fifteen original Bonami function signatures, registers and
  order. Slots 6–15 are compatibility stubs: LONG functions return
  `MDNSERR_NOTREADY`; void functions do nothing, without invoking hooks or
  changing output storage. Version is 41.1. The original constants, public
  structures, flags, events and NIPC headers are included, with BSD notices.
  The comparison source was amigazen/Bonami
  `540e93b2eae5bc35cef8fa05ac9a254174a29364`.
- Documented the deliberate task ownership/lifetime differences in the
  README and function autodocs: engine close releases registrations, only
  the registering task can change/remove them, local duplicates are refused,
  and that task's termination/final library close withdraws its services.
- Replaced raw task-address ownership and manual ready/wait-list scans with
  `GetETaskID()` and `FindTaskByPID()`. IPC version 2 carries the ID. The latter
  uses Exec's task lookup, including SMP running/spinning lists. No returned
  task pointer is dereferenced by the reaper. A reused task address no longer
  transfers the old registration to the new task.
- Conflict handling ignores identical SRV/TXT data, non-IN records and TTL-zero
  goodbyes. Compressed SRV targets are compared by decoded DNS name.
- Instance names must fit one 63-byte label; literal dots are rejected until
  escaping is implemented. Suffix renaming skips locally occupied names.
- Interface reconfiguration restarts host and service probing; registrations
  do not advance through probing while disconnected or before the host name
  settles.
- Proxy names must be local names, supplied with an address, and differ from
  this host's current/base name. Proxy hostnames are still not independently
  probed, as documented; this implements the review's minimum own-name check.
- Invalid TTL updates are rejected before mutation. Valid TTL changes apply
  to the registration and trigger announcements.

The same update fixes the reproduced protocol findings from the
[responder audit](mdns-rfc6762-responder-review.md): known-answer suppression
uses owner/target and half the actual service TTL; incompatible question
classes and nonzero DNS opcode/rcode are ignored; service withdrawal keeps
the shared host A record; service updates announce twice; simultaneous probes
include and compare the complete TXT/SRV record set, with canonical DNS target
case and bounded peer-record collection.

## Validation

All paths below are local artifacts under `build/` and are not committed.

| Check | Result and scope |
|---|---|
| Host packet-path fixtures with ASan/UBSan | 25/25 pass against exact commit `55e7f21`; independent fixture encoding, mocked Exec/time/network |
| Native hosted registration | 6/6 pass, including final-library rerun: records, duplicate rejection, update, proxy, dead client and goodbyes |
| Native hosted TTL | 3/3 pass: custom/default wire TTLs and invalid values |
| Native hosted tie-break | 2/2 pass against a Linux peer proposing matching TXT and differing SRV |
| Native ABI probe | All slots 6–15, library version, invalid update TTL and current task-ID lookup pass; repeated with final library |
| Generated SDK builds | Library, link library and daemon build for linux-x86_64 and amiga-m68k |
| Original ABI/header comparison | Original signatures/registers, generated offsets and original constant values match, except intentional version/revision increase |

Reproduce the deterministic fixtures with:

```sh
python3 tools/review-mdns-responder.py \
  --aros-tree build/upstream-pr-prep --revision review/bonami-review
```

Fixture logs: `build/mdns-responder-review/55e7f21ec2ac29f774de27eada5524b8d01c1c54/`.
Native logs, captures and SDK build scripts:
`build/review-bonami-2026-10-18/` (`native-register-final`, `native-ttl`,
`native-tie`, `native-abi`, `make-build`, `m68k`). The hosted image was an
existing UP image with the newly built narrow library/daemon installed;
this was not a full current-upstream image rebuild. SDK make builds used
`-Wall -Wextra -Werror`, with warning exclusions required by generated SDK
code; m68k also excluded volatile-register warnings.

The final x86-64 `bonami.library` SHA-256 is
`a891f36c9cc2741c829914deb60b3c0b39190047bb5dc65a1b8f3e5b7127cb37`.
The final small library change replaces a cast of `MinList` to `List` with
a type-correct empty-list check, resolving the m68k strict-aliasing warning.
TTL/tie-break runs preceded this equivalent empty-list check; registration
and ABI checks were repeated with the final library. A final Codacy follow-up
removed an unreachable, duplicate host-ready test from the daemon; the 25
fixtures and both SDK builds were repeated after that behavior-preserving change.
Native packet captures precede this final removal.

## Browse/resolve and remaining scope

Browse/resolve already exist locally in `network/mdns/aros/src/bonami_api.c`
and `mdns_browse.c`; they were never part of the registration-only #1591.
That full implementation is preserved. Its hooks currently run in the daemon
through `CallHookPkt()`. Event delivery, callback lifetime/cancellation and
client isolation need a separate review before publishing the discovery slice.
The stubs reserve the original ABI without claiming these features work here.

This is not a claim of full RFC compliance or complete SMP safety. Actual SMP
runtime, hardware, Apple Bonjour and Windows interoperability were not tested.
The task-ID fixture covers address reuse, not native SMP scheduling. Network
return re-probing has deterministic fixture coverage, not a hosted interface
reconfiguration capture. Broader IPC publication/daemon shutdown races and
handle-incarnation concerns from the earlier audit remain separate work.
IPv4-only operation, multi-packet known-answer handling, service-type response
delay and one-second repeat suppression remain documented limitations.

Publication updates the existing PR and its description; no separate review
comment is posted. The original description is saved in
`build/review-bonami-2026-10-18/pr-before.json`.

Published head confirmed as `55e7f21ec2ac29f774de27eada5524b8d01c1c54`.
All four checks passed: Codacy, Windows-compatible filenames, C/H line endings
and mergefreeze. This records automated results, not maintainer acceptance.
