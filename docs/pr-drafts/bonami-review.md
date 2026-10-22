Programs can publish local services through `bonami.library`; mdnsd probes,
announces, answers for and withdraws their DNS-SD records. This is the
registration slice proposed in #1559, on top of #1560. Browse, resolve and
lookup remain separate work. The proposed library name and broader event/API
architecture remain open to maintainer guidance.

### Changes following review

This update addresses [Kalamatee's review](https://github.com/aros-development-team/AROS/pull/1591#issuecomment-6250825444):

- All fifteen original Bonami functions have their original order, signatures
  and registers. Slots 6–15 are compatibility stubs: LONG functions return
  `MDNSERR_NOTREADY`, void functions do nothing. They invoke no callbacks,
  alter no output buffers and acquire no memory ownership. Original public
  flags, events, structures, constants and NIPC headers are included.
- The README/autodocs explicitly describe deliberate lifetime differences:
  `CloseMDNSEngine()` releases the calling task's registrations; only the
  registering task may update/remove them; local duplicates are rejected;
  that task's final library close or termination withdraws them even if
  another task in the program keeps the library open.
- Ownership and client cleanup use `GetETaskID()`/`FindTaskByPID()` instead of
  raw task addresses and manual ready/wait scans. Exec's lookup also covers
  running/spinning SMP tasks. Reusing a task address no longer inherits its
  old registrations. Library/daemon private IPC is now version 2; install
  them together. The library version is 41.1.
- Identical SRV/TXT records and TTL-zero goodbyes do not cause renaming.
  SRV target comparison handles compressed names and DNS case equivalence.
  Local suffix renaming skips occupied names.
- Instances are limited to one 63-byte label. Literal dots return
  `MDNSERR_BADPARAM` until presentation escaping is implemented.
- Network reconfiguration restarts service probing with host probing.
  Disconnected services wait for an interface and a settled host name.
- Proxy registrations require a local hostname/address pair and reject this
  host's current/base name. **Proxy hostnames are not independently probed**;
  the caller must supply a name/address it may advertise.
- Invalid update TTLs are rejected before modifying the registration; valid
  TTL updates apply and announce.

The update also fixes reproduced responder defects: known-answer suppression
checks owner/target and half the actual service TTL; incompatible classes and
nonzero opcode/rcode are ignored; withdrawing one service preserves the shared
host A record; updates announce twice; simultaneous probes include and compare
the complete TXT/SRV set rather than only the SRV.

### Implemented registration API

| Function | Behavior |
|---|---|
| `OpenMDNSEngine()` | Checks that mdnsd is running |
| `CloseMDNSEngine()` | Withdraws the calling task's registrations |
| `RegisterServiceA(tags)` | Registers a service for asynchronous probing/publication |
| `UpdateServiceA(ref, tags)` | Updates port, TXT or TTL |
| `DeregisterService(ref)` | Withdraws the service |

Calls exchange messages with mdnsd and wait for replies; without it they
return `MDNSERR_NOENGINE`. The README includes a client example and documents
supported registration tags. TXT input is space-separated items with escaped
spaces, encoded as separate DNS TXT strings. TTLs are 1–4500 seconds;
defaults are 4500 for PTR/TXT and 120 for SRV. Probing waits for the host name;
type browse responses are delayed and aggregate matching service instances.

### Validation of this update

- **25/25 deterministic packet-path fixtures pass with ASan/UBSan** against
  `55e7f21ec2ac29f774de27eada5524b8d01c1c54`, including conflicts, compressed
  SRV, known-answer ownership/TTL, full-set tie-break, update announcements,
  local renaming, proxy validation, task-ID reuse and re-probing. Exact PR
  sources are compiled with mocked Exec/time/network; this does not emulate SMP.
- **Native linux-x86_64 hosted:** registration 6/6, TTL 3/3 and tie-break 2/2
  pass over a veth pair with Linux packet inspection/independent peer input.
  A native ABI probe calls slots 6–15 and checks version, invalid update TTLs
  and current task-ID lookup. Registration and ABI were repeated with the
  final library after its type-correct MinList empty check. The final Codacy
  follow-up removes only an unreachable duplicate host-ready test; packet
  captures precede that removal, while fixtures/builds were repeated after it.
- **Builds:** generated library, link library and daemon build for
  linux-x86_64 and amiga-m68k. Builds use `-Wall -Wextra -Werror` with warning
  exclusions needed by generated SDK code. Hosted uses an existing UP image
  with these new binaries, rather than a full current-upstream image rebuild.
- Original signatures/registers, generated function offsets and original
  public constant values were compared against amigazen/Bonami
  `540e93b2eae5bc35cef8fa05ac9a254174a29364` (except the intentional version bump).

The harnesses and ABI probe are in the local companion
aros-midihub workspace
(`tools/review-mdns-responder.py`, `tools/mdns-*-hosted.sh`,
`tests/review/bonami_abi_probe.c`); they are outside this AROS source slice
and the new fixtures have not been published with this PR.
Earlier hosted coverage included answer delay, unicast, hostname changes and
the parent responder suite; those larger suites were not repeated in this update.

### Limits and remaining work

IPv4 only. No runtime test on SMP/hardware, against Apple's Bonjour or Windows,
or between two AROS machines. Network-return re-probing has deterministic
fixture coverage, not a hosted interface-reconfiguration capture. These fixes
do not establish complete IPC/daemon-shutdown or SMP safety. Discovery callback
delivery and broader API/lifetime work remain separate review topics.

Multi-packet known answers are not awaited, service-type enumeration answers
are not delayed, and one-second multicast repeat suppression is not implemented.
TXT input is limited to 256 characters. This is not a claim of complete RFC
6762/6763 conformance.

The code derives from [amigazen Bonami](https://github.com/amigazen/Bonami)
and retains its BSD 2-Clause notices. The original library hosted its engine;
this implementation uses messages to a system daemon.

Original submission provenance:
