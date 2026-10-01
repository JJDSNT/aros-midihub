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
