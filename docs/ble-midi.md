# BLE MIDI central transport

The optional `MIDIHubBLE` program connects to a registered BLE MIDI
peripheral through AROS `bluetooth.library`. It finds the MIDI service
`03B80E5A-EDE8-4B33-A751-6CE34EC4C700` and I/O characteristic
`7772E5DB-3868-4112-A1A9-F2669D106BF3` by their 128-bit UUIDs, receives
notifications, writes MIDI packets, and exposes `MIDIHub BLE In` and
`MIDIHub BLE Out` through CAMD. The codec is portable and shared by all AROS
targets.

This is a **central** implementation: AROS initiates a connection to a BLE MIDI
peripheral. Making AROS itself appear as a BLE MIDI device to iOS requires a
GATT server and BLE advertising support, which the current AROS Bluetooth
stack does not provide. GarageBand on an iPhone cannot discover this program
as a BLE MIDI peripheral.

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
timestamp wrap, and multi-packet SysEx. The AROS executable has not been
built or run with a BLE radio; Bluetooth APIs are absent from the existing
Linux hosted build in this workspace.

The first runtime sends complete MIDI messages without transmit running
status. Received real-time and System Common messages are decoded by the
codec, but the current CAMD bridge handles channel messages and SysEx only.
Incoming timestamps are parsed but CAMD receives messages immediately; clock
correlation and scheduling are future work. One program instance handles one
BLE peripheral.

Protocol reference: [MIDI Association BLE MIDI 1.0 specification](https://midi.org/midi-over-bluetooth-low-energy-ble-midi).
