Title: mdnsd: delay shared type replies and suppress repeats

## Scope

This is one internal responder change. It does not alter the Bonami public
API, ABI, message format or tool behavior, and it has no unmerged dependency.
The prepared branch is `review/mdns-shared-delay`, commit `479d1c1646`, based
directly on upstream master `adff8323a1`.

RFC 6762 section 6 requires shared multicast responses to be delayed by a
random 20--120 ms and says that the same record must not normally be
multicast again on the same interface within one second.

## Existing behavior and reproduced gap

Ordinary service-type browse questions already schedule shared PTR replies
through `rs_AnswerTick` and `answers_flush()`. Service-type enumeration at
`_services._dns-sd._udp.local`, however, calls `types_send()` immediately.
The responder also keeps only a coarse wall-clock value for a registration,
not an exact multicast tick per record and interface.

Against unmodified `adff8323a1`, three focused responder cases fail:

- `shared-repeat`: an ordinary service PTR is multicast again inside one
  second;
- `types-delay`: a service-type enumeration PTR is sent immediately;
- `types-repeat`: that type PTR is also sent again inside one second.

The controls pass on the same revision: ordinary shared responses are already
delayed, legacy unicast type replies remain immediate, and activity on one
interface does not suppress another interface.

## Minimum change

- Reuse the existing random answer scheduler for multicast service-type
  enumeration.
- Record exact ordinary and service-type PTR send ticks per interface.
- Omit a shared PTR that was multicast on that interface less than one second
  ago.
- Preserve immediate legacy unicast replies and their query ID/question.
- Clear the new timing state when registrations or interfaces restart.

All registrations of the same service type retain its last type-PTR tick, so
removing whichever registration was visited first cannot lose the suppression
state.

## Validation

- Baseline: 31/34 responder cases pass; only the three cases above fail.
- Candidate: responder 34/34, discovery 38/38 and query 12/12 under
  AddressSanitizer and UndefinedBehaviorSanitizer.
- `cppcheck --enable=warning,performance,portability` on the changed daemon
  sources.
- Complete mdnsd compile and link for hosted linux-x86_64 and amiga-m68k,
  with direct compilation of the changed sources using warnings as errors.
- Companion repository `make test` passes.

The expanded Linux-hosted packet fixture reproduces the old immediate and
repeated multicast replies. A positive packet capture of this candidate has
not yet been claimed: the available complete hosted image predates the
candidate's build/API context, while the current partial build image rejects
its Bonami fixture registrations. Run the candidate in one version-consistent
hosted image before publication, or state this limitation explicitly.

Not tested on physical m68k or big-endian hardware.
