# AROS PR #1848: preserve MungWall allocation alignment

[AROS #1848](https://github.com/aros-development-team/AROS/pull/1848) is
ready for review, not a draft. Branch `exec-mungwall-alignment`, commit
`cc40ebad9d`, is one commit directly on AROS `master` `59b6c2433b` and has no
PR dependencies.

## Scope

- Check the MungWall header at the caller's pointer before using the
  rounded-down `AllocAbs` fallback.
- Return the normalized raw allocation size together with its address.
- Leave both address and size untouched when no MungWall header exists.
- Scan live MungWall allocations on the exceptional missing-header path so a
  neighbouring broken wall can identify its owner.

On m68k, `AROS_WORSTALIGN` is 4 and `MEMCHUNK_TOTAL` is 8. The old unconditional
round-down therefore moved a valid four-byte-aligned TLSF allocation four bytes
into its pre-wall before looking for the header. The internal signature change
does not alter the public Exec ABI; it keeps the allocator address and size as
one normalized result.

## Validation

- `mungwall.c`, `freemem.c` and `memory.c` compile with `-Werror` using the
  generated AROS flags for linux-x86_64 and amiga-m68k;
- `git diff --check` passes;
- the PR is non-draft and mergeable; all four GitHub checks pass.

The historical fault was captured on a Pi 3/m68k MungWall boot. This revision
has not yet been run on m68k hardware or in an m68k emulator. A complete clean
AROS build is also not claimed: the isolated source worktree is intentionally
sparse and lacks unrelated global build prerequisites.
