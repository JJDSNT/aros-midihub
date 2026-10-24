AROS PR: [#1802](https://github.com/aros-development-team/AROS/pull/1802), merged 2026-10-24 as `02175e4765` after the review fixes, branch `review/mdns-service-types` (`build/mdns-types`), opened 2026-10-22.

A browse of `_services._dns-sd._udp` (`MDNS_TYPE_SERVICES_DNS_SD`) is now told
the service types present on the link (RFC 6763 section 9). Before, it was
refused with `MDNSERR_BADTYPE`.

mdnsd already answered this question for the services registered on the
host (`mdns_reg.c`). This is the asking side, which the Bonami port had as
its "meta browse" and which #1695 listed among what it left for later.

**How it fits the queued discovery.** The PTRs of that name point at types,
not instances. Each type becomes one snapshot: `de_Type` holds the type (as
`"_http._tcp"`), `de_Interface` the link, and the name, address, port and
TXT are empty. Nothing is resolved for a type. `MEVENT_ADD` comes when a type
is first seen, and `MEVENT_REMOVE` when its last PTR expires or is withdrawn,
with the same grace a goodbye has elsewhere. A PTR there whose target is not
a type (an instance, a name without `_tcp` or `_udp`) is left out. A resolve
of that name is still refused with `MDNSERR_BADTYPE`. Other browses and
resolves are unchanged. Type snapshots share the 32 views and their bounds
with instances. `DISCOVERY.md` and `README.md` describe it.

Validation:

- The host discovery fixture compiles this `mdns_discovery.c` with the
  daemon's packet path and IPC dispatch. A new case covers: two types
  announced give two `MEVENT_ADD`; a repeat gives nothing; an instance and a
  non-type PTR are ignored; an instance browse alongside is unaffected; a
  goodbye gives `MEVENT_REMOVE`; a resolve of the name is refused. After the
  2026-10-24 review it also proves that type targets cause no SRV/TXT queries
  and that a second browse receives the cached types immediately. The updated
  case fails on `fbe98e714b`; all 38 discovery cases pass under ASan/UBSan on
  `263a854570`.
- mdnsd builds and links for hosted x86-64 and amiga-m68k.
- Hosted x86-64 AROS against the host's Avahi, with the command-line tools
  of the follow-up pull request: `dnssd -B _services._dns-sd._udp` lists the
  `_http._tcp` and `_ssh._tcp` the host advertises, and `Nearby` with no type
  names them as "Web server (HTTP)" and "SSH login".
