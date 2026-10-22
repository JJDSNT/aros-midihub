Title: mdnsd: isolate browse view exhaustion and bound cache scans

The DNS-SD discovery cache merged in #1695 can let one overflowing service
type fail another browse during the same view rebuild. If the first type has
filled all 32 views, failing its browse does not release those views until the
next rebuild, so a later PTR for a small type also finds no room.

Release every view belonging to the failed type immediately. The rest of the
current rebuild can then assign that room to other live browses.

Also precompute which cache records are reached before full-cache eviction and
the once-a-second refresh pass. A records are joined to the wanted SRVs once,
instead of each A record walking every SRV and every operation. With `R`
records and `O` operations, those passes now do `O(R * O + R * R)` name
comparisons rather than `O(R * R * O)` in the full-cache case.

Validation:

- The directed same-pass regression fails on merged commit `95d9d5ded9` and
  passes with this change. The full hosted discovery fixture passes 37/37
  under ASan and UBSan.
- `bonami.library` and `mdnsd` compile and link with warnings as errors for
  hosted x86-64 and amiga-m68k.

No public interface or configured bound changes.
