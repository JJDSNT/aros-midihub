# BLE MIDI transport

The optional `MIDIHubBLE` program connects to a registered BLE MIDI
peripheral through AROS `bluetooth.library`. It finds the MIDI service
`03B80E5A-EDE8-4B33-A751-6CE34EC4C700` and I/O characteristic
`7772E5DB-3868-4112-A1A9-F2669D106BF3` by their 128-bit UUIDs, receives
notifications, performs the initial GATT characteristic read, writes MIDI
packets, and exposes `MIDIHub BLE In` and
`MIDIHub BLE Out` through CAMD. The codec is portable and shared by all AROS
targets.

This program is the **central** implementation: AROS initiates a connection to
a BLE MIDI peripheral. The companion `aros-ble-midi-peripheral.patch` supplies
the portable GATT server and HCI advertising codecs, and
`aros-ble-gatt-service-api.patch` connects those facilities to
`bluetooth.library`. MIDIHub supplies a tested advertising payload containing
the BLE MIDI service UUID. The remaining component for making AROS discoverable
by GarageBand is `btmidi.class`, which will own the service and CAMD bridge.

## Peripheral architecture

BLE MIDI belongs to the Bluetooth subsystem as a profile class, rather than
being hard-coded into the generic host stack:

- `btcore` owns ATT/GATT server behavior, CCCD state, notifications, and the
  HCI advertising commands.
- `bluetooth.library` owns each radio's advertising state, accepts peripheral
  connections, creates a GATT server for each LE connection, and exposes a
  service registration API to profile classes.
- `btmidi.class` registers the standard BLE MIDI service and I/O
  characteristic, translates BLE MIDI packets, and bridges the byte stream to
  CAMD.
- Bluetooth Preferences controls radio state, pairing, trust, and the loaded
  Bluetooth class. The class's configuration window controls whether the MIDI
  service is advertised, its local name, and its CAMD port names.
- MIDIHub Preferences may link to the same class configuration and show MIDI
  activity. SoundFont and synthesizer settings stay in MIDIHub because they
  are independent of Bluetooth.

This follows the existing AROS model in which `bthid.class`, `btserial.class`,
and `btpan.class` sit above `bluetooth.library`. It also lets any future GATT
server profile reuse the same stack support.

The current local-service API uses legacy advertising and supports one local
GATT service per radio. It serializes radio programming and delivers incoming
writes to the profile's own Exec task, so MIDI parsing and CAMD calls never run
inside the Bluetooth hardware task. Peripheral-role SMP is still absent; the
BLE MIDI characteristic therefore remains unencrypted until responder pairing
is added to the stack.

## Build and run

1. Apply [the GATT prerequisite patch](../patches/aros-ble-midi-gatt.patch) to
   the matching AROS checkout and build `bluetooth.library` and the optional
   MetaMake target `contrib-aros-midihub-ble`.
2. In Bluetooth Preferences, scan, register, and connect to a BLE MIDI
   peripheral. Run `BTDevLister SERVICES` to find its address and verify
   that GATT services have been enumerated.
3. Run `MIDIHUB:C/MIDIHubBLE <address>`, using the Bluetooth address shown by
   `BTDevLister`. Press Ctrl-C to stop.
4. Connect a CAMD sender to `MIDIHub BLE Out` and a CAMD receiver to
   `MIDIHub BLE In`. Send Note On/Off, Program Change and SysEx in both
   directions. The program prints received and sent message counts on exit.

The GATT patch passed AROS apply checks and portable stack tests. The BLE MIDI
codec passes local packet tests covering channel messages, running status,
timestamp wrap, and multi-packet SysEx. The `MIDIHubBLE` source compiled and
linked with the existing AROS x64 GCC and headers generated from the current
Bluetooth API. The full MetaMake target and patched `bluetooth.library` have
not been built. The program has not been run with a BLE radio.

The first runtime sends complete MIDI messages without transmit running
status. The CAMD bridge accepts channel messages, System Common, System
Real-Time, and SysEx. Incoming timestamps are parsed but CAMD receives messages
immediately; clock correlation and scheduling are future work. One program
instance handles one BLE peripheral.

The program creates virtual CAMD ports through the client API. This gives
applications the same CAMD interface as USB MIDI without installing a
`DEVS:Midi` driver. See [the CAMD integration design](camd-integration.md).

Protocol reference: [MIDI Association BLE MIDI 1.0 specification](https://midi.org/midi-over-bluetooth-low-energy-ble-midi).
