Title: camd: preserve unread driver input on receive overflow

## Scope

The public CAMD driver interface passes each received byte to `Receiver()`.
Internally, that producer and the receiver process share a 1025-entry ring and
an `unpicked` count. The existing implementation writes every byte without
checking whether the ring is full, so a fast driver can wrap the write cursor
over unread input. The older overwritten bytes are then parsed as if they were
still intact, and the documented `CMEF_RecvOverflow` condition is not reported.

Keep unread entries intact. Once the ring reaches its usable capacity,
`Receiver()` records overflow, wakes the receiver process and drops new bytes
until that process has drained the preserved input. The receiver drains once
more after observing the overflow flag, reports `CMEF_RecvOverflow`, cancels a
SysEx only when one is still being collected, and resets parsing so data bytes
from a lost running-status sequence are ignored until a fresh status arrives.
The producer/consumer count and overflow flag use the existing AROS atomic
operations.

The change is confined to `camd.library`. It adds only private driver state;
CAMD remains version 41.1 and no public API or ABI changes. The branch is based
directly on upstream master and does not depend on the separate realtime output
ring fix in PR #1888.

The modified private header is also normalized from its legacy single-byte
copyright character to the AROS `Copyright (C) 1995-2026` form, making the
source valid UTF-8/ASCII while following the current contribution policy.

## Reproduction and validation

The external CAMD compatibility fixture uses the software loopback driver to
send bursts into the 1025-entry receive ring. Besides checking overflow and
subsequent recovery, it leaves a completed SysEx unread, forces an unrelated
overflow and verifies that the completed message remains intact. This covers
the review case where unconditional cancellation would rewind the SysEx buffer
and release a semaphore no longer owned by the receiver process.

In the isolated `--receive-overflow` run on unmodified upstream master
`8ab9e5f7b9`:

- linking, ordinary messages, order and normal SysEx pass;
- the oversized burst is not reported as `CMEF_RecvOverflow`;
- the following channel message still arrives;
- the result is 6 passed and 1 failed.

With the review regression included, unmodified upstream reports 8 passed and
2 failed. The reviewed commit reports 10 passed and 0 failed: both overflows
are reported, the completed unread SysEx is preserved and delivery recovers.

Additional validation:

- `camd.library` rebuild and link for hosted x86-64;
- `camd.library` rebuild and link for amiga-m68k;
- companion repository `make test` passes;
- `git diff --check` passes.

No physical MIDI hardware was used. The regression exercises CAMD's internal
driver receive ring through the software loopback driver.
