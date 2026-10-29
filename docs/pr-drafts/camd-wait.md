Title: camd: wait for every timer reply

AROS PR: [#1956](https://github.com/aros-development-team/AROS/pull/1956),
branch `review/camd-wait`, commit `6c01b38b91`.

## Scope

`CamdWait()` is the private delay used when a hardware output ring is full or
while a SysEx is still being transmitted. The timer process currently owns a
semaphore, releases it after a short timer request and waits for the caller to
give it back. The caller returns as soon as it has acquired and released that
semaphore. It can therefore enter the next `CamdWait()` before the timer
process has reacquired it. That call returns immediately instead of waiting.
A caller holding `Forbid()` then spins without allowing either the timer or
the hardware driver to run.

Use an explicit request/reply instead. A caller reserves a signal, tells the
timer process which task and signal to answer, and waits for that answer. The
existing semaphore continues to serialize callers. If the task has no free
signal, the reserved `SIGF_SINGLE` is used and any bit that was already
pending is restored afterwards.

The public behavior is exercised through the existing `PutSysEx()` path; no
public vector, tag, structure or driver interface changes. `camd.library`
therefore remains version 41.1. The branch is directly on upstream master and
does not depend on the v42 or v43 proposals.

## Reproduction and validation

The external compatibility fixture installs a deliberately slow legacy CAMD
driver. Its worker takes one SysEx byte per timer tick. The client links to
that driver, calls `PutSysEx()` while holding `Forbid()`, and verifies both
that the call returns and that the complete SysEx arrives through the input
cluster.

On unmodified upstream CAMD 41.1, the ordinary compatibility contract first
passes 59 checks. The legacy-wait run then links to the driver but does not
return from `PutSysEx()` within 30 seconds.

With the published commit:

- the CAMD 41.1 compatibility contract passes 59/59;
- the isolated legacy-wait run passes 5/5, including the no-free-signal
  fallback and preservation of an already pending `SIGF_SINGLE`;
- complete generated `camd.library` builds and links pass for hosted x86-64
  and amiga-m68k;
- the companion repository `make test` and both repositories'
  `git diff --check` pass.

No physical MIDI hardware was used. The slow driver exists only in the
external test fixture and is not part of the upstream change.
