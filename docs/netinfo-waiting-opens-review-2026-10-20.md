# Netinfo: expunge durante uma abertura em espera — 20/10/2026

Atendido o [segundo review do #1677](https://github.com/aros-development-team/AROS/pull/1677#issuecomment-6292476390)
no commit `a8684ee0f08bd9b294b5a35342cafdcaa2c2f1a7`.
Todos os checks do GitHub passaram, incluindo Codacy. O PR permanece pronto
para revisão e sua descrição agora reúne o comportamento final e os testes.

O código gerado incrementa `lib_OpenCnt` somente depois de o callback de
abertura retornar. Enquanto esse callback libera `nid_Lock` para esperar a
limpeza do worker, uma segunda tentativa de expunge podia descarregar o
módulo. O novo contador privado `nid_OpenWaiters` é inicializado em zero,
incrementado antes de liberar o lock e decrementado depois de retomá-lo.
Expunge também exige que o contador seja zero. Todas as operações nesse
contador ocorrem sob o semáforo. A API pública permanece igual.

Artefatos: `build/review-netinfo-waiters-2026-10-20/`.

- Módulos completos compilados e ligados para x86-64 e amiga-m68k pelos
  makefiles gerados, com SDKs existentes e outputs isolados.
- `tools/review-netinfo-waiters.py`: extrai os corpos reais dos callbacks
  DevOpen/DevExpunge e os compila com ASan/UBSan e shims de plataforma.
  Durante Delay, termina o worker e executa duas chamadas de expunge com
  lib_OpenCnt zero. A versão anterior falha em sete checks; a correção passa
  nos 32, incluindo outras aberturas em espera, falha ao reiniciar, unidade
  inválida, lock ocupado e proteção existente pelo task ID.
- O teste host modela autorização de unload, sem liberar código em execução;
  não emula o escalonador nativo nem SMP.
- A suíte nativa anterior passou novamente nos 72 checks, com oito
  reaberturas forçadas, mapas e worker atuais, I/O CMD_RESET real nas unidades
  de usuários/grupos e fechamento/expunges repetidos. Avail FLUSH também
  completou e o harness não detectou assinaturas de falha nativa no log.

As execuções nativas usam AROS hospedado UP e imagem existente com módulos
substituídos. Não houve rebuild integral do master ou execução em hardware,
m68k nativo ou SMP. Os quatro commits publicados e a descrição foram
verificados quanto à preferência de autoria/atribuição do usuário.
