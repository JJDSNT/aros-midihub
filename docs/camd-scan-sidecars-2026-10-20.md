# Ressalva do scanner CAMD — 20/10/2026

Conferência final: todos os checks do #1744 passaram, incluindo Codacy.

#1732 foi mesclado em `5b9d27bbeef1ef6d4f98bb69a035c9981dc0e919`, com a
[ressalva de ignorar arquivos .info e .dbg](https://github.com/aros-development-team/AROS/pull/1732#issuecomment-6290911690).
O scanner tratava esses arquivos auxiliares como drivers inválidos,
retornando -1 e provocando novas tentativas desnecessárias do watcher.

Correção publicada no [PR #1744](https://github.com/aros-development-team/AROS/pull/1744),
pronto para revisão, sem dependência aberta. Branch `review/camd-scan-sidecars`,
commit `835f15562097405d20f1b6a1af37e99288275051`, baseado no upstream
`663483bdcd`. Apenas `workbench/libs/camd/rethinkcamd.c` foi alterado:

- Compila o padrão DOS `#?.(info|dbg)` uma vez por scan.
- Ignora arquivos correspondentes sem distinguir maiúsculas/minúsculas.
- Mantém diretórios excluídos e erros reais de carregamento reportados.
- Documenta a exclusão no autodoc de `RethinkCAMD()`.

Artefatos: `build/review-sidecars-2026-10-20/`. O módulo CAMD completo
compila e liga para x86-64 e amiga-m68k pelos makefiles gerados.
O probe nativo passou nos 25 checks; a biblioteca anterior de #1732 falhou
em 11 checks no mesmo cenário. Cobertura inclui sufixos minúsculos,
maiúsculos e mistos, `.info`, diretórios, candidatos inválidos,
`device.info.driver`, `device.dbg.bak`, driver real ao lado dos arquivos
auxiliares, publicação de sua porta e scans repetidos.

Os dois runs completaram `Avail FLUSH` com a correção separada de netinfo;
o harness não detectou assinaturas de falha nativa nos logs. SDKs existentes
e imagem hospedada UP com binários substituídos: sem claim de rebuild
integral do master ou execução em m68k nativo, hardware ou SMP.

Autor e committer do commit: Jaime Dias. A mensagem e a descrição do PR
não incluem coautoria de ferramenta nem links de sessão.
