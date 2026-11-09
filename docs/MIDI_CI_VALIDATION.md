# MIDI-CI validation record

Last run: 2026-11-07. Results distinguish host execution from AROS runtime
execution.

## Implemented test target

```sh
make build/midici-test
./build/midici-test
```

Result:

```text
midici: 0 failure(s)
```

The target compiles `tests/midici.c`, `src/midici.c` and the canonical
Midihub `src/ump.c` with C99, `-Wall -Wextra -Werror`.

The complete existing host suite also passed with `make test`. This includes
the Network MIDI, UMP and CAMD private endpoint/queue/worker regressions as
well as the new target. AddressSanitizer and UndefinedBehaviorSanitizer passed
the MIDI-CI target with `ASAN_OPTIONS=detect_leaks=0`; LeakSanitizer itself
cannot operate under this sandbox's tracing environment. A ThreadSanitizer
binary was built for the concurrency test, but this container aborts it before
the test with `unexpected memory mapping`; no ThreadSanitizer result is
claimed.

## Current automated coverage

| Case | Evidence | Result |
| --- | --- | --- |
| MIDI-CI 1.2 Discovery decode | Byte vector derived from M2-101 Table 6; fields checked | pass on host |
| Discovery encode/decode | Exact byte-for-byte round trip | pass on host |
| Reply to Discovery | Function Block 3 encode/decode | pass on host |
| Endpoint Inquiry/Reply | M2-101 Tables 9/11 byte vectors; borrowed reply payload and 14-bit length | pass on host |
| ACK | M2-101 Table 13 Timeout Wait report and text, exact round trip | pass on host |
| NAK | MIDI-CI 1.1 short form and 1.2 full malformed report, exact round trip | pass on host |
| Invalidate MUID | Exact known frame encode/decode | pass on host |
| Discovery responder | Known Discovery produces the expected Reply, including Output Path echo | pass on host |
| Endpoint responder | Status 0 produces Product Instance Id; malformed inquiry produces NAK 0x41 | pass on host |
| Responder MUID collision | Unused MUID changes then replies; used MUID emits Invalidate, changes and does not reply | pass on host |
| Invalidate receive | Local target renews MUID without reply; transaction reset/cancellation is deterministic | pass on host |
| Truncated frame | Missing terminator/version-required data rejected | pass on host |
| Non-7-bit SysEx data | Rejected before typed decode | pass on host |
| Unsupported major-version bits | Explicit `MIDICI_UNSUPPORTED` | pass on host |
| Future minor version | Known prefix parsed, appended fields ignored | pass on host |
| MUID reserved range | Entropy mapping always produces assignable MUID | pass on host |
| MUID collision | Same local source triggers new MUID and transaction reset | pass on host |
| Transaction correlation | Unique 7-bit request IDs and exact remote/kind completion | pass on host |
| Conflicting operation | Same remote and kind returns busy | pass on host |
| Concurrent clients | Eight host threads enter one externally serialized endpoint context; one accepted, seven busy | pass on host |
| Timeout | Caller-supplied monotonic deadline produces one timeout event | pass on host |
| SysEx7 over UMP | Discovery packetized by canonical `ump_from_midi1`, reconstructed by canonical bounded `ump_to_midi1`, then decoded | pass on host |
| Decoder bounds smoke test | 10,000 deterministic arbitrary buffers, length 0–80, through all Management decoders | pass on host and ASan/UBSan |
| Function Block sized input | Canonical PR #2101 registry source with 104-byte two-record array; canonical records zero-fill appended fields | pass on host |
| Function Block full input | 112-byte records round-trip MIDI-CI version 2 and five SysEx8 streams; 256 and inconsistent stride rejected | pass on host |
| Function Block public header | `x86_64-aros-gcc` compile canary asserts 112-byte size and offsets 104/108 | pass for AROS Linux-hosted target |
| Function Block public runtime | Old-header client publishes 104/112 records, guarded short output and known Stream vector | 8/8 on AROS Linux-hosted |
| Function Block differential | Identical client against unpatched CAMD 43 reports size 104, rejects 112 input and lacks both values | expected 4 failures reproduced |
| AROS coordinator ownership | Two independent Exec tasks contend; exactly one owns and one is busy | pass on raspi-aarch64 AROS/QEMU |
| AROS coordinator cancellation | Exact owner/task/token cancellation releases the operation to a new task | pass on raspi-aarch64 AROS/QEMU |
| AROS coordinator dead owner | Owner exits without cancellation; next request reaps it and succeeds | pass on raspi-aarch64 AROS/QEMU |
| AROS coordinator shutdown | Remaining logical ownership cleared and public prototype port removed | pass on raspi-aarch64 AROS/QEMU |
| CAMD abnormal client termination | 20 `RemTask()` rounds; each owns opener, signalled watch, snapshot, publisher and signalled input session | pass on AROS linux-hosted |
| CAMD dead-owner reaper | Healthy observer receives every endpoint departure before capacity reuse | pass on AROS linux-hosted |
| CAMD stale signal/capacity recovery | Fresh watch and snapshot succeed after every forced death; no crash or exhaustion | pass on AROS linux-hosted |
| Native UMP resident driver | Adjacent extension publishes UMP/MIDI 2.0 and opens independent input/output sessions | pass on AROS linux-hosted |
| Native UMP exact delivery | Four-word Data128 looped through driver callbacks without MIDI 1.0 conversion | pass on AROS linux-hosted |
| Native UMP removal | First removal refused while sessions pin provider; detach succeeds after close | pass on AROS linux-hosted |

## AROS-native coordinator run

The probe was built by AROS MetaMake as
`AROS/Extras/aros-midihub/Tests/MIDICICoordProbe` and executed with:

```sh
tools/midici-coordinator-qemu.sh \
  /home/jaime/aros-build-aarch64-core/bin/raspi-aarch64/AROS/Extras/aros-midihub/Tests/MIDICICoordProbe \
  /home/jaime/aros-build-aarch64-core/bin/raspi-aarch64/AROS \
  /tmp/midici-coord-run-2
```

Result: `MIDI-CI-COORDINATOR 10 checks, 0 failures` followed by
`PROBE-DONE`. The first run found a real cancellation race: the client exited
before its cancellation was consumed, so the coordinator correctly reaped it
as dead and rejected the now-stale cancel. Keeping the client alive until the
ACK removed that ambiguity; the passing behavior is the required service
contract.

## CAMD abnormal-client-termination run

The final combined library and compatibility client were rebuilt from PR
#2101 head `e82edc9287` in `/home/jaime/AROS-camd43` using the existing
Linux-hosted build `build-camd43-hosted`, then injected into a copied system:

```sh
tools/camd-task-death-hosted.sh \
  build-aros-linux/bin/linux-x86_64/AROS \
  build-camd43-hosted/bin/linux-x86_64/AROS/Libs/camd.library \
  build-camd43-hosted/bin/linux-x86_64/AROS/Extras/aros-midihub/C/MIDIHubCAMDCompat \
  /tmp/camd43-pr-task-death-20261108
```

Result:

```text
PASS forced termination creates all endpoint object kinds repeatedly
PASS the private reaper withdraws each dead task's endpoint
PASS watch, snapshot and signal slots recover after every death
camdcompat: 3 passed, 0 failed, 0 skipped
PASS abnormal-task-termination regression on AROS linux-hosted
```

Differential reproduction used the identical suite and system image with the
previous CAMD 43 library. The child reached `TASK_DEATH_READY`, but its first
forced removal produced no endpoint-departure event. The test stopped at round
zero with 0 passes and 3 failures. This demonstrates the missing capability;
the passing result is not merely a test that also passed before the patch.

This is actual AROS runtime evidence. It is not described as QEMU or hardware
validation. The initial sandboxed boot could not initialize the X display;
the same copied image passed when Xvfb was allowed to create its display
socket. No CAMD assertion failed in that discarded boot because the suite did
not start.

## CAMD Function Block public-ABI run

The isolated patch was first validated against `4c1510f1d0`. Final integration
was repeated from PR #2101 head `e82edc9287` in the canonical
`/home/jaime/AROS-camd43` worktree. The public-header canary was compiled
against the proposal headers with:

```sh
build-aros-linux/bin/linux-x86_64/tools/crosstools/x86_64-aros-gcc \
  -c tests/camd_function_block_public_canary.c \
  -Ibuild-camd43-fb-hosted/bin/linux-x86_64/AROS/Developer/include \
  -Ibuild-camd43-fb-hosted/bin/linux-x86_64/gen/include \
  -o /tmp/camd-function-block-public-canary.o
```

The runtime client was deliberately compiled against the unextended 104-byte
header and executed with:

```sh
tools/camd-function-block-hosted.sh \
  build-aros-linux/bin/linux-x86_64/AROS \
  build-camd43-fb-hosted/bin/linux-x86_64/AROS/Libs/camd.library \
  build-camd43-hosted/bin/linux-x86_64/AROS/Extras/aros-midihub/C/MIDIHubCAMDCompat \
  /tmp/camd-function-block-hosted-main
```

Result: 8 passes, 0 failures. It accepts a legacy 104-byte publication and
returns zero for absent appended fields; then projects the known UMP words
`f0118037 05010205 00000000 00000000`, accepts the 112-byte record and returns
MIDI-CI version 2 plus five SysEx8 streams. A 104-byte output surrounded by an
eight-byte `0xa5` canary is not overwritten, while its `Size` is updated to
112. The identical run with CAMD 43 lacking the proposal produces four
expected failures, directly demonstrating the missing capability.

The general CAMD contract, v42 and v43 sections pass 71/71, 38/38 and 229/229
against the candidate. The aggregate runner also has one lifecycle expectation
outside this patch: it observes one debug-driver load where it expects two.
The unpatched baseline produces the identical result, so it is not attributed
to the Function Block change. These are AROS Linux-hosted results only; no
AArch64 or hardware validation is claimed.

## CAMD native UMP driver-extension run

The independently reviewable patch remains
`patches/aros-camd-native-ump-driver-extension.patch`; its implementation is
now commit `e82edc9287` in PR #2101. Final integration used the single
canonical worktree and `build-camd43-hosted`. `MIDIHubUMPDriverProbe` is a resident one-port driver
whose `MidiDeviceDataUMPV1` retains valid MIDI 1.0 fallback callbacks and adds
the optional UMP tail. It was run with:

```sh
tools/camd-ump-driver-hosted.sh \
  build-aros-linux/bin/linux-x86_64/AROS \
  build-camd43-hosted/bin/linux-x86_64/AROS/Libs/camd.library \
  build-camd43-hosted/bin/linux-x86_64/AROS/Extras/aros-midihub/C/MIDIHubUMPDriverProbe \
  /tmp/camd43-pr-native-ump-20261108
```

Result: 7 passes, 0 failures. The endpoint snapshot reports native UMP,
current MIDI 2.0 protocol and the reference transport; separate input and
output sessions open; a Data128 event arrives with the same four words; and
`RemoveMidiDevice()` is refused until both sessions close, after which detach
succeeds. This is AROS Linux-hosted runtime evidence.

The first runtime attempt trapped immediately after successful publication.
Moving the legacy allocations below the native branch had left their cleanup
pointers uninitialized; the success path reached `FreeVec()` on indeterminate
values. Initializing both to `NULL` fixed the defect, and the final run above
uses that correction. Not yet validated: deterministic driver backpressure
and `OutputReady`, every UMP MT class, per-reader overflow isolation, loaded
DEVS:Midi reference-module scanning, or actual Network MIDI 2.0/Bluetooth UMP
transport integration.

## What this proves

- The initial wire/core code is portable C and has no OS dependencies.
- The known Discovery message survives the repository's existing SysEx7 UMP
  packet boundaries without alteration.
- All MIDI-CI 1.2 Management message bodies have a no-allocation C codec;
  decoded variable data is an explicitly borrowed view of the input frame.
- The initial Management Responder and request table are bounded and
  deterministic.
- Two engine instances that start with the same candidate MUID can detect the
  collision and diverge when the coordinator supplies fresh entropy.

## What this does not prove

- The protocol codec has not run inside AROS; only the coordinator ownership
  probe has.
- The MIDI-CI codec still has not run through `camd.library` 43; the separate
  CAMD task-death runtime test exercises endpoint lifecycle, not MIDI-CI bytes.
- No coordinator service or public IPC ABI is implemented. The native probe
  validates logical lease ownership and natural task exit. Forced `RemTask`
  and CAMD-handle cleanup are now covered by the separate CAMD regression, but
  coordinator restart and MIDI-CI rediscovery are not.
- The Initiator peer/discovery-round state machine, Profiles, Property
  Exchange and Process Inquiry are not implemented in the C engine.
- There is no MIDI-CI 1.2.1 conformance claim. Management bytes were checked
  against the pinned `midici` conformance fixture, but no independently
  maintained implementation was executed as a differential oracle.
- No MIDI-CI/Property Exchange Workbench or hardware interoperability run has
  occurred.
- No native UMP CAMD driver path exists in the audited PR, so the host UMP
  round trip validates codecs and boundaries, not physical transport.

## Required next validation layers

### Host codec/state suite

- every Management, Profile, PE and Process message, valid and malformed;
- fuzz every decoder with ASan/UBSan and bounded allocation/failure injection;
- PE chunk duplicate, gap, reorder, overflow, timeout and cancellation;
- MUID collision during active operations; multiple peer MUIDs; request-ID
  wrap and capacity exhaustion;
- byte differential against a pinned independent implementation.

### AROS CAMD 43 runtime suite

- MIDI 1.0 complete SysEx Discovery through a loopback endpoint;
- UMP SysEx7 through a native resident-driver loopback after CI05;
- two input sessions, queue loss isolation and ordering;
- offline/reconnect/retire/driver removal during partial SysEx and active
  transaction;
- endpoint watch overflow and resnapshot;
- coordinator restart and dead-client lease recovery integrated with the
  portable engine (CAMD forced client termination is covered separately);
- memory ownership under MungWall/available AROS diagnostics;
- CAMD 41/42 compatibility suite unchanged.

### Interoperability suite

Record the peer/tool name, exact version, transport, configuration, raw
transcript and expected/actual outcome. Minimum scenarios:

1. AROS Initiator discovers an independent Responder.
2. Independent Initiator discovers the AROS Responder.
3. MUID collision and Invalidate MUID recovery.
4. Profile inquiry and one state transition.
5. PE capabilities, multi-chunk Get, Set error and subscription timeout.
6. Process capabilities and a bounded MIDI Message Report.
7. MIDI 1.0 byte-stream and UMP SysEx7 transports.

Failures and tool limitations remain in this file; they are not summarized as
passes.
