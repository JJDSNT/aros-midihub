# L10: levantamento da associação entre dispositivo e porta MIDI

Atualizado em 2026-10-24. Substitui as recomendações anteriores, que
misturavam capacidade Bluetooth, publicação CAMD e consumo no Preferences.

## O que foi conferido antes de definir a mudança final

Base examinada: AROS `43e2e698c4075f8b9c19b05702ceca17b1ff5442`.
Os caminhos abaixo são relativos ao repositório AROS.

| Fato confirmado no código | Evidência | Consequência |
|---|---|---|
| Classes já expõem a referência ao dispositivo | `rom/bluetooth/classes/{bthid,btbattery,btserial}`: `btcGetAttrsA(BCGA_BINDING, ...)`, atributo `BCBA_Device` | Não criar outra API para o nome do dispositivo. |
| Nome e conexão já são atributos públicos Bluetooth | `rom/bluetooth/bluetooth/include/libraries/bluetooth.h`: `BDA_Name`, `BDA_IsConnected` | Reutilizar esses atributos com o lock do dispositivo; copiar strings emprestadas antes de desbloquear. |
| A binding MIDI já guarda a porta que recebeu | `rom/bluetooth/classes/btmidi/btmidi.h`: `struct btmidi_binding.port`; `btmidi_central.c`: reserva antes de publicar a binding | Isso prova a associação interna, mas não estabelece que ela deva virar atributo público da classe. |
| A liberação é protegida pelo lock de escrita do dispositivo | `rom/bluetooth/bluetooth/classes.c`: `btReleaseSvcBinding()` limpa a binding e chama a classe mantendo esse lock | Esse é o contrato da infraestrutura interna que o mantenedor não quer impor a aplicações MIDI. |
| A porta não muda enquanto a binding existe | `btmidi_force_binding()` atribui `binding->port`; `btmidi_release_binding()` libera a reserva antes de liberar o objeto | Isso não oferece identidade persistente entre bindings nem escolhe uma interface pública para CAMD. |

## Decisão do mantenedor sobre a interface

[AROS #1789](https://github.com/aros-development-team/AROS/pull/1789),
recebeu uma decisão contrária a `BCBA_MidiPort`. O mantenedor observou que:

- não havia consumidor da nova tag no PR nem no AROS;
- os atributos `BCBA_` existentes descrevem a binding de forma genérica, e
  nenhuma outra classe acrescenta um atributo específico de perfil;
- uma aplicação teria de percorrer a infraestrutura interna de serviços e
  locks de `bluetooth.library`, enquanto aplicações MIDI encontram portas por
  CAMD;
- a lacuna de nomes `btmidi.in.N`/`btmidi.out.N` é real, mas deve ser proposta
  no nível de CAMD junto com o programa que precisa dela;
- a fixture privada não era um teste aceitável da interface pública.

A tag, o header, o getter e a fixture foram removidos. O #1789 passou a conter
somente o aviso de esgotamento das quinze portas de dispositivos (L06), com a
classe em 45.3 depois do merge de #1735 como 45.2.

## Estado de prontidão de L10

L10 voltou a **investigação**, não a implementação. A capacidade ausente está
demonstrada: MIDIHub e outros clientes CAMD veem somente o número em
`btmidi.in.N`/`btmidi.out.N` e não sabem qual dispositivo ocupa a porta. A
interface mínima ainda não foi aceita.

Antes de chamá-la pronta para implementação é necessário:

1. identificar o consumidor concreto e o comportamento de UI/roteamento que
   depende da associação;
2. levantar as interfaces públicas atuais de nomes e identidade de portas em
   CAMD e o comportamento dos drivers já existentes;
3. decidir se a mudança mínima é uma consulta genérica de nome/identidade em
   CAMD ou apenas uma apresentação mantida pelo consumidor;
4. definir estabilidade, ausência de dispositivo, reconexão e fallback para
   drivers antigos;
5. validar com uma binding real e com o consumidor, sem acessar estruturas
   privadas de `btmidi.class`.

Os fatos sobre `BCBA_Device`, `BDA_Name` e `BDA_IsConnected` continuam úteis
para diagnóstico interno, mas não constituem autorização para expor a porta
por uma tag Bluetooth específica.

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
