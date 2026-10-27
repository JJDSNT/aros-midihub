Title: camd: wrap the realtime output ring at its own end

## Scope

`AllocDriverData()` allocates an ordinary output ring and a separate byte
ring for System Real Time messages. It initializes the ordinary ring's end,
then writes that same field a second time with `OUTBUFFERSIZE_RT` and leaves
`bufferend_rt` unset.

`IncBuffer_rt()` can therefore never wrap the realtime read and write
cursors at their allocation's end. After enough realtime traffic both
cursors advance beyond the allocated block and eventually corrupt the port
state.

Initialize `bufferend_rt` from `buffer_rt`, as the existing realtime cursor
helper expects. This changes one assignment in `camd.library`; CAMD remains
version 41.1 and no public API, ABI, structure or behavior contract changes.

## Reproduction and validation

The external CAMD compatibility fixture uses the loopback driver and sends
100000 MIDI Clock messages, then a channel message to prove that the port is
still usable.

On unmodified upstream master `36668e1379`:

- the 100000 realtime messages fail to arrive intact;
- the following channel message also fails;
- the contract run reports 64 passed and 3 failed. Two failures are caused
  by this ring corruption; the third is the separately tracked receive-ring
  overflow case.

With this commit:

- all 100000 realtime messages arrive;
- the following channel message arrives;
- repeated hosted runs report 66 passed and 1 failed. The remaining failure
  is the same independent receive-ring overflow case present in the baseline
  and is outside this one-line change.

Additional validation:

- complete `camd.library` build and link for hosted x86-64;
- complete `camd.library` build and link for amiga-m68k;
- companion repository `make test` passes;
- `git diff --check` passes.

No physical MIDI hardware was used; the defect and regression concern CAMD's
internal driver output ring and use the software loopback driver.
