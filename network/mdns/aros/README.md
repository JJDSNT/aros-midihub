# Bonami AROS port

This directory contains the native AROS port of `bonami.library`. The sources
are derived from the Bonami revision pinned in `../bonami` and retain its BSD
2-Clause license. They are copied here deliberately so the AROS contribution
can be built and reviewed without requiring a nested checkout.

It has two parts:

- `mdnsd`, the one responder and browser of the host. The AROSTCP service
  manager starts it from `db/services.d/mdns` and stops it with the stack;
  it registers with `netservices.library` and, around a reconfiguration,
  says goodbye, lets go of the stack and takes it up again with what the
  applications had registered. It installs in
  `SYS:System/Network/Services`. Started by hand it runs unmanaged until
  Ctrl-C.
- `bonami.library`, a thin client with the existing Bonami ABI. Every call is
  a message to mdnsd's public port; the library holds no mDNS state and does
  not open `bsdsocket.library`. Without mdnsd its calls return
  `MDNSERR_NOENGINE`.

A registration or a browse belongs to the task that made it. It ends with
`DeregisterService()`/`StopBrowse()`, with `CloseMDNSEngine()`, or with the
task's last `CloseLibrary()`; mdnsd then announces the service's goodbye and
never calls that task's hook again. A task that ends without closing the
library is not noticed. `CloseMDNSEngine()` no longer stops the engine for
other callers.

A browse hook still runs in mdnsd's process, as it ran in `mdns.task`: it
must only copy the message and signal its own task.

`<libraries/netservice.h>` is published by AROS under the GNU GPL version 2,
while Bonami is BSD 2-Clause. Only `src/mdnsd_service.c` includes it; whether
that header's license is meant to bind a daemon that merely registers is a
question to settle with AROS before this goes upstream.

Build from an AROS build tree:

```sh
make contrib-aros-midihub-bonami contrib-aros-midihub-mdnsd
make contrib-aros-midihub-mdnsprobe
```

`BonamiProbe` without arguments opens the engine and prints its counters.
`BonamiProbe REGISTER name [TYPE type] [PORT n] [BROWSE type] [SECONDS n]`
announces an instance and prints what a browse reports.

`tools/bonami-hosted.sh` runs it on Linux-hosted AROS against the host's
Avahi over a veth pair and checks that each side resolves the other's
service and that the host sees the AROS service leave when its program
only closes the library. It passed on 2026-10-11 on x86-64. It needs the AROS network
patches in `patches/net-series`: before them AROSTCP delivered no IPv4
multicast on a SANA-II interface and the hosted `eth.device` dropped it.

The first native run found that the port had only ever been right on a
big-endian machine. Addresses inside the engine are numbers
(`0xAABBCCDD` is A.B.C.D) and are converted with `htonl()`/`ntohl()` where
they meet a socket call or the public API; DNS fields are written byte by
byte. The same source is correct on either byte order.

mdnsd keeps what other hosts announce for the time their PTR record gives,
drops it on a goodbye, and hands what it knows to a browse that starts
later. It keeps host addresses by name, so an address record may come before
the SRV record that names the host.

Still missing, and next: probing and conflict handling before a name is
announced, known-answer suppression, a query for an instance's SRV, TXT and
address when only its PTR is known, more than one interface, following an
address that changes without a reconfiguration, and IPv6. The cross-build for
AArch64 has not been repeated since the split.

AROS now provides a clean-room Envoy-compatible `nipc.library` and public
`<envoy/nipc.h>` API. This initial port does not yet build Bonami's optional
inquiry bridge against them: the three NIPC compatibility functions remain in
the ABI and return `MDNSERR_NOTREADY` until that separate integration is
implemented and validated. Core mDNS/DNS-SD does not depend on NIPC.

The protocol limitations and remaining interoperability work are listed in
the parent [README](../README.md).
