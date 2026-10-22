A driver port is opened by the first link that joins one of its clusters,
and only then was the link's direction recorded (`isInOpen`/`isOutOpen`).
A receiver that linked to `<driver>.in.N` while the port was already open for
a sender on `<driver>.out.N` was not recorded. When the sender left, CAMD saw
neither direction in use and closed the port under the receiver, which then
received nothing more.

The direction is now recorded for every link that joins, whether or not it
opened the port.

Validation on hosted x86-64, current master, with a test-only copy of
`debugdriver` that logs `OpenPort`/`ClosePort`: link a sender to `out.0`, a
receiver to `in.0`, remove the sender, then the receiver.

- master: `ClosePort 0` right after the receiver links, while it is still linked
- with this change: `ClosePort 0` only when the receiver is removed

`debugdriver` sends still work. camd.library builds for x86-64 and amiga-m68k.
