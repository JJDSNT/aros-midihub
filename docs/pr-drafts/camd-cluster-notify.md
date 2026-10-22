Four related changes to how CAMD finds drivers and tells clients about
clusters. They touch the same code, so they come in one PR.

**Lock the cluster notification list.** `StartClusterNotify()` and
`EndClusterNotify()` changed the list with no lock while `NewCluster()` and
`RemoveCluster()` walked it from other tasks. The list gets its own
semaphore, taken after `CLSemaphore` and never before, so both calls still
work under `LockCAMD()`.

**Implement `RethinkCAMD()`.** It was a stub printing "not implemented". It
now loads drivers in `DEVS:Midi` that are not loaded yet. A client may have
linked to `<name>.out.N` before the driver existed, which creates that
cluster empty; the new driver's ports join it instead of making a duplicate,
and open at once for the links waiting there. The scan has its own
semaphore so `CLSemaphore` is not held over `LoadSeg()`. A driver whose
ports fail to attach is expunged and closed; `FreeDriverData()` also frees
each port's `DriverData` now.

**Load drivers added to `DEVS:Midi` without being asked.** A device class
that writes its driver once CAMD is running (`camdusbmidi.class` for a device
plugged in later, `btmidi.class` when it starts) cannot make CAMD load it:
as the review of #1507 asked, a class does not open camd.library, so it
cannot call `RethinkCAMD()`. CAMD's existing wait process now asks DOS to
signal it when `DEVS:Midi` changes and starts a short-lived process that
waits half a second and runs the same scan. If a candidate still cannot be loaded,
it retries up to twenty times at half-second intervals; a new directory
notification starts a fresh retry window. The
wait process does not scan itself, since callers of `CamdWait()` may hold
the locks a scan needs. No process stays resident for this. `UninitCamd()`
stops new scans and waits until the scanner is gone from Exec before
freeing anything. Where the filesystem cannot notify, drivers are found when
CAMD starts, as before.

**Participant notification.** `MIDI_PartSignal` and `MIDI_PartHook` were
stored and never used, and `mcl_Participants`/`mcl_PublicParticipants` never
kept. When a link or a driver port joins or leaves a cluster, the counts are
redone and every other node linked there is told once. The hook runs in the
changing task under `CLSemaphore`, like a receive hook.

Validation on hosted x86-64 against an unmodified upstream system, comparing
current master's camd.library with this branch:

| | master | this PR |
|---|---|---|
| CAMD 41 contract suite (60 checks) | 54 pass, 5 fail, 1 skip | same |
| `RethinkCAMD()` with `debugdriver` installed after boot | 4/8 | 8/8 |
| Participant signal, hook and counts (10 checks) | 2/10 | 10/10 |
| Driver copied to `DEVS:Midi` after boot, client already linked, nobody calls `RethinkCAMD()` (8 checks) | 5/8 | 8/8 |

The five contract failures are SysEx error reporting, unchanged and fixed by
#1734. In the driver test the client's waiting receiver gets connected and
its sender reaches the driver, a second write of the same file loads
nothing twice, and CAMD is then expunged with `Avail FLUSH`, reloaded and
used again without a fault.

`Avail FLUSH` on the unmodified image also faults in netinfo.device; #1677
fixes that and was used for the flush runs. The notification-list lock is a correctness change from reading the
code; the race it closes was not reproduced. No new compiler warnings;
builds for x86-64 and amiga-m68k.


### Review follow-up (2026-10-20)

Addresses [the maintainer's review](https://github.com/aros-development-team/AROS/pull/1732#issuecomment-6290003540):

- The public `SetMidiAttrsA()` autodoc specifies the participant hook's
  `(hook, midinode, cluster)` arguments, register assignments, updated counts,
  exclusive `CLSemaphore`, and restrictions on relinking, deletion and waiting.
  `MIDI_PartSignal` is documented as a signal number targeting `MIDI_SignalTask`.
- The driver watcher retries unsuccessful loads for about ten seconds rather
  than relying on one delay or a second filesystem notification. Invalid files
  do not keep its scanner resident indefinitely. `RethinkCAMD()` documents its
  unsuccessful-scan return value.
- All `FreeSignal()` calls in the CAMD wait process now pass signal numbers.

Fresh hosted UP AROS validation: the participant probe passes 12 checks;
the driver-watch probe passes all 8 checks with an initially incomplete file
completed two seconds later directly from the host, without a second DOS
notification. The previous watcher failed two checks in that scenario.
Both runs finish `Avail FLUSH`. The complete CAMD module builds and links
for x86-64 and amiga-m68k. These are isolated SDK/module builds and hosted
runs, not physical-hardware or SMP execution.
