# AROS PR #1847: fix m68k test-and-set return value

[AROS #1847](https://github.com/aros-development-team/AROS/pull/1847) is
ready for review, not a draft. Branch `review/pthread-m68k-tas`, commit
`8f220e83bd`, is one commit directly on AROS `master` `59b6c2433b` and has no
PR dependencies. All four GitHub checks pass.

## Scope

- Make the m68k `__sync_lock_test_and_set` fallback return the value that was
  in the lock before the exchange.
- Remove the now-obsolete m68k warnings from the pthread AutoDocs.
- Add a focused test for spin trylock/lock/unlock and `pthread_once`.

The old fallback wrote the requested value and returned that same value.
Consequently the first acquisition of a clear spinlock wrote one and reported
one: `pthread_spin_trylock()` returned `EBUSY`, while `pthread_spin_lock()`
kept yielding indefinitely. The fix preserves the old value while interrupts
are disabled and does not change the existing m68k locking model. The separate
m68k compare-and-swap fallback is outside this PR.

## Validation

- complete `libpthread.a` builds for linux-x86_64 and amiga-m68k;
- the new test builds and links with `-Wall -Wextra -Werror` on both targets;
- the test passes inside Linux-hosted AROS;
- the linked m68k binary loads the old lock word before the store and returns
  that saved value;
- cppcheck warning/performance/portability passes for the new test;
- `git diff --check` and all four GitHub checks pass.

The m68k test binary was cross-built but was not executed on m68k hardware or
an emulator during this validation.
