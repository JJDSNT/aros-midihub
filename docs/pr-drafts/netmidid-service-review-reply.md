<!-- Reply to Kalamatee's review of #1679 (2026-10-22), posted with the push of 8939c84655. -->

Thank you. Fixed in `8939c84655`:

- Each periodic turn now calls `applemidi_peers_expired()` with
  `APPLEMIDI_CLOCK_TIMEOUT` and ends the peer it names, whichever side
  invited. A configured peer that vanishes without BY is then invited
  again by the existing back-off.
- The peer table is now given `(uint32_t)now`, in 1/10000 s as `peers.h`
  defines, instead of `now / 10`. That applies to the invitation, `seen`
  and `receive` calls.
- With `ENV:netmidid.log` set, stderr goes to the same file after
  stdout, so configuration and bind errors are logged.

The hosted node-rtpmidi suite has two new cases: the configured peer is
killed without BY and comes back after the timeout (the service expires
it and has a second session), and a configuration error written to stderr
shows up in `netmidid.log`. The suite passes, and the revision builds and
links for x86_64 and m68k with `-Werror`. #1693, #1742 and #1743 are
rebased onto it.

The inline nits don't show up on my side: there is no submitted review
with line comments on this PR. Could you submit the pending review so I
can address them?
