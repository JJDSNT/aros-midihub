# Netinfo: reabertura durante expunge — 20/10/2026

Atendido o [review do #1677](https://github.com/aros-development-team/AROS/pull/1677#issuecomment-6291062875)
no commit `73ef4f5b90ec0f365aa443025abe88846c741194`.
O PR foi atualizado, permanece pronto para revisão e todos os checks
passaram, incluindo Codacy. Os três commits publicados e a descrição foram
verificados quanto à preferência de autoria/atribuição do usuário.

`NetInfo__DevOpen()` agora espera quando o worker ainda existe, mas sua
mensagem de saída já foi respondida. Cada espera solta `nid_Lock`, chama
`Delay(1)` e retoma o lock para reavaliar o estado. Isso permite a limpeza do
worker e impede criar uma unidade associada aos mapas que ele vai liberar.
A espera existente pelo task ID continua protegendo a saída final do módulo.
A alteração acrescenta dez linhas em `workbench/devs/netinfo/netinfo.c`,
preservando a codificação original e sem mudar a ABI.

Artefatos: `build/review-netinfo-2026-10-20/`.

- Módulo completo compilado e ligado para x86-64 e amiga-m68k pelos makefiles
  gerados, usando SDKs existentes e outputs isolados.
- Probe nativo: oito reaberturas forçadas, alternando a unidade inicial de
  usuários/grupos; 72 checks passaram com a correção.
- Para reproduzir a ordem do review, o probe mantém o semáforo, enfileira um
  opener e responde à mensagem real de saída do worker antes de liberar o
  lock. O opener tem prioridade maior que a do worker nessa imagem UP.
  Esse controle é exclusivo do teste e não foi inserido no dispositivo.
- Verifica task ID antigo encerrado, novo worker, mapas atuais, I/O real
  `CMD_RESET` nas duas unidades e remoção após fechamento/expunges repetidos.
- A versão anterior retorna o worker em encerramento nas oito tentativas.
  São 24 checks sem sucesso no baseline: o teste não envia I/O por unidades
  já identificadas como inseguras; os checks de I/O ficam sem sucesso por
  essa decisão deliberada, sem provocar o acesso inválido.
- Ambos os runs completam `Avail FLUSH`; nenhuma das assinaturas de falha
  nativa verificadas foi detectada nos logs seriais.

São execuções em AROS hospedado UP com binários substituídos, sem rebuild
integral do master e sem execução SMP, em hardware ou m68k nativo. A primeira
tentativa de expunge pode apenas solicitar a saída do worker; o módulo é
removido em uma tentativa posterior após o encerramento, como documentado
na descrição atualizada do PR.
