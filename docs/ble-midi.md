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
a BLE MIDI peripheral. Upstream AROS now supplies the portable GATT server,
local service-record API, notifications, LE advertising, and the
`btgatt.class` Preferences UI. MIDIHub supplies `btmidi.class`, which registers
the standard service and bridges it to CAMD.

## Peripheral architecture

BLE MIDI belongs to the Bluetooth subsystem as a profile class, rather than
being hard-coded into the generic host stack:

- `btcore` owns ATT/GATT server behavior, CCCD state, notifications, and the
  HCI advertising commands.
- `bluetooth.library` owns each radio's advertising state, accepts peripheral
  connections, serves registered GATT records, and broadcasts service writes.
- `btmidi.class` registers the standard BLE MIDI service and I/O
  characteristic, translates BLE MIDI packets, and bridges the byte stream to
  CAMD.
- Bluetooth Preferences controls radio state, pairing, trust, loaded classes,
  advertising, and whether the BLE MIDI service is enabled through the
  upstream `btgatt.class` window. The `btmidi.class` settings window names
  the CAMD device and its two clusters and shows activity: whether the
  service is offered, the CAMD state, and packet, message, and error counts
  in each direction.
- MIDIHub Preferences may link to the same class configuration and show MIDI
  activity. SoundFont and synthesizer settings stay in MIDIHub because they
  are independent of Bluetooth.

This follows the existing AROS model in which `bthid.class`, `btserial.class`,
and `btpan.class` sit above `bluetooth.library`. It also lets any future GATT
server profile reuse the same stack support.

The service-record API keeps values in `bluetooth.library`; incoming writes
arrive as `BEHMB_SERVICEWRITE` events with immutable packet snapshots on the
class's Exec task, so MIDI parsing and CAMD calls never run inside the Bluetooth
hardware task. The remaining AROS patch adds peripheral-role SMP, bonding, and
encryption on reconnection. Legacy Just Works is the default; the existing
`btlesc` boot argument enables Secure Connections with Just Works, Numeric
Comparison, or Passkey Entry. OOB pairing remains unsupported.
Outgoing values are likewise queued as immutable notifications. The class uses
20-byte packets so it also works before a central negotiates an ATT MTU larger
than the mandatory default. The generic queue retains up to 256 pending
snapshots and drops the oldest if producers sustain a higher rate than every
active radio can consume.

## Build and run

### AROS as the central

1. Apply [the remaining BLE patch](../patches/aros-ble-midi-upstream-gaps.patch)
   to the matching AROS checkout and build `bluetooth.library` and the optional
   MetaMake target `contrib-aros-midihub-ble`.
2. In Bluetooth Preferences, scan, register, and connect to a BLE MIDI
   peripheral. Run `BTDevLister SERVICES` to find its address and verify
   that GATT services have been enumerated.
3. Run `MIDIHUB:C/MIDIHubBLE <address>`, using the Bluetooth address shown by
   `BTDevLister`. Press Ctrl-C to stop.
4. Connect a CAMD sender to `MIDIHub BLE Out` and a CAMD receiver to
   `MIDIHub BLE In`. Send Note On/Off, Program Change and SysEx in both
   directions. The program prints received and sent message counts on exit.

### AROS as the peripheral

1. Apply the remaining BLE patch in [the patch guide](../patches/README.md),
   then build the normal `contrib-aros-midihub` target. Its dependency set
   installs `btmidi.class` in `SYS:Classes/Bluetooth`.
2. Open Bluetooth Preferences and verify that `btmidi.class` appears on the
   Classes page. Open the `btgatt.class` settings, enable advertising, and
   leave the `BLE MIDI` service enabled. The advertised name is the local name
   configured for the Bluetooth radio.
3. In an iOS BLE MIDI connection panel or MIDI Wrench, scan for the configured
   AROS Bluetooth name and connect.
4. Connect an AROS CAMD application to `MIDIHub BLE In` or
   `MIDIHub BLE Out`. Incoming BLE packets, including SysEx, are decoded into
   CAMD; CAMD output is encoded into notifications for every subscribed iOS
   central.
5. Open the `btmidi.class` settings from the Bluetooth Preferences Classes
   page. The Received and Sent counters should rise while MIDI flows; the
   Errors counter should stay at zero.

#### Class settings

The settings window edits the names of the CAMD device (`MIDIHub BLE`), the
cluster that carries MIDI received from Bluetooth (`MIDIHub BLE In`), and the
cluster whose MIDI is sent over Bluetooth (`MIDIHub BLE Out`). `Use` applies
them at once: the class reopens its CAMD node, and CAMD clients and MIDIHub
routes find the clusters again by their new names. `Save` also writes them to
the Bluetooth configuration on disk. If CAMD cannot open the configured names,
the class falls back to the defaults and the window says so. As everywhere in
CAMD, a cluster name another program also uses joins that program's cluster,
so pick distinct names unless sharing is intended.

Advertising, pairing, and whether the service is offered stay in Bluetooth
Preferences and the `btgatt.class` window.

The patch passes the AROS apply check and portable stack tests. The BLE MIDI
codec passes local packet tests covering channel messages, running status,
timestamp wrap, and multi-packet SysEx. The `MIDIHubBLE` source compiled and
linked with existing AROS headers. The class and its settings window pass an
m68k AROS syntax build with `-Wall -Wextra -Werror` against headers generated
from the patched `bluetooth.conf`. MetaMake expands the class target successfully. A full class link
and a physical radio exchange remain to be performed.

The first runtime sends complete MIDI messages without transmit running
status. The CAMD bridge accepts channel messages, System Common, System
Real-Time, and SysEx. Incoming timestamps are parsed but CAMD receives messages
immediately; clock correlation and scheduling are future work. One program
instance handles one BLE peripheral.

The program creates virtual CAMD ports through the client API. This gives
applications the same CAMD interface as USB MIDI without installing a
`DEVS:Midi` driver. See [the CAMD integration design](camd-integration.md).

Protocol reference: [MIDI Association BLE MIDI 1.0 specification](https://midi.org/midi-over-bluetooth-low-energy-ble-midi).
