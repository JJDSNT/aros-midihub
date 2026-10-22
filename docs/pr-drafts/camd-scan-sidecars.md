An icon or debug companion file in `DEVS:Midi` makes `RethinkCAMD()` return
-1 even when every real driver loads. The automatic watcher consequently
retries scans unnecessarily.

Address the [remaining nit from the merged #1732](https://github.com/aros-development-team/AROS/pull/1732#issuecomment-6290911690):
skip files matching `#?.(info|dbg)` before checking or loading a driver.
The pattern is parsed once per scan and matched case-insensitively with the
existing DOS APIs. Directories remain excluded. Other load failures still
return -1, including files such as `device.info.driver` or `device.dbg.bak`.
The `RethinkCAMD()` autodoc lists the ignored files.

This is an independent follow-up on current master, containing one change
to `workbench/libs/camd/rethinkcamd.c`; #1732 is already merged.

Validation:

- Complete CAMD module builds and links pass for hosted x86-64 and
  amiga-m68k through generated makefiles.
- Native hosted UP AROS: 25 checks pass, including lowercase, uppercase and
  mixed-case suffixes, `.info`, subdirectories, invalid actual candidates,
  suffix lookalikes, an actual debugdriver installed beside companion files,
  its published port and a repeated successful scan. The previous #1732
  library fails 11 checks in the same fixture.
- Both native runs complete `Avail FLUSH` using the separate netinfo fix;
  no native fault signatures are detected in their serial logs.

The native fixture/harness is in the companion aros-midihub workspace.
These are isolated module/SDK builds and an existing hosted UP image with
replacement binaries, not a complete current-master rebuild or native
m68k, hardware or SMP execution.
