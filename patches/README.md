# AROS BLE MIDI remaining patch

Upstream AROS commits `8ff80b6627` and `37313d8aa0` now provide the generic
GATT server, local service records, notifications, legacy LE advertising,
128-bit UUID discovery, and the `btgatt.class` Preferences UI. The former
three-patch BLE foundation in this repository has therefore been retired.

`aros-ble-midi-upstream-gaps.patch` is based on upstream commit
`37313d8aa0` (October 3, 2026) and now contains only the BLE MIDI gaps that
remain:

- encode and send ATT Write Command (`0x52`) for Write Without Response;
- route `BTPR_GATTWRITENORSP` and write-only endpoints through that operation;
- expose the negotiated ATT payload size instead of a fixed 20-byte value;
- include enabled 128-bit GATT service UUIDs in legacy advertising, which is
  required for iOS to discover the BLE MIDI service;
- attach an immutable value snapshot to every service-write event, so a later
  ATT packet cannot overwrite MIDI data before the class consumes the event,
  and name the device that wrote it (`BENA_Device`), so a class can keep
  per-connection state;
- queue outgoing notification snapshots until each active radio consumes
  them, instead of coalescing a MIDI burst into the latest value. The queue is
  bounded at 256 snapshots; on sustained producer overrun it drops the oldest;
- accept peripheral-role SMP pairing, including Legacy Just Works and Secure
  Connections Just Works, Numeric Comparison, and Passkey Entry; answer
  controller LTK requests, distribute bonding keys, and reuse stored keys on
  reconnection;
- add the L2CAP LE Connection Parameter Update procedure. As the central the
  stack grants a peripheral's request through HCI LE Connection Update; as
  the peripheral it asks for the interval set with the new
  `BSA_LEConnInterval` stack attribute, within the limits iOS accepts.
  `btmidi.class` sets 15 ms, as the BLE MIDI specification asks;
- answer the LE Remote Connection Parameter Request event (subevent 0x06)
  that a controller raises when the peer starts the link-layer Connection
  Parameters Request procedure: valid parameters are accepted, others are
  refused. Before, the event was ignored and the procedure could time out
  with the link; on a Pi 3 an iPhone's connections dropped with "connection
  timeout". The Connection Update Complete event (0x03) is now logged with
  the interval in effect;
- report the largest notification payload every connected LE device receives
  whole (`BSA_LENotifyPayload`), so BLE MIDI can size packets to the
  negotiated ATT MTU;
- add `BGDP_STREAM` for characteristics whose value is a stream of events:
  a read returns no data, as the BLE MIDI specification requires.

It also fixes two generic SMP issues that affect the central role as well:

- the IO capability table selected Just Works instead of Passkey Entry when a
  KeyboardDisplay device met a DisplayOnly or DisplayYesNo device;
- an initiator rejected a responder's Secure Connections commitment that
  arrived before its own controller reported the DHKey. It now holds the
  commitment until the DHKey is ready.

Phase 3 key distribution follows the Core specification in both roles: the
responder sends its keys first, and the initiator answers once it has
received them.

The modified AROS files and their diff context are covered by the AROS Public
License 1.1; see [AROS-LICENSE](AROS-LICENSE). MIDIHub's own source remains
under its existing license. The patch advances `bluetooth.library` to 45.18
because it adds event data tags used by `btmidi.class`.

Apply it to the updated `~/AROS` checkout, without changing Bellatrix:

```sh
git -C ~/AROS apply --check "$PWD/patches/aros-ble-midi-upstream-gaps.patch"
git -C ~/AROS apply "$PWD/patches/aros-ble-midi-upstream-gaps.patch"
make -C ~/AROS/rom/bluetooth/stack -f Makefile.host test
```

The patch applies cleanly to the stated commit. The sanitizer-enabled host
suite passes `2530/2530` checks, including new ATT, GATT, and SMP responder
tests. The SMP tests also run an initiator against a responder, check the
full IO capability table, and check the Phase 3 order. The changed `bluetooth.library` sources and `btmidi.class` also pass an
m68k AROS syntax build.

`btmidi.class` uses the upstream service-record API. It registers the BLE MIDI
service with `btAddServiceRecord()`, receives incoming values through
`BEHMB_SERVICEWRITE`, and publishes outgoing packets with
`btSetServiceValue()`. `btgatt.class` owns radio advertising and the list of
enabled services in Bluetooth Preferences.

Peripheral-role pairing falls back to Legacy Just Works by default. With the
existing `btlesc` boot argument, capable controllers and peers negotiate Secure
Connections, including Numeric Comparison through the standard Bluetooth
pairing popup and Passkey Entry. OOB pairing remains unsupported.

## Raspberry Pi on-board Bluetooth

Upstream AROS has the Raspberry Pi 3's on-board Bluetooth transport
(`pl011bt.resource`, `h4bthci.device`, `brcmbt.fwl`). The transport went in
before the stack had its firmware-loader mechanism (loaders in
`DEVS:Bluetooth/FWLoaders/`, bound by `BTStackLoader`), so there was nothing
to load a patchram with yet. Now that the mechanism exists, the patchram
download completes the radio's support; without it the radio gets no real
address. It is independent of BLE
MIDI, meant for upstream, and applies on its own to upstream commit
`37313d8aa0`.

`aros-raspi-bt-firmware.patch` adds `distfiles-raspi-aarch64-bt-fw` to
`arch/aarch64-raspi/boot/mmakefile.src`, beside the WiFi firmware download,
and makes `distfiles-raspi-aarch64le` depend on it. It fetches the patchram
`brcmbt.fwl` loads from `DEVS:Firmware/brcm/`: `BCM43430A1.hcd` (Pi 3, Zero W)
and `BCM4345C0.hcd` (Pi 3B+), from the Raspberry Pi `bluez-firmware`
repository. Without it a BCM43430A1 runs from ROM and reports the
placeholder address `AA:AA:AA:AA:AA:AA`.

The radio itself is registered once in Bluetooth Preferences (hardware
page: `DEVS:Bluetooth/h4bthci.device`, unit 0, then Save). `BTStackLoader`
brings up the saved radios on every boot, after binding the firmware
loaders, so the patchram is applied during the radio's own bring-up. A line
in `S:Startup-Sequence` would do the same for every target to serve one, so
none is added.

```sh
git -C ~/AROS apply "$PWD/patches/aros-raspi-bt-firmware.patch"
```

### Debug console on the mini-UART

`aros-raspi-console-miniuart.patch` is needed for the on-board radio to work
on `raspi-aarch64`. The bootstrap (`serialdebug.c`) and the kernel
(`kernel_startup.c`, `bcm27xx_ser_putc`) printed their serial log through
the PL011 and muxed GPIO 14/15 to it. On a Pi 3, 3B+ and Zero 2 W that
PL011 belongs to the Bluetooth radio, which `pl011bt.resource` puts on GPIO
30-33. Two pins then fed the PL011's receive line, and every kernel `bug()`
was sent to the radio. On the first test the radio never answered
`HCI_Reset` (`init step 0 -> timeout`).

The patch follows the firmware's routing, as Linux does. When the device
tree's `serial0` alias names the mini-UART (`/soc/serial@7e215040`), the
bootstrap prints through the AUX mini-UART the firmware has set up
(`enable_uart=1`, 115200, GPIO 14/15). It leaves the PL011 and its pins
alone, and passes the mini-UART's address in `KRN_DebugUartBase`, which the
kernel recognises for its early and later output. The serial cable stays on
the same pins at the same speed. Waits on the mini-UART are bounded, so a
disabled one cannot hang the boot. A Pi 2, or a Pi 3 with
`dtoverlay=disable-bt`, keeps the PL011 console.

```sh
git -C ~/AROS apply "$PWD/patches/aros-raspi-console-miniuart.patch"
```

## llvmpipe link order

`aros-llvmpipe-link.patch` is a build fix found while building
`raspi-aarch64` with `--with-toolchain=llvm`. It is unrelated to MIDI and
meant for upstream.

`workbench/hidds/llvmpipe` links the target-side LLVM archives as `-lLLVM…`
names. `TARGET_CXX_LDFLAGS` puts `-L$(CROSSTOOLSDIR)/lib` first, and when
the AROS toolchain itself was built with LLVM that directory also holds the
host's `libLLVM*.a`. The linker then picks those, and the link fails with
"is incompatible with aarch64elf". The patch passes the archives from
`$(AROS_DEVELOPER)/lib` by path, which the `mmakefile` already globs, so no
search order is involved. With it, `llvmpipe.hidd` links.

```sh
git -C ~/AROS apply "$PWD/patches/aros-llvmpipe-link.patch"
```

## USB MIDI CAMD fix

`aros-usb-midi-camd.patch` still applies cleanly to upstream commit
`37313d8aa0`. It fixes
the Poseidon USB MIDI class's CAMD receive buffer format, transmit ring
handling, and failed port-open behavior. The modified AROS source is covered
by [AROS-LICENSE](AROS-LICENSE).

```sh
git -C ~/AROS apply --check "$PWD/patches/aros-usb-midi-camd.patch"
git -C ~/AROS apply "$PWD/patches/aros-usb-midi-camd.patch"
```

The patch passed `git apply --check`. Runtime status and the hardware test
procedure are in [USB MIDI validation](../docs/usb-midi.md).

## CAMD names on 64-bit targets

`aros-camd-names-64bit.patch` fixes `camd.library` on AArch64 and x86_64.
Its internal `mysprintf()` took its arguments from `&fmt+1`, which only
works where variadic arguments sit on the stack one after the other (m68k,
i386). Elsewhere they are passed in registers, so every cluster name, built
by `NewCluster()` with `mysprintf(..., "%s", name)`, came out as garbage.
The same went for the `DEVS:Midi` driver paths (`devs:Midi/0▒▒` in the
boot log). On a Raspberry Pi 3 running `raspi-aarch64`, MIDIHub.prefs
showed the endpoints with garbled names, and nothing could reach `MIDIHub
BLE In`/`Out` by name. The patch formats through `VNewRawDoFmt()` with a
`va_list`, which is correct on every architecture and reads `%ld` as the
`int` the callers pass.

```sh
git -C ~/AROS apply "$PWD/patches/aros-camd-names-64bit.patch"
```

## CAMD driver scan of arena-loaded modules

`aros-camd-arena-segments.patch` fixes a crash in `camd.library` when it
loads a `DEVS:Midi` driver. To find the driver's `MidiDeviceData`, CAMD
walks the driver's segments and reads each hunk's size from the longword
before it. The ELF loader now loads a module into one arena
(`ELF_MODULE_ARENA`): every section's hunk then has a stored size of 0, and
a container hunk at the end of the list has the size of the whole arena.
CAMD subtracted the header from 0, the size wrapped to about 4 GB, and the
scan ran to the end of memory. On a Raspberry Pi 3 this happened in the
first task that opened `camd.library`, as a bus fault at the top of RAM.
`lddemon` reads the same size but finds its `Resident` in the first bytes.
The patch skips hunks no larger than their header, in the scan and in
`isPointerInSeglist()`; the container hunk still covers their contents. It
is not specific to 64-bit targets.

```sh
git -C ~/AROS apply "$PWD/patches/aros-camd-arena-segments.patch"
```

## CAMD driver rescan

`aros-camd-rescan.patch` still applies cleanly to upstream commit
`37313d8aa0`. It makes
the existing `RethinkCAMD()` entry rescan `DEVS:Midi` and load new driver files
once, even when CAMD is already open. It serializes scans separately from the
CAMD list lock so `LoadSeg` and driver initialization do not run while that
list is locked. It does not unload a driver whose file is removed.

```sh
git -C ~/AROS apply --check "$PWD/patches/aros-camd-rescan.patch"
git -C ~/AROS apply "$PWD/patches/aros-camd-rescan.patch"
```

The patch passed `git apply --check`, and its changed C files compiled with
the AROS x64 GCC. A full CAMD build and native rescan test are pending. The
driver lifecycle and the remaining removal issue are documented in
[the CAMD integration design](../docs/camd-integration.md). The modified AROS
source is covered by [AROS-LICENSE](AROS-LICENSE).
