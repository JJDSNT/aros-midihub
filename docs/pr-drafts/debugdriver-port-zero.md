CAMD numbers a driver's ports from 0, but `OpenPort()` stored the user data
at `UserData[portnum - 1]`. Opening port 0 writes one element before the
array.

What that overwrites depends on the layout. On raspi-aarch64 it was the
`TransmitFunc` pointer, and the driver crashed on the first message sent to
port 0. On hosted x86-64 it lands in padding and nothing visible happens.
The store now uses `UserData[portnum]`.

Builds for x86-64 and amiga-m68k; on hosted x86-64, notes and SysEx sent to
`debugdriver.out.0` go through.
