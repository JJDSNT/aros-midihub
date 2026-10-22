On the event-delivery question: #1695 now proposes a concrete answer, so it
can be judged as code rather than in the abstract.

1. **One bounded queue per browse/resolve, read in the client's own task**
   with `ReadMDNSEvent()`. mdnsd never runs a Hook, and the library starts no
   dispatcher process.
2. **An optional wakeup in the device I/O style.** `RequestMDNSEvent(ref,
   request)` lends mdnsd a message with the client's own reply port; mdnsd
   replies exactly once when events are waiting, on overflow, on stop or when
   it exits. The client `Wait()`s on its port and then reads. mdnsd never
   calls `Signal()` on a client task and keeps no task pointer.
3. **Stop works like `AbortIO()` + `WaitIO()`**: the request is back on the
   client's port before `StopBrowse()`/`StopResolve()` returns.

A client with its own timer loop can ignore (2) and just read. The original
Bonami Hook signature is kept but refused with a NULL-hook requirement.

The wakeup is a separate commit, so the queue can be accepted without it if
you prefer another mechanism. Two questions where your preference decides:

- Is a lent request acceptable here, or would you rather have a signal bit
  or an existing AROS notification mechanism?
- Should the Hook path stay refused, or eventually get a library-side
  dispatcher for Bonami source compatibility?
