Two corrections to btmidi.class's BLE-MIDI codec (`blemidi.c`), read
against "Specification for MIDI over Bluetooth Low Energy (BLE-MIDI)" 1.0,
MMA/AMEI RP-052. Follow-up to #1507; nothing else in the class changes.

**The header's reserved bit.** The decoder refused any packet whose header
had bit 6 set. The specification reserves that bit: sent as 0 and not
looked at by the receiver. A sender that ever sets it would have all its
MIDI dropped. It is now ignored. The running-status path also loses a
condition that was always true, and the codec cites the sections it
implements.

**F9 and FD.** These status bytes are undefined in MIDI 1.0, whose receivers
ignore them. They are now taken off the stream in both directions instead of
being handed to CAMD or sent to the peer.

Validation: the host test of the codec (the packets drawn in the
specification's figures, decoded and written back) fails on master at the
reserved-bit case and passes with this change, under ASan/UBSan.
btmidi.class, its CAMD driver and the embedded driver array build for
x86-64 and amiga-m68k (full module targets, link included).
