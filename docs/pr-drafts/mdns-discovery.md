DNS-SD clients can discover remote services and resolve their IPv4 endpoint
without executing hooks in mdnsd. It is **ready for review** and proposes a
concrete answer to the event-delivery question in **#1559**. The choice of
wakeup mechanism is left to maintainers: it is isolated in the last commit.

#1591 is merged; this branch is rebased on `master` and contains only the
discovery commits.

### Proposed client contract

- `BrowseServiceTypeA()` and `ResolveServiceA()` require the explicit
  `MDNS_EventMode=MDNS_EVENT_QUEUE` tag and a NULL hook. Existing signatures
  and vector offsets are preserved; hook delivery remains unimplemented.
- Each operation has one bounded event queue. Appended `ReadMDNSEvent(ref,
  event)` copies one complete snapshot into caller storage, in the caller's
  own task.
- Appended `RequestMDNSEvent(ref, request)` is an optional wakeup following
  the device I/O model. The caller lends an `MDNSEventRequest` with its own
  reply port; mdnsd replies to it exactly once: `MDNS_OK` when events are
  waiting, `MDNSERR_BUSY` on overflow, `MDNSERR_CANCELLED` when stopped,
  `MDNSERR_NOENGINE` when mdnsd ends. A caller that already runs a timer loop
  can just read; a caller that should sleep `Wait()`s on its port. Either
  way there is no dispatcher process, no client code in the daemon and no
  direct `Signal()` of a client task.
- `StopBrowse()`/`StopResolve()` synchronously remove operations/queues and
  hand a lent request back before they return. mdnsd orders a stop and an
  event itself, so exactly one reply arrives. On exit mdnsd replies every lent
  request before it removes its public port.
- Ownership uses stable Exec task IDs. Local references retain the daemon's
  task ID, so references from before a restart return `MDNSERR_NOENGINE`
  instead of reaching new operations with reused remote IDs.
- Bounded queues coalesce pending snapshots for the same instance/link.
  Overflow returns `MDNSERR_BUSY`; stop, discard the client's cached view and
  resubscribe. Event/reference outputs are unchanged on failure.
- Snapshots carry an individual instance label, interface/address, port and
  counted binary DNS TXT. They preserve dots/backslashes, 63-byte names and
  MIDI 2.0 endpoint/product metadata without escaped-text expansion.

As with a device, the caller must stop an operation and collect its request
before deleting the port. mdnsd cannot enforce that: a request lent by a task
that has ended, or that closes the library with one still lent, is dropped
unread as a best effort only.

`DISCOVERY.md` specifies the contract, both usage patterns, errors, bounds and
review questions. Version is 41.2; private IPC is version 3. Install both
components together. Generic lookup/query and Envoy remain outside this slice.

### Commits

The queue and its reading are the first five commits. The sixth only renames
the mode tag from `MDNS_EVENT_POLL`, since the queue no longer implies
polling. The seventh adds `RequestMDNSEvent()`; it can be dropped on its own
if maintainers prefer reading alone or a different wakeup.

### Cache and querying

The bounded per-link cache keeps counted labels and expands compression with
bounded traversal. PTR, SRV, TXT and A expire/refresh independently. Browse
queries missing components; resolve works asynchronously without a prior PTR.
Query retries back off and include known answers; goodbye/cache-flush handling
allows a one-second rescue period. Network changes remove old views and
restart querying.

### Validation

On `34e151b6d989eb3acc3d2e3ee29b4d61104060fc`:

- **33/33 host discovery fixtures pass with ASan/UBSan**: labels/order, binary
  TXT, each record's expiry, goodbye/rescue, updates, ownership, stale daemon
  ID, stop/dead clients, overflow, resolve/timeout, network reset, refresh,
  compressed targets, malformed pointers/RDATA, wrong class/source port,
  multiple links, clock wrap and missing-data backoff. Ten of them cover the
  lent request: reply on event, immediate reply when events wait, one reply
  per lend, stop before and after an event, refusal of a second/foreign/stale
  request, overflow, shutdown, release and a vanished owner. Removing the
  stop's reply, the owner check or the immediate reply each fails them.
- **25/25 parent registration/responder fixtures pass**.
- Library and daemon build for **linux-x86_64 and amiga-m68k** using generated
  SDK builds with the warning exclusions recorded for #1591.
- **Native linux-x86_64 hosted with an independently encoded Linux DNS-SD
  peer**, four runs: browse/resolve, counted TXT carrying a 98-byte endpoint
  and 42-byte product ID, unsupported-hook refusal, unchanged failed-read
  output, stop, timeout, daemon exit/restart, stale-handle rejection and a new
  browse. Lent requests: woken by `Wait()` on browse and resolve, second
  request refused, `MDNSERR_CANCELLED` already on the port when `StopBrowse()`
  returns, and `MDNSERR_NOENGINE` already on the port when mdnsd's port is
  gone. Existing UP image with new binaries, not a full current-upstream
  rebuild.

Harnesses are local companion aros-midihub files outside this AROS slice.

### Limits and follow-up

IPv4 only; 512-byte TXT, 128 cached records, 32 views, 16 operations (four per
task), eight snapshots per operation. One address per interface; full multiple
address/SRV priority-weight policy is not included. Local registrations are not
reflected into the discovery cache yet. Generic lookup/query, enumeration,
Envoy, NSEC and larger known-answer continuation are outside this slice. The
public-port race audit from #1591 remains separate. The lent request has been
exercised on uniprocessor hosted AROS only; no native SMP/hardware, Apple
Bonjour or Windows runtime results are claimed.

This PR does **not** enable the current Network MIDI consumer automatically.
N9 must adopt the queue and raw TXT, recover subscriptions and retain full
endpoint names before claiming reliable discovery by name. Whether a queue
with an optional lent request is the right AROS answer remains a maintainer
decision in #1559, not an assumed approval.
