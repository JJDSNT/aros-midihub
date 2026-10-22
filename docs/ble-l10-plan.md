# Plano de decisão para L10

Data: 2026-10-22. Estado: recomendação para decisão; nenhuma nova API ou
mudança nos PRs foi implementada por este plano.

## Situação atual

L11 está concluída: BLE usa a classe e o driver nativos do AROS. Preferences
reconhece as portas numéricas e as rotas periféricas antigas são migradas
para a porta 0. A janela da classe já mostra a associação dispositivo/porta.
Falta validar a integração atual em execução com rádio físico.

O restante de L10 é mostrar essa associação também no Preferences. A classe
mantém a informação internamente, mas sua consulta pública não retorna o
número da porta da binding. É possível acrescentar essa consulta sem mudar
CAMD. Isso cria, porém, uma API pública Bluetooth a manter, mesmo que o
Preferences passe depois a consultar metadados genéricos do CAMD.

L01 é outro problema: a atribuição de uma porta livre a um dispositivo pode
fazer uma rota salva pelo nome numérico atingir outro dispositivo. Mostrar
o nome atual na interface não torna a identidade da rota persistente.

## Alternativas

| Caminho | Entrega | Limite e custo futuro |
|---|---|---|
| Consulta direta na classe agora | Preferences associa uma binding à porta e mostra o dispositivo | Nova API pública, revisão upstream, sincronização com desconexão e consumidor específico de BLE. Não resolve L01 nem presença genérica no CAMD. A consulta pode continuar útil para ferramentas Bluetooth, mas não deve ser descartada como API temporária. |
| Metadados CAMD primeiro | Classe/driver fornece identidade, presença e nome; CAMD os publica para os aplicativos | Exige acordo de ABI e implementação ainda não aceitos upstream. Reaproveita o desenho e os protótipos existentes, mas eles ainda não constituem uma API operacional. |
| Adiar ambas as implementações | Usar a janela da classe e as portas numéricas atuais | Mantém a limitação da interface e o risco L01; não se deve apresentar a rota por porta como identidade estável do dispositivo. |

## Recomendação e sequência

Recomendo adiar a implementação da consulta pública exclusiva de BLE.
Preparar agora o desenho mínimo de metadados CAMD e validar o funcionamento
atual. Não condicionar L10 ao projeto completo de UMP ou ao registro de
drivers residentes.

1. **Validar o estado atual.** Executar o pacote em uma árvore AROS atual;
   conferir Preferences, abertura das portas, troca de bindings, reconexão
   e a migração das rotas da porta 0. Repetir o teste com Pi/telefone e,
   para portas 1–15, com um periférico BLE real. Registrar separadamente o
   comportamento de uma rota quando a porta muda de dispositivo (L01).
2. **Preparar uma proposta mínima para B29/B30/B31.** Aproveitar o ADR e o
   rascunho ABI existentes. Delimitar identidade por dispositivo, presença
   por porta e nome de exibição. O fluxo é classe/driver → CAMD → cliente.
   Preservar `MidiDeviceData`, nomes de clusters e comportamento de drivers
   sem extensão. Definir quem copia os dados, referências, concorrência,
   retirada da binding e notificação/reconsulta. Não tratar nome ou endereço
   Bluetooth isolado como garantia universal de identidade estável.
3. **Fechar o escopo de integração antes de implementar publicamente.**
   Identificar o subconjunto necessário do registro/consulta de endpoints
   existente nos protótipos. Não supor que basta acrescentar três campos:
   descoberta do bloco, armazenamento CAMD e consulta pelo cliente precisam
   funcionar juntos. Separar B32 (UMP/timestamps), drivers residentes e
   demais ampliações. Levar o desenho à revisão upstream quando autorizado;
   o aceite de #1507 não equivale ao aceite desta extensão.
4. **Com o contrato acordado, implementar uma cadeia completa.** Começar
   pelo fallback de drivers antigos e pelos metadados BLE; depois adaptar
   Preferences para consultar CAMD. Verificar que a identidade permanece
   correta quando um dispositivo troca de porta ou outra binding a ocupa.
   A adoção de identidade nas rotas é trabalho explícito de L01, não efeito
   automático de exibir o nome em L10.
5. **Encerrar L10 quando o resultado for demonstrado.** Preferences mostra
   o dispositivo correto, deixa de associá-lo após remoção, acompanha uma
   nova binding sem dados antigos e continua funcionando com CAMD/driver
   sem metadados. Validar cópias/lifetime, concorrência e builds x64/m68k;
   compilar sozinho não demonstra a correção da interface em execução.

## Quando antecipar a consulta BLE

Reabrir essa decisão se testes de uso mostrarem que a seleção por portas
numéricas impede o uso necessário com vários dispositivos e a consulta for
útil independentemente do CAMD, ou se upstream orientar explicitamente esse
caminho. Nesse caso, propor uma extensão pequena da consulta de bindings,
com resultado copiado e lifetime definido, e encapsular o consumidor no
Preferences para uma futura migração. Não acessar estruturas privadas nem
recriar uma implementação local da classe.

Enquanto não houver resposta sobre os metadados, continuamos a validação
e os trabalhos independentes dos drivers de rede. Não iniciar uma API BLE
alternativa apenas porque a proposta CAMD ainda está em discussão.

Referências: [backlog BLE](ble-midi-backlog.md),
[backlog CAMD](camd-backlog.md),
[decisão de arquitetura](camd-endpoint-architecture-decision.md) e
[rascunho ABI](camd-endpoint-abi-draft.md).
