# System SoundFonts: analogy with AROS Themes and Theme

**Status:** Architectural proposal for later AROS upstream discussion; not implemented or approved.

## Motivation

SoundFont files should be system-level musical resources, rather than assets owned by MIDIHub or a particular synthesizer. The intended analogy is the AROS distinction between a **Themes** collection and the **Theme** selected for use. Before implementation, verify the exact existing AROS assign names, directory conventions, and theme selection semantics rather than assuming the analogy is literally identical.

## Proposed convention

- **`SoundFonts:`** — a logical AROS assign exposing the collection of installed SoundFont banks (initially SF2, with extensibility for other formats). It is not a new physical disk volume.
- **Active/default SoundFont** — a separate system preference, analogous conceptually to the currently selected theme; consider a singular `SoundFont:` assign only after verifying that the Themes/Theme pattern maps cleanly to a *file* rather than a directory. Do not commit to the singular assign prematurely.
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

1. Inspect AROS's actual `Themes:` / `Theme:` assign and preference implementation, including ENV/ENVARC and fallback behavior.
2. Decide whether a singular `SoundFont:` assign is appropriate for a selected *file*, or whether a named preference plus `SoundFonts:` is more idiomatic.
3. Define precedence for system, user and application defaults; paths, missing files, removable volumes and licensing.
4. Determine whether an AROS SoundFont Preferences UI is warranted or whether the initial integration should use existing preferences.
5. Assess whether `SoundFonts:` should be a multi-assign search path and how duplicate filenames are resolved.
6. Make MIDIHub Synth and a future Radium port independent consumers; test loading the same bank simultaneously without global mutable synth state.

## Next step

Document and verify the real AROS Themes/Theme implementation, then propose the smallest upstream-compatible SoundFont assign and preference convention. **Do not introduce a new system service or synthesizer API solely to provide file discovery.**

Related: [MIDIHub Preferences](preferences.md).
