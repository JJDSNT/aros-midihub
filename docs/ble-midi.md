# BLE MIDI transport

`btmidi.class` carries BLE MIDI in both Bluetooth LE roles:

- **AROS as the central.** bluetooth.library offers the services of every
  registered device to the classes, and `btmidi.class` binds to each BLE MIDI
  service (`03B80E5A-EDE8-4B33-A751-6CE34EC4C700`, I/O characteristic
  `7772E5DB-3868-4112-A1A9-F2669D106BF3`). Each bound device, such as a
  keyboard or a controller, gets a CAMD port of its own.
- **AROS as the peripheral.** The class registers the BLE MIDI service with
  the stack's GATT server, so a phone or computer can connect to AROS and
  play through the class's first CAMD port.

## Two versions of the class

There are two, and they present themselves to CAMD differently.

| | upstream class (AROS PR #1507) | copy in this repository (`ports/aros/btmidi`) |
|---|---|---|
| What it is to CAMD | a MIDI device: the driver `DEVS:Midi/btmidi`, 16 ports | a client: one MidiNode with two clusters per role |
| Peripheral role | `btmidi.in.0`, `btmidi.out.0` | `BLE MIDI In`, `BLE MIDI Out` (editable) |
| A bound device | `btmidi.in.<n>`, `btmidi.out.<n>`, n from 1, first free port | `<name> In`, `<name> Out` |
| Opens `camd.library` | no; CAMD loads the driver, which opens the class | yes, once per node |
| MIDI parsing | CAMD's, from a byte stream | the class's own reassembly |
| Window | status: which device has which port, activity | settings: the names, activity |

The upstream class is the design. The first version sent upstream was the
client one; the reviewer asked for the device model that
`camdusbmidi.class` uses, and the class was rewritten for it on 2026-10-11
(see [upstream pull requests](upstream-prs.md)). The copy here is the
earlier version and is still what `contrib-aros-midihub` builds, and
MIDIHub.prefs still recognises its ports through its configuration chunk.
Both have to move to the driver's ports; see Open work. The steps below
give the upstream names, with the copy's names in parentheses where they
differ.

### The upstream class as a CAMD driver

It follows `rom/usb/classes/camdmidi`:

- `camd/btmididriver.c` is a `struct MidiDeviceData` with 16 ports. The
  class embeds it as a relocatable object and writes it to
  `DEVS:Midi/btmidi` when it starts, replacing an older one. CAMD loads it
  the next time it scans `DEVS:Midi`; its `Init()` opens the class.
- For each port CAMD opens, the driver gets an adapter from the class
  (`btmidiCAMDOpenPort()`). Bytes from BLE MIDI packets, timestamps removed,
  go to CAMD's receive function one at a time. When CAMD calls
  `ActivateXmit`, a soft interrupt moves what its transmit function has
  into the adapter's ring and signals the task that owns the port, which
  packs the bytes into BLE MIDI packets.
- Port 0 is the peripheral role. Ports 1 to 15 go to bound devices, the
  first free one each, for as long as the binding exists, connected or not.
  A port no device is bound to does not open. CAMD may keep a port open
  after its device's binding is gone; what is sent to it is then dropped.
- Several centrals can send to port 0 at once, but CAMD parses one byte
  stream per port, so only one SysEx at a time reaches it: a second one is
  left out, and a message that interrupts a SysEx ends it.

What this model cannot say: which device is behind a port, whether a port
has a device at the moment, and a name to show. A device may get another
port number when a different device is removed. See
[the CAMD integration design](camd-integration.md) for the driver-side
extension that would carry this.

The packet codec is portable, shared by
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
  characteristic, translates BLE MIDI packets, and exchanges the byte stream
  with CAMD.
- Bluetooth Preferences controls radio state, pairing, trust, loaded classes,
  advertising, and whether the BLE MIDI service is enabled through the
  upstream `btgatt.class` window. The `btmidi.class` window shows the ports
  and activity: whether the service is offered, the state of the CAMD
  driver, and packet, message, and error counts in each direction. In the
  copy here it also edits the names of the node and its two clusters.
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
Each central that writes to the service has its own decoder (up to four at
once), so centrals sending at the same time do not corrupt each other's
messages; the copy here also reassembles each central's SysEx separately,
which the upstream class cannot, as described above. As the peripheral the class asks the
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

A step-by-step hardware test with a Raspberry Pi 3 and an iPhone (MIDI
Wrench, GarageBand) is in [the Pi 3 test](ble-midi-pi3-test.md).

### AROS as the central

1. Use an AROS checkout at upstream `86aab939c4` or later, which has the BLE
   work that used to be `aros-ble-midi-upstream-gaps.patch` here, and build
   `bluetooth.library` and the normal
   `contrib-aros-midihub` target, which installs `btmidi.class`.
2. In Bluetooth Preferences, scan, register, and connect to a BLE MIDI
   peripheral. Once its services have been enumerated, `btmidi.class` binds
   to it; the device's window lists the BLE MIDI service with `btmidi.class`
   under "Services and their bindings".
3. Connect a CAMD receiver to the device's port, `btmidi.in.<n>`, and a CAMD
   sender to `btmidi.out.<n>`; the class's window says which port the device
   has (`<device name> In` and `<device name> Out` with the copy here). Send
   Note On/Off, Program Change and SysEx in both directions.
4. Switch the device off and on again. When the stack reconnects it, the
   class reads the characteristic, subscribes again, and MIDI resumes on the
   same CAMD ports.

The binding never connects the device itself; it follows the stack's connect
and disconnect events, as `btbattery.class` does. After a link comes up it
waits a second, because a link serves one GATT request at a time and other
classes look at the device too. It then reads the characteristic once, as
the specification asks of a central, and subscribes to notifications.
Packets are sized to the characteristic's negotiated payload. Messages sent
to the device's port while it is away are dropped.

To use `MIDIHubBLE` instead for diagnosis, build `contrib-aros-midihub-ble`
and run `MIDIHUB:C/MIDIHubBLE <address>` with the address `BTDevLister`
shows. Its ports are `MIDIHub BLE In` and `MIDIHub BLE Out`; do not run it
for a device the class is already bound to.

### AROS as the peripheral

1. With an AROS checkout as above, build the normal `contrib-aros-midihub`
   target. Its dependency set
   installs `btmidi.class` in `SYS:Classes/Bluetooth`.
2. Open Bluetooth Preferences and verify that `btmidi.class` appears on the
   Classes page. Open the `btgatt.class` settings, enable advertising, and
   leave the `BLE MIDI` service enabled. The advertised name is the local name
   configured for the Bluetooth radio.
3. In an iOS BLE MIDI connection panel or MIDI Wrench, scan for the configured
   AROS Bluetooth name and connect.
4. Connect an AROS CAMD application to `btmidi.in.0` or `btmidi.out.0`
   (`BLE MIDI In` or `BLE MIDI Out` with the copy here). Incoming BLE
   packets, including SysEx, are decoded into CAMD; CAMD output is encoded
   into notifications for every subscribed iOS central. With the upstream
   class, `DEVS:Midi/btmidi` has to exist before `camd.library` is first
   opened: the class writes it when it starts, and an unpatched CAMD only
   scans `DEVS:Midi` when it is loaded.
5. Open the `btmidi.class` window from the Bluetooth Preferences Classes
   page. The Received and Sent counters should rise while MIDI flows; the
   Errors counter should stay at zero.

#### Class settings (the copy here only)

The upstream class has nothing to configure: CAMD names a driver's ports.
In the copy here, the settings window edits the names of the CAMD device (`BLE MIDI`), the
cluster that carries MIDI received from Bluetooth (`BLE MIDI In`), and the
cluster whose MIDI is sent over Bluetooth (`BLE MIDI Out`). `Use` applies
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

The upstream class, rewritten as a driver, builds for linux-x86_64 hosted and
raspi-aarch64. On hosted AROS, with this repository's CAMD patches, the
class writes the driver, CAMD loads it and opens port 0, 5000 messages sent
in bursts all reach the class, and input with running status and SysEx is
parsed by CAMD; that run fed the driver's receive hook directly. It has not
run on a radio yet, nor against an unpatched `camd.library`, nor on m68k.

Both versions send complete MIDI messages without transmit running status,
and accept channel messages, System Common, System Real-Time, and SysEx.
Incoming timestamps are parsed but CAMD receives messages immediately; clock
correlation and scheduling are future work. The upstream class puts as many
whole messages into a packet as were waiting when it was woken.

The copy here creates virtual CAMD ports through the client API, each node
with its own `camd.library` base. That gave applications a CAMD interface
without a `DEVS:Midi` driver, and it is exactly what upstream turned down: a
transport is a device, not a client. See
[the CAMD integration design](camd-integration.md).

## Open work

Besides a full build and tests with physical radios and iOS, these gaps remain.
They are listed by impact, and each one is removed from this list once it is
fixed.

1. **The upstream class on hardware.** The driver version has not run on a
   radio; [the Pi 3 test](ble-midi-pi3-test.md) is owed for PR #1507, and the
   central role has never been tried with a real peripheral.
2. **Replace the copy here.** `ports/aros/btmidi` and the shared
   `camd_bridge` it uses should go once the upstream class is merged, and
   `contrib-aros-midihub` should stop building a class of the same name.
3. **MIDIHub.prefs.** It finds the peripheral role's ports by reading the
   copy's configuration chunk (`btmidi_cfg.h`) and a bound device's from the
   device's name. With the upstream class they are `btmidi.in.<n>` and
   `btmidi.out.<n>`, there is no chunk, and which device has which port is
   only shown in the class's window. Saved routes that name `BLE MIDI In`
   need migrating.
4. **A stable port per device.** A bound device takes the first free port,
   so its number can change when another device is removed. The class could
   keep an address-to-port map in its configuration; the driver-side
   extension in [the CAMD integration design](camd-integration.md) is the
   proper answer.
5. **Incoming timestamps.** They are parsed but not used; messages reach CAMD
   when they arrive, without jitter correction. The driver interface carries
   no time.

Protocol reference: [MIDI Association BLE MIDI 1.0 specification](https://midi.org/midi-over-bluetooth-low-energy-ble-midi).
