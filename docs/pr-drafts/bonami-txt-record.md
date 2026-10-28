Title: bonami.library: implement TXT record allocation helpers

## Scope

`MakeTXTRecordA()` and `FreeTXTRecord()` already occupy public Bonami ABI slots
14 and 15. The public header already defines `MDNS_TXTRecord`,
`MDNS_MAX_TXT`, their signatures and error values, but AROS currently keeps
both entries as compatibility stubs: the maker always returns
`MDNSERR_NOTREADY` and the releaser does nothing.

Implement the established Bonami behavior locally in the library. The maker
requires an `MDNS_TXTRecord` string and two output pointers, accepts zero
through `MDNS_MAX_TXT` bytes, allocates a NUL-terminated copy, and returns its
byte length without the terminator. Invalid input and allocation failure leave
both outputs unchanged. The matching function releases that fixed-size
allocation and accepts NULL.

This is one library-only change. It adds no ABI slot, tag, error value, daemon
message or private IPC operation, and it does not need mdnsd to be running.
The library revision becomes 41.4 so clients can distinguish the implemented
helpers from the 41.3 stubs. It is based directly on upstream master
`cbca98214f` and has no dependency on an open pull request.

The implementation follows the existing BSD-2-Clause Bonami source and keeps
the imported amigazen/Jaime Dias notices already present on the source file.

## Reproduction and validation

The external native AROS probe checks NULL arguments, a missing tag, a NULL
tag value, unchanged outputs on every error, empty and ordinary strings, the
exact 256-byte limit, rejection at 257 bytes, and `FreeTXTRecord(NULL)`.

With unmodified upstream master 41.3, the probe reports 1 passed and 10 failed:
only the NULL free is harmless; every maker case still sees the stub.

With this commit, the same probe reports 11 passed and 0 failed on hosted
x86-64. The returned bytes and length match the input and successful buffers
are released through the public function.

Additional validation:

- complete `bonami.library` build and link for hosted x86-64;
- complete `bonami.library` build and link for amiga-m68k;
- the built library identifies itself as 41.4;
- `cppcheck --enable=warning,performance,portability` passes after suppressing
  only its inability to expand `AROS_LIBFUNC_EXIT`;
- companion repository `make test` passes;
- `git diff --check` passes.

No physical m68k or big-endian hardware was used. The hosted regression loads
the real candidate library and calls its public ABI.
