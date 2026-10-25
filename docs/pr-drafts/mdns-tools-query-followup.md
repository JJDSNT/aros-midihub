# AROS PR #1846: complete dnssd record query modes

[AROS #1846](https://github.com/aros-development-team/AROS/pull/1846) is
ready for review, not a draft. Branch `review/mdns-tools-query`, commit
`c6722aee2f`, is one commit directly on AROS `master` `827810f5f1`. It
depends only on the already merged #1803 and #1836, not on the open responder
PR #1845.

## Scope

- Add `dnssd -q name rrtype` on the queue-native `QueryRecordA()`,
  `ReadMDNSRecordEvent()` and `RequestMDNSEvent()` contract. It accepts A,
  PTR, TXT, AAAA, SRV, ANY or a strict numeric 1..65535 type and runs until
  Ctrl-C.
- Add `dnssd -G v4 hostname` through the bounded `LookupHostA()` call.
- Complete `dnssd -Z`: for each resolved browse event, make a bounded SRV
  query, decode its uncompressed wire target, and print faithful PTR, SRV,
  TXT and A lines. The SRV target and A owner are no longer omitted or
  invented from the instance name.
- Check for bonami.library 41.3 before calling its appended LVOs. A 41.2
  installation is refused cleanly instead of calling beyond its vector table.
- Correct the README's stale 41.2/private-IPC-3 statement and document the
  completed commands.

There is no public API, ABI, daemon, library or wire-protocol change. Nearby,
Share and Handshake remain unchanged.

## Validation

- generated `dnssd` builds: linux-x86_64 and amiga-m68k;
- cppcheck warning/performance/portability pass;
- the Codacy scope warning from the first revision is corrected;
- all four GitHub checks pass on `c6722aee2f`;
- Linux-hosted AROS against Avahi: all 19 tool checks pass;
- an independent Linux mDNS peer proves `-q` and `-G` receive A records via
  source port 5353;
- `-Z` prints an SRV target different from the service instance and uses that
  exact target as the A owner;
- numeric type with trailing input is rejected;
- repository `make test` passes.

The zone command still represents the one IPv4 address selected by queued
discovery. IPv6, multiple-address selection and local-registration reflection
remain separate limitations.
