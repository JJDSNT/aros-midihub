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
   `raspi-aarch64` with `aros-ble-midi-upstream-gaps.patch` and
   `aros-raspi-bt-firmware.patch` applied, plus the `contrib-aros-midihub`
   target.
2. Check that the card has the Bluetooth patchram in `DEVS:Firmware/brcm/`
   (`BCM43430A1.hcd` for a Pi 3 or Zero W, `BCM4345C0.hcd` for a Pi 3B+).
   The firmware patch downloads both with the boot image.

## Bring Bluetooth up

1. Boot. `S:Startup-Sequence` runs `BTStackLoader`, which binds the firmware
   loader (`brcmbt.fwl`) and brings up the radios saved in `bluetooth.prefs`.
2. The first time, register the on-board radio: in Bluetooth Preferences, on
   the hardware page, enter `DEVS:Bluetooth/h4bthci.device` with unit 0, Add,
   then Save. The radio is listed with a real address; if it shows
   `AA:AA:AA:AA:AA:AA`, the patchram was not found.
3. On the Classes page check that `btgatt.class` and `btmidi.class` are
   loaded. Open the `btgatt.class` settings: tick "Let Bluetooth LE devices
   find and connect to this machine" and keep `BLE MIDI` offered. Use or
   Save.
4. Open MIDIHub.prefs. The Overview should list `BLE MIDI In` and
   `BLE MIDI Out` as `BLE MIDI`, `Offered`.

Keep the Bluetooth Preferences log window open during the tests; most checks
below read it.

## The Diagnostics page

MIDIHub.prefs (`MIDIHUB:Prefs/MIDIHubPrefs`, inside the package drawer)
has a **Diagnostics** page that shows on one screen what the tests below
check: the CAMD endpoints, a MIDI monitor, a Send test button, the BLE MIDI
state with the connected devices, and the latest Bluetooth log lines. Select
an endpoint, then **Monitor** to watch it or **Send test** to play a scale
and a SysEx to it; this replaces the `MIDIHubCAMDProbe` commands. A photo of
this page is usually all that is needed to report a result.

The **Ask centrals for a 15 ms connection interval** switch turns the
connection-interval request on or off for the next connection. If the iPhone
disconnects about a second after connecting (`connection timeout` in the
log), switch it off, reconnect, and compare.

## Test 1: MIDI Wrench connects, iPhone to AROS

1. On AROS, in a Shell: `MIDIHUB:C/MIDIHubCAMDProbe --monitor "BLE MIDI In" 300`.
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
   `MIDIHUB:C/MIDIHubCAMDProbe --send "BLE MIDI Out"`.
2. MIDI Wrench shows eight Note On/Off pairs (C4 to C5) and a 6-byte SysEx
   `F0 7E 7F 06 01 F7`. A whole SysEx in one piece shows that a larger
   packet or correct multi-packet reassembly went through.
3. The class window's Sent counters rise; Errors stays at 0.

## Test 3: GarageBand plays what AROS sends

1. Disconnect MIDI Wrench. In GarageBand open an instrument (for example the
   keyboard), then connect the AROS radio from GarageBand's Bluetooth MIDI
   devices setting (Settings, Advanced).
2. Run `MIDIHUB:C/MIDIHubCAMDProbe --send "BLE MIDI Out"` again. GarageBand
   plays the scale. Timing should sound even; an uneven scale suggests the
   connection interval stayed long (see the log from test 1).

## Test 4: reconnection and the read

1. Turn Bluetooth off on the iPhone and on again, and reconnect from MIDI
   Wrench. Repeat tests 1 and 2 without restarting anything on AROS.
2. If the iPhone paired in test 1, reconnecting must not ask to pair again;
   the log shows the stored key being used.
3. Some iOS versions read the BLE MIDI characteristic on connection. A read
   now returns no data, as the specification requires; nothing should be
   received on `BLE MIDI In` at connection time.

## Test 5: port names

1. In the class settings, rename the ports (for example `Pi In` and `Pi Out`)
   and Use. MIDIHub.prefs' overview shows the new names as `BLE MIDI`.
2. Run the probe's `--send` and `--monitor` with the new names; both work
   without reconnecting the iPhone.
3. Set the names back with Defaults.

## Test 6: the radio comes back after a reboot

1. Reboot without touching anything.
2. Expected: the radio is up with the same address, from the saved
   `bluetooth.prefs`, and the firmware was applied during its bring-up: the
   Bluetooth log shows the patchram loaded once, with no controller restart
   afterwards.
3. Repeat test 1 to confirm that BLE MIDI still works.

## What to report

For each test: passed or failed, the relevant Bluetooth Preferences log
lines (connection, pairing, connection interval), the class window counters,
and what MIDI Wrench showed. For a failure, also the `MIDIHubCAMDProbe`
output and the iOS version.
