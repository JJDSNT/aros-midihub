# BLE MIDI transport

`btmidi.class` carries BLE MIDI in both Bluetooth LE roles:

- **AROS as the central.** bluetooth.library offers the services of every
  registered device to the classes, and `btmidi.class` binds to each BLE MIDI
  service (`03B80E5A-EDE8-4B33-A751-6CE34EC4C700`, I/O characteristic
  `7772E5DB-3868-4112-A1A9-F2669D106BF3`). Each bound device, such as a
  keyboard or a controller, gets a CAMD node named after it: what it plays
  arrives on `<name> In`, and what CAMD clients send to `<name> Out` is
  written to it.
- **AROS as the peripheral.** The class registers the BLE MIDI service with
  the stack's GATT server, so a phone or computer can connect to AROS and
  play through `MIDIHub BLE In` and `MIDIHub BLE Out`.

The packet codec and the reassembly of MIDI messages are portable, shared by
both roles and tested on the host. Upstream AROS supplies the GATT client and
server, the local service-record API, notifications, LE advertising, and the
`btgatt.class` Preferences UI.

The older `MIDIHubBLE` program does the central role by hand for one device
given by address. It remains as a diagnostic; it should not run alongside
the class for the same device.

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
Each central that writes to the service has its own decoder, running status,
and SysEx buffer (up to four at once), so centrals sending at the same time do
not corrupt each other's messages. As the peripheral the class asks the
central for a 15 ms connection interval about a second after it connects;
iOS otherwise tends to keep 30 ms or more.
Outgoing values are likewise queued as immutable notifications. A notification
carries at most the ATT MTU less 3 bytes and goes to every subscribed central,
so the class sizes its packets to the smallest payload among the connected
centrals (`BSA_LENotifyPayload`): 20 bytes until every central has negotiated
a larger MTU, up to 244. A long SysEx then needs about a tenth of the packets.
A read of the characteristic returns no data, as the specification requires
(`BGDP_STREAM`). The generic queue retains up to 256 pending
snapshots and drops the oldest if producers sustain a higher rate than every
active radio can consume.

## Build and run

### AROS as the central

1. Apply [the remaining BLE patch](../patches/aros-ble-midi-upstream-gaps.patch)
   to the matching AROS checkout and build `bluetooth.library` and the normal
   `contrib-aros-midihub` target, which installs `btmidi.class`.
2. In Bluetooth Preferences, scan, register, and connect to a BLE MIDI
   peripheral. Once its services have been enumerated, `btmidi.class` binds
   to it; the device's window lists the BLE MIDI service with `btmidi.class`
   under "Services and their bindings".
3. Connect a CAMD receiver to `<device name> In` and a CAMD sender to
   `<device name> Out`. Send Note On/Off, Program Change and SysEx in both
   directions.
4. Switch the device off and on again. When the stack reconnects it, the
   class reads the characteristic, subscribes again, and MIDI resumes on the
   same CAMD ports.

The binding never connects the device itself; it follows the stack's connect
and disconnect events, as `btbattery.class` does. After a link comes up it
waits a second, because a link serves one GATT request at a time and other
classes look at the device too. It then reads the characteristic once, as
the specification asks of a central, and subscribes to notifications.
Packets are sized to the characteristic's negotiated payload. Messages sent
to `<name> Out` while the device is away are dropped.

To use `MIDIHubBLE` instead for diagnosis, build `contrib-aros-midihub-ble`
and run `MIDIHUB:C/MIDIHubBLE <address>` with the address `BTDevLister`
shows. Its ports are `MIDIHub BLE In` and `MIDIHub BLE Out`, which are also
the defaults of the peripheral role, so do not use both at once.

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
immediately; clock correlation and scheduling are future work.

The class creates virtual CAMD ports through the client API. Each CAMD node
holds its own `camd.library` base, so the peripheral role and every bound
device can open and close their ports independently. This gives
applications the same CAMD interface as USB MIDI without installing a
`DEVS:Midi` driver. See [the CAMD integration design](camd-integration.md).

## Open work

Besides a full build and tests with physical radios and iOS, these gaps remain.
They are listed by impact, and each one is removed from this list once it is
fixed.

1. **Incoming timestamps.** They are parsed but not used; messages reach CAMD
   when they arrive, without jitter correction.
2. **Bound devices in the settings window.** The window shows the peripheral
   role only; it could list the bound devices and their CAMD names.
3. **MIDIHub.prefs integration.** Show the BLE MIDI endpoint in the overview
   and link to the class settings window.

Protocol reference: [MIDI Association BLE MIDI 1.0 specification](https://midi.org/midi-over-bluetooth-low-energy-ble-midi).
