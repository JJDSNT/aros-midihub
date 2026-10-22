# Upstream review: first verified pass, 2026-10-17

This is a review report, not a set of approved fixes. Product sources, AROS
worktrees, branches and PRs were not changed. Local review reproducers were
added. The [scope and remaining checklist](aros-upstream-review.md) still
apply; this pass does not complete the five-PR audit.

**Later follow-up:** the [AppleMIDI correction and integration pass](applemidi-review-2026-10-17.md)
records a reproduced SysEx defect, local product changes and subsequent
transport tests. The unchanged-source statements here describe this first
pass only.

## Revisions and coverage

GitHub was queried for the author's open PRs before reviewing. These are the
published heads, not the newer local BLE branch:

| PR | Head | Work in this pass | Current recommendation |
|---|---|---|---|
| [#1591 Bonami](https://github.com/aros-development-team/AROS/pull/1591) | `24f476235e7ed6babaad84a44e90242948a029f9` | Registration/removal, task reaping, library open/close, IPC and daemon startup/shutdown; compared Exec scheduler and locking | Changes requested; source findings below need native execution tests |
| [#1588 UMP/Network MIDI 2.0](https://github.com/aros-development-team/AROS/pull/1588) | `953be81c3ee874d040654633d1ed783fcc95dd27` | Identity codec and handshake, sampled session/FEC paths; existing host tests plus new regressions and mutation run | Changes requested: two reproduced defects |
| [#1586 network drivers](https://github.com/aros-development-team/AROS/pull/1586) | `2710bd6e56c831cc452117755c4bcac4a54f9b6a` | Read driver, interrupt, open/close and expunge paths; compared CAMD transmitter and Exec soft-interrupt implementation | Review incomplete; NM-001 needs refinement and measurement |
| [#1507 BLE MIDI](https://github.com/aros-development-team/AROS/pull/1507) | `658d7413037f07d44fd81e022644bfb090483a90` | Read existing maintainer comments, class/central/adapter/driver paths; tested published codec | Changes requested for known codec discrepancy; radio and lifecycle validation still pending |
| [#1587 RTP-MIDI](https://github.com/aros-development-team/AROS/pull/1587) | `2ff4abfe52946f5c8df4bec2d4c0ab6dad15736b` | Sampled timing/bounds; existing protocol tests and mutation run against exact PR library sources | Review incomplete; passing tests are not full protocol approval |

Exec references were read from the local AROS checkout at
`95a501ff473fb3fa419c51586d768dff4cd44155`; the referenced Exec/task files
had no working-tree edits. In particular: `rom/exec/findtask.c`,
`disable.c`, `forbid.c`, `cause.c`, and `rom/task/LockTaskList.c` /
`NextTaskEntry.c`. No claim is made that this checkout is today's upstream
master. The Bonami directory was clean at its published PR head.

## Drivers: reproduced findings in #1588

### NM2-001 — zero-capacity identity outputs overflow (medium)

**Location:** `workbench/network/netmidi/netmidi2/netmidi2.c:161–178`,
`copy_text()` / `netmidi2_identity()`.

**Evidence:** ASan reports `heap-buffer-overflow`, a one-byte write at line
166, independently for `name_size == 0` and `id_size == 0`. The reproducer
passes a non-NULL one-past pointer as an empty range, so the result does not
depend on inventing a NULL-pointer contract. Capacities 1 and 8 succeed for
each output. Existing Network MIDI 2.0 and UMP tests also pass with ASan/UBSan.

**Fix direction:** reject invalid output parameters before modifying either
output, or make the helper a no-op for capacity zero. Specify whether
`NULL, 0` is supported, then test that contract. Apply to the source of the
cut and the PR branch so regeneration cannot restore the bug.

**Exposure:** a public C API misuse/boundary case; the session receive paths
examined here supply nonzero internal arrays. This is not evidence of a
remotely triggerable overflow in the existing daemon.

### NM2-002 — malformed acceptance establishes a client session (medium)

**Location:** same file, `netmidi2_receive_from()`, lines 586–593.

**Evidence:** an `INVITATION_ACCEPTED` command claims one name word but has
zero payload words. `netmidi2_identity()` returns `-1`. The client discards
that result and calls `start_session()`, becoming `NETMIDI2_ESTABLISHED`.
The host's invitation path already checks the same parser's result and
rejects missing name/product fields; the client path lacks that validation.

**Reproduced output:** `identity parser=-1; client established=1` (the runner
also prints the receive return value). This is a state-validation defect,
not an ASan failure.

**Fix direction:** validate the entire accepted identity before transitioning
or publishing peer metadata; reject malformed/missing fields consistently
with the host path. Preserve retry/teardown behavior for legitimate repeated
acceptances. Test invalid length, empty identity, valid acceptance and retry.

## Bonami: refined hypotheses and additional source findings

### BA-001 — scope the live-owner finding to SMP (high on SMP)

`mdns_client.c:58–76` scans only `TaskReady` and `TaskWait`, protected by
`Disable()`. Exec's `FindTask()` also checks `TaskRunning` and `TaskSpinning`
on SMP, taking their spinlocks. Exec explicitly documents that `Disable()`
and `Forbid()` affect only the calling CPU on EXECSMP builds.

On a uniprocessor, mdnsd is the current task while reaping, so another
ordinary client cannot simultaneously be the running task. The initial
generic CPU-active-owner reproducer is therefore insufficient on UP.
On SMP, a live owner executing on another CPU is absent from both lists,
and list traversal itself also lacks the required cross-CPU protection.

**Fix direction:** investigate the public `task.resource` task-list/lifecycle
facilities, checking implementation and availability per supported target.
Do not copy private Exec locks into Bonami or assume that task enumeration
alone solves address reuse. A lifecycle strategy must also cover BA-003.
**Verification:** scheduler/source comparison; no SMP runtime reproduction.

### BA-002 — normal shutdown has a reply-drain protocol

`mdnsd.c:253–259` removes the public port, drains messages with
`MDNSERR_NOENGINE`, replies to them, then deletes the port. The client holds
`Forbid()` across lookup and enqueue. Under the uniprocessor scheduling
contract, this closes the normal remove/enqueue race: a message enqueued
before removal is drained; a later caller cannot find the port.

Do not report an ordinary shutdown hang merely because `WaitPort()` has
no timeout. Unexpected daemon death remains a failure-mode concern; the
SMP lookup-to-enqueue race also needs independent treatment because
`Forbid()` is local to one CPU. Test normal shutdown, abrupt termination
and pending calls separately. A timeout cannot free an in-flight message
or reply port while a late reply is possible. No native IPC tests run here.

### BA-003 — task-address reuse remains unresolved

The source explicitly acknowledges this limitation. Neither list locks nor
adding running tasks gives a raw `struct Task *` a generation. It remains a
design risk pending a native ownership strategy and termination/reuse tests.

### BA-004 — two concurrent starts can publish two daemons (medium)

**Location:** `mdnsd.c:169–204`.
The duplicate check ends before allocation, service setup and `net_up()`;
`AddPort()` happens much later, without rechecking. Two processes can both
observe no port and then publish ports with the same public name. Calls
can reach different registries, and stopping one can expose the other.
This race also exists on UP because the intervening work can schedule.

**Native precedent/fix direction:** use the Exec named-port ownership
pattern, making the final check and publication atomic under the applicable
scheduling/locking contract. Allocate first, then recheck; the loser must
clean up. Do not hold `Forbid()` across network or DOS initialization.
**Test:** simultaneous launches with an initialization barrier; assert one
owner, one registry and correct loser cleanup. Source finding; not executed.

### BA-005 — withdrawing one service also withdraws the host address (medium)

**Location:** `mdns_reg.c:173–228`, `service_send():255`,
`service_remove():928–934`.

Removing an announced registration invokes `service_send(..., TRUE)`;
this always requests the address record, and `service_append()` assigns
TTL zero to that A record as well. For a local registration it names the
host, even though the host and other registrations are still present.
The source confirms construction of a host-address goodbye on individual
deregistration. Actual cache disruption/requery behavior is not measured.

**Native precedent/fix direction:** `mdnsHostGoodbye()` already owns host
address withdrawal when networking stops. Individual service deregistration
should withdraw its PTR/SRV/TXT without withdrawing a live shared host A.
Proxy addresses need shared ownership accounting if multiple registrations
refer to the same target. **Test:** announce two services, remove one and
capture the goodbye; the remaining service and host address must remain
usable. Source finding; no packet-capture reproduction yet.

### BA-006 — handles are reused after daemon restart (medium)

**Location:** `mdnsd.c:179`, `mdns_reg.c:893`, `service_find()` and
`mdnsRegUpdate()` / `mdnsRegRemove()`.

Every daemon starts `md_NextHandle` at 1. A task can retain handle 1 from
before a restart, then register a different service and receive handle 1
again. Using the stale handle can update/remove the new service: the task
ownership comparison also matches. This needs no task-address reuse and is
distinct from BA-003. The API documents daemon disappearance but provides
no daemon generation in the reference.

**Fix direction:** define restart invalidation and an incarnation-safe
reference before adding client reconnection logic; preserve the public
ABI deliberately. **Test:** same task, register A, restart daemon, register
B, use old A reference; B must be unchanged. Source trace; not executed.

## Drivers: qualifications for #1586 and #1507

**NM-001:** both drivers have a no-owner/no-service drain loop without a
budget. On UP, a normal producer task cannot refill CAMD concurrently with
the soft interrupt. Do not infer an infinite live-producer loop from that
scenario. Large queued SysEx can still make interrupt work proportional to
an application-sized payload; SMP and interrupt-context producers require
separate analysis. Measure actual worst-case latency and CAMD reactivation
before prescribing a byte budget. No timing or starvation test was run.

**BLE published/local discrepancy (classification corrected):** RP-052
requires transmit header bit 6 to be zero but does not explicitly require
acceptance when set. The following test failure establishes a difference
from the local tolerance policy, not a proven BLE-MIDI violation. See the
[BLE improvement review](ble-midi-review-2026-10-17.md).

The PR head rejects header bit 6 when
set. The current local regression suite expects receivers to ignore this
reserved bit and aborts on `tests/blemidi_class.c:151` against the published
codec. Local commits `110fbcb6a1` and `4c2c7f778a` change the codec's reserved
bit handling/citations and undefined F9/FD handling, but are absent from the
published head. Review those commits for inclusion and run tests against the
resulting PR head. The abort means later tests were not reached; no claim
that F9/FD cases ran in this invocation. No radio tests were run.

The central TX code discards packet-send errors; review its loss/recovery
policy with fault injection. Do not equate all writes with GATT request
busy errors: the inspected stack has a write-without-response path that
explicitly permits transmission during another GATT transaction.

## Executed validation and reproduction

Run the new #1588 checks without switching any AROS branch:

```sh
rtk proxy python3 tools/review-netmidi2.py --aros-tree /home/jaime/AROS-netmidi
```

The runner extracts sources/headers at the pinned commit with `git show`,
builds with `-Wall -Wextra -Werror -fsanitize=address,undefined`, and saves
individual logs under `build/upstream-review/<commit>/`. It exits 1 while
regressions remain. `--revision` permits retesting a future corrected commit.
LeakSanitizer is disabled; these results establish address/UB checks only.

| Check | Result |
|---|---|
| Name/product output capacities 0, 1, 8 independently | Two ASan overflow failures at 0; four passes at 1/8 |
| Invalid invitation acceptance | Fails: malformed identity establishes session |
| Existing Network MIDI 2.0 suite, pinned #1588 | Pass |
| Existing UMP suite, pinned #1588 | Pass |
| Existing protocol suite with pinned #1587 library | Pass |
| Mutation runner, 100,000 rounds, seed 7 | Pass with ASan/UBSan; 200,000 RTP inputs, 175,161 parsed, 130,151 journals read and passed through recovery |
| BLE class suite against published #1507 codec | Aborts at reserved-header-bit case |

The protocol and mutation runners also link MIDIHub's local AppleMIDI,
session/clock/peer helpers (not part of #1587); the mutation runner uses
the pinned #1588 sources and MIDIHub's separate portable BLE codec. It is
not a fuzz test of the BLE class or Bonami. No clean AROS builds, SMP runs,
hardware/radio tests or independent MIDI interoperability tests were done.

## Recommended next changes

1. Correct NM2-001 and NM2-002 on the #1588 source branch and rerun the new
   regressions plus valid handshake/FEC cases.
2. Resolve Bonami ownership and SMP contract, confirm the new startup,
   deregistration and restart findings on hosted AROS, then amend #1591.
3. Reconcile BLE local codec fixes with the published PR and complete the
   promised radio/lifecycle checks.
4. Continue the full RTP-MIDI/UMP protocol review and native driver
   concurrency/ABI review; no ready-for-merge verdict for these from this pass.
