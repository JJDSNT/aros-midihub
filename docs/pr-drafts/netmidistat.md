Add `C:NetMIDIStatus` to inspect the snapshot already published by netmidid:
the session name, refused invitations, active driver ports, peer names and
traffic/loss/timing/SysEx/journal counters. Inactive ports are omitted.

Example:

```text
session "AROS MIDI", 0 invitations refused
applemidi.3 "Kept peer": in 4 messages in 1 packets, 0 lost; out 17 messages ...
```

The command takes a shared copy using the parent's versioned status semaphore
and prints after releasing it. It does not open sockets or CAMD links, start
the service or change its configuration. An absent service or unsupported
snapshot version produces a diagnostic and RETURN_WARN (5).

### Dependency and scope

**Dependent draft: requires #1679**, which provides the service, status ABI
and header. The branch contains its three commits followed by one command
commit, unchanged, which reads the service only through that ABI; rebase onto master after #1679 merges. This change adds only the
command source, build entry and README under
`workbench/network/netmidid/netmidistat`.

The existing 32-slot ABI also describes Network MIDI 2.0 ports, so the command
can display them when the service supports that transport. This PR does not
depend on #1693 and adds no MIDI 2.0 transport behavior.

### Validation

- The command builds and links for hosted x86-64 and amiga-m68k through
  generated makefiles and by direct SDK builds with `-Wall -Wextra -Werror`
  (the existing m68k volatile-register exclusion applies).
- Native hosted UP AROS: seven checks pass for service absence, incompatible
  version, session/refused-invitation output, AppleMIDI counters, MIDI 2.0 slot
  numbering in a synthetic snapshot, inactive-port omission and publisher
  removal. The synthetic MIDI 2.0 slot is a formatting test, not network interop.
- With the actual service and node-rtpmidi 1.0.0, the command reports the
  expected four incoming messages and seventeen outgoing messages on the
  reserved port. The three-boot suite also passes both invitation directions,
  MIDI traffic, port isolation, stop and reconnection.

These runs reuse an existing hosted image with replacement binaries and
existing SDKs. They are not a full current-master rebuild or native m68k,
hardware or SMP execution. The command uses the parent's semaphore lookup
and lifetime protocol; this PR does not establish its SMP safety.
The native probe and test harness remain in the companion aros-midihub
workspace, outside this small AROS source slice.
