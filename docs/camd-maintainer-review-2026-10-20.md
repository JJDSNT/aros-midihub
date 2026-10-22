# Revisões CAMD e atualização dos PRs — 20/10/2026

Correções publicadas em resposta aos comentários do mantenedor:

| PR | Correção | Head publicado |
| --- | --- | --- |
| [#1732](https://github.com/aros-development-team/AROS/pull/1732) | Autodoc dos participantes, tentativas limitadas para arquivos de drivers incompletos e números corretos em `FreeSignal()` | `6515ae8b9a5a90d04b16cedfc656b32ec45d85b6` |
| [#1733](https://github.com/aros-development-team/AROS/pull/1733) | Retomar lotes limitados da transmissão após consumo USB, incluindo mensagens entre lotes | `a605a02d66904cc978ffab4342567e35479f6957` |
| [#1734](https://github.com/aros-development-team/AROS/pull/1734) | Recuperar descarte com novo F0/FlushMidi e ignorar receptores sem fila ou com SysEx filtrado | `f8054ae7f1c0db6dd892855f856c3aabb30181ad` |

O scanner de #1732 tenta novamente até vinte vezes com intervalos de meio
segundo. Uma nova notificação reinicia a janela; um arquivo inválido não mantém
o scanner residente indefinidamente. O teste completa o arquivo dois segundos
depois diretamente pelo host, sem outra notificação DOS.

## Validação

Artefatos e logs: `build/review-camd-2026-10-20/`.

| Verificação | Resultado |
| --- | --- |
| CAMD, bibliotecas dos dois PRs separadamente, x86-64 e amiga-m68k | módulos completos compilados e ligados |
| Classe USB, driver e array incorporado, x86-64 e amiga-m68k | módulos completos compilados e ligados |
| Participantes em AROS hospedado | 12 checks, todos passaram |
| Driver inicialmente incompleto, sem segunda notificação | 8 checks passaram; implementação anterior falhava em 2 |
| Recuperação e filtros de SysEx em AROS hospedado | 16 checks, todos passaram |
| Compatibilidade CAMD 41 com #1734 | 59 passaram, 0 falhas, 1 caso ignorado por precisar de dispositivo |
| Produtor/parser USB reais com shims e ASan/UBSan | 10 checks passaram; implementação anterior falhava em 7 |
| `PutSysEx()` real, driver gerado, soft interrupts Exec e sink USB simulado | 101, 4096 e 65536 bytes completos, chamadas retornaram |

Os probes nativos acima terminaram `Avail FLUSH`, usando a correção separada de
netinfo. As execuções usam AROS hospedado UP e SDKs existentes, não hardware,
SMP ou uma recompilação integral do master atual. #1733 permanece draft:
recepção/transmissão físicas, unplug/replug e dois dispositivos precisam de
validação real. O sink USB dos probes é simulado.

## Serviço após o merge das sessões

#1678 foi mesclado em `c03312337ce72c07df30614d422e8b5cffd829f0`.
Os branches foram atualizados sobre o master
`a1b8aa6fd8b7ed6e0cae1ecbe83c82ecb905c343`:

- [#1679](https://github.com/aros-development-team/AROS/pull/1679):
  `f541409f06cea0f76b0d1d1f3065f598d7319356`, somente dois commits do serviço.
- [#1693](https://github.com/aros-development-team/AROS/pull/1693):
  `6e71764ba2c999c9c1ce4c1fa4237a42bf7be6ec`, os mesmos dois commits e a
  integração MIDI 2.0. Continua draft dependente de #1679; #1692 já foi mesclado.

O código consumidor do serviço foi preservado. Em #1679, a biblioteca MIDI 2.0
recebe apenas as alterações já presentes no master; esse serviço não a liga.
A árvore inteira do serviço MIDI 2.0 coincide com a anterior ao rebase.
Os dois serviços compilam e ligam para x86-64 e amiga-m68k com warnings como
erros, mantendo a exclusão existente de registradores voláteis no SDK m68k.
As nove unidades da biblioteca RTP-MIDI compilam nos dois targets, e a suíte
de protocolo passou com ASan/UBSan.

A nova execução do serviço AppleMIDI passou nas três inicializações contra
node-rtpmidi 1.0.0: convites nas duas direções, clocks, notas/controller/SysEx,
isolamento e reserva das portas 0/3, escala e acorde de saída, contadores,
parada e reconexão após timeout e saída/retorno do peer (duas sessões).
Discovery foi ignorado porque esse recorte não o implementa. Nenhuma
assinatura de falha nativa foi detectada nos três logs seriais. A imagem
hospedada e os módulos de apoio anteriores foram reutilizados, substituindo
o binário do serviço pelo novo. Logs em `service-native/`.

## Mensagens dos commits e descrições

Os oito branches publicados foram atualizados atomicamente com
`--force-with-lease`, protegendo alterações remotas concorrentes.
Foi verificada a ausência das referências de ferramenta solicitadas pelo
usuário nas oito descrições e nas mensagens dos 28 commits apresentados
pelos PRs. Foram retirados trailers de coautoria e links de sessão de commits
pendentes; a árvore de código dos seis branches sem rebase foi preservada.
O histórico já mesclado do upstream não foi reescrito.

Os outros heads publicados são #1677 `bb6fd9041e7858dcc331d5f9c88c3e7bd4d714da`,
#1695 `fc4e114e07dba94cab5490ec41f1160bcabf65ed` e
#1735 `3918469bab111a3d633cc6261c654a2ebf6ae918`.
As descrições de #1732, #1733, #1734, #1679 e #1693 documentam as correções e
dependências atuais. Não foram publicados comentários em nome do usuário.

Conferência final no GitHub: os oito PRs estão com checks `SUCCESS`; #1679
foi marcado pronto para revisão. Apenas #1733 (hardware USB) e #1693
(dependência #1679) permanecem draft. As oito descrições e as mensagens dos
28 commits foram verificadas novamente depois da publicação.

Por solicitação posterior do usuário, #1733 também foi retirado do rascunho
e sua descrição foi ajustada. A validação física continua pendente e
documentada; ela não impede o encaminhamento para revisão. Apenas #1693
continua draft por depender explicitamente de #1679.
