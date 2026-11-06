# MIDI-CI validation record

Last run: 2026-11-06. Results distinguish host execution from AROS runtime
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

- No test in this record has run inside AROS.
- No `camd.library` 43 binary was exercised by the new MIDI-CI test.
- The coordinator/service, IPC behavior and abnormal process cleanup are not
  implemented.
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
- forced client task termination and coordinator restart;
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
