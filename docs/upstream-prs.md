# Upstream pull requests

Estado inicial consultado no GitHub em **2026-10-22, 07:51 UTC (04:51 BRT)**,
para PRs de `JJDSNT` contra `aros-development-team/AROS`.
Naquele momento eram **24 merged, 7 abertos (3 prontos para review e 4 drafts), 1 fechado sem merge**.
“Accepted” neste inventário significa efetivamente merged; checks aprovados
não equivalem a aprovação de review.

O [registro de situação, dependências e propostas bloqueadas](upstream-status-2026-10-22.md)
distingue pré-requisitos publicados, decisões dos mantenedores e nossa
coordenação da migração #1788. Na consulta inicial, todos os sete PRs abertos
estavam mergeable e com os quatro checks aprovados; nenhuma revisão formal
ou thread inline aparecia neles.

Após a abertura do #1789 no mesmo dia, são **24 merged, 8 abertos
(4 prontos para review e 4 drafts) e 1 fechado sem merge**. Os oito estão
`MERGEABLE` e `REVIEW_REQUIRED`. Os quatro checks do #1789 passaram; os
checks dos outros sete permanecem documentados pela consulta inicial.

## AROS pull request policy (2026-10-23/24)

The AROS maintainer moved the contribution process into [PULL_REQUESTS.md](https://github.com/aros-development-team/AROS/blob/master/PULL_REQUESTS.md) in [commit a8fcffdf](https://github.com/aros-development-team/AROS/commit/a8fcffdf160964b675da81fd3398f9ec972ba51e) and tightened it in [commit 4269a3ac](https://github.com/aros-development-team/AROS/commit/4269a3acc381002e340971d36c0e11c074783f8e).

**Submission gate for all future Midihub/Bonami/CAMD upstream PRs:**

1. **No draft PRs.** Keep unfinished work on local/fork branches and in this repository's backlog; submit only complete changes ready for review and inclusion. Existing draft PRs require reassessment against the new policy; do not simply mark incomplete work ready.
2. **No unaccepted PR dependencies.** A submitted PR must stand on its own against upstream master. Wait for prerequisite PRs to be merged before opening dependent work; if changes cannot reasonably be separated, submit a single coherent PR. Limit scope to one logical change and one component/subsystem; explain any essential cross-component edits.
3. **Licensing and notices.** AROS-specific original code must use the standard `Copyright (C) 1995-<year>, The AROS Development Team. All rights reserved.` notice, not a personal copyright. Preserve original notices and licenses for imported/derived code (especially Bonami); disclose origin and license in the PR description. Review the APL and `LICENSE.Author` guidance before proposing cross-project reuse.
4. **Pre-submit verification.** Follow AROS style and LF line endings; update versions and docs as needed; build and run relevant tests/regressions. State exactly which 32/64-bit and LE/BE targets were tested, and which were not; check pointer sizes, alignment, ABI and byte order, particularly for m68k and network code. Describe scope, rationale, verification and limitations in the PR body.
5. **Active author follow-up.** Track review and CI, respond promptly to reviewer requests, and update or explain outstanding issues. The maintainer explicitly warns that unattended PRs may be closed without merge.

**Known historical conflicts requiring review (snapshot 2026-10-22, not a live status check):** #1801 was a draft stacked on #1788/#1743; #1803 was a draft stacked on #1802. Do not repeat this submission pattern. Check their current GitHub state and dependency graph before taking action; the inventory below is a dated snapshot.

## Abertos

Atualizado em **2026-10-25**: **39 merged, 4 abertos (prontos para review),
nenhum draft e 1 fechado sem merge**. #1803 e #1836 foram mergeados; os três
PRs anteriores e o novo #1848 são independentes entre si e diretamente
baseados em master.

| PR | Proposta | Situação | O que falta agora |
| --- | --- | --- | --- |
| [#1848](https://github.com/aros-development-team/AROS/pull/1848) | `exec`: preservar o alinhamento real e o tamanho do bloco ao liberar memória com MungWall | Pronto para review; `cc40ebad9d`; não é draft; mergeable; quatro checks aprovados | Um commit diretamente sobre master, sem dependências. Os três objetos alterados compilam com `-Werror` para linux-x86_64 e amiga-m68k. A reprodução histórica é em Pi 3/m68k; esta revisão ainda não foi executada em hardware ou em uma imagem m68k com MungWall. Aguardar review. |
| [#1847](https://github.com/aros-development-team/AROS/pull/1847) | `pthread`: devolver o valor anterior no test-and-set m68k | Pronto para review; `8f220e83bd`; não é draft; quatro checks aprovados | Um commit diretamente sobre master, sem dependências. `libpthread.a` e o teste dedicado compilam em x86-64 e m68k; o teste passa no AROS hosted. O binário m68k foi inspecionado, mas não executado em hardware/emulador nesta validação. Aguardar review. |
| [#1846](https://github.com/aros-development-team/AROS/pull/1846) | `dnssd -q`, `-G` e zona completa por meio das APIs de registro aceitas | Pronto para review; `c6722aee2f`; não é draft; quatro checks aprovados | Um commit diretamente sobre master; depende somente dos já mergeados #1803/#1836, não do #1845. Builds x86-64/m68k, cppcheck, `make test` e 19 checks hosted passam. O único alerta inicial do Codacy (escopo de variável) foi corrigido. Aguardar review. |
| [#1845](https://github.com/aros-development-team/AROS/pull/1845) | `mdnsd`: reunir respostas de vários serviços para DNS legado (RFC 6762 §6.7) | Pronto para review; `d835629e93`; não é draft; quatro checks aprovados | Um commit diretamente sobre master, sem dependências de PR. Reproduz 2 pacotes/8 registros no master e entrega 1 pacote/7 registros para dois serviços, com ID/pergunta, TTL e classe legados preservados. Fixtures 27+38+12, builds x86-64/m68k e execução hosted passam. Aguardar review. |

## Merged

Datas em UTC. O campo `state=MERGED`, e não o `reviewDecision`
histórico, determina esta classificação.

| PR | Mudança | Merge |
| --- | --- | --- |
| [#1836](https://github.com/aros-development-team/AROS/pull/1836) | BonAmi: lookup IPv4 e consultas genéricas de registros por fila | 2026-10-25 |
| [#1803](https://github.com/aros-development-team/AROS/pull/1803) | Ferramentas `dnssd`, `Nearby`, `Share`, `Handshake` sobre a fila | 2026-10-25 |
| [#1801](https://github.com/aros-development-team/AROS/pull/1801) | Peers e Hosts por nome e anúncio `_midi2._udp` | 2026-10-24 |
| [#1802](https://github.com/aros-development-team/AROS/pull/1802) | mdnsd: listar os tipos de serviço da rede (RFC 6763 §9) | 2026-10-24 |
| [#1789](https://github.com/aros-development-team/AROS/pull/1789) | btmidi.class: report MIDI port exhaustion | 2026-10-24 |
| [#1788](https://github.com/aros-development-team/AROS/pull/1788) | netmidi: separate AppleMIDI control from the RTP-MIDI payload | 2026-10-24 |
| [#1743](https://github.com/aros-development-team/AROS/pull/1743) | netmidid: advertise AppleMIDI sessions through system mDNS | 2026-10-24 |
| [#1735](https://github.com/aros-development-team/AROS/pull/1735) | btmidi.class: follow BLE-MIDI on the header's reserved bit; drop F9 and FD | 2026-10-24 |
| [#1796](https://github.com/aros-development-team/AROS/pull/1796) | netmidid: address the #1693 review of the MIDI 2.0 transport | 2026-10-22 |
| [#1693](https://github.com/aros-development-team/AROS/pull/1693) | netmidid: connect Network MIDI 2.0 sessions to CAMD ports | 2026-10-22 |
| [#1742](https://github.com/aros-development-team/AROS/pull/1742) | netmidid: add NetMIDIStatus session and port counters | 2026-10-22 |
| [#1793](https://github.com/aros-development-team/AROS/pull/1793) | netmidid: finish service error and drop reporting | 2026-10-22 |
| [#1792](https://github.com/aros-development-team/AROS/pull/1792) | mdnsd: isolate browse view exhaustion and bound cache scans | 2026-10-22 |
| [#1679](https://github.com/aros-development-team/AROS/pull/1679) | netmidid: a network MIDI service, with AppleMIDI as its first transport | 2026-10-22 |
| [#1695](https://github.com/aros-development-team/AROS/pull/1695) | mdnsd, bonami.library: queued DNS-SD browse and resolve with a lent-request wakeup | 2026-10-22 |
| [#1677](https://github.com/aros-development-team/AROS/pull/1677) | netinfo: defer expunge until the worker finishes | 2026-10-20 |
| [#1744](https://github.com/aros-development-team/AROS/pull/1744) | camd: ignore icon and debug files during driver scans | 2026-10-20 |
| [#1734](https://github.com/aros-development-team/AROS/pull/1734) | camd: report oversized SysEx and honour MIDI_ErrFilter | 2026-10-20 |
| [#1733](https://github.com/aros-development-team/AROS/pull/1733) | camdusbmidi: make USB MIDI devices usable through CAMD | 2026-10-20 |
| [#1732](https://github.com/aros-development-team/AROS/pull/1732) | camd: lock cluster notification, find new drivers, tell nodes of participants | 2026-10-20 |
| [#1731](https://github.com/aros-development-team/AROS/pull/1731) | debugdriver: store port 0's user data in UserData[0] | 2026-10-20 |
| [#1730](https://github.com/aros-development-team/AROS/pull/1730) | camd: keep a driver port open while a receiver is linked to it | 2026-10-20 |
| [#1678](https://github.com/aros-development-team/AROS/pull/1678) | rtpmidi: add AppleMIDI sessions, peers and clock sync | 2026-10-20 |
| [#1591](https://github.com/aros-development-team/AROS/pull/1591) | mdnsd, bonami.library: register a service over multicast DNS | 2026-10-19 |
| [#1692](https://github.com/aros-development-team/AROS/pull/1692) | netmidi2: reject invalid identity outputs and malformed acceptances | 2026-10-18 |
| [#1588](https://github.com/aros-development-team/AROS/pull/1588) | network: the Universal MIDI Packet and Network MIDI 2.0 as a link library | 2026-10-18 |
| [#1587](https://github.com/aros-development-team/AROS/pull/1587) | network: RTP-MIDI as a link library | 2026-10-18 |
| [#1586](https://github.com/aros-development-team/AROS/pull/1586) | midi: CAMD drivers for MIDI over the network | 2026-10-18 |
| [#1676](https://github.com/aros-development-team/AROS/pull/1676) | camd: return success after expunge cleanup | 2026-10-18 |
| [#1507](https://github.com/aros-development-team/AROS/pull/1507) | bluetooth: add BLE MIDI class | 2026-10-18 |
| [#1585](https://github.com/aros-development-team/AROS/pull/1585) | camd: look for a driver's MidiDeviceData at aligned addresses | 2026-10-17 |
| [#1560](https://github.com/aros-development-team/AROS/pull/1560) | mdnsd: answer for the host's name over multicast DNS | 2026-10-13 |
| [#1558](https://github.com/aros-development-team/AROS/pull/1558) | AROSTCP: a closing socket takes its group memberships with it | 2026-10-11 |
| [#1543](https://github.com/aros-development-team/AROS/pull/1543) | AROSTCP: IPv4 multicast on SANA-II interfaces, with a netdb crash and hosted eth.device multicast | 2026-10-11 |
| [#1485](https://github.com/aros-development-team/AROS/pull/1485) | llvmpipe: link the target-side LLVM archives by path | 2026-10-08 |
| [#1483](https://github.com/aros-development-team/AROS/pull/1483) | camd: cluster names on 64-bit targets, and driver scan of arena-loaded modules | 2026-10-08 |
| [#1506](https://github.com/aros-development-team/AROS/pull/1506) | bluetooth: add remaining LE peripheral and GATT support | 2026-10-07 |
| [#1484](https://github.com/aros-development-team/AROS/pull/1484) | raspi-aarch64: keep the debug console off the Bluetooth radio, fetch its patchram | 2026-10-07 |
| [#1112](https://github.com/aros-development-team/AROS/pull/1112) | bluetooth: Raspberry Pi onboard controller (PL011 transport, H:4 device, Broadcom patchram loader) | 2026-09-01 |

## Fechado sem merge

[#1482](https://github.com/aros-development-team/AROS/pull/1482) foi dividido em [#1506](https://github.com/aros-development-team/AROS/pull/1506) e [#1507](https://github.com/aros-development-team/AROS/pull/1507), ambos merged.
Não é uma proposta ainda aguardando revisão.

## Contexto histórico — registros de 2026-10-11 a 2026-10-13

O texto abaixo conserva o relato da publicação inicial. Suas referências a
PRs ainda abertos e a decisões aguardadas pertencem àquela data; para os
bloqueios atuais, use o registro acima.

Issue #1559 asks the maintainer where the mDNS service should live, how to
split it, how clients should get events, which protocol limits block it,
where its tests go and what to call the library. #1560 is the first cut,
made so that it needs none of those answers: `workbench/network/mdns` with
an mdnsd that only answers for `<hostname>.local` (probing, announcing,
conflicts and tie-break, plain DNS and unicast-response answers, goodbye),
about 1,600 lines carved out of `network/mdns/aros/src` with the daemon's
state renamed (`struct MDNSDaemon`, `md_`). No library, no client API, no
tools. `tools/mdnsd-responder-hosted.sh` tests it alone: five boots, 22
checks. The cut lives only on the branch (worktree `~/AROS-net`); it is not
kept in `~/AROS`, where it would install the same `mdnsd` and `services.d`
entry as `contrib-aros-midihub-mdnsd`. To work on it, copy the directory
from the branch into `~/AROS/workbench/network/mdns`, build
`workbench-network-mdnsd`, and afterwards remove it and rebuild
`contrib-aros-midihub-mdnsd` to get the full daemon back.

#1543 is `patches/net/series` as three commits on upstream `51bffe65a4`
(worktree `~/AROS-net`). Before it was sent, AROSTCP and the mDNS components
were rebuilt on hosted AROS against that head and `tools/bonami-hosted.sh`
passed against Avahi. It was merged on 2026-10-11 as fourteen commits, and
the patches are gone from here.

The maintainer reviewed it the same day
([comment](https://github.com/aros-development-team/AROS/pull/1543#issuecomment-6112953952)
): nine findings, four of them correctness problems that a
test on hosted AROS against Avahi alone could not show. A tunnel interface
was written through as a SANA-II one; IGMP queries were dropped because
nothing joined the all-hosts group, so a membership would time out at a
switch after about 260 seconds; the stack never sent `S2_MULTICAST`; and
three different tests decided what counts as Ethernet. All nine were fixed
in further commits on the branch (`79ea1ce688` to `3e28b45c24`), carried
here as seven more patches in the series, with
[a reply](https://github.com/aros-development-team/AROS/pull/1543#issuecomment-6113185978)
point by point. `tools/igmp-hosted.sh` is new: the host plays an IGMP
querier and AROS has to answer each query; it fails without the all-hosts
join. The stack was also built for raspi-aarch64 and amiga-m68k. Still
untested: a real SANA-II driver, a `TUNNEL` line, an unknown wire type.
Open question put to him: the first membership's interface as the
route-free fallback, or a 224.0.0.0/4 interface route.

His second look found three things left and one cosmetic point. Wanted
before merge: `net/sana2arp.c` still got the public `in_var.h`, so
`in_mcast_macaddr()` compiled only as an implicit declaration. Follow-ups
the PR made relevant: the hosted device did not free its multicast list,
and `if_detach()` left the all-hosts membership of a removed interface, so
after a reload that removes and re-adds an interface the queries were
filtered out again. All four are fixed as four more commits on the branch
(`353adedb5d` to `e8dd882b53`), with
[a reply](https://github.com/aros-development-team/AROS/pull/1543#issuecomment-6114458179),
and four more patches here. `in_ifdetach()` does not free the entries that
sockets hold, as he had suggested, but cuts them loose from the interface;
the reply says why and asks whether he wants it otherwise. `tools/igmp-reload-hosted.sh` is new,
with the tools `NetReload` and `McastJoin`: it removes an interface by a
reload, adds it again, joins a group on it and plays the querier; it fails
without `in_ifdetach()`. Both earlier tests still pass and the stack
builds for raspi-aarch64 and amiga-m68k.

Seen while writing that test: after a reload, mdnsd was gone from
224.0.0.251 on an interface that had been removed and added again. Looked
into on the same day, it was two defects:

- **mdnsd, ours.** The stack signals the start and the end of a reload and
  has usually finished before mdnsd runs, so both signals arrive at once.
  mdnsd closed its sockets for the start and then skipped opening them
  because the start was also set, and nothing opened them later: after its
  first reload it had no socket at all. Fixed in `mdnsd.c`: both at once
  means close and open.
- **AROSTCP, upstream's.** `in_pcbdetach()` released a socket's multicast
  options under `#ifdef ENABLE_MUPTICAST`, a misspelling, so a closed socket
  never left its groups and the host went on answering IGMP queries for
  them. That is why the first defect did not show on the interface that
  stayed. #1543 was merged before this could join it, so it went upstream
  as #1558 and was merged the same day.

`tools/igmp-reload-hosted.sh` now checks both: mdnsd has to be in its group
on both interfaces after the reloads and to have announced it again, and a
group must no longer be reported once the socket that joined it has closed.
Each check fails with its fix taken out.

Outside AROS: [kmatheussen/camd#1](https://github.com/kmatheussen/camd/issues/1),
opened on 2026-10-12, presents the CAMD work to Kjetil Matheussen, who
wrote the library, and asks what he thinks: the fixes (two of them are in
his sources too), versions 42 and 43, and the driver side, where the
transmit function's contract, the undocumented "new driver format" and
drivers never being unloaded are things we had to guess at. His repository
is a single commit from 2011, so an answer is not to be counted on.

The sections below are the PR bodies as first written, except the one about
`btmidi.class`, which describes the class as it is after the review.

## Review of #1507: a transport is a CAMD device

The first `btmidi.class` opened `camd.library`, made a MidiNode with
`CreateMidiA()` and published two clusters per role with `AddMidiLinkA()`.
The maintainer (Kalamatee) stopped there, before the BLE side
([comment](https://github.com/aros-development-team/AROS/pull/1507#issuecomment-6108626888)):
that is the API of a MIDI application, and the class differed from every
other MIDI hardware driver in the tree. He asked for the model of
`camdusbmidi.class`: a `struct MidiDeviceData` stub in `DEVS:Midi` that
CAMD loads, exported open and close functions returning a per-port adapter,
bytes rather than messages to CAMD, output pulled through `ActivateXmit`,
and roles mapped to numbered ports.

The class was rewritten that way and pushed as two more commits on the same
branch (`a45711131d`, `db7145795c`, and on 2026-10-12 `658d741303`: a
version in the adapter shared with the driver, and a malformed packet kept
whole from CAMD), with
[a reply](https://github.com/aros-development-team/AROS/pull/1507#issuecomment-6112279392)
that lists the changes against his eight points, where the class
deliberately differs from the USB one, what was and was not tested, defects
noticed in `camdusbmidi` while mirroring it (from reading, offered as
separate fixes), and a question: whether an additive, binary-compatible
extension on the driver side, for a port's identity, name and presence,
would be welcome. Owed: the Pi 3 test of this version, reported in the PR;
a squash before merge if he wants one; and, if he is open to the extension,
an issue with a concrete sketch before any code.

What this means for the CAMD work here is in
[the CAMD integration design](camd-integration.md) and the amendment to
[the architecture decision](camd-endpoint-architecture-decision.md). The
rewrite lives in the worktree `~/AROS-btmidi`.


---

## bluetooth: BLE MIDI (LE peripheral pairing, connection parameters, btmidi.class)

This adds MIDI over Bluetooth LE, as the MIDI Association's BLE MIDI 1.0
specification describes it, offered to CAMD applications. The first six
commits complete the LE side of the stack where BLE MIDI needed it. Each
one is useful on its own, and each builds and passes the host suite. The
last commit adds the class.

### The stack

| commit | what it adds |
|---|---|
| write without response, and the negotiated ATT payload | ATT Write Command; `BTPR_GATTWRITENORSP` and write-only endpoints use it; `BEA_MaxPktSize` reports MTU - 3 |
| advertise enabled 128-bit service UUIDs | iOS finds a BLE MIDI peripheral by its 128-bit service UUID in the advertisement |
| hand service writes and notifications over intact | `BEHMB_SERVICEWRITE` carries a copy of the value and the writing device (`BENA_Data`, `BENA_DataLength`, `BENA_Device`); notifications are queued as snapshots instead of coalesced; 45.18 |
| pair as the LE peripheral | SMP responder: legacy Just Works by default, LE Secure Connections with `btlesc` (Just Works, Numeric Comparison, Passkey Entry); LTK requests, bonding, reconnection |
| LE connection parameter updates | the L2CAP procedure in both roles; `BSA_LEConnInterval`; the LE Remote Connection Parameter Request event is answered |
| notification payload and event-stream characteristics | `BSA_LENotifyPayload`; `BGDP_STREAM` (a read returns no data) |

Three SMP bugs found on the way also affect the central role: the IO
capability table chose Just Works where Table 2.8 asks for Passkey Entry
(KeyboardDisplay against DisplayOnly/DisplayYesNo); Phase 3 must start with
the responder's keys; and an initiator rejected a Secure Connections
commitment that arrived before its own DHKey.

The ignored LE Remote Connection Parameter Request event (subevent 0x06) was
the cause of iPhone connections to a Raspberry Pi 3 dropping with
"connection timeout" soon after connecting: the iPhone starts the link-layer
procedure and the controller waits for the host.

### btmidi.class

In `rom/bluetooth/classes/btmidi`, beside `btgatt` and `btbattery`. As it
is in #1507 after the review:

- **CAMD driver:** the class embeds a `struct MidiDeviceData` stub with 16
  ports and writes it to `DEVS:Midi/btmidi` when it starts. CAMD loads it
  and the stub opens the class. The class does not open `camd.library`.
- **Peripheral:** registers the BLE MIDI service; a connecting phone or
  computer plays through port 0, `btmidi.in.0` and `btmidi.out.0`. Each
  central has its own decoder, so several can send at once, one SysEx at a
  time. `btgatt.class` still decides whether the service is offered and
  whether the radios advertise.
- **Central:** binds to the BLE MIDI service of each registered device and
  gives it the first free port from 1. Like `btbattery.class`, a binding
  never connects the device itself; it follows connect and disconnect events.
- **Status window:** lists which device has which port and shows the
  peripheral role's activity. There are no names to edit.

### Testing

The host suite goes from 2137 to 2530 checks: ATT and GATT client writes,
L2CAP connection parameter updates, an initiator-against-responder SMP
loopback for every association, the full IO capability table and the Phase 3
order. Each commit builds `bluetooth.library`, and the class builds and links
for `raspi-aarch64`.

On a Raspberry Pi 3 with its on-board radio and an iPhone running MIDI
Wrench, the iPhone connects to AROS and MIDI goes both ways between it and
CAMD. That was the first, client version of the class; the driver version
has been run on hosted AROS only (see [BLE MIDI](ble-midi.md)). The central
role has not been tried with a BLE MIDI peripheral yet.

---

## camd: cluster names on 64-bit targets, and driver scan of arena-loaded modules

Two bugs that make CAMD unusable on AArch64 and x86_64. The second one also
crashes any target that loads modules into an arena.

**Cluster names.** `mysprintf()` took its variadic arguments from `&fmt+1`,
which only works where they are passed on the stack one after the other
(m68k, i386). On AArch64 and x86_64 every cluster name came out as garbage,
and so did the `DEVS:Midi` driver paths, so no program could find a cluster
by name. It now formats through `VNewRawDoFmt()` with a `va_list`.

**Driver scan.** `LoadDriver()` reads each hunk's size from the longword
before its `BPTR` and scans the hunk for `MidiDeviceData`. With
`ELF_MODULE_ARENA` every section hunk stores a size of 0 and a container
hunk holds the arena's size. Subtracting the header from 0 wrapped the size
and the scan ran off the end of memory. `lddemon` reads the same field but
finds its `Resident` in the first bytes. Hunks no larger than their header
are now skipped; the container still covers them.

Found on a Raspberry Pi 3 running `raspi-aarch64`: first every endpoint had
a garbled name, then, once names were fixed and CAMD really loaded
`DEVS:Midi/debugdriver`, a bus fault at the top of RAM (the dump showed the
`'MDEV'` compare and a size of `0xd0e2c79c`).

---

## raspi-aarch64: keep the debug console off the Bluetooth radio, fetch its patchram

The on-board Bluetooth transport from #1112 needs two more things on
`raspi-aarch64`. The transport went in before the stack had its
firmware-loader mechanism.

**Debug console.** The bootstrap and the kernel printed their serial log
through the PL011 and muxed GPIO 14/15 to it. On a Pi 3, 3B+ and Zero 2 W
that PL011 belongs to the radio, which `pl011bt.resource` puts on GPIO 30-33.
Two pins fed the PL011's receive line and every `bug()` went to the radio:
`h4bthci.device` opened, but the radio never answered `HCI_Reset`. When the
device tree's `serial0` alias is the mini-UART, the bootstrap now prints
through it, leaves the PL011 alone, and passes its address in
`KRN_DebugUartBase`, which the kernel recognises. Linux does the same. The
cable stays on the same pins at the same speed. A Pi 2, or a Pi 3 with
`dtoverlay=disable-bt`, keeps the PL011 console.

**Patchram.** `distfiles-raspi-aarch64-bt-fw` fetches `BCM43430A1.hcd` and
`BCM4345C0.hcd` from the Raspberry Pi `bluez-firmware` repository into
`DEVS:Firmware/brcm/`, beside the WiFi firmware. Without it the radio runs
from ROM with the placeholder address `AA:AA:AA:AA:AA:AA`.

Tested on a Raspberry Pi 3: the radio comes up with its real address once
registered in Bluetooth Preferences, and the serial log still appears on the
header.

---

## llvmpipe: link the target-side LLVM archives by path

`LLVM_LIBS` turned the archives in `$(AROS_DEVELOPER)/lib` into `-lLLVM...`
names, which the linker looks up through every `-L` in order.
`TARGET_CXX_LDFLAGS` puts the crosstools lib directory first, and when the
AROS toolchain itself was built with LLVM that directory also holds the
host's `libLLVM*.a`. The link of `llvmpipe.hidd` then failed with "is
incompatible with aarch64elf". The archives are now passed by path.

Seen building `raspi-aarch64` with `--with-toolchain=llvm`.
