# BLE MIDI improvement review, 2026-10-17

Published #1507 remains at `658d7413037f07d44fd81e022644bfb090483a90`.
Local `AROS-btmidi` is clean at `4c2c7f778a`, with two unpublished codec
commits (`110fbcb6a1`, `4c2c7f778a`). No source or PR was changed in this
review. The local codec suite passes normally and with ASan/UBSan.

## Correct the reserved-bit finding

The [MMA/AMEI RP-052 document](https://www.hangar42.nl/wp-content/uploads/2017/10/BLE-MIDI-spec.pdf),
section 7, specifies transmit header bit 6 as zero, reserved for future
use. It does not explicitly require a receiver to accept that bit set.
The local test demanding acceptance is a tolerance policy, not proof
that the published decoder violates BLE-MIDI. The previous audit's
classification was too strong. Review the local commits separately:
reserved-bit tolerance, comments/simplification, and F9/FD filtering are
distinct changes. Do not publish them as one established normative fix.

## First reliability target: TX failure policy

In both the published and local class, `send_to_device()` in
`btmidi_central.c` drains the CAMD ring while ignoring returns from
`blemidi_writer_put()` and `blemidi_writer_flush()`. Its packet callback
returns failure from `btDoChannel()` to the writer; the caller then
disregards that failure. The writer explicitly documents that a refused
packet is lost, and clears its packet length after failure.

The peripheral pump also ignores writer errors, although its callback
does increment `ms_Errors` on `btSetServiceValue()` failure. This is not
equivalent to per-central delivery confirmation.

Improvement: fault-inject actual transport refusal/error outcomes, add
central TX failure accounting, and define bounded pending-output handling
for failures known to occur before acceptance. Preserve order, avoid
replaying data whose acceptance is uncertain, and define disconnect/SysEx
reset and queue-full behavior. Test Note Off, a fragmented SysEx, saturated
queues and reconnect. No radio loss or hanging-note reproduction was
performed here; the confirmed observation is the ignored error path.

## Native validation and timing

The CAMD driver form promised in #1507 still needs a radio test on a Pi
with a phone, and the central role needs a real BLE MIDI peripheral.
Host codec tests cannot establish ATT MTU negotiation, subscriptions,
pairing, queue behavior or disconnect safety. The old C0 driver-alignment
prerequisite is now upstream through merged #1585.

Measure connection interval, negotiated payload and message spacing under
load before changing timing policy. RP-052 sections 4 and 6 require short
connection intervals and MTU negotiation. The current central takes a
negotiated payload but does not itself request a shorter link; the
peripheral registers a 15 ms interval request. Parsed input timestamps
are discarded before CAMD byte delivery. Timed delivery and stable port
identity can follow separately; they should not require the new CAMD
endpoint API to land with this PR.

Priority: transport failure handling and real-radio/lifecycle evidence,
then codec refinements and measured latency improvements. BLE-MIDI is
governed by RP-052 and ATT/GATT, rather than the RTP-MIDI RFC.
