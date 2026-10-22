# Network MIDI 2.0 corrections and dependent service draft, 2026-10-18

The user authorized opening work with explicit dependencies as drafts.
The latest upstream fetched for this work was
`9bf9977d8dd65786165731e276b302a9bc035a0d`. Nick Andrews moved the
shared network-driver C source and adapter header from `common/` into
`workbench/devs/camd/networkmidi2/`; both new work and the existing
AppleMIDI service now follow that location.

| PR | Published head | Status / remaining dependency |
|---|---|---|
| [#1692](https://github.com/aros-development-team/AROS/pull/1692) | `1d7de169fc45f4630474e1dc63b7c49ab6f17176` | Ready for review; independent library corrections |
| [#1693](https://github.com/aros-development-team/AROS/pull/1693) | `7ddf7e46a82523a4700369bf2b6bd03f26db14d9` | Draft; depends on #1679 and #1692, transitively #1678 |
| [#1679](https://github.com/aros-development-team/AROS/pull/1679) | `70d61fb1d2a6c6f406101637cbe6144137d2ff94` | Existing draft; follows upstream header move; still depends on #1678 |

## Library corrections

The audit's NM2-001 and NM2-002 were reproduced against actual upstream
`9bf9977d8d`: the two zero-capacity output cases overflow under ASan,
and malformed acceptance establishes a Client session. Three of the
nine pinned regression/suite cases failed before the correction.

#1692 requires non-NULL, positive-capacity identity outputs and rejects
invalid arguments/encoded lengths before changing either output. Accepted
peer identities are decoded into temporary buffers and validated before
publishing metadata or establishing the Client session. Invalid lengths,
empty names and empty product IDs produce NAK / Command Malformed;
pending invitation retries remain possible. This follows
[M2-124-UM v1.0 section 6.15 / table 25](https://amei-music.github.io/midi2.0-docs/amei-pdf/M2-124-UM_v1-0_Network-MIDI-2-0-UDP.pdf).
The earlier host invitation path is unchanged.

All nine cases now pass under ASan/UBSan. The Network MIDI 2.0 suite was
extended with NULL/zero/nonzero output checks, no partial modification,
malformed and empty accepted identity cases, retained peer metadata,
retry, valid acceptance and duplicate acceptance. These companion tests
remain in MIDIHub; the upstream correction changes two library files.
The zero-capacity API finding is not a demonstrated remote daemon overflow.

## Service integration and upstream layout

The isolated N7 feature commit is `7ddf7e46a8`: seven files, 577 additions
and nine removals over the prepared prerequisites. #1693 adds Network
MIDI 2.0 Host/Client sessions by IPv4 address, CAMD port assignment and
UMP/MIDI translation. Its aggregate master diff contains its explicitly
listed prerequisites until they merge. Rebase after those merges.

#1679 was advanced without rewriting published history: merge the latest
upstream layout, then fix the service's adapter-header include path.
Its AppleMIDI-only cross-build remains byte-identical to the preceding
validation (`35349df3770f5e85ad3d94f16a2db8916350498cd8e9e434c21cc0f29f1912d6`).
The main generation scripts and driver/service templates were adjusted
as well; regeneration reproduces 47 proposed files exactly and preserves
both libraries and the status tool across service stage changes.

The library corrections, parent service adjustment and integration branch
were pushed atomically. PR descriptions state current scope, merge order,
isolated feature commits and validation limits. #1692 is not draft because
its library prerequisite is already upstream; #1693 is explicitly draft.

## Validation and limits

- Generated makefiles build both libraries, all four CAMD drivers and the
  N7 service with the existing hosted x86-64 SDK in isolated output paths.
  The SDK view selects the newly built archives, including corrected MIDI 2.0.
- Direct service/driver cross-builds and RTP-MIDI ASan/UBSan suite pass.
- Fresh service/driver binaries on the existing hosted UP AROS image pass
  three Host checks: incoming notes/controller/SysEx through CAMD and
  All Notes Off after Bye; outgoing scale/SysEx; persisted product identity
  without mDNS advertisement.
- The three Client checks pass: invite the configured IPv4 Host with the
  persisted identity; receive its four messages through CAMD; send the
  CAMD scale/SysEx back. Both serial logs have no detected fault signatures.
- Separately, the exact corrected library passes Linux loopback exchanges
  with independent NetUMP `f74b0b8` and BEBSDK `022d0f2`, as Host and Client:
  notes, controller, SysEx and goodbye. The hosted AROS runs use our own
  peer and do not constitute independent wire interoperability.
- At final inspection, all checks were successful for #1692, #1693 and
  updated #1679, including Codacy; all three were mergeable without conflicts.

This does not establish a fresh full current-master AROS build, hardware
or SMP coverage, independent loss-recovery interoperability, simultaneous
AppleMIDI/MIDI 2.0 stress or MIDI 2.0 unload/restart safety. #1677's netinfo
lifetime review remains separate. No Bonami/mDNS dependency or native
UMP CAMD application API was added; this service's CAMD traffic is MIDI 1.0.

Local preparation branches in `build/upstream-pr-prep` are
`review/netmidi2-identity-fix`, `review/service-driver-layout` and
`review/netmidid-midi2`. Validation artifacts, before/after regression
logs, native images/logs, independent peer sources/binaries and PR body
snapshots are under `build/review-midi2-2026-10-18` or the pinned
`build/upstream-review/<revision>` directories. The verified bundle is:

```text
build/review-midi2-2026-10-18/published.bundle
SHA-256 b58a016edb33f8e9fb095e6092e9f0fdb7e14294ebc99a05a33849cbe0faa91d
```

It requires upstream `9bf9977d8d` and `eee73b6156`.
