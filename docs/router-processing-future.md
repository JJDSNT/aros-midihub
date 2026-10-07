# Future MIDI Router processing pipeline

## Status

**Future investigation / out of current scope.**

The current MIDIHub routing implementation should remain deliberately simple.
This document records a possible later evolution so that today's architecture
does not unnecessarily prevent it.

## Idea

A future routing application could allow optional MIDI processors to be placed
between an input endpoint and an output endpoint:

```text
Input endpoint
      |
      v
[ MIDI processing pipeline ]
      |
      +-- Channel filter
      +-- Transpose
      +-- Velocity curve
      +-- CC / Program remap
      +-- MIDI 1.0 <-> UMP translation
      +-- Clock processing
      +-- Quantize / humanize
      +-- Arpeggiator / chord mapping
      +-- protocol- or SysEx-specific processors
      |
      v
Output endpoint
```

A possible route model would therefore evolve from:

```text
Route = Source -> Destination
```

toward:

```text
Route = Source -> Processor[] -> Destination
```

The word *DSP* may be used informally for this concept, but the initial idea is
**MIDI event/data processing**, not audio DSP. Audio processing remains outside
the MIDI router and belongs to the appropriate AROS audio infrastructure.

## Why this probably becomes an application

`MIDIHub.prefs` should remain a Preferences-style surface for system-level
configuration, overview, profiles, and basic persistent routing. Building and
editing processing chains is an interactive task and would eventually exceed
the intended scope of a Preferences program.

If this direction proves useful, the likely UI split is:

```text
MIDIHub.prefs
    |
    +-- endpoint overview
    +-- basic persistent routes
    +-- network MIDI configuration
    +-- synth configuration
    +-- profiles

MIDIHub Router application
    |
    +-- route editing
    +-- processor/filter chains
    +-- richer routing visualization
    +-- processor parameters
    +-- monitoring/debugging of a pipeline
```

The exact product name is intentionally not fixed yet.

The application would be an **editor/controller**, not the owner of the live
route. Closing it should not stop MIDI. Persistent execution should remain in a
resident routing service or, if routing ownership later moves into CAMD, in the
corresponding CAMD-owned facility.

```text
MIDIHub Router app
       |
       | edits desired pipeline
       v
resident router / future CAMD-owned service
       |
       | executes Source -> Processor[] -> Destination
       v
      CAMD
```

## Architectural implication for current work

This is **not** a requirement to implement processors now. The important
near-term consideration is to avoid unnecessarily freezing the route model or
runtime around assumptions that make an ordered processing chain difficult to
add later.

In particular, future design reviews should consider whether:

- route persistence can be versioned and extended without breaking existing
  simple route files;
- the runtime can represent zero or more processors between endpoints;
- processors can have independent configuration/state;
- a processor API could eventually be modular or extensible;
- latency, ordering, timestamp preservation, SysEx, MIDI 1.0 and UMP semantics
  remain well defined through a chain;
- legacy CAMD applications can continue to see ordinary CAMD endpoints without
  understanding the processing model.

A simple route must remain the trivial case:

```text
Source -> [] -> Destination
```

so this future direction does not require breaking the current basic router.

## Relationship to CAMD

CAMD remains the standard MIDI graph and application-facing infrastructure.
This proposal should not create a competing MIDI API.

The future Router application would instead be a specialized CAMD application
that uses CAMD-visible endpoints and provides a modern processing/routing layer
above them. This may also become one place where modern capabilities can be
made useful to legacy CAMD software while preserving compatibility.

If CAMD itself later gains appropriate persistent routing or processing
facilities, ownership should be reconsidered rather than duplicating them in
MIDIHub.

## Relationship to MIDIHub.prefs

This proposal does not change the current `MIDIHub.prefs` scope.

The Preferences UI should continue to favor **overview and basic control**.
Advanced transformations, processor chains, scripting, keyboard splits,
graphical processing nodes, and similar workstation-like functionality remain
outside the Preferences application.

The future Router application is the possible home for those capabilities.

## Decision for now

1. Keep the existing basic router and `MIDIHub.prefs` focused.
2. Treat processing/filter pipelines as a future investigation.
3. Preserve an architectural path from
   `Source -> Destination` to
   `Source -> Processor[] -> Destination`.
4. If a rich pipeline editor is implemented, prefer a dedicated Router
   application rather than expanding `MIDIHub.prefs` into a workstation.
5. Keep runtime execution independent from whichever GUI edits the routes.
