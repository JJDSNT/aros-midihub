# Radium 0.55 as an optional AROS integration test

The historical AROS port of Radium 0.55 lives in
[aros-development-team/contrib/MultiMedia/radium](https://github.com/aros-development-team/contrib/tree/master/MultiMedia/radium).
It uses AROS MetaMake and CAMD. This repository does **not** vendor Radium,
add the entire contrib repository as a submodule, or add Radium to MIDIHub's
default build.

## Prepare source trees

Run from an existing MIDIHub checkout:

```sh
bash scripts/setup-radium-dev.sh --aros-tree /absolute/path/to/AROS
```

The script creates a sibling checkout named `aros-contrib-radium` (override
with `--workspace /path/to/workspace`), enables sparse checkout for
`MultiMedia/radium`, and checks out the pinned contrib commit
`a13eda718af94f383c6ec35bfc7d5a88704ba775`. It creates symlinks:

- `AROS/contrib/aros-midihub` → the MIDIHub checkout.
- `AROS/contrib/MultiMedia/radium` → the Radium source directory within
  the sparse contrib checkout (its original MetaMake path).

The source tree is `contrib/`; the *distribution* directory is `Extras/`.
The desired Radium distribution placement is `Extras/MultiMedia/Radium`,
subject to verifying its existing package/install rules. Do not confuse this
distribution path with the source-tree symlinks.

Without `--aros-tree`, the script only prepares the sparse checkout.
It refuses to overwrite existing paths or dirty Radium working trees. Run
from a shell with Git and network access; a sparse checkout still needs
Git metadata and may fetch history/objects.

## Build and validation

The existing Radium target is `contrib-multimedia-radium` and depends on
`workbench-libs-realtime-linklib`; its `mmakefile.src` includes
`config/aros-contrib.cfg`. The MIDIHub target is
`contrib-aros-midihub` and includes `config/aros.cfg`. Both rely on
AROS MetaMake, but target discovery and prerequisites must be verified
against the actual AROS build checkout before claiming a successful build.

Initial validation:
1. Verify MetaMake discovers both linked projects.
2. Build Radium independently on the intended AROS target; record compiler,
   linker, realtime.library, and CAMD errors.
3. Launch Radium and inspect legacy CAMD cluster discovery and note I/O.
4. Route its MIDI output to the MIDIHub synthesizer and verify AHI audio.
5. Verify system SoundFont path selection (`ENV:SYS/soundfont.var` and
   `SOUNDFONTS:`) without coupling Radium to MIDIHub internals.
6. Check packaging separately for `Extras/MultiMedia/Radium`.

This is a test harness, **not** a claim that Radium already builds or runs
on the current AROS tree. If substantive port changes become necessary,
consider extracting the port into a separate repository then.
