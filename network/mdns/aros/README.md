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
make contrib-aros-midihub-mdnstools contrib-aros-midihub-mdnsprobe
```

## Additions to the Bonami API

The AROS library is version 40.2 and adds, after the existing functions:

| Addition | What it does |
|---|---|
| `LookupHostA(host, &addr, tags)` | Waits for a host's IPv4 address, at most `MDNS_Timeout` seconds (default 3). `addr` is in network byte order. A name without a dot is taken in `.local`. |
| `QueryRecordA(name, rrtype, hook, tags)`, `StopQuery(ref)` | A standing question for any record. Each answer goes to the hook as a `struct MDNSRecordMsg`; names inside PTR and SRV data are written out in full. |
| `MDNS_HostName` with `MDNS_HostAddr` in `RegisterServiceA()` | Registers a service for another host, which need not speak mDNS itself. |

They close the three "dns-sd feature gaps" of Bonami's `TODO.md` and are
meant to be offered to Bonami, not to stay an AROS dialect. mdnsd does not
hear its own answers, so a question about one of this host's own services
gets none; `LookupHostA()` does know this host's name.

## Tools

`tools/` holds Bonami's command-line front ends, ported from
`SDK/Examples` of the pinned revision: `Nearby` (browse), `Handshake`
(resolve), `Share` (advertise) and `dnssd`, which gains `-P`, `-q` and `-G`
from the additions above. They install in `Extras/aros-midihub/C` for now.
What changed for AROS is in `ba_common.h`: hooks are declared with
`BA_HOOK()`, output goes through `VNewRawDoFmt()`, and addresses are printed
byte by byte, so the same source builds for every AROS architecture.

In the shell a `key=value` argument of `Share` needs quotes
(`Share Name web 80 "path=/"`): `ReadArgs()` otherwise takes the `=` as a
separator. Each pair becomes a TXT string of its own; a value cannot contain
a space.

`BonamiProbe` without arguments opens the engine and prints its counters.
`BonamiProbe REGISTER name [TYPE type] [PORT n] [BROWSE type] [SECONDS n]`
announces an instance and prints what a browse reports.

`tools/bonami-hosted.sh` runs it on Linux-hosted AROS against the host's
Avahi over a veth pair and checks that each side resolves the other's
service and that the host sees the AROS service leave when its program
only closes the library, that a name the host already has is taken as
"Name (2)", and that each tool does its part: 13 checks. It passed on
2026-10-11 on x86-64. It needs the AROS network
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

Of RFC 6762 and 6763 mdnsd does this much:

- A name is probed three times before it is announced, then announced
  twice. If another host answers for it, the service becomes "Name (2)",
  "Name (3)" and so on, and is probed again. `RegisterServiceA()` returns
  before that; the client is not told the final name, a browse is.
- It answers PTR questions for a type, SRV, TXT and ANY questions for an
  instance, A questions for the host, and the `_services._dns-sd._udp`
  enumeration. A PTR record the asker lists as known is not sent again.
- PTR and TXT records live 75 minutes, SRV and address records two; a
  withdrawn service says goodbye.
- A browse asks after 1, 2, 4... seconds and then once a minute, the first
  time allowing a unicast answer. An instance known only by its PTR record
  is asked for its other records, and a host without a known address for
  its address.

Still missing: defending a name that is already announced, the tie-break
between two hosts probing the same name at once, probing the host name,
known answers in mdnsd's own questions, delayed and aggregated responses,
more than one interface, following an address that changes without a
reconfiguration, and IPv6. The cross-build for AArch64 has not been repeated
since the split.

AROS now provides a clean-room Envoy-compatible `nipc.library` and public
`<envoy/nipc.h>` API. This initial port does not yet build Bonami's optional
inquiry bridge against them: the three NIPC compatibility functions remain in
the ABI and return `MDNSERR_NOTREADY` until that separate integration is
implemented and validated. Core mDNS/DNS-SD does not depend on NIPC.

The protocol limitations and remaining interoperability work are listed in
the parent [README](../README.md).
