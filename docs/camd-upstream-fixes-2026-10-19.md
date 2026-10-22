# CAMD fixes sent upstream, 2026-10-19

All on AROS master `f19edd39eb`, each in its own worktree under `build/`,
built with `build/review-camd-2026-10-19/build.py` (x86-64) and
`build-m68k.py` (full amiga-m68k mmake target, link included), tested on the
clean hosted image `~/aros-build-netmidi-clean/bin/linux-x86_64/AROS` with
only the library or driver swapped. Runners: `run.sh`, `run-compat.sh`,
`run-part.sh`, `run-usb.sh`, `run-watch.sh` in that directory; probes in
`tests/review/camd_*`.

| PR | Branch | What |
|---|---|---|
| #1730 | `review/camd-port-open` | port closed under a receiver linked after a sender |
| #1731 | `review/debugdriver-port-zero` | `UserData[-1]` on port 0 (x86-64: padding; aarch64: crash) |
| #1732 | `review/camd-cluster-notify` | notify lock, `RethinkCAMD()`, driver watch, participants |
| #1733 | `review/usb-midi-camd` (draft) | USB MIDI usable through CAMD; no device test |
| #1734 | `review/camd-sysex-errors` | `MIDI_ErrFilter`, `CMEF_SysExTooBig`, off by ones |
| #1735 | `review/btmidi-codec` | BLE-MIDI header reserved bit ignored; F9/FD dropped |

## Decisions

- A class never opens camd.library (Kalamatee, #1507 review). #1733 had
  `RethinkCAMD()` in the USB class; removed. CAMD finds new drivers itself
  (#1732 commit 4): its wait process gets a DOS notification on
  `DEVS:Midi` and starts a short-lived scanner. The wait process must not
  scan: `CamdWait()` callers may hold the locks a scan needs (deadlock). No
  resident process was added.
- `sysex-errors` sent without v42: only the `sysexdropped` counter depended
  on it.

## Found, not fixed

- (Retracted) An `Avail FLUSH` "stall" was the test script stopping: the
  probe returned 20 and the script had no `FailAt 21`. Master camd.library
  flushes fine (`run-flush.sh`, `f-watch-failat`). Probe scripts that may
  fail need `FailAt 21`.
- Fixed in #1733 commit 4: `camdusbmidi.class` was not built for
  amiga-m68k, and its m68k path used the 2006 `CAMDDriver.c` (`Flags=0`,
  refused by AROS camd). Now built on m68k with the compiled driver as a C
  array (btmidi's recipe from `58e427774c`).
- Sent as #1735: the two btmidi codec commits from `~/AROS-btmidi`.

## Pending

- C1 issue draft: `docs/pr-drafts/camd-driver-block-issue.md`. On hold by
  the owner's choice: architectural proposals with wide consequences (C1,
  the endpoint API) are not raised upstream for now; fixes and natural
  choices are.

## All open PRs together (2026-10-19, master `a399fe2fc7`)

Branch `review/all-prs-check` in `build/all-prs`: master plus the heads of
#1677, #1678/#1679/#1693, #1695, #1730-#1735, merged without a conflict.
Every touched module built for x86-64 and amiga-m68k (full targets) with
the fixed harness; x86-64 netmidid linked by hand because the harness's
link still resolves `-lrtpmidi` from the SDK sysroot first.

Hosted x86-64, combined binaries: CAMD contract 59/0 (master 54/5),
RethinkCAMD 8/8, participants 10/10, port-open order correct, driver watch
8/8 with two flush cycles, netinfo flush test 2/2, USB without a device,
BLE codec test, mDNS host suites 33/33 and 25/25, native discovery OK.
AppleMIDI service (`tools/applemidi-service-hosted.sh`, node-rtpmidi peers)
on the same combined image: every check passed; the advertisement case is
skipped because discovery (N8) is not in any PR.

## Issue #1736 (2026-10-19)

Opened at the owner's request, separately from the fix PRs: CAMD drivers
handed over from memory (`AddMidiDevice`/`RemMidiDevice`), as AHI allows
with `OpenLibrary()` and `AHI_AddAudioMode()` (usbaudio.class, #1727). Asks
only about the file in `DEVS:Midi`, not the driver model. Text:
`docs/pr-drafts/camd-memory-drivers-issue.md`.
