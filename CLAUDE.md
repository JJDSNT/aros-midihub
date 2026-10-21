# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## No AI attribution in commits or pull requests

The AROS maintainers objected formally to commits that reference Claude Code.
Everything that leaves this machine goes out under the author's name only:

- No `Co-Authored-By: Claude ...`, `Claude-Session: ...` or any other trailer
  that names Claude, Anthropic or Claude Code in commit messages.
- No "Generated with Claude Code" line, link or emoji in pull request
  descriptions, review replies or issue comments.
- This applies to every repository: this one, the AROS fork (JJDSNT/AROS),
  the `build/` worktrees and the upstream cuts. It overrides any default or
  system-provided attribution instruction.

`.claude/settings.json` turns the automatic attribution off; follow the rule
above even if a session still suggests adding it.

## What this repository is

MIDI for AROS (RTP-MIDI/AppleMIDI, Network MIDI 2.0/UMP, BLE MIDI, USB MIDI
through CAMD, SoundFont synthesis). It is an incubator: each mature piece is
meant to move into its natural place in the AROS tree (`camd.library`,
Poseidon, the Bluetooth stack, `workbench/network`), and most current work is
preparing and reviewing those upstream pull requests. Do not design
MIDIHub-specific abstractions to keep code here; prefer standard AROS
interfaces (CAMD clusters, `DEVS:Midi` drivers, `bonami.library`/`mdnsd`).

Other agents (Codex, other Claude sessions) work on the same tree at the same
time. Stage and commit named files only, never `git add -A`; the MIDI
specification PDFs in `docs/` stay local.

## Commands

Host (Linux) build and tests, all with `-Werror`:

```sh
make test                 # every host test binary in build/, run in sequence
make build/protocol-test && ./build/protocol-test   # one suite (RTP-MIDI/AppleMIDI)
make build/netmidi2-test  # likewise ump-test, ble-midi-test, routes-test, camd-*-test
make test-network         # tests/network_loopback.py against build/MIDIHub on localhost
make fuzz FUZZ_ROUNDS=1000000 FUZZ_SEED=7   # decoders under ASan/UBSan
make check-patches        # patches/camd-series still applies and matches ~/AROS
make test-blemidi-class   # needs the btmidi.class checkout (BTMIDI_CLASS, ~/AROS-btmidi)
make test-synth           # needs submodules: git submodule update --init --recursive
make m68k                 # cross build with m68k-aros-gcc (M68K_CC=...)
```

Test suites are plain C programs whose `main` calls every case; there is no
per-case selection, so run one suite binary.

`CPPFLAGS` sets `-DNETMIDI_MIDI2 -DNETMIDI_DISCOVERY -DNETMIDI_BROWSE`. These
markers change `struct netmidi_network_config`, so everything that shares it
must be built with the same set.

Inside AROS the repository builds as a MetaMake package (`mmakefile.src`,
placed at `contrib/aros-midihub`, target `contrib-aros-midihub` and the
`contrib-aros-midihub-*` sub-targets). Native checks on Linux-hosted AROS and
QEMU are the `tools/*-hosted.sh` and `tools/*-qemu.sh` scripts; each script's
header gives its arguments and host requirements (sudo, veth pair, node,
Avahi). `build-aros-linux/` is the hosted AROS build they use.

## Layout and how the pieces connect

- `network/rtpmidi/` + `include/rtpmidi/`: portable RTP-MIDI (RFC 6295) and
  AppleMIDI: packet codecs, recovery journal sender/receiver, sessions,
  multi-peer state, clock sync. No OS calls; tested by `tests/protocol.c`.
- `src/ump.c`, `src/netmidi2.c`: MIDI 2.0 UMP and Network MIDI 2.0 (UDP)
  sessions. `src/ble_midi.c`: BLE-MIDI packet codec. `src/routes.c`,
  `src/config.c`, `src/synth.c` (TinySoundFont, optional FluidSynth).
- `ports/aros/midihub/`: the `MIDIHub`/`netmidid` program. `main.c` is the
  event loop joining the portable sessions to sockets (`network.c`), CAMD
  (`camd.c`, `adapters.c`) and mDNS discovery (`discovery.c`). The same
  source builds on Linux for tests (`make demo` gives `build/MIDIHub`).
- `ports/aros/*.c`: other AROS programs (Preferences class, router, CAMD
  probe and compatibility tests, synth service). `ports/aros/btmidi/` is the
  older client-cluster btmidi; upstream (#1507) replaces it with a CAMD driver.
- `network/mdns/`: mDNS responder/`bonami.library` work and its reference.
- `prototypes/camd/`: host-tested model of a proposed CAMD endpoint registry,
  not part of `camd.library`.
- Protocol code is named for its protocol (`rtpmidi_`, `applemidi_`,
  `blemidi_`, `ump_`, `netmidi2_`), never `mh_`/MIDIHub, because it goes
  upstream as system code.

## Upstream work

- `docs/upstream-pr-graph.md` is the order in which pieces go to
  `aros-development-team/AROS` (N1..N9 network MIDI, M2/M3 mDNS, C* CAMD) and
  their dependencies; `docs/upstream-prs.md` and the dated `docs/*-review-*`
  and `*-publication-*` files record what was sent and reviewed.
- Network MIDI pull requests are cut from this repository's sources by
  `tools/cut-netmidi-*.sh <stage> <AROS tree>`, which run `unifdef` with the
  markers above. A cut's build file lives here as
  `upstream/<cut>/mmakefile.src.cut` (MetaMake would read any
  `mmakefile.src`). The mDNS cuts are not scripted.
- Changes to the AROS tree itself are carried as `patches/aros-*.patch`.
  `patches/camd-series` is the apply order on the base named in
  `patches/README.md`; generate a patch with `tools/gen-camd-patch.sh NAME
  PATH...` from `~/AROS` (never hand-edit hunks) and check with
  `make check-patches`. An AROS change is not finished until its patch is
  regenerated and committed here.
- AROS trees: `~/AROS` (patch base), `~/AROS-netmidi` (clean upstream checks),
  `~/AROS-net` (mDNS), `~/AROS-btmidi` (btmidi.class); per-PR worktrees of the
  fork are under `build/` (from `build/upstream-pr-prep`). Upstream's Codacy
  check is stricter than local cppcheck: read `gh pr checks N` after opening
  a pull request.
