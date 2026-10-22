# Upstream source ownership

Once a component has merged into AROS, use its upstream implementation.
Keep local implementation code only for identified work intended for a
subsequent upstream proposal, with its purpose and blockers recorded.
Tests must exercise that source rather than an independent copy. A missing
UI enhancement is not a reason to keep an obsolete runtime implementation.

## BLE retirement, 2026-10-22 — L11 complete

Removed the old CAMD-client `ports/aros/btmidi` class, the manual
`ports/aros/ble_bridge.c` program, and the parallel codec and advertising
helpers under `src/` and `include/blemidi/`. Their build targets and tests
of the obsolete implementation are gone. The package now depends on
AROS's `kernel-bluetooth-classes-btmidi` target from merged #1507.

Preferences uses the native numeric driver ports and no longer reads the
old private configuration chunk or derives nonexistent clusters from a
device's name. Peripheral route migration is retained. The public query
needed to associate a bound device with its port remains L10; the native
class window provides that mapping meanwhile.

`make test` and fuzz compile the canonical class codec, including active
#1735 when that checkout is selected. Shared `camd_bridge.c` remains because
other application programs use it. It is not a second BLE implementation.

See [BLE MIDI](ble-midi.md) for installation, test source selection and
validation limits. Existing build images may still contain old installed
artifacts: rebuild the native class in a current AROS tree rather than
copying an old contrib class into the image.

## Retained work and remaining migration

| Area | Why code remains here | What remains |
|---|---|---|
| Network service and protocol libraries | Active proposals #1679, #1693, #1742, #1743 and the component split #1788 | Coordinate consumers and exports with those proposal branches; do not let local and upstream fixes diverge. |
| Full mDNS port | Contains additional query/tool/Envoy work and unsubmitted M4 work; the local full port is not identical to the narrower queued API proposal #1695 | Migrate consumers and package dependencies to the accepted API and separate the remaining upstream proposals. The local full library is still built; this BLE cleanup does not claim that duplication is resolved. |
| CAMD extension prototypes and patches | Identified future endpoint/metadata work beyond the merged fixes | Follow the blockers in the CAMD backlog; remove superseded material as each change merges and consumers migrate. |

The current proposal dependencies are recorded in
[the upstream status register](upstream-status-2026-10-22.md). This cleanup
changes the companion repository; it does not rewrite any open AROS PR.
