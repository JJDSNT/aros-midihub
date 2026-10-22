# Descoberta do anúncio MIDI pelo Avahi — 20/10/2026

A pendência de interoperabilidade no [#1743](https://github.com/aros-development-team/AROS/pull/1743)
foi encerrada. O anúncio da revisão
`989227ceaca9d9f4b8d10caf59563a25479f265a` foi descoberto e resolvido pelo
Avahi 0.8, sem alteração ao código AROS.

## Causa

O harness anterior iniciava `avahi-browse -rpt` antes de iniciar o AROS e
ativava mdnsd apenas mais tarde. `-t` significa encerrar após obter uma lista
inicial mais ou menos completa, conforme o próprio `avahi-browse --help`.
O browser terminava antes do novo anúncio e só mostrava uma entrada antiga
em cache. A captura de pacotes continuava funcionando, criando a impressão
de uma falha entre o anúncio e sua descoberta.

## Correção do teste

O harness permanente é [review-netmidi-advertise-hosted.py](../tools/review-netmidi-advertise-hosted.py).
Ele usa `-rp`, exige que o browser permaneça vivo até o encerramento do
cenário, gera um nome único por execução e exige dois ciclos ordenados de
adição, resolução e remoção. Os endereços/portas resolvidos precisam ser os
da imagem AROS. A entrada antiga em cache não pode satisfazer o teste.

O parser de captura aceita o nome da instância como argumento opcional,
mantendo o nome anterior como default para os artefatos históricos.

## Resultado

Artefatos em `build/review-avahi-2026-10-20/`:

- `continuous/`: reprodução inicial corrigida, dois eventos de adição,
  dois de resolução e dois de remoção.
- `final/`: execução pelo harness permanente, com a instância única
  `AROS Avahi 18e03f7bdd062a30`; sequência exata `+ = - + = -`.
- Avahi resolveu as duas publicações como `192.168.166.2:5004`.
- O primeiro ciclo cobre o início tardio de mdnsd; o segundo cobre seu
  reinício e a republicação, terminando com a parada do serviço MIDI.
- A captura também passou na verificação de nome, PTR/SRV/TXT/A, endereço,
  porta e dois goodbyes TTL-zero.
- O peer node-rtpmidi enviou quatro mensagens por IP enquanto mdnsd estava
  inicialmente ausente; o snapshot do serviço confirma a recepção.
- O harness não detectou assinaturas de falha nativa no log serial.

A descrição do #1743 foi atualizada com os resultados e a causa da
observação anterior. Seu head não mudou: não houve necessidade de alterar
o serviço ou o responder. O PR continua draft pela dependência #1679.

Validação em AROS hospedado UP, com imagem/SDKs existentes e binários
substituídos. A evidência cobre Avahi/Linux neste cenário; não estabelece
execução em hardware/SMP, Bonjour/Windows ou conformidade integral dos RFCs.
