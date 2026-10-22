# NetMIDIStatus e anúncio AppleMIDI — 20/10/2026

| PR | Head | Estado e dependência |
| --- | --- | --- |
| [#1742 — NetMIDIStatus](https://github.com/aros-development-team/AROS/pull/1742) | `669a30346a423f2559b2b0a2ea5d6fb2bccc8fbd` | Draft; depende de #1679 |
| [#1743 — anúncio AppleMIDI](https://github.com/aros-development-team/AROS/pull/1743) | `989227ceaca9d9f4b8d10caf59563a25479f265a` | Draft; depende de #1679; usa #1591 já mesclado |

Ambos partem do head #1679 `f541409f06cea0f76b0d1d1f3065f598d7319356`
e acrescentam um commit próprio. Depois do merge do pai, fazer rebase e
reavaliar os drafts. Os PRs são independentes entre si, de #1693 e de #1695.
Os checks dos dois PRs passaram, incluindo Codacy. As mensagens de commits,
autores/committers e descrições foram conferidos após a publicação.

## Escopo

`NetMIDIStatus` lê o snapshot versionado do serviço e imprime sessão,
convites recusados e contadores das portas ativas. Retorna WARN (5) para
serviço ausente ou versão incompatível. Os slots MIDI 2.0 já são reservados
pelo ABI pai, mas só apresentam tráfego quando o serviço implementar esse
transporte. Não acrescenta sockets, links CAMD ou alterações ao serviço pai.

O anúncio oferece apenas AppleMIDI como `_apple-midi._udp`. O responder do
sistema continua responsável pelo protocolo mDNS/DNS-SD. O recorte antigo N8
incluía MIDI 2.0; o novo modo `n8-applemidi` evita essa dependência adicional.
A configuração instalada permanece desabilitada.

O poll já não deixa de verificar o daemon após obter um handle. Uma
atualização vazia valida o registro a cada dez segundos; o responder atual
também renova o anúncio. Ausência ou rejeição de handle antigo limpa os
registros para nova tentativa. Nenhuma mudança à biblioteca ou à API de
eventos foi incluída neste PR.

## Verificações

Artefatos: `build/review-publication-2026-10-20/`.

- Comando e serviço compilam e ligam para x86-64 e amiga-m68k, diretamente
  e pelos makefiles gerados. Os builds diretos tratam warnings como erros,
  com a exclusão existente de registradores voláteis do SDK m68k.
- Foram gerados os headers Bonami do código mesclado. No build isolado,
  o serviço liga explicitamente o arquivo RTP-MIDI recém-compilado para
  evitar que o SDK selecione sua biblioteca antiga. Essas adaptações são
  do harness; os PRs mantêm os alvos normais do build AROS.
- `status-native/`: sete checks nativos passaram para ausência, versão
  incompatível, sessão/contadores, numeração MIDI 2.0 em snapshot sintético,
  omissão de portas inativas e consulta após remoção do publicador.
- `advertise-host-run.log`: dezessete checks ASan/UBSan passaram compilando
  o discovery exportado com mocks de biblioteca/daemon.
- `service-native/`: três boots passaram contra node-rtpmidi 1.0.0, com
  convites nas duas direções, clocks, tráfego CAMD, isolamento/reserva de
  portas, contadores, parada e reconexão.
- `restart-native-verified/`: binário final com mDNS inicialmente ausente,
  início tardio e reinício do daemon. MIDI por IP continuou funcionando.
  A captura confirma nome configurado, PTR/SRV/TXT/A, `192.168.166.2:5004`,
  republicação e dois goodbyes TTL-zero, na parada do daemon e do serviço.

Os logs seriais dos runs finais não apresentam as assinaturas de falha
nativa verificadas pelo harness. O probe sintético testa apenas impressão
MIDI 2.0, sem simular interoperabilidade desse transporte. Os testes usam
SDKs e imagem hospedada UP existentes com binários substituídos, sem rebuild
integral do master nem execução em hardware, m68k nativo ou SMP.

## Avahi: pendência resolvida

[A investigação posterior](mdns-advertisement-avahi-2026-10-20.md) encontrou
um erro no harness: `avahi-browse -rpt` encerrava após a lista inicial, antes
de o daemon tardio anunciar a sessão. Com `-rp`, nome único e verificação
de vida do browser, passaram dois ciclos completos de adição/resolução/remoção,
com endereço e porta corretos. A descrição do #1743 foi atualizada; o código
AROS permanece na mesma revisão. O antigo check que aceitava a entrada em
cache foi substituído por verificações da instância exata e da ordem dos eventos.

Os primeiros testes adicionais corrigiram problemas do harness: amostragem
dos contadores fora do período útil e um comando `Status` que não localizava
o daemon iniciado pelo caminho completo. O teste final amostra durante a
sessão e sinaliza o task do daemon de teste. Um binário diagnóstico foi usado
apenas em `restart-native-trace/`; não foi publicado nem usado na captura final.

O protocolo de vida útil do semáforo de status e as limitações de concorrência
IPC/public-port da biblioteca pertencem às mudanças pais. Estes PRs não
estabelecem sua segurança SMP ou conformidade integral com os RFCs.
