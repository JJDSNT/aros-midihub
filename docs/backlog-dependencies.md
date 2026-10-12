# What depends on what across the backlogs

One graph over the five lists of work owed:
[CAMD](camd-backlog.md) (B), [RTP-MIDI](rtp-midi-backlog.md) (R),
[AppleMIDI](applemidi-backlog.md) (AM), [UMP](ump-backlog.md) (UP) and
[Network MIDI 2.0](network-midi2-backlog.md) (NM). Written on 2026-10-12
from those files; when an item is added, closed or renumbered there, change
it here in the same commit.

An arrow means "has to be done first". Only hard dependencies are drawn: an
item that is merely better done after another is not. Items with no arrow
in or out are not in the picture; they are listed under "Free" below.

## The picture

```mermaid
flowchart LR
    classDef outside fill:#fde2e2,stroke:#b42318,color:#000
    classDef decision fill:#fff3c4,stroke:#946200,color:#000
    classDef ready fill:#d9f2e3,stroke:#1a7f4b,color:#000

    subgraph OUT[Outside our hands]
        SPEC[/"Specification texts<br/>M2-104-UM, M2-124-UM"/]:::outside
        I1559[/"AROS issue #1559<br/>IPv6 answer"/]:::outside
        P1507[/"AROS PR #1507<br/>yes to a driver-side block"/]:::outside
    end

    subgraph DEC[Decisions to take]
        D1{{"D1 who owns<br/>the sockets"}}:::decision
        D2{{"D2 peers to ports"}}:::decision
        D4{{"D4 no time across<br/>the driver"}}:::decision
        ND1{{"NM D1 one driver<br/>or two"}}:::decision
    end

    subgraph RTP[RTP-MIDI]
        R02["R02 journal that<br/>does not fit"]:::ready
        R11["R11 several commands<br/>in a packet"]
        R17["R17 latency<br/>against jitter"]:::ready
        R21["R21 fuzzing<br/>and captures"]
        R23["R23 enhanced<br/>chapter C"]
    end

    subgraph AM[AppleMIDI]
        AM02["AM02 clock object<br/>per peer"]:::ready
        AM05["AM05 several peers<br/>on AROS"]
        AM06["AM06 run against<br/>rtpmidid"]:::ready
        AM07["AM07 who may<br/>connect"]
        AM08["AM08 DEVS:Midi<br/>driver"]
        AM09["AM09 register through<br/>system mDNS"]:::ready
        AM10["AM10 browse<br/>for peers"]
        AM11["AM11 clock<br/>intervals"]
        AM12["AM12 reconnection"]
        AM14["AM14 IPv6"]
        AM15["AM15 table size"]
        AM16["AM16 configuration"]
        AM19["AM19 fuzzing<br/>and captures"]
        AM20["AM20 no command<br/>line"]
    end

    subgraph UMP[UMP]
        UP01["UP01 check against<br/>the text"]
        UP03["UP03 SysEx arrives<br/>in pieces"]:::ready
        UP05["UP05 count what<br/>is dropped"]:::ready
        UP06["UP06 other groups"]
        UPSPEC["UP04 UP07 UP08 UP10<br/>UP13 UP17<br/>rules from the text"]
        UP09["UP09 JR timestamps"]
        UP11["UP11 translate up<br/>to MIDI 2.0"]
        UP12["UP12 one table with<br/>camd.library"]
        UP16["UP16 move sources"]
    end

    subgraph NM[Network MIDI 2.0]
        NM01["NM01 check against<br/>the text"]
        NM02["NM02 session knows<br/>its peer"]:::ready
        NMSPEC["NM05 NM12 NM20<br/>rules from the text"]
        NM04["NM04 act on a<br/>long loss"]
        NM06["NM06 table of<br/>sessions"]
        NM07["NM07 a runtime"]
        NM08["NM08 discovery"]
        NM09["NM09 timed delivery"]
        NM10["NM10 run against<br/>another implementation"]
        NM11["NM11 reach CAMD<br/>through a driver"]
        NM13["NM13 retransmission"]
        NM14["NM14 MIDI 2.0<br/>protocol"]
        NM15["NM15 IPv6"]
        NM16["NM16 ping identifier"]
        NM17["NM17 vectors, fuzzing,<br/>captures"]
    end

    subgraph CAMD[CAMD]
        B29["B29 identity<br/>per port"]
        B30["B30 presence<br/>per port"]
        B31["B31 name<br/>per port"]
        B32["B32 UMP and time<br/>from a driver"]
    end

    R02 --> R11
    AM06 --> R21
    AM06 --> R23
    AM06 --> AM19

    AM02 --> AM05
    AM02 --> AM11
    AM05 --> AM07
    AM05 --> AM08
    AM05 --> AM12
    AM05 --> AM15
    AM07 --> AM16
    R17 --> AM16
    AM09 --> AM10
    AM10 --> AM12
    AM08 --> AM20
    D1 --> AM08
    D2 --> AM08
    D4 --> AM08
    D2 --> AM16
    I1559 --> AM14
    I1559 --> NM15

    SPEC --> UP01
    SPEC --> NM01
    UP01 --> UPSPEC
    UP01 --> UP09
    UP01 --> UP11
    UP01 --> UP12
    UP12 --> UP16
    UP05 --> UP06

    NM01 --> NMSPEC
    NM01 --> NM04
    NM04 --> NM13
    NM02 --> NM06
    NM02 --> NM16
    NM06 --> NM07
    AM05 --> NM07
    NM07 --> NM08
    AM10 --> NM08
    NM07 --> NM09
    UP09 --> NM09
    R17 --> NM09
    NM07 --> NM10
    NM10 --> NM17
    NM01 --> NM17
    NM07 --> NM11
    AM08 --> NM11
    UP03 --> NM11
    ND1 --> NM11
    UP11 --> NM14
    B32 --> NM14

    P1507 --> B29
    B29 --> B30
    B29 --> B31
    B31 --> B32
    UP01 --> B32
```

Green can be started today. Red is not ours to move. Yellow is a decision
that is the project owner's.

## What the graph says

Of 78 open items, 32 have nothing in front of them, 30 sit behind something
outside our hands or behind a decision, and 16 wait only for other work of
ours.

**What opens the most**, counting everything that follows directly or
through other items:

| Do this | It opens | Which is |
|---|---|---|
| Have the specification texts | 21 | All of UMP's and Network MIDI 2.0's conformance work, and CAMD's B32 |
| AM02, the clock object | 14 | Several peers, and through them the driver, the accept rule, reconnection, configuration, and Network MIDI 2.0's whole runtime |
| UP01, check UMP against the text | 13 | The rest of UMP, B32, timed delivery and the MIDI 2.0 protocol over the network |
| AM05, several peers on AROS | 12 | The same as AM02 less the clock intervals |
| NM02, the session knows its peer | 8 | The session table, the runtime and everything run on it |
| A yes in PR #1507 | 5 | B29 to B32 and, last, the MIDI 2.0 protocol over the network |
| D2, peers to ports | 4 | The driver, configuration, and Network MIDI 2.0's way into CAMD |

**The longest chain of our own work** is five steps:
AM02 → AM05 → NM07 → NM10 → NM17. It starts in AppleMIDI and ends in
Network MIDI 2.0, which is why AppleMIDI goes first: its runtime is the one
the other transport is built beside.

**Three things outside our hands hold the rest:**

- *The specification texts* hold the most and cost the least to get: they
  are documents to obtain and read, not answers to wait for.
- *PR #1507* holds only CAMD's driver-side block. Nothing in RTP-MIDI or
  AppleMIDI waits for it.
- *Issue #1559* holds only the two IPv6 items.

**The decisions** hold little now and much later. D1, D2 and D4 are needed
by one item, the driver (AM08), and that item is three steps into
AppleMIDI. They can wait until AM05 is done without delaying anything.

## Free: nothing before, nothing after

These can be done in any order and at any time. Their priority comes from
their own backlog, not from this graph.

| Backlog | Items |
|---|---|
| RTP-MIDI | R01, R04, R14, R18, R19, R20, R22, R24, R25 |
| AppleMIDI | AM01, AM03, AM04, AM13, AM17, AM18, AM21 |
| UMP | UP02, UP14, UP15 |
| Network MIDI 2.0 | NM03, NM18, NM19 |
| CAMD | B16, B22 |

R01, R04, AM01, AM03, AM04, UP02 and NM03 are P0 in their own lists: defects
in code as it is. That they open nothing else is no reason to leave them.

## An order that follows from it

1. **The P0 defects that are free**, on portable code with host tests: R01,
   R02, R04, then AM01, AM03, AM04.
2. **AM02**, the clock object, because of what it opens; then **AM06**, the
   run against rtpmidid, before building further on code that has met no
   other implementation.
3. **AM05**, several peers. Here the driver's decisions (D1, D2, D4) become
   due.
4. **AM08**, the driver, and **AM09**/**AM10**, discovery through the system
   service.
5. In parallel with any of the above, whenever the texts are at hand:
   **UP01** and **NM01**. They are reading, they need nothing from AROS, and
   between them they open a quarter of everything here.
6. **Network MIDI 2.0's runtime** (NM02, NM06, NM07) after step 3.

CAMD's B16 and B22 fit anywhere. B29 to B32 wait for the maintainer; opening
the issue with the sketch of the driver-side block, which was promised in
PR #1507, is the one step there that is ours.

## Soft dependencies, not drawn

- AM06 is better after R01 and AM01 to AM03: a peer that sends a header we
  reject, or padding we refuse, would fail for reasons already known.
- AM09 can be done now against the `bonami.library` in this repository; its
  final form waits for how service registration enters AROS (issue #1559,
  PR #1560).
- AM03 and AM18 want the counters of R18 to be visible, but can be done
  with their own.
- R11 and R17 are worth doing whichever way D4 goes.
