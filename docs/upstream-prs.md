# Upstream pull requests

Four branches in `~/AROS`, each based on `upstream/master` and meant to be
pushed to `JJDSNT/AROS` and opened against `aros-development-team/AROS`
`master`, as PR #1112 was. Nothing has been pushed yet. The descriptions
below are the PR bodies.

| branch | title | commits |
|---|---|---|
| `bluetooth-le-midi` | bluetooth: BLE MIDI (LE peripheral pairing, connection parameters, btmidi.class) | 7 |
| `camd-64bit-fixes` | camd: cluster names on 64-bit targets, and driver scan of arena-loaded modules | 2 |
| `raspi-bt-console-firmware` | raspi-aarch64: keep the debug console off the Bluetooth radio, fetch its patchram | 2 |
| `llvmpipe-link` | llvmpipe: link the target-side LLVM archives by path | 1 |

The BLE MIDI branch works on its own. On real hardware it needs the CAMD
fixes, and on a Raspberry Pi 3 also the raspi fixes. The llvmpipe fix only
matters when building with an LLVM-built toolchain.

---

## bluetooth: BLE MIDI (LE peripheral pairing, connection parameters, btmidi.class)

This adds MIDI over Bluetooth LE, as the MIDI Association's BLE MIDI 1.0
specification describes it, offered to CAMD applications. The first six
commits complete the LE side of the stack where BLE MIDI needed it. Each
one is useful on its own, and each builds and passes the host suite. The
last commit adds the class.

### The stack

| commit | what it adds |
|---|---|
| write without response, and the negotiated ATT payload | ATT Write Command; `BTPR_GATTWRITENORSP` and write-only endpoints use it; `BEA_MaxPktSize` reports MTU - 3 |
| advertise enabled 128-bit service UUIDs | iOS finds a BLE MIDI peripheral by its 128-bit service UUID in the advertisement |
| hand service writes and notifications over intact | `BEHMB_SERVICEWRITE` carries a copy of the value and the writing device (`BENA_Data`, `BENA_DataLength`, `BENA_Device`); notifications are queued as snapshots instead of coalesced; 45.18 |
| pair as the LE peripheral | SMP responder: legacy Just Works by default, LE Secure Connections with `btlesc` (Just Works, Numeric Comparison, Passkey Entry); LTK requests, bonding, reconnection |
| LE connection parameter updates | the L2CAP procedure in both roles; `BSA_LEConnInterval`; the LE Remote Connection Parameter Request event is answered |
| notification payload and event-stream characteristics | `BSA_LENotifyPayload`; `BGDP_STREAM` (a read returns no data) |

Three SMP bugs found on the way also affect the central role: the IO
capability table chose Just Works where Table 2.8 asks for Passkey Entry
(KeyboardDisplay against DisplayOnly/DisplayYesNo); Phase 3 must start with
the responder's keys; and an initiator rejected a Secure Connections
commitment that arrived before its own DHKey.

The ignored LE Remote Connection Parameter Request event (subevent 0x06) was
the cause of iPhone connections to a Raspberry Pi 3 dropping with
"connection timeout" soon after connecting: the iPhone starts the link-layer
procedure and the controller waits for the host.

### btmidi.class

In `rom/bluetooth/classes/btmidi`, beside `btgatt` and `btbattery`:

- **Peripheral:** registers the BLE MIDI service; a connecting phone or
  computer plays through the CAMD clusters `BLE MIDI In` and `BLE MIDI Out`.
  Each central has its own reassembly state, so several can send at once.
  `btgatt.class` still decides whether the service is offered and whether
  the radios advertise.
- **Central:** binds to the BLE MIDI service of each registered device and
  gives it a CAMD node named after it. Like `btbattery.class`, a binding
  never connects the device itself; it follows connect and disconnect events.
- **Settings window:** renames the peripheral role's ports and shows their
  activity.

### Testing

The host suite goes from 2137 to 2530 checks: ATT and GATT client writes,
L2CAP connection parameter updates, an initiator-against-responder SMP
loopback for every association, the full IO capability table and the Phase 3
order. Each commit builds `bluetooth.library`, and the class builds and links
for `raspi-aarch64`.

On a Raspberry Pi 3 with its on-board radio and an iPhone running MIDI
Wrench, the iPhone connects to AROS and MIDI goes both ways between it and
CAMD. The central
role has not been tried with a BLE MIDI peripheral yet.

---

## camd: cluster names on 64-bit targets, and driver scan of arena-loaded modules

Two bugs that make CAMD unusable on AArch64 and x86_64. The second one also
crashes any target that loads modules into an arena.

**Cluster names.** `mysprintf()` took its variadic arguments from `&fmt+1`,
which only works where they are passed on the stack one after the other
(m68k, i386). On AArch64 and x86_64 every cluster name came out as garbage,
and so did the `DEVS:Midi` driver paths, so no program could find a cluster
by name. It now formats through `VNewRawDoFmt()` with a `va_list`.

**Driver scan.** `LoadDriver()` reads each hunk's size from the longword
before its `BPTR` and scans the hunk for `MidiDeviceData`. With
`ELF_MODULE_ARENA` every section hunk stores a size of 0 and a container
hunk holds the arena's size. Subtracting the header from 0 wrapped the size
and the scan ran off the end of memory. `lddemon` reads the same field but
finds its `Resident` in the first bytes. Hunks no larger than their header
are now skipped; the container still covers them.

Found on a Raspberry Pi 3 running `raspi-aarch64`: first every endpoint had
a garbled name, then, once names were fixed and CAMD really loaded
`DEVS:Midi/debugdriver`, a bus fault at the top of RAM (the dump showed the
`'MDEV'` compare and a size of `0xd0e2c79c`).

---

## raspi-aarch64: keep the debug console off the Bluetooth radio, fetch its patchram

The on-board Bluetooth transport from #1112 needs two more things on
`raspi-aarch64`. The transport went in before the stack had its
firmware-loader mechanism.

**Debug console.** The bootstrap and the kernel printed their serial log
through the PL011 and muxed GPIO 14/15 to it. On a Pi 3, 3B+ and Zero 2 W
that PL011 belongs to the radio, which `pl011bt.resource` puts on GPIO 30-33.
Two pins fed the PL011's receive line and every `bug()` went to the radio:
`h4bthci.device` opened, but the radio never answered `HCI_Reset`. When the
device tree's `serial0` alias is the mini-UART, the bootstrap now prints
through it, leaves the PL011 alone, and passes its address in
`KRN_DebugUartBase`, which the kernel recognises. Linux does the same. The
cable stays on the same pins at the same speed. A Pi 2, or a Pi 3 with
`dtoverlay=disable-bt`, keeps the PL011 console.

**Patchram.** `distfiles-raspi-aarch64-bt-fw` fetches `BCM43430A1.hcd` and
`BCM4345C0.hcd` from the Raspberry Pi `bluez-firmware` repository into
`DEVS:Firmware/brcm/`, beside the WiFi firmware. Without it the radio runs
from ROM with the placeholder address `AA:AA:AA:AA:AA:AA`.

Tested on a Raspberry Pi 3: the radio comes up with its real address once
registered in Bluetooth Preferences, and the serial log still appears on the
header.

---

## llvmpipe: link the target-side LLVM archives by path

`LLVM_LIBS` turned the archives in `$(AROS_DEVELOPER)/lib` into `-lLLVM...`
names, which the linker looks up through every `-L` in order.
`TARGET_CXX_LDFLAGS` puts the crosstools lib directory first, and when the
AROS toolchain itself was built with LLVM that directory also holds the
host's `libLLVM*.a`. The link of `llvmpipe.hidd` then failed with "is
incompatible with aarch64elf". The archives are now passed by path.

Seen building `raspi-aarch64` with `--with-toolchain=llvm`.
