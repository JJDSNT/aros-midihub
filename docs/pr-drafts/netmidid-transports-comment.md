<!-- Comment for #1679, posted with the force-push of 60f807ef96. -->

I have restructured this PR before review. The service was an AppleMIDI
event loop, and Network MIDI 2.0 (#1693) could only be added inside it.
It is now `netmidid` with a transport interface, and AppleMIDI is its
first transport:

1. `aa6ca46a01` adds the core: configuration, sockets, CAMD driver ports,
   published counts and the loop, behind the interface in `netmidid.h`. It
   knows no protocol.
2. `60f807ef96` adds the AppleMIDI transport: `transport_applemidi.c` and
   `config_applemidi.c`, plus one line where transports and their keys are
   registered.

What the AppleMIDI service does, prints and reads from `ENV:NetMIDI` is
unchanged from the earlier revision, and the same hosted node-rtpmidi suite
passes. #1693 now adds Network MIDI 2.0 the same way, as files of its own
and one line in each registry; before, it was 476 lines inside `main.c`.
#1742 and #1743 are rebased onto this.
