# L10: levantamento e implementação da associação MIDI

Atualizado em 2026-10-22. Substitui as recomendações anteriores, que
misturavam capacidade Bluetooth, publicação CAMD e consumo no Preferences.

## O que foi conferido antes de definir a mudança final

Base examinada: AROS `43e2e698c4075f8b9c19b05702ceca17b1ff5442`.
Os caminhos abaixo são relativos ao repositório AROS.

| Fato confirmado no código | Evidência | Consequência |
|---|---|---|
| Classes já expõem a referência ao dispositivo | `rom/bluetooth/classes/{bthid,btbattery,btserial}`: `btcGetAttrsA(BCGA_BINDING, ...)`, atributo `BCBA_Device` | Não criar outra API para o nome do dispositivo. |
| Nome e conexão já são atributos públicos Bluetooth | `rom/bluetooth/bluetooth/include/libraries/bluetooth.h`: `BDA_Name`, `BDA_IsConnected` | Reutilizar esses atributos com o lock do dispositivo; copiar strings emprestadas antes de desbloquear. |
| A binding MIDI já guarda a porta que recebeu | `rom/bluetooth/classes/btmidi/btmidi.h`: `struct btmidi_binding.port`; `btmidi_central.c`: reserva antes de publicar a binding | Expor apenas esse dado faltante, sem uma segunda tabela de associação. |
| A liberação é protegida pelo lock de escrita do dispositivo | `rom/bluetooth/bluetooth/classes.c`: `btReleaseSvcBinding()` limpa a binding e chama a classe mantendo esse lock | O cliente segura o lock de leitura desde obter `BSVA_Binding` até consultar a porta; não pode guardar a binding depois de desbloquear. |
| A porta não muda enquanto a binding existe | `btmidi_force_binding()` atribui `binding->port`; `btmidi_release_binding()` libera a reserva antes de liberar o objeto | A consulta não exige nova tarefa, lock de metadados ou mecanismo de atualização do nome. Isso não é identidade persistente entre bindings. |

## Implementação publicada

[AROS #1789](https://github.com/aros-development-team/AROS/pull/1789),
branch `review/btmidi-port-info`, baseado diretamente no upstream e
independente do codec #1735.

- `BCBA_MidiPort`: atributo opcional de `BCGA_BINDING`, resultado `ULONG`.
- Nome/dispositivo/conexão continuam nas interfaces comuns existentes.
- Nenhuma alteração na API CAMD, vetores ou caminho de dados MIDI.
- Classe antiga ignora o atributo e deixa a saída inalterada; o consumidor
  precisa verificar suporte antes de usar o número.
- Portas 1–15 pertencem às bindings de periféricos. Centrais que se conectam
  ao serviço local compartilham a porta 0 e não são essas bindings.
- Esgotamento das portas de dispositivos passa a produzir aviso no log (L06).

A proposta inicial de snapshots públicos de portas foi retirada do diff
final do PR. Seus testes de cópia/concorrência não constituem evidência da
interface final e não são apresentados como tal.

## Validação e o que permanece pendente

A classe e o driver embutido compilam para x64 e m68k. A ferramenta de teste
nativa compila nas duas arquiteturas. Dois boots AROS hosted passaram:
getter da classe nova com fixtures de binding e fallback com a classe antiga,
sem CAMD carregado. O teste cobre portas 1–15, composição com `BCBA_Device`,
argumentos nulos/escopo inválido e saída preservada em atributo desconhecido.

As fixtures não criam bindings reais: não demonstram comportamento de rádio,
renomeação/reconexão/remoção real nem concorrência do lock de dispositivo.
Esses testes continuam necessários, assim como a adoção no Preferences.
A parte da classe está implementada e proposta upstream; L10 inteira ainda
não está concluída. B29/B30/B31 não bloqueiam nenhuma dessas entregas.

## Regra para os outros itens do backlog

Cada item precisa distinguir: comportamento existente confirmado no código,
lacuna demonstrada, contrato já disponível, mudança mínima necessária,
dependência técnica real e teste que demonstrará o resultado. Onde ainda
não há levantamento suficiente, registrar **investigação pendente**, sem
classificar como implementação pronta nem inventar um bloqueio externo.

Veja o [backlog BLE](ble-midi-backlog.md) e a
[auditoria de dependências](ble-dependencies-audit-2026-10-22.md). Esta última
é uma classificação de dependências, não uma especificação pronta de todos
os itens liberados.
