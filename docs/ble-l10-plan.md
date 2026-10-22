# Plano de L10: completar a interface da classe BLE

Data: 2026-10-22. Corrigido após a discussão com o usuário. Este plano
substitui a recomendação anterior de adiar a consulta até a extensão CAMD.
Nenhuma API nova está implementada por esta alteração documental.

## Decisão

A classe Bluetooth administra a associação entre dispositivo e porta.
Ela deve oferecer uma consulta pública segura dessa associação,
independentemente de como Preferences, ferramentas Bluetooth ou uma futura
integração CAMD a consumam. Não há dependência técnica de B29/B30/B31.
A implementação deve funcionar com o CAMD atual, preservando seu contrato.

L10 tem duas entregas distintas: a capacidade pública da classe e sua
adoção no Preferences. A primeira não deve aguardar a segunda nem a
extensão CAMD. A futura publicação genérica de metadados pelo driver é
outra integração, com contrato próprio; uma consulta Bluetooth não define
por si só o ABI CAMD nem exige chamadas do driver em contexto inadequado.

## Sequência

1. Definir a consulta usando as convenções públicas de atributos/bindings
   Bluetooth existentes. Identificar a porta atribuída e como obter os
   dados de dispositivo, nome e estado sem acesso às estruturas privadas.
   Distinguir binding existente, conexão de rádio, serviço pronto e porta
   CAMD aberta; um desses estados não prova os demais.
2. Especificar cópia de resultados, tamanhos, lifetime das referências,
   sincronização e resultado de consulta durante remoção/troca da binding.
   Avaliar a consulta de binding contra uma enumeração copiada das portas;
   escolher o contrato a partir da segurança e das convenções da classe,
   não de uma tela específica. Submeter a API proposta ao review normal
   upstream; isso não é aceite presumido nem bloqueio por outro projeto.
3. Implementar na classe canônica/branch de follow-up upstream, sem uma
   segunda implementação neste repositório. Validar consulta sem CAMD
   aberto, com portas abertas e durante remoção e reutilização da porta.
   Incluir testes de concorrência/lifetime e builds x64/m68k. A revisão SMP
   deve cobrir o estado compartilhado usado pela consulta.
4. Em uma entrega separada, adaptar Preferences para consumir a API e
   confirmar nomes e estados corretos em execução. Manter funcionamento
   com classe antiga que não ofereça a consulta. L10 completo inclui essa
   adoção; a capacidade Bluetooth fica disponível antes dela.
5. Futuramente integrar os metadados ao CAMD quando B29/B30/B31 tiverem
   contrato acordado. O fluxo continuará classe/driver → CAMD → aplicativos.
   Essa etapa não bloqueia as anteriores.

## Critérios e limites

A consulta deve informar a associação atual sem ponteiros inválidos, nomes
antigos após troca de dispositivo nem falsa indicação de conexão por uma
porta estar aberta. Testar binding sem conexão, reconexão, mudança de nome,
remoção durante consulta e reutilização da porta por outro dispositivo.
Separar o que se prova por testes de estado do que exige um rádio real.

L01 (atribuição persistente de porta e proteção de rotas) é trabalho
separado. Consultar a associação atual não cria identidade persistente.
A atribuição pode ser melhorada na própria classe com política explícita
para dispositivos esquecidos, endereços mutáveis, esgotamento e reutilização
segura; identidade genérica de endpoints no CAMD continua sendo B29.

Veja a [auditoria de dependências Bluetooth](ble-dependencies-audit-2026-10-22.md)
para os demais itens. Referências: [backlog BLE](ble-midi-backlog.md),
[backlog CAMD](camd-backlog.md) e
[decisão de arquitetura](camd-endpoint-architecture-decision.md).
