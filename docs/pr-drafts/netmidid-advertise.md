Publish netmidid's AppleMIDI session as `_apple-midi._udp` through the merged
system `bonami.library`/`mdnsd` registration API. Other DNS-SD clients can
discover the session name and its control port without entering the AROS
address manually.

The daemon owns multicast sockets, probing, conflicts, records and goodbyes;
netmidid only registers its session. It retries late library/daemon availability,
checks registration handles at most once per ten seconds and withdraws on
shutdown. A stale handle after daemon restart is discarded and registered
again. The public `UpdateServiceA()` call used for validation also refreshes
the announcement in the current responder.

The service remains disabled by default (`Enabled=no`). Its start order is
after mdnsd. Existing `ENV:NetMIDI` configuration applies, for example:

```ini
session_name=AROS MIDI
local_port=5004
```

MIDI sessions remain usable by IPv4 when the discovery library or responder
is absent. Successful registration is asynchronous; probing may still be
pending. Instance names follow the merged API's current constraints: one
label, at most 63 bytes, without literal dots.

### Dependencies and scope

**Dependent draft: requires #1679**, the AppleMIDI service. #1591, which
provides registration and the responder, is already merged. The branch
contains the two #1679 commits and one advertisement commit; rebase onto
master when the parent merges.

This AppleMIDI-only slice does not depend on #1693, #1695 or the status-command
PR. It does not browse/resolve remote peers or add the Network MIDI 2.0
advertisement. It adds no Bonami callbacks, dispatcher or event-delivery API.
The implementation and lifetime behavior of the merged library are preserved.

### Validation

- Complete service builds and links pass for hosted x86-64 and amiga-m68k,
  through generated makefiles and direct SDK builds. Direct builds use
  `-Wall -Wextra -Werror` with the existing m68k volatile-register exclusion.
  Matching Bonami headers are generated from the merged library sources,
  avoiding older installed SDK interfaces.
- Seventeen ASan/UBSan checks compile the exact exported discovery source
  with mocked platform calls: no services, unavailable library/daemon,
  registration, rate limiting, a daemon observed absent, a fast restart,
  reused foreign handles, transient update failure and idempotent cleanup.
- Native hosted UP AROS and node-rtpmidi 1.0.0: both invitation directions, clocks,
  notes/controller/SysEx through CAMD, port isolation/reservation, counters,
  service stop and reconnect pass over three boots. The status command used
  for inspection is a separately proposed change.
- Native packet capture checks the exact configured instance and PTR/SRV/TXT/A
  records, AROS address and port, late daemon startup, re-registration after
  daemon restart, and TTL-zero goodbyes at daemon/service stop. MIDI traffic
  also succeeds while mdnsd is initially absent.

### Fresh Avahi interoperability (2026-10-20)

The configured instance is now verified by Avahi 0.8 in a continuous native
hosted run. A unique name per run prevents cached entries from satisfying the
test. The browser stays alive for the entire scenario, and the harness requires
two ordered add/resolve/remove cycles: initial late daemon startup, daemon
shutdown, restart and re-registration, then netmidid shutdown. Both resolutions
report the expected AROS address and control port (192.168.166.2:5004).

The earlier observation gap came from using `avahi-browse -t`, which exits
after its initial list before this test's late responder starts. The corrected
companion harness uses `-rp`, checks browser lifetime, and validates the
unique instance and event order. No AROS implementation change was needed.
The final run also verifies PTR/SRV/TXT/A and both TTL-zero withdrawals in the
packet capture, plus MIDI traffic by IP while mdnsd is initially absent.
Harness: `tools/review-netmidi-advertise-hosted.py` in the companion workspace.

These tests use existing SDKs and an existing hosted UP image with newly
built service/library/responder binaries. They do not claim a full
current-master rebuild, hardware/SMP execution, Apple Bonjour or Windows
runtime validation, or complete RFC 6762/6763 compliance. The merged
library's public-port/IPC concurrency limitations remain separate work.
The companion aros-midihub workspace contains the test harness and fixtures.
