# Radium 0.55 — optional AROS integration target

Radium is already ported in the AROS [contrib repository](https://github.com/aros-development-team/contrib/tree/master/MultiMedia/radium).
MIDIHub tracks that repository as a **real Git submodule**, pinned to
`a13eda718af94f383c6ec35bfc7d5a88704ba775`, at `third_party/aros-contrib`.
The submodule references the entire upstream Git repository (a Git submodule
cannot point to a subdirectory). A **local sparse checkout** materializes
only `MultiMedia/radium` plus Git's required top-level files.

## Setup

```sh
bash scripts/setup-radium-dev.sh --aros-tree /absolute/path/to/AROS
```

The script initializes the submodule without checking out all its files,
enables sparse checkout for `MultiMedia/radium`, checks out the pinned
submodule revision, and creates symlinks in the AROS source tree:

- `AROS/contrib/aros-midihub` → MIDIHub working tree
- `AROS/contrib/MultiMedia/radium` → `third_party/aros-contrib/MultiMedia/radium`

Run without `--aros-tree` to prepare the submodule only. Existing source
paths are not overwritten. **Sparse checkout settings are local Git state**;
`.gitmodules` records the repository and pinned gitlink, not sparse patterns.
Re-run the setup script after a fresh clone. Do not run an ordinary full
submodule checkout first if minimizing the working tree is important.

## Source versus distribution

AROS **source** paths are under `contrib/`, not `contrib/extras/`.
The intended **distribution** path is `Extras/MultiMedia/Radium`,
subject to verification of the historical port's installation rules.

## Validation checklist

1. Verify MetaMake discovers both linked source trees.
2. Build `contrib-multimedia-radium` (Radium 0.55; depends on
   `workbench-libs-realtime-linklib`) separately from
   `contrib-aros-midihub`.
3. Test legacy CAMD cluster discovery and MIDI note I/O.
4. Route Radium's MIDI output to MIDIHub Synth and verify AHI audio.
5. Test the system SoundFont path (`ENV:SYS/soundfont.var`, `SOUNDFONTS:`).
6. Verify packaging into `Extras/MultiMedia/Radium`.

No successful build or runtime test is claimed yet. Radium remains optional
and is not a dependency of MIDIHub's default build.
