# Separate AppleMIDI control and the RTP-MIDI payload

The library introduced by AROS #1678 combined two protocol layers under
`rtpmidi`: the RTP-MIDI packet and journal implementation, and AppleMIDI
control packets, sessions, peers, RS feedback and CK synchronization.
The portable payload did not call AppleMIDI, but its build target and
public header directory also owned AppleMIDI. The runtime's generic
helpers introduced another dependency: MIDI 2.0 and the service core
linked this combined library for port ownership, text and event queues.

## Components

| Component | Sources | Public headers | Upstream link target |
| --- | --- | --- | --- |
| RTP-MIDI payload | `network/rtpmidi/{rtpmidi,sender,receiver,timing}.c` | `include/rtpmidi/` | `linklibs-rtpmidi` |
| AppleMIDI control | `network/applemidi/{applemidi,session,peers,clock}.c` | `include/applemidi/` | `linklibs-applemidi` |
| Neutral helpers | `network/netmidi/{ports,text,queue}.c` | `include/netmidi/{ports,text,queue,limits}.h` | `linklibs-netmidi-common` |

AppleMIDI's transport consumes both protocol components. Its control
codec can also be used and tested without the RTP payload. Both protocols
may use neutral helpers; the RTP payload never includes or calls the
AppleMIDI control component. The service core needs only the neutral
helpers; its MIDI 2.0 transport adds `netmidi2` and UMP, with no dependency
on RTP-MIDI or AppleMIDI. Existing socket and CAMD interfaces remain.

The host Makefile builds three separate static archives. The contrib
MetaMake file also builds separate archives, with package-scoped names
`arosmidihub-rtpmidi`, `arosmidihub-applemidi`, and `arosmidihub-common`,
so they coexist with upstream. The old `arosmidihub` aggregate now holds
only the other package modules; it no longer contains protocol objects.

## Migration

This is a source-level migration of internal static libraries. Consumers
must update includes, helper names and link dependencies together. It is
not a wire-format change, nor an ABI promise for already linked modules.

| Old interface | New owner/interface |
| --- | --- |
| `rtpmidi/{applemidi,session,peers,clock}.h` | `applemidi/{applemidi,session,peers,clock}.h` |
| `applemidi_*` control, session and peer functions | Same symbols, in `libapplemidi` |
| `rtpmidi_clock_offset()` | `applemidi_clock_offset()`, beside the CK exchange |
| `rtpmidi_clock_due()` | Remains in the RTP payload; session supplies the offset |
| `applemidi_ports*`, `APPLEMIDI_PORT*` | `netmidi_ports*`, `NETMIDI_PORT*` |
| `applemidi_text_length()`, `applemidi_name_fit()` | `netmidi_text_length()`, `netmidi_name_fit()` |
| `rtpmidi_event_queue`, `rtpmidi_queued_event`, `rtpmidi_queue_*` | `netmidi_event_queue`, `netmidi_queued_event`, `netmidi_queue_*` |
| `APPLEMIDI_WANTED_*`, `RTPMIDI_QUEUE_HORIZON` | Runtime policy in `netmidid.h`: `NETMIDID_WANTED_*`, `NETMIDID_QUEUE_HORIZON` |

The helper bodies and buffer sizes are preserved. CAMD and MIDI 2.0 now
use a neutral 32768-byte working-buffer bound, rather than including the
RTP payload header for its identical SysEx limit. RTP's assembler keeps
its own bound; the AppleMIDI transport checks that it fits the runtime.

The cut script `cut-netmidi-common.sh` exports the neutral component.
`cut-netmidi-library.sh n2` exports that component plus the pure payload;
`n3` additionally exports the separate AppleMIDI component. The service
cuts use explicit include and link dependencies. `n4-core` links only
`netmidi-common`. Source moves and build changes for the merged libraries
are prepared on AROS branch `review/applemidi-component-split`, based on
upstream `43e2e698c4`. The existing service PR branches are not rewritten
by this repository change. The library migration must be coordinated with
the consumer changes before either side is merged.

## Validation

- `make test`: existing assertions retained, with control/session cases
  moved to `tests/applemidi.c` and configuration round trips to
  `tests/config.c` (756 assertions before and after the split).
  Payload regressions link no AppleMIDI library or configuration module.
- `make test-components`: isolated public headers and source sets for
  the payload and control regressions, then core-only and MIDI-2-only
  runtime links without either protocol's headers or source.
- `make test-network`: passed. One earlier run timed out in the existing
  feedback case; the same class of timing sensitivity is recorded in
  `netmidid-transports-2026-10-21.md`. No retry logic or test timeout was
  changed to obtain the pass.
- `make fuzz FUZZ_ROUNDS=100000 FUZZ_SEED=7`: passed under ASan/UBSan.
- Native MetaMake service and selftest targets: hosted x86-64 and
  amiga-m68k, all features enabled, including discovery and browse.
  The old aggregate library was cleaned before building the split targets.
- Fresh generated MetaMake builds of `n4-core`, `n4`, `n7` and
  `n8-applemidi`: passed on hosted x86-64 and amiga-m68k with warnings as
  errors. The advertisement cut uses matching upstream Bonami public and
  generated headers in the isolated test SDK; the installed SDK mixes
  older generated prototypes with different public discovery types.
- `tools/applemidi-service-hosted.sh`: 8/8 with node-rtpmidi, including
  bidirectional CAMD, SysEx, per-peer ports, CK, status, Avahi, stop and
  invitation timeout/reconnection (two sessions).
- `tools/netmidi2-hosted.sh`: 3/3, including UMP/MIDI translation through
  CAMD in both directions, SysEx, All Notes Off and Avahi advertisement.

Artifacts are local under `build/review-component-split-2026-10-22/`;
generated-cut build logs are below its `cuts/` directory. These results
cover hosted execution and m68k cross-builds, not m68k hardware execution.
