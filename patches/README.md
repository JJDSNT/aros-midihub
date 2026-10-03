# AROS BLE MIDI GATT prerequisite patch

`aros-ble-midi-gatt.patch` applies to the AROS source tree at upstream commit
`13c7f81274825dd9bca047fc7caebfa21576a163` (October 1, 2026). It is a
source patch for AROS, not a MIDIHub runtime component. The modified AROS
files and their diff context are covered by the AROS Public License 1.1;
see [AROS-LICENSE](AROS-LICENSE). The rest of MIDIHub retains its own license.

The patch addresses the GATT client prerequisites for using a BLE MIDI
peripheral from AROS:

- Preserve 128-bit service and characteristic UUIDs during GATT discovery.
  The existing 16-bit `uuid16` fields remain available to current callers.
- Convert discovered UUIDs into the big-endian representation used by
  `bluetooth.library` service and endpoint attributes, and match retained
  objects by their complete UUID.
- Add ATT Write Command (`0x52`) and route the existing
  `BTPR_GATTWRITENORSP` request through it. Characteristics that only support
  Write Without Response use that command on endpoint writes.
- Report a characteristic payload limit based on the negotiated ATT MTU.

Apply from this repository without changing the Bellatrix checkout:

```sh
git -C ~/AROS apply --check "$PWD/patches/aros-ble-midi-gatt.patch"
git -C ~/AROS apply "$PWD/patches/aros-ble-midi-gatt.patch"
make -C ~/AROS/rom/bluetooth/stack -f Makefile.host test
```

Run the commands with the current directory set to the MIDIHub repository.
The patch was checked with `git apply --check` against the stated AROS commit.
The patched portable Bluetooth stack passed its host test runner with
AddressSanitizer and UndefinedBehaviorSanitizer (`2124/2124` checks). An AROS
build and a real BLE MIDI device exchange still need to be performed after
the patch is applied. This patch does not add GATT Server support or the
MIDIHub BLE MIDI transport itself.

## BLE peripheral foundation

`aros-ble-midi-peripheral.patch` applies after
`aros-ble-midi-gatt.patch`. It adds generic facilities to the portable AROS
Bluetooth core:

- a fixed-allocation GATT server and attribute database;
- primary service and characteristic discovery with 16-bit or 128-bit UUIDs;
- MTU exchange, reads, long reads, Write Request, and Write Command;
- Client Characteristic Configuration handling and notifications;
- HCI command encoders for legacy advertising parameters, advertising data,
  scan response data, and advertising enable.

```sh
git -C ~/AROS apply --check "$PWD/patches/aros-ble-midi-gatt.patch"
git -C ~/AROS apply "$PWD/patches/aros-ble-midi-gatt.patch"
git -C ~/AROS apply --check "$PWD/patches/aros-ble-midi-peripheral.patch"
git -C ~/AROS apply "$PWD/patches/aros-ble-midi-peripheral.patch"
make -C ~/AROS/rom/bluetooth/stack -f Makefile.host test
```

With both patches applied, the sanitizer-enabled host suite passes
`2163/2163` checks. The new tests simulate BLE MIDI service discovery, a MIDI
Write Command, CCCD subscription, and a server notification.

## Local GATT service API

`aros-ble-gatt-service-api.patch` applies after both BLE patches above. It
connects the portable server to `bluetooth.library` and provides the generic
peripheral facilities required by a Bluetooth profile class:

- register a local 128-bit GATT service and its characteristics;
- advertise the service through one selected LE radio;
- accept incoming LE central connections and create one GATT server per link;
- deliver characteristic writes to an Exec message port owned by the class;
- send notifications to a subscribed central;
- keep local services, characteristics, devices, and connections alive while
  asynchronous messages refer to them.

The advertising setup is a serialized HCI command sequence. The library only
reports success after the controller accepts the final command, and propagates
controller errors to the caller. While legacy connectable advertising owns the
LE radio, LE discovery and outgoing LE connections are rejected; classic
inquiry and BR/EDR connections remain available.

```sh
git -C ~/AROS apply --check "$PWD/patches/aros-ble-midi-gatt.patch"
git -C ~/AROS apply "$PWD/patches/aros-ble-midi-gatt.patch"
git -C ~/AROS apply --check "$PWD/patches/aros-ble-midi-peripheral.patch"
git -C ~/AROS apply "$PWD/patches/aros-ble-midi-peripheral.patch"
git -C ~/AROS apply --check "$PWD/patches/aros-ble-gatt-service-api.patch"
git -C ~/AROS apply "$PWD/patches/aros-ble-gatt-service-api.patch"
```

A class receives `struct BtGATTWriteMsg` objects on the port supplied through
`BGCRA_WritePort`. It must release every message with
`btFreeGATTWriteMsg()` after consuming the inline payload. These messages are
not Exec reply messages and must not be passed to `ReplyMsg()`.

The first API intentionally uses legacy advertising and permits one local
service per radio. Peripheral-role SMP pairing is not implemented yet, so a
local characteristic must currently work without authenticated or encrypted
permissions. These limitations are recorded for later Bluetooth stack work;
they do not prevent the standard unencrypted BLE MIDI service.

## USB MIDI CAMD fix

`aros-usb-midi-camd.patch` applies to the same upstream AROS commit. It fixes
the Poseidon USB MIDI class's CAMD receive buffer format, transmit ring
handling, and failed port-open behavior. The modified AROS source is covered
by [AROS-LICENSE](AROS-LICENSE).

```sh
git -C ~/AROS apply --check "$PWD/patches/aros-usb-midi-camd.patch"
git -C ~/AROS apply "$PWD/patches/aros-usb-midi-camd.patch"
```

The patch passed `git apply --check`. Runtime status and the hardware test
procedure are in [USB MIDI validation](../docs/usb-midi.md).

## CAMD driver rescan

`aros-camd-rescan.patch` applies to the same upstream AROS checkout. It makes
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
