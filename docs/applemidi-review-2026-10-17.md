# AppleMIDI: correction and integration review, 2026-10-17

Follow-up to the [first audit](aros-upstream-review-2026-10-17.md).
Product changes in this pass are local to MIDIHub. No AROS source branch,
published PR, or remote discussion was changed.

The subsequent [branch and PR preparation](upstream-preparation-2026-10-17.md)
records isolated local commits, the fetched upstream base, draft
descriptions and a transfer bundle. The unchanged-branch statement above
describes this validation pass.

## RTP-001: valid segmented SysEx discarded after an intervening packet

The assembler compared the RTP sequence numbers of SysEx-bearing packets
only. A SysEx start at sequence 65534, MIDI Clock at 65535, and SysEx end
at 0 was treated as lost data. A packet without MIDI commands between
segments caused the same failure. Both cases were reproduced against the
unmodified host runtime: three messages discarded, zero completed in the
new regression (the third message deliberately has a real packet loss).

[RFC 6295 section 3.2](https://www.rfc-editor.org/rfc/rfc6295.html#section-3.2)
encodes realtime commands separately; its
[Appendix C.1](https://www.rfc-editor.org/rfc/rfc6295.html#appendix-C.1)
permits System Real-Time commands between SysEx segments in the default
configuration. Receiving such a command is not evidence of packet loss.

The correction adds `rtpmidi_sysex_packet()` to the library. The runtime
calls it for every validated, fresh RTP packet, after rejecting old or
duplicate packets and before returning for an empty command section.
Contiguous packets preserve assembly, including sequence wrap; a genuine
gap clears partial SysEx. Existing direct `rtpmidi_sysex_feed()` callers
retain their sequence checking, and must use the new helper to support
intervening packets correctly. No structure layout changes.

The [library patch](../patches/aros-rtpmidi-sysex-packet.patch) applies to
the exact published #1587 head,
`2ff4abfe52946f5c8df4bec2d4c0ab6dad15736b`. It belongs in #1587; the
corresponding runtime call belongs in the forthcoming N4 service PR.
Regenerating N2/N3/N4 from this repository includes both changes.

## Executed checks

| Check | Result |
|---|---|
| `rtk make test demo` | All host suites passed |
| `rtk make test-network` | Passed, including realtime/guard interleaving, sequence wrap, an old packet, and actual loss |
| Published #1587 sources extracted, patch applied in an isolated Git repository, protocol suite with ASan/UBSan | Passed; AppleMIDI/session helpers and configuration remain local sources, outside #1587 |
| `rtk make fuzz FUZZ_ROUNDS=100000 FUZZ_SEED=7` | Passed under ASan/UBSan: 200,000 RTP inputs, 130,151 journals passed through recovery |
| `tools/applemidi-node-host.sh build/applemidi-review-node` | Both invitation directions passed, clocks exchanged, notes and SysEx received and acknowledged |
| N3 + N4 cross-compilation for hosted x86-64 AROS | Passed with `-O2 -Wall -Wextra -Werror` |

Node peer: `rtpmidi@1.0.0`, optional discovery packages omitted. This peer
does not implement recovery journals; its interoperability result does
not establish journal interoperability.

## Isolated AROS run

Sources were generated into `build/applemidi-review-source` using the
existing driver, N3 library and N4 service cut scripts. The N4 sources
and all N3 library sources were compiled directly with
`build-aros-linux/bin/linux-x86_64/tools/crosstools/x86_64-aros-gcc`,
`--sysroot /home/jaime/aros-build-netmidi-clean/bin/linux-x86_64/AROS/Developer`,
`-DNETMIDI_SERVICE`, and the three generated include directories. This
checks the generated C sources and SDK link; it is not a fresh full AROS
build or a rerun of mmake dependency generation.

The installed AROS image was copied from
`/home/jaime/aros-build-netmidi-clean/bin/linux-x86_64/AROS`, then its
service replaced with the new binary. The existing hosted script makes
its own separate boot copies:

```sh
rtk proxy sh tools/applemidi-service-hosted.sh \
  build/applemidi-review-source/AROS build/applemidi-review-hosted
```

For this service run, the driver and CAMD binaries were reused from that
image, whose previously documented build includes the CAMD alignment
correction C0. The subsequent driver run below uses a newly built driver.
Service-run artifact SHA-256 values:

```text
netmidid     85a655a13d8c8541977ae9e2b778473062310d43e39f48b065f3861f16847c57
applemidi    a86ed641e40766a1a99b54a39060e1c848e59bdd469c32df0f6af5452c616bc7
camd.library 1bc7dc2af88a935201c49cd1ec4f2ddce619d3abc8798ed724b9471b4ffe3c14
```

All seven native service checks passed across three boots:

- Stack startup, two peers assigned to ports 0 and 3, and each peer's four
  messages received only on its CAMD port.
- Scale and SysEx from CAMD reached only the peer assigned to that port.
- Status exposed the expected per-port message/packet counts.
- Service stopped when requested.
- Service invited the configured IP/port, exchanged clocks, and sent a scale.
- All six chord messages arrived; the second boot sent 23 messages in 19 packets.
- Initial invitation timed out while the peer was absent; the peer then
  connected, left, and reconnected (two established sessions).

Advertisement was skipped because N4 has no discovery. Logs remain under
`build/applemidi-review-hosted/`.

## Driver lifecycle run

The generated driver source and adapter header were compared byte for
byte with #1586 head `2710bd6e56c831cc452117755c4bcac4a54f9b6a`.
The AppleMIDI driver was then compiled with the same SDK and warning flags
as the service and installed only in the isolated image. Its SHA-256 is
`ed325507c3509cd4f0a7621d096cf0db6e163a0e1ddd7863a1e2e4d443139136`.

```sh
rtk proxy sh tools/netmidi-driver-hosted.sh \
  build/applemidi-review-source/AROS build/applemidi-review-driver
```

The initial lifecycle invocation exposed a test setup defect: unlike the
service script, the driver script did not enable the disabled service
entry in its boot copy. That invocation cannot evaluate driver behavior.
`prepare()` now explicitly sets `Enabled=yes`, matching the service test.

That same invocation also reached `Avail FLUSH` and trapped in CAMD
cleanup, independently of receiving any peer MIDI. The serial log reports
`free-list corruption at double free`, with this stack:

```text
Exec_35_FreeMem
UninitCamd+0xfd
set_call_libfuncs
Camd_3_Camd_ExpungeLib
LDFlush
```

**CAMD-001 — expunge callback has no success result.** Existing
`workbench/libs/camd/camd_init.c` registers the void function `UninitCamd`
directly. The generated `camd_start.c:415` calls the symbol set with
`test_fail=1`; `compiler/libinit/libinit.c` invokes callbacks as
`int (*)(APTR)` and leaves the library resident when one returns zero.
Interpreting a void cleanup routine this way can retain an already cleaned
library and execute cleanup again. The backtrace is consistent with that
failure; the initial run alone does not isolate every contributing factor.
This source is outside #1586's changed files.

The [minimal CAMD patch](../patches/aros-camd-expunge-result.patch) uses the
existing `Expunge()` wrapper, which calls cleanup and returns `TRUE`, on
AROS as well. The broader existing `aros-camd-endpoint-expunge.patch`
already uses a success-returning wrapper with an additional endpoint lease
check. The new patch extracts the legacy-library correction without that
future API; the two patches are alternatives, not an additive series.

Only `camd_init.o` was rebuilt, using the configured CAMD compiler flags
and generated headers. The test library was relinked with the other
existing CAMD objects; the external source tree and installed library
were not edited. Corrected library SHA-256:
`566d693caefba720050a6eaf1f0190ab7e85e524b6b4630628f329713d3f176f`.

With the corrected harness and library, all five functional driver checks
passed, including a real driver unload and delivery through the replacement.
However, serial output still contained a fault. The harness now treats
`Trap signal` or `free-list corruption` as failure even when MIDI delivery
assertions pass. First-run logs remain under
`build/applemidi-review-driver/`; this intermediate run is under
`build/applemidi-review-driver-fixed/`. The incompatible-layout fixture is
reused from the existing image.

## NETINFO-001: device freed while its worker is still cleaning up

The remaining fault was isolated with
`tools/review-camd-flush.sh`: open/close CAMD, dump loaded symbols, flush
memory, and check the serial log, without starting the network stack.
`SymbolDump` maps fault address `0x435ff189` to `netinfo.device:EntCleanup`.
Disassembly locates the failing call to `EndNotify()` through a NULL DOS
library base. The notification itself is embedded in the map, not a NULL
pointer.

`NetInfo__DevExpunge()` replies to the worker's death message and returns
`TRUE` immediately, even if it cannot acquire the semaphore. The generated
module expunge can then close its auto-opened libraries and unload its
code while `NetInfoPoll()` is still freeing maps and notifications. This
is an existing system defect outside the network MIDI PRs.

The [separate netinfo patch](../patches/aros-netinfo-expunge-task.patch)
refuses expunge while the worker is present or its lock unavailable. The
worker keeps task switching forbidden from clearing `nid_Task` through
return to DOS's exit code, following the existing UP shutdown pattern in
`NetInfoStartup()`. A later expunge can then succeed. No MIDI code changes
are included. This scheduler contract still requires independent SMP
review; the executed target is hosted uniprocessor AROS.

Only `netinfo.c` and `server.c` objects were rebuilt and relinked with the
other existing objects, using their configured SDK flags. The patch was
checked against the unchanged source at the exact #1586 head. Test binary
SHA-256: `c5a6e3c3c48a8717ee63bdc96e32080ee008e5119ee666b6873329b06954dda9`.

| Isolated open/close/flush check | Result |
|---|---|
| Corrected CAMD, original netinfo | Fault in `EntCleanup`; `build/camd-flush-review-fixed/` |
| Corrected CAMD and netinfo | Two open/close/flush cycles, clean native exit, no serial fault; `build/camd-flush-review-twice/` |
| Original CAMD, corrected netinfo; same image otherwise | First probe and flush completed, second open blocked until the 35-second boot timeout; `build/camd-flush-review-control/` |

The driver lifecycle run with both system fixes and serial-fault detection
passed all five checks under `build/applemidi-review-driver-final/`:

- Incompatible adapter layout rejected while the service continued running.
- First peer's four messages delivered through CAMD.
- Actual CAMD/driver unload under the service, followed by delivery from
  the second peer through the newly loaded driver.
- Sending to the CAMD port while the service was stopped completed.
- Restarted service attached again and delivered the third peer's messages.

Both boots exited without a trap or allocator-corruption report in serial.
This validates these concrete UP lifecycle scenarios; arbitrary concurrent
access, SMP and larger failure-injection matrices remain outside the result.

## Concrete next PRs

1. Update **#1587** with RTP-001 and retain **#1586** as the transport
   drivers. Driver interrupt/lifecycle questions remain open; these tests
   do not constitute SMP, exhaustive unload-race, or arbitrary SysEx-size
   approval.
2. Send **“camd: return success from the expunge callback”** separately
   from the network drivers and the future endpoint API, after checking
   the corrected native run. This addresses CAMD-001 in the existing
   library and needs no new public interface.
3. Send **“netinfo: defer expunge until the worker finishes”** separately
   as the system cleanup correction described above, with its scheduler
   scope explicit. It is relevant to `Avail FLUSH`, not mDNS discovery.
4. Prepare **N3: “rtpmidi: add AppleMIDI sessions, peers and clock sync”**
   after #1587. Include only `applemidi`, `session`, `peers`, `ports`,
   `clock`, their headers, and the expanded library build file. State
   the dependency on #1587 explicitly.
5. Prepare **N4: “netmidid: connect AppleMIDI sessions to CAMD ports”**
   after #1586 and N3. Include the daemon, adapter bridge, address-based
   configuration and disabled-by-default service entry. Describe both
   invitation directions, one peer per CAMD port, stop and reconnect.
   Carry the runtime half of RTP-001. N5 status can follow separately.

These are reviewable dependent changes; opening a draft before its bases
merge should identify that dependency rather than claim a standalone
master build. mDNS registration/browsing and the unresolved #1591 API
stay in their own later changes. Network MIDI 2.0 fixes remain necessary
for #1588, but do not gate this AppleMIDI path.

This pass establishes tested AppleMIDI transport behavior, not complete
RFC 6295 conformance. The existing AppleMIDI RTP marker-bit compatibility
choice, the full recovery journal review, hardware behavior and native
timing under load still require explicit coverage. Native status reports
four late timed events per peer in the first boot; this run does not
establish a native latency bound. The specific interleaving regression
runs on the host; the native probe uses complete SysEx messages.
