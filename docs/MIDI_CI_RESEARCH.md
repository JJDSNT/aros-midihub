# MIDI-CI research baseline

Status: evidence baseline and first implementation slice, 2026-11-06.

This document distinguishes verified source behavior from design inference. It
does not treat a README claim or an API declaration as working state-machine
behavior.

## Reproducible baseline

- Midihub: local `main` at `b65ba21`; the worktree already contained unrelated
  mDNS changes, which this work does not modify.
- CAMD 42 dependency: AROS commit `a0ca679f44` in the history of PR 2101.
- CAMD 43 draft: AROS PR
  [#2101](https://github.com/aros-development-team/AROS/pull/2101), head
  `4c1510f1d08fad7efb1081b9d005734ef2557c28`, 28 commits. The PR calls its
  names, records and values provisional.
- cmidi2: `49d4fb19a8e79b287e472b0a6d3a0e083fe77c53`.
- ni-midi2: `26e008c10b18e2397d7b65e184b71da3327e78d1`.
- Rust `midici` crate: crates.io `0.0.1`, whose manifest identifies
  `https://github.com/hxphsts/midici`.
- The similarly named `sgm-audio/midici` at
  `6bb28b67624f64e2dedb7163043702b6bedbef8d` is responder-oriented and is not
  the Initiator/Responder crate described by the mission. It was inspected to
  avoid conflating the projects.

The exact upstream source was fetched as Git objects or temporary source
trees; no third-party source was copied into this repository.

## Existing Midihub behavior

Before this slice the repository had no MIDI-CI codec or state engine. It did
have a well-tested portable UMP layer:

- `src/ump.c` packetizes MIDI 1.0 SysEx as UMP SysEx7 and reconstructs it with
  bounded caller storage;
- it implements UMP Endpoint/Function Block Stream replies for one static
  Function Block, but that record explicitly advertises no MIDI-CI and no
  SysEx8;
- `ports/aros/midihub/transport_midi2.c` currently declares MIDI 1.0 protocol,
  translates a peer's UMP into MIDI 1.0 bytes for the CAMD driver and converts
  outgoing CAMD bytes back to UMP. It is not a native CAMD 43 UMP driver path;
- the Network MIDI 2.0 session code transports arbitrary complete UMP command
  payloads, so it need not learn MIDI-CI policy once the CAMD endpoint path can
  remain native.

The new engine reuses the canonical UMP byte/SysEx7 conversion in tests. It
does not create a second UMP runtime.

## Normative scope

The official [MIDI 2.0 Core collection](https://midi.org/midi-2-0-core-specification-collection)
currently lists:

| Document | Version used here | Role |
| --- | --- | --- |
| M2-101-UM MIDI-CI | 1.2.1 | Discovery, MUIDs, Profiles, PE envelope, Process Inquiry |
| M2-102-U Profiles common rules | 1.1 | Profile identity and common behavior |
| M2-103-UM Property Exchange common rules | 1.2 | requests, chunks, encoding, subscriptions and errors |
| M2-104-UM UMP and MIDI 2.0 Protocol | 1.1.2 | UMP framing, SysEx7, endpoint/Function Block discovery and Stream Configuration |

The official [MIDI-CI page](https://midi.org/midi-ci-specification) identifies
Discovery, Profile Configuration, Property Exchange and Process Inquiry as
the MIDI-CI areas and requires bidirectional communication. The Property
Exchange page separately describes the Initiator/Responder relationship and
payload rules.

### Two discovery planes, not one negotiation mechanism

MIDI-CI Discovery is Universal SysEx (`Sub-ID#1 0x0D`), establishes MUIDs and
advertises MIDI-CI categories. UMP Endpoint Discovery and Function Block
Discovery are Stream messages (`MT 0xF`) describing transport topology,
protocol capability and Group layout. UMP Stream Configuration selects MIDI
1.0 or MIDI 2.0 protocol on a UMP endpoint.

MIDI-CI 1.1 Protocol Negotiation is deprecated. It remains a decode-only
compatibility concern; it is not a requirement for the AROS architecture.
This agrees with the current
[Windows MIDI Services implementation note](https://microsoft.github.io/MIDI/kb/midi2-implementation-details/),
which uses UMP Endpoint Discovery/Stream Configuration and does not implement
the deprecated MIDI-CI equivalents.

## External implementation comparison

| Implementation | Verified source behavior | Verified gaps / constraints | AROS lesson |
| --- | --- | --- | --- |
| [cmidi2](https://github.com/atsushieno/cmidi2) | MIT, single C header, static-inline UMP helpers and constructors/accessors for Discovery, Profiles, PE and Process Inquiry; unit tests exercise representative messages. | README explicitly gives no API/ABI stability. Source contains message construction/parsing helpers, not peer ownership, collision recovery, request tables, clocks, retries or session lifecycle. Some deprecated Protocol Negotiation helpers remain. | Useful reference and possible source of individual reviewed codecs, but not a MIDI-CI runtime. Its public instability argues against exposing it directly as `midici.library`. |
| [ni-midi2](https://github.com/midi2-dev/ni-midi2) | MIT, C++17, typed UMP/SysEx/MIDI-CI factories and validating views; tests cover management, Profiles, PE, Process Inquiry and SysEx collectors. | No Initiator/Responder engine, MUID owner, deadline scheduler or transaction/session manager was found. C++17 STL and dynamic containers are a poor fit for an AROS C shared-library ABI. | Strong type/test patterns and malformed-view validation are reusable design evidence; wholesale porting is not justified. |
| [midici 0.0.1](https://crates.io/crates/midici/0.0.1) | MIT OR Apache-2.0; sans-I/O; no-alloc Responder and allocating Initiator; MUID collision handling; request IDs; 3 s timeouts; Profile, Process and PE state; chunk planning/reassembly; Mcoded7; SysEx7 UMP adapter; conformance fixtures. | README says M2-103 v1.1 and M2-104 v1.1.1, and explicitly says MIDI-CI 1.2.1 was not verified. Initiator requires allocation. Rust 1.94 is not an appropriate dependency for the initial AROS C library. Its fixtures are derived from specification tables, not captured interoperability evidence. | Best state-machine model: caller-owned clock and I/O, explicit events, bounded Responder storage, transaction correlation and streaming PE. Reimplement the necessary patterns in portable C rather than porting Rust wholesale. |
| [sgm-audio/midici](https://github.com/sgm-audio/midici) | MIT, layered sans-I/O management/PE responder, bounded subscriptions and transport adapters; repository records 108 host tests at its inspected revision. | Primarily Responder-side; its own README says third-party/hardware interoperability is outstanding and ALSA was not built in the recorded run. | Reinforces separation between engine, PE resources and transport adapters; not a complete Initiator reference. |
| MIDI-CI / Property Exchange Workbench | The MIDI Association overview identifies Yamaha's Property Exchange Workbench as a prototyping/testing tool. | No publicly reproducible download or source revision was found in the inspected official public material. No AROS interoperability run has occurred. | Treat as an external validation target, not as source code or current evidence. Record version and captured transcripts when access is available. |

### Coverage conclusion

No inspected C implementation supplies the complete ownership and lifecycle
layer needed by AROS. cmidi2 is portable codec material; ni-midi2 is a typed
C++ codec/view library. The Rust crate demonstrates the missing state model,
but its specification versions and toolchain rule out a wholesale import.
The selected strategy is a small portable C engine with caller-owned storage,
clock, entropy and I/O, expanded incrementally against independent vectors.

## OS architecture lessons

- Linux documents UMP transport/topology in the kernel and MIDI-CI in user
  space over ordinary SysEx in its
  [MIDI 2.0 design](https://origin.kernel.org/doc/html/latest/sound/designs/midi-2.0.html).
  ALSA exposes Endpoint and Block records while keeping policy outside the
  transport.
- Linux's USB MIDI 2.0 gadget interface exposes `midi_ci_version` and
  `sysex8_streams` per Function Block. This is direct evidence that those are
  topology metadata, not MIDI-CI session state.
- Apple exposes UMP endpoint/Function Block objects and separate MIDI-CI
  device management in [CoreMIDI](https://developer.apple.com/documentation/coremidi).
  Its UMP Function Block constructor carries the topology fields rather than
  embedding MIDI-CI transactions in the transport API.
- CAMD should therefore carry complete MIDI messages, stable endpoint identity
  and relevant topology. MUIDs, peer sessions, PE transactions and Profiles
  stay above CAMD.

## Implemented research outcome

The first portable C slice is in `include/midici/midici.h` and
`src/midici.c`. It implements:

- strict MIDI-CI envelope and all Management body codecs: Discovery/Reply,
  Endpoint Inquiry/Reply, ACK, MIDI-CI 1.1/1.2 NAK and Invalidate MUID;
- assignable 28-bit MUID generation and collision recovery;
- an initial no-allocation Management Responder with Discovery/Endpoint
  replies, structured NAKs and both used/unused collision branches;
- fixed-capacity request correlation, conflict rejection, timeout polling and
  disconnect cancellation;
- no OS calls, hidden allocation, threads or transport ownership.

The tests also pass a byte-exact Discovery through the existing Midihub
MIDI-byte-to-UMP SysEx7 packetizer and back through its bounded reassembler.
The byte vectors are cross-checked against the pinned `midici` 0.0.1
spec-derived Management fixture; that is source comparison, not independent
interoperability evidence. This is a foundation, not a claim of complete
MIDI-CI 1.2.1 support.
