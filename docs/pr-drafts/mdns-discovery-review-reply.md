<!-- Reply to Kalamatee's review of #1695 (2026-10-22), posted with the push of e04efca130. -->

Thank you, both problems were real. Fixed in `e04efca130`:

**Problem 1.** A record is now cached only when an active operation can
reach it:

- the PTRs of a browsed type;
- the SRV and TXT of an instance of a browsed type, or of an instance being
  resolved;
- the A of a host that such an SRV names.

Everything else on the network is ignored. Refresh and missing-record
queries go out only for reachable records (RFC 6762 §5.2).

When the cache is full, the oldest record no operation reaches gives way.
Only if every record is still reached are the operations that want the new
record failed with `MDNSERR_BUSY`; nobody else is touched. `fail_all()` is
gone. Views exist only for browsed types. More than 32 instances of a type
fail the browses of that type once, and then their records are no longer
wanted, so a restarted browse of another type is unaffected.

With strict admission, an A that comes before the SRV naming its host would
be dropped. Responders do send them in that order. So up to eight such A
records are held for one second and cached when their SRV arrives; older
ones are asked for by the missing-record query.

**Problem 2.** `mdnsDiscoveryTick()` rebuilds the views, and looks for
resolutions, only when the cache or the operations changed or the next
record expires. Ended owners are found by the new `mdnsDiscoveryReap()` on
the existing five-second reap timer, not with a `FindTaskByPID()` per
operation on every pass.

**Minor points.** `DISCOVERY.md` now says that an instance changing while
its removal is unread is seen only in its newest state. It also documents
the admission and eviction policy, and records LVOs 20 and 21 as part of
the #1559 decision.

**Validation.**

- The host fixture of the real cache, packet path and IPC passes 37/37
  under ASan/UBSan. Four of those cases are new and fail on the previous
  revision:
  - 200 instances of an unbrowsed type are not cached and do not disturb a
    browse;
  - a full cache evicts records nobody reaches any more;
  - 33 instances fail only their own type's browse;
  - an A before its SRV is kept within a second and queried for after.
- The 25 responder fixtures pass.
- bonami.library and mdnsd build with `-Werror` for x86_64 and m68k.
- The native hosted probe against an independent Linux DNS-SD peer passes
  (browse, resolve, TXT, cancel, restart).
