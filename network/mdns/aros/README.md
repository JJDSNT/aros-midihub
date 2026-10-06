# Bonami AROS port

This directory contains the native AROS port of `bonami.library`. The sources
are derived from the Bonami revision pinned in `../bonami` and retain its BSD
2-Clause license. They are copied here deliberately so the AROS contribution
can be built and reviewed without requiring a nested checkout.

The port currently provides:

- AROS library entry points for the existing Bonami ABI;
- a shared `mdns.task` using Exec message ports and `bsdsocket.library`;
- registration, browsing, cache-based resolving, and status calls;
- public headers installed by MetaMake; and
- UDP multicast on the standard mDNS port 5353.

Build the library and its small runtime diagnostic from an AROS build tree:

```sh
make contrib-aros-midihub-bonami
make contrib-aros-midihub-mdnsprobe
```

The resulting files are `Libs/bonami.library` and
`Extras/aros-midihub/C/BonamiProbe` in the AROS system tree.

AROS now provides a clean-room Envoy-compatible `nipc.library` and public
`<envoy/nipc.h>` API. This initial port does not yet build Bonami's optional
inquiry bridge against them: the three NIPC compatibility functions remain in
the ABI and return `MDNSERR_NOTREADY` until that separate integration is
implemented and validated. Core mDNS/DNS-SD does not depend on NIPC.

This first port preserves Bonami's single shared engine state. Before it is
installed as a general system service, attachment ownership must be tracked
per client so one caller cannot stop an engine still used by another caller.
The protocol limitations and remaining interoperability work are listed in
the parent [README](../README.md).
