Prevent netinfo.device from unloading while its worker is still cleaning up
or an open callback is waiting for that worker to exit. The original
`Avail FLUSH` fault occurs when cleanup calls `EndNotify()` after the module
has closed its DOS library base.

### Lifetime and reopening

- Expunge refuses when the device lock is unavailable. It requests worker
  shutdown and defers unloading while that worker still exists.
- The worker records its stable Exec task ID. Expunge succeeds only once
  `FindTaskByPID()` no longer finds it, after it has left this module's code.
  A replacement worker does not overwrite that ID until the old task is gone.
- An open recognizes a replied death message as an exiting worker, even
  while `nid_Task` is non-NULL. It drops `nid_Lock`, waits one tick, reacquires
  the lock and checks again, allowing cleanup to finish before reusing maps.
- While dropping that lock, the opener increments the private
  `nid_OpenWaiters` counter; it decrements it after reacquiring the lock.
  Expunge also refuses while this counter is nonzero. The generated device
  code increments `lib_OpenCnt` only after `DevOpen()` returns, so that normal
  count alone cannot protect a waiting callback from a second flush.

The first expunge may only request worker exit; a later expunge removes the
module once the worker and waiting opens are gone. The public device API
is unchanged.

Addresses the maintainer's reviews about
[reopening an exiting worker](https://github.com/aros-development-team/AROS/pull/1677#issuecomment-6291062875)
and [expunge during a waiting open](https://github.com/aros-development-team/AROS/pull/1677#issuecomment-6292476390).

### Validation

- Complete device builds and links pass for x86-64 and amiga-m68k through
  generated makefiles with existing SDKs.
- ASan/UBSan: 32 checks pass using the exact `DevOpen`/`DevExpunge` callback
  bodies with mocked scheduling/platform calls. During `Delay()`, the harness
  completes the worker and invokes expunge twice with `lib_OpenCnt == 0`.
  It covers remaining waiting opens, restart failure, invalid units, lock
  contention and the existing worker-ID guard. The prior revision fails
  seven checks. Unloading is modeled; the host test does not free code that
  is still executing or emulate native SMP.
- Native hosted UP AROS: all 72 checks still pass across eight forced reopens,
  alternating the initial password/group unit. The probe holds the lock,
  queues an opener and replies to the actual worker death message to force
  the reviewed ordering. It verifies the old task is gone, a new worker and
  current maps are used, real `CMD_RESET` I/O completes on both database
  units, and repeated expunge removes the closed device.
- The native run completes `Avail FLUSH`; the harness detects no native
  fault signatures. Earlier open/close/flush/reopen coverage and the original
  `EntCleanup()` fault reproduction are retained in the companion workspace.

The native probe and callback harness are companion aros-midihub files,
outside this AROS slice. Tests reuse an existing hosted image with replacement
binaries, not a fresh full current-master rebuild or native hardware/m68k/SMP
execution. Task-ID and lock reasoning must not be mistaken for an SMP runtime
validation.
