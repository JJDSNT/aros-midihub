**Title:** camd: an optional driver-side block for port identity, presence and name

Following up on the question at the end of the #1507 discussion, as
promised there: a sketch before any code.

### The problem

Since #1507, BLE MIDI, USB MIDI (#1733) and network MIDI (#1586) all reach
CAMD the way you asked, as `DEVS:Midi` drivers with numbered ports. That
model cannot say three things a user needs:

- **Which device is behind a port.** Identity is driver name + port
  number. `btmidi` gives a bound device the first free port, so when one
  keyboard goes away another can move into its port, and a saved route
  silently points at the wrong device. The same holds for AppleMIDI peers
  in the network drivers.
- **Whether a port has a device now.** All 16 ports of `btmidi`,
  `poseidonusb` or `applemidi` look the same to a client, bound or not.
- **A name to show.** A client sees `btmidi.in.3`, not "Keystation 49".

### What I am asking about now: the driver side

An optional block a driver may place next to its `MidiDeviceData`, found
the way `MidiDeviceData` is (a magic of its own in the loaded seglist):

- `MidiDeviceData`, the library vectors, `OpenPort()`/`ClosePort()`, the
  `<driver>.in.N`/`.out.N` clusters and every existing driver and
  application stay exactly as they are. A driver without the block is
  treated as today.
- Per port, the driver can give: **identity evidence** (a short opaque
  key, e.g. a BLE address or a USB serial, with how stable it is),
  **presence** (a device is bound or not) and a **display name**.
- The driver tells CAMD that a port's information changed through a
  function CAMD hands it, the same way CAMD hands `OpenPort()` its transmit
  and receive functions, so the dependency still runs from CAMD to the
  driver and never the other way.

### Where it leads: the client side, later and separately

So you can see the whole direction: the block is the input to a client-side
endpoint API I would propose separately, after this one. In short, CAMD
would give each port a stable ID that survives reboots and port changes,
and let a program list endpoints with their name and presence, be told
when they change, and open a session to one. It is additive: clusters and
the CAMD 41 calls stay as they are. UMP and timestamps from a driver would
come later still, on the same block.

I would rather not expose the block's data through new tags on the current
calls first, since that would leave two client paths to the same
information once the endpoint API exists.

### Questions

1. Is an optional driver-side block of this kind acceptable in
   camd.library, or would you rather see this solved another way?
2. Is it acceptable for the client side to come afterwards as that
   endpoint API, rather than as tags on the existing calls?

If the direction is acceptable, the first PR would add the block with
`btmidi.class` as the first driver to provide it, and show that the
existing CAMD 41 contract suite is unchanged.
