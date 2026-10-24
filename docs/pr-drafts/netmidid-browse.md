AROS PR: [#1801](https://github.com/aros-development-team/AROS/pull/1801), merged 2026-10-24 as `14b571dc20`, branch `review/netmidid-browse` (prepared in `build/ready-netmidid-browse-review`), opened 2026-10-22.

With the queued DNS-SD discovery that bonami.library 41.2 has (#1695),
netmidid can find what its configuration names wherever it is on the link,
for both of its transports, and it advertises its Network MIDI 2.0 Host.

**AppleMIDI, a peer by name.** `peer_name` in `ENV:NetMIDI` is a session to
invite wherever it is advertised as `_apple-midi._udp`. It is invited when it
appears, again more slowly while it does not answer (5 s doubling to a
minute), from the first wait again after a session, and forgotten when its
advertisement leaves.

**Network MIDI 2.0, a Host by name.** `midi2_peer_name` is a Host to be the
Client of, found by the `UMPEndpointName` of its text record (or its
instance name), as M2-124-UM section 4 has a user choose a Host.

**The MIDI 2.0 Host advertised.** The service's own Host is registered as
`_midi2._udp` with `UMPEndpointName` and `ProductInstanceId` in its text
record and a 60 s time to live (M2-124-UM 4.4, 4.6).
`netmidi_discovery_add_text()` and `netmidi_discovery_item()`, which #1743
dropped while nothing used them, come back here to write that record.

**How it uses the library.** Each search has an event queue, read in the
service's own task from the loop it already runs, at most once a second:
each read is a round trip to mdnsd. No hook runs in mdnsd. An overflowed
queue (`MDNSERR_BUSY`) or a search that is no longer ours is stopped and
begun again; when mdnsd ends, every registration and search is asked for
again once it is back. The text record is read as it is on the wire,
length-prefixed strings, keys without regard to ASCII case.

### Base

#1788 and #1743 are merged. The branch was rebuilt on 2026-10-24 as the
single proposal commit `1ff702b743`, directly over upstream master
`467f8fe329`; it carries no dependency commits. Its stable patch id is the
same as the former stacked commit `255496d21f`.

### Validation

- Generated MetaMake builds for amiga-m68k and hosted x86-64, links
  included: the Bonami includes, `linklibs-netmidi-common`,
  `linklibs-rtpmidi`, `linklibs-applemidi`, `linklibs-netmidi2`,
  `workbench-network-netmidid` and `workbench-network-netmidistat`.
- Hosted x86-64 AROS with mdnsd and bonami.library built from master and
  the netmidid of this branch's generated build,
  against the host's Avahi, node-rtpmidi 1.0.0 and a Network MIDI 2.0
  endpoint built from the same netmidi2 library:
  - the AppleMIDI session advertised, renamed through `ENV:NetMIDI`, and
    withdrawn with a goodbye;
  - a session wanted by name found, invited, seen leaving and returning,
    and invited again;
  - after four unanswered invitations, a session that connects and ends
    with BY while still advertised is invited again from the first wait,
    within 22 s; the same run fails with that reset taken out;
  - a Host wanted by its endpoint's name found and joined as its Client,
    with MIDI 1.0 messages and SysEx through CAMD both ways;
  - a Host that ends the session with Bye after four unanswered
    invitations invited again within 22 s; fails without the reset;
  - AROS's own Host resolved by Avahi as `_midi2._udp` with its endpoint
    name and product id in the text record.
- Host fixtures compile this branch's exact `discovery.c` against mocked
  library calls under ASan/UBSan: 24 browse checks (queue reading, rate,
  overflow, daemon restart, a new run, wire TXT parsing) and 27 for
  registrations. The repository's default fixture run and full `make test`
  pass too.
- cppcheck reports nothing for the changed files.

The final x86-64 `netmidid` and bonami.library binaries after the rebase are
byte-for-byte identical to the binaries used by the full hosted runs. A
fresh MIDI 2.0 message/discovery run also passed with mdnsd rebuilt from the
final master, including #1802.
