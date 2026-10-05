# BLE MIDI test: Raspberry Pi 3 and iPhone

This checks the BLE MIDI work on real hardware: AROS `raspi-aarch64` on a
Raspberry Pi 3 using its on-board Bluetooth radio, and an iPhone running
MIDI Wrench and GarageBand. Here AROS is the BLE MIDI **peripheral**: the
iPhone connects to it. The central role (AROS connecting to a BLE MIDI
keyboard) needs such a keyboard and is described in [BLE MIDI](ble-midi.md).

## What the Pi 3 radio can and cannot do

The Pi 3's BCM43438 is a Bluetooth 4.1 controller. LE Secure Connections
needs the LE P-256 commands of 4.2, so the pairing will be **legacy Just
Works** even with the `btlesc` boot argument. The Bluetooth Preferences log
line about LE Secure Connections says which one the stack uses. The other
work under test needs no 4.2 feature: advertising the BLE MIDI service,
notifications, the 15 ms connection interval request, MTU-sized packets and
empty reads.

## Prepare the SD card

1. Build the image as in [the patch guide](../patches/README.md): AROS
   `raspi-aarch64` with `aros-ble-midi-upstream-gaps.patch`,
   `aros-raspi-bt-firmware.patch` and `aros-raspi-bt-startup.patch` applied,
   plus the `contrib-aros-midihub` target.
2. Check that the card has the Bluetooth patchram in `DEVS:Firmware/brcm/`
   (`BCM43430A1.hcd` for a Pi 3 or Zero W, `BCM4345C0.hcd` for a Pi 3B+).
   The firmware patch downloads both with the boot image.

## Bring Bluetooth up

1. Boot. `S:Startup-Sequence` runs `BTStackLoader`, which binds the firmware
   loader (`brcmbt.fwl`), and then registers the on-board radio
   (`AddBTHardware DEVS:Bluetooth/h4bthci.device`); nothing has to be done by
   hand.
2. In Bluetooth Preferences the radio is listed with a real address. If it
   shows `AA:AA:AA:AA:AA:AA`, the patchram was not found.
3. On the Classes page check that `btgatt.class` and `btmidi.class` are
   loaded. Open the `btgatt.class` settings: tick "Let Bluetooth LE devices
   find and connect to this machine" and keep `BLE MIDI` offered. Use or
   Save.
4. Open MIDIHub.prefs. The Overview should list `MIDIHub BLE In` and
   `MIDIHub BLE Out` as `BLE MIDI`, `Offered`.

Keep the Bluetooth Preferences log window open during the tests; most checks
below read it.

## Test 1: MIDI Wrench connects, iPhone to AROS

1. On AROS, in a Shell: `MIDIHUB:C/MIDIHubCAMDProbe --monitor "MIDIHub BLE In" 300`.
2. In MIDI Wrench, open its Bluetooth MIDI devices panel, find the name of
   the AROS radio and connect.
3. In the log, expect in this order:
   - the iPhone connecting as central;
   - `LE pairing accepted as peripheral (legacy Just Works)`, if iOS pairs
     (it may connect without pairing; both are fine);
   - about a second later, `<iPhone> accepted the shorter connection
     interval.` A `declined` line is not a failure, but note it.
4. Play notes on MIDI Wrench's keyboard. The probe prints each Note On and
   Note Off (`90 3c 64`, `80 3c 40` and so on). Move a controller if MIDI
   Wrench offers one.
5. In MIDIHub.prefs, the BLE MIDI button opens the class settings: the
   Received counters rise and Errors stays at 0.

## Test 2: AROS to MIDI Wrench

1. With MIDI Wrench still connected and showing its incoming MIDI, run
   `MIDIHUB:C/MIDIHubCAMDProbe --send "MIDIHub BLE Out"`.
2. MIDI Wrench shows eight Note On/Off pairs (C4 to C5) and a 6-byte SysEx
   `F0 7E 7F 06 01 F7`. A whole SysEx in one piece shows that a larger
   packet or correct multi-packet reassembly went through.
3. The class window's Sent counters rise; Errors stays at 0.

## Test 3: GarageBand plays what AROS sends

1. Disconnect MIDI Wrench. In GarageBand open an instrument (for example the
   keyboard), then connect the AROS radio from GarageBand's Bluetooth MIDI
   devices setting (Settings, Advanced).
2. Run `MIDIHUB:C/MIDIHubCAMDProbe --send "MIDIHub BLE Out"` again. GarageBand
   plays the scale. Timing should sound even; an uneven scale suggests the
   connection interval stayed long (see the log from test 1).

## Test 4: reconnection and the read

1. Turn Bluetooth off on the iPhone and on again, and reconnect from MIDI
   Wrench. Repeat tests 1 and 2 without restarting anything on AROS.
2. If the iPhone paired in test 1, reconnecting must not ask to pair again;
   the log shows the stored key being used.
3. Some iOS versions read the BLE MIDI characteristic on connection. A read
   now returns no data, as the specification requires; nothing should be
   received on `MIDIHub BLE In` at connection time.

## Test 5: port names

1. In the class settings, rename the ports (for example `Pi In` and `Pi Out`)
   and Use. MIDIHub.prefs' overview shows the new names as `BLE MIDI`.
2. Run the probe's `--send` and `--monitor` with the new names; both work
   without reconnecting the iPhone.
3. Set the names back with Defaults.

## Test 6: is the Startup-Sequence line needed?

`aros-raspi-bt-startup.patch` assumes nothing else registers the on-board
radio. The firmware loaders do not (`btAddFirmwareLoader()` only patches
radios already present). `BTStackLoader` brings up the radios saved in
`bluetooth.prefs`, though, so after the radio has been saved once the line
only matters on a fresh installation. Both cases:

1. Comment out the `AddBTHardware DEVS:Bluetooth/h4bthci.device` block in
   `S:Startup-Sequence`. Delete `ENVARC:Sys/bluetooth.prefs` and
   `ENV:Sys/bluetooth.prefs` (keep a copy if you have pairings to keep), and
   reboot.
2. Expected: Bluetooth Preferences lists no radio.
3. Run `AddBTHardware DEVS:Bluetooth/h4bthci.device` in a Shell. Expected: the
   radio appears with its real address, because the firmware loaders are
   already bound. Save in Bluetooth Preferences and reboot.
4. Expected: the radio now comes up from the saved configuration, still
   without the line.
5. Restore the line and reboot. Expected: the radio comes up once. The line
   gives the full path, and the stack matches it against the saved name
   whether or not that one has a path; the log may say the hardware is
   already online.

If the radio appears in step 2, something else registers it and the startup
patch should not go upstream; note what the Bluetooth log says about where
it came from.

## What to report

For each test: passed or failed, the relevant Bluetooth Preferences log
lines (connection, pairing, connection interval), the class window counters,
and what MIDI Wrench showed. For a failure, also the `MIDIHubCAMDProbe`
output and the iOS version.
