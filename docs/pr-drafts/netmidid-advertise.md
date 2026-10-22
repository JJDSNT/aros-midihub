AROS PR: [#1743](https://github.com/aros-development-team/AROS/pull/1743), branch `review/netmidid-advertise` (`build/ready-netmidid-advertise`); updated 2026-10-22 after the review, revision `5666327816`.

Publish netmidid's AppleMIDI session as `_apple-midi._udp` through the merged
system `bonami.library`/`mdnsd` registration API. Other DNS-SD clients can
discover the session name and its control port without entering the AROS
address manually.

The daemon owns multicast sockets, probing, conflicts, records and goodbyes;
netmidid only registers its session. It retries late library/daemon
availability, checks its registration at most once per ten seconds and
withdraws it on shutdown. A stale handle after a daemon restart is discarded
and registered again.

**mdnsd: no announcement for an update that changes nothing.** The handle
check is an `UpdateServiceA()` with no tags. On master, `mdnsRegUpdate()`
re-announced an announced service on every update, so that check would have
cost two multicast announcements every ten seconds. The first commit makes
mdnsd announce an update only when it changes the text, port or time to
live (RFC 6762 sections 8.3 and 8.4); an empty update is now only a check
that the daemon still knows the handle. This benefits any client of the API.

**Configuration changes.** A change to `ENV:NetMIDI` begins the sessions
anew, as before, but keeps the registration when the session name and port
are unchanged. Only a registration no longer asked for is withdrawn.

**Refusals.** A registration refused for another reason than an absent
daemon (`MDNSERR_DUPLICATE`, a rejected name, no memory) is retried every ten
seconds and its reason printed once.

The service remains disabled by default (`Enabled=no`), and its file keeps
the description of each key and the warning about `accept`. It starts after
mdnsd. Existing `ENV:NetMIDI` configuration applies, for example:

```ini
session_name=AROS MIDI
local_port=5004
```

MIDI sessions remain usable by IPv4 when the discovery library or responder
is absent. Successful registration is asynchronous; probing may still be
pending. Instance names follow the merged API's current constraints: one
label, at most 63 bytes, without literal dots.

### Scope

Rebased on master, after #1693. Only the AppleMIDI session is advertised;
the Network MIDI 2.0 advertisement (`_midi2._udp`) is a separate change. No
browse or resolve, no Bonami callbacks, dispatcher or event-delivery API.

### Validation

- A host fixture compiling the exact `mdns_reg.c`, `mdns_dns.c`,
  `mdns_host.c`, `mdns_packet.c` and `mdns_client.c` (Exec, clock and
  network mocked) runs 26 RFC cases under ASan/UBSan. The new case, two
  empty updates then a changed port, fails on master (3 announcements for
  the empty updates) and passes here (none, then the announcement for the
  port).
- 27 ASan/UBSan checks compile the exact `discovery.c` with mocked library
  calls: no services, unavailable library/daemon, registration, rate
  limiting, a daemon observed absent, a fast restart, reused foreign
  handles, transient update failure, a configuration that keeps, drops or
  renames the service, a refused registration retried, and idempotent
  cleanup.
- netmidid (all sources, with the rtpmidi and netmidi2 link libraries) and
  mdnsd compile and link for x86-64 and amiga-m68k with `-Werror`.
