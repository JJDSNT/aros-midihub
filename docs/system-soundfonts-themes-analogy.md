# System SoundFonts: analogy with AROS Themes and Theme

**Status:** Architectural proposal, now grounded in the verified AROS Themes/Theme implementation; SoundFont assigns are not implemented or approved.

## Motivation

SoundFont files should be system-level musical resources, rather than assets owned by MIDIHub or a particular synthesizer. The intended analogy is the AROS distinction between a **Themes** collection and the **Theme** selected for use. The existing AROS Themes/Theme implementation was inspected in the upstream source, as documented below. The SoundFont proposal remains separate from what AROS actually implements.

## Verified AROS precedent (upstream source)

- [Startup-Sequence](https://github.com/aros-development-team/AROS/blob/master/workbench/s/Startup-Sequence) defines `THEMES:` as `SYS:Prefs/Presets/Themes`.
- The same startup script reads `ENV:SYS/theme.var` and assigns `THEME:` to that path; when absent, it falls back to `THEMES:AROSDefault`. If `THEME:Images` exists, it prepends that directory to the `IMAGES:` assign.
- [env-archive/mmakefile.src](https://github.com/aros-development-team/AROS/blob/master/workbench/prefs/env-archive/mmakefile.src) creates the initial `ENVARC:SYS/theme.var` containing `THEMES:<configured-theme>` (for non-classic preference sets), and a `SYS:Prefs/Presets/theme.default` file.
- [Appearance Preferences](https://github.com/aros-development-team/AROS/blob/master/workbench/prefs/appearance/appearanceeditor.c) enumerates and reads theme assets under `THEMES:<name>/...`.

Thus `THEMES:` is a **catalogue of directories**, while `THEME:` is an **assign to the selected theme directory**. This is a verified AROS convention, not merely an analogy.

## Proposed convention

- **`SoundFonts:`** — a logical AROS assign exposing the collection of installed SoundFont banks (initially SF2, with extensibility for other formats). It is not a new physical disk volume.
- **Active/default SoundFont** — store the full path of the selected bank file in `ENV:SYS/soundfont.var` (persistent copy: `ENVARC:SYS/soundfont.var`), e.g. `SOUNDFONTS:GeneralUser-GS.sf2`. This parallels the theme selection variable while respecting the fact that an SF2 bank is a file rather than a theme directory. **No singular `SOUNDFONT:` assign or artificial directory is proposed.**
- **Storage** — keep physical storage configurable; allow system-provided and user-installed banks without requiring all banks to reside inside the MIDIHub package.
- **Selection** — default bank selection is independent of per-application and per-track overrides.

Example conceptual layout:

```text
SoundFonts:
    GeneralUser-GS.sf2
    Orchestral/
        Orchestra.sf2
    Vintage/
        VintageKeys.sf2
```

Names above are illustrative, not a prescribed distribution or claim that these banks are installed.

## Consumer architecture

Applications such as a future AROS Radium port, other DAWs, MIDI players and MIDIHub Synth discover SoundFont files through the same system convention. They remain free to instantiate their own synthesizer engines, with independent audio routing, effects, offline rendering and per-track banks. MIDIHub Synth can use the system default bank while providing a CAMD MIDI endpoint and AHI audio output; it must not become the sole owner of the SoundFont collection.

**Separation of concerns:** `SoundFonts:` is the asset catalogue; CAMD transports/routes MIDI events; TinySoundFont/FluidSynth or another engine renders audio; AHI or the DAW audio engine handles PCM. Sharing SoundFont files does not imply sharing a synthesizer instance.

## Questions for upstream investigation

1. **Verified:** AROS defines `THEMES:` / `THEME:` in Startup-Sequence, reads `ENV:SYS/theme.var`, and falls back to `THEMES:AROSDefault`. Confirm separately how Appearance Preferences writes and applies selection changes at runtime.
2. **Design decision:** use a full file path in `ENV:SYS/soundfont.var` / `ENVARC:SYS/soundfont.var`; do not create a singular `SOUNDFONT:` assign or a wrapper directory.
3. Define precedence for system, user and application defaults; paths, missing files, removable volumes and licensing.
4. Determine whether an AROS SoundFont Preferences UI is warranted or whether the initial integration should use existing preferences.
5. Assess whether `SoundFonts:` should be a multi-assign search path and how duplicate filenames are resolved.
6. Make MIDIHub Synth and a future Radium port independent consumers; test loading the same bank simultaneously without global mutable synth state.

## Next step

Use the verified AROS Themes/Theme precedent to prototype the smallest upstream-compatible SoundFont catalogue and default-bank preference. The file-versus-directory distinction is resolved by storing the complete bank file path in the preference, with no singular assign. Still investigate runtime update semantics, fallback, and missing-file handling. **Do not introduce a new system service or synthesizer API solely to provide file discovery.**

Related: [MIDIHub Preferences](preferences.md).
