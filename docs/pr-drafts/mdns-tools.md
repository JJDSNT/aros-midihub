AROS PR: [#1803](https://github.com/aros-development-team/AROS/pull/1803), ready for review, branch `review/mdns-tools` (prepared in `build/ready-mdns-tools-review`), opened 2026-10-22.

The command-line tools of the Bonami port, installed in `C:` beside the other
network commands:

- **dnssd**, after Apple's `dns-sd` and Avahi's `avahi-browse`:
  - `-R` registers a service and `-P` registers one for another host;
  - `-B` browses a type and `-L` resolves one instance;
  - `-Z` emits valid PTR/TXT records and reports address and port in a
    comment; the queued event has no SRV target hostname, so it does not
    invent an SRV record or an A owner;
  - `-B _services._dns-sd._udp` lists the service types on the link;
  - `-E`, `-F` and `-V` list the domains and give the library's version.
- **Nearby**, **Share** and **Handshake** say the same in plain words, with
  an alias table ("web", "printer", "ssh", ...): find services (or, without a
  type, the kinds present), advertise one until Ctrl-C, and resolve one by
  name.

They were written for hook delivery. Browse and resolve now use the queued
discovery that was merged (#1695). Each tool sleeps on a request lent with
`RequestMDNSEvent()` and reads the events with `ReadMDNSEvent()` in its own
task, with a time limit from timer.device where one is asked for (`Nearby
TIME`, `Handshake TIME`). A text record is read as it is on the wire, and one
given to register has a space or a backslash inside an item escaped. Numeric
fields and the textual/per-item TXT limits are checked before registration.
Output is streamed from `VNewRawDoFmt()` to dos.library, with no fixed-size
destination buffer and no C library I/O or string functions. The tools open
bonami.library at `BONAMI_LIB_VERSION`, 41. They keep the amigazen copyright
of the original tools, BSD-2-Clause like the library.

**Independent base.** #1802 is merged. The branch was rebuilt on 2026-10-24
as the single tools commit `d758d02a22`, directly over upstream master
`8ded7e1869`; it carries no dependency commits. The tools are presented with
the rest of the Bonami port, for the maintainers to decide whether AROS wants
them.

**Not here yet.** `dnssd -q` (any record) and `-G` (a host's address) need
host lookup and record queries, which come in a later pull request as new
vectors.

Validation:

- Generated MetaMake builds of the four tools for amiga-m68k and hosted
  x86-64, links included, no warnings. cppcheck reports nothing for them.
- Hosted x86-64 AROS with mdnsd and bonami.library from this branch, against
  the host's Avahi advertising "Host Web" (`_http._tcp`, 8080, `path=/`) and
  "Host SSH" (`_ssh._tcp`). Sixteen checks pass:
  - `dnssd -V` names bonami.library 41;
  - `-B _services._dns-sd._udp` lists both types;
  - `-B _http._tcp` gives the instance with its address, port and text;
  - `-Z` gives valid PTR/TXT records, with address and port in a comment and
    no synthetic SRV/A records;
  - `-L` and `Handshake` resolve it;
  - `-L` of an absent name says it was not resolved;
  - `Nearby` lists the kinds, and `Nearby web PORTS` shows the server;
  - overflowing or partially numeric ports and TTLs, an oversized TXT item,
    and port zero are rejected before registration;
  - the host's Avahi sees `Share`'s `AmigaShare` with its text (a space
    inside an item kept) and `dnssd -R`'s `AROS Printer`.

One preceding repetition reproduced MD20: Avahi saw both PTRs from AROS but
timed out resolving their instances. Both programs had registered, the next
run and the final exact binaries passed all sixteen checks, and the runner now
keeps the mDNS packet capture for a recurrence.
