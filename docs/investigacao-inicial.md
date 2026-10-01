# Investigação inicial — AROS MIDIHub

Estado: 30 de setembro de 2026. Este documento registra o que foi verificado e
define a sequência de implementação. O primeiro incremento contém codecs de
pacotes AppleMIDI e RTP-MIDI, a negociação de convite para um par, testes no
host, um autoteste nativo e um programa de diagnóstico UDP. O programa abre as
duas portas, troca convites e mensagens CK e pode enviar uma nota de teste.
Ele ainda não estima a diferença entre relógios nem agenda eventos futuros.
A ponte CAMD para mensagens curtas e SysEx está implementada, mas ainda não
foi exercitada em uma instância do AROS. Áudio ainda não está implementado.

Os testes atuais usam vetores construídos diretamente dos formatos descritos
nas especificações; ainda não incluem capturas de um peer real nem demonstram
interoperabilidade com outro produto. O teste de rede usa dois processos
MIDIHub em localhost.

Verificações locais deste incremento: `make test` no Linux passou; o mesmo
teste passou com AddressSanitizer e UndefinedBehaviorSanitizer (detecção de
vazamentos desabilitada porque o ambiente executa sob `ptrace`); o GCC m68k
do Bellatrix aceitou os três módulos do núcleo e o autoteste AROS com
`-Wall -Wextra -Werror -fsyntax-only`. No build Linux hosted, o
`x86_64-aros-gcc` compilou os três módulos e produziu `libarosmidihub.a`.
Após construir `compiler` e `core-linklibs`, o autoteste e o MIDIHub foram
ligados no build Linux hosted. O teste de rede também cobre SysEx completo e
segmentado.
O programa UDP compila no host e passa no teste de loopback. O compilador
`x86_64-aros-gcc` do build Linux hosted ligou o programa AROS. A execução
dentro do AROS ainda depende de um ambiente Linux hosted funcional.
O GCC m68k e as bibliotecas AROS já presentes no build Bellatrix também
ligaram o executável `MIDIHub-m68k` por meio do alvo `make m68k`; ainda não
houve teste de execução no AROS m68k.

## O que já existe no AROS local

- `workbench/libs/camd/` implementa `camd.library` (configuração ABI versão
  41.1). A API inclui `CreateMidiA`, `AddMidiLinkA`, `PutMidi`, `GetMidi`,
  `PutSysEx`, `GetSysEx` e notificação de clusters. Os cabeçalhos estão em
  `compiler/include/midi/`. Isso oferece uma interface imediata com os
  aplicativos MIDI existentes; uma biblioteca pública MIDIHub não é
  necessária para a primeira ponte.
- `camd.library` percorre `DEVS:Midi` ao inicializar, carrega cada driver com
  `LoadSeg` e cria clusters `<driver>.in.<porta>` e `<driver>.out.<porta>`.
  A classe USB MIDI gera um arquivo nesse diretório. Não há aplicação
  MIDI/CAMD em `workbench/prefs/` nem em `contrib/` neste checkout.
  O MIDIHub hoje cria clusters como cliente CAMD; um driver virtual em
  `DEVS:Midi` é uma alternativa de integração a avaliar, sobretudo para
  disponibilizar portas antes de iniciar a sessão de rede.
- `rom/usb/classes/camdmidi/` contém uma classe Poseidon que cria um driver
  CAMD para USB MIDI. Seu `mmakefile.src` habilita a classe em i386, x86_64 e
  ppc; a linha de arm está desabilitada e não há alvo aarch64. Há comentários
  `FIXME` no tratamento de SysEx. Isso limita aquela classe USB, não o
  MIDIHub: a ponte de rede usa CAMD e não depende de controlador USB MIDI.
- `rom/usb/classes/simplemidi/` é outra classe USB MIDI, separada da integração
  CAMD. Não se deve presumir que ambas expõem as mesmas portas aos aplicativos.
- A busca por uma implementação nativa RTP-MIDI/AppleMIDI para AmigaOS, AROS
  ou MorphOS não encontrou uma candidata utilizável. O projeto
  [amiditools](https://github.com/cnvogelg/amiditools) oferece um driver CAMD
  de MIDI por UDP para AmigaOS 3, mas seu protocolo é próprio e explicitamente
  incompatível com RTP-MIDI. Ele pode orientar a integração com CAMD; seu
  código GPL-3.0 não deve ser incorporado ao código MIT deste projeto.
- `workbench/network/stacks/AROSTCP/` fornece a pilha de rede. A primeira
  aplicação AROS dependerá de `bsdsocket.library` em tempo de execução.
- O checkout em `~/AROS` tem `contrib/aros-bluzing` como link simbólico para o
  projeto separado. Não existe `contrib/extras` nesse checkout; será preciso
  criar o diretório na árvore de trabalho do AROS ao integrar este projeto.

## Protocolo: fatos que definem o MVP

- [RFC 6295](https://www.rfc-editor.org/rfc/rfc6295.html) especifica o payload
  RTP-MIDI, inclusive timestamps, comandos MIDI e recovery journal. Ele não
  define o estabelecimento de uma sessão AppleMIDI. O
  [protocolo de rede MIDI da Apple](https://developer.apple.com/library/archive/documentation/Audio/Conceptual/MIDINetworkDriverProtocol/MIDI/MIDI.html)
  documenta essa camada e descreve seu payload como *majoritariamente*
  conforme ao RFC 6295. Convém modelar as duas camadas separadamente.
- AppleMIDI usa duas portas UDP consecutivas: N para controle e N+1 para
  dados. O convite `IN`/`OK`/`NO` ocorre primeiro na porta de controle e depois
  na de dados. `BY` encerra a sessão. Tokens, SSRC e números multibyte são
  tratados em ordem de rede.
- A sincronização `CK` usa timestamps de 100 microssegundos e uma troca de
  três mensagens. A recepção deve respeitar timestamps futuros, mesmo que a
  primeira versão use um agendamento simples. O iniciador renova a
  sincronização pelo menos a cada 60 segundos.
- A Apple anuncia `_apple-midi._udp` via Bonjour. A conexão manual por IP e
  porta pode ser a primeira entrega; descoberta mDNS/DNS-SD vem depois.
- O driver da Apple envia journals, mas aceita pacotes recebidos sem journal.
  Para o primeiro teste de interoperabilidade, o transmissor pode enviar
  `J=0`; o receptor deve reconhecer e delimitar um journal presente mesmo
  antes de implementar sua aplicação. Perda de pacote e recuperação de notas
  exigem tratamento posterior explícito: sem journal de saída, a primeira
  versão não promete recuperação completa conforme RFC 6295.

## Arquitetura recomendada

```text
Aplicativos MIDI AROS <-> camd.library <-> integração CAMD do MIDIHub
                                             |
                                      sessão AppleMIDI
                                             |
                                      codec RTP-MIDI
                                             |
                                UDP / bsdsocket.library
```

O codec RTP-MIDI e a máquina de estados AppleMIDI devem ser C portátil, sem
dependências de Exec, CAMD ou sockets e sem suposições sobre tamanho de
ponteiros, alinhamento ou endian do host. A integração AROS usa os mesmos
caminhos de CAMD, rede, relógio do sistema e, futuramente, áudio em qualquer
target; não há uma implementação desses serviços por arquitetura. Uma aplicação
de console,
`MIDIHub`, pode hospedar a ponte no primeiro marco. O núcleo preserva eventos
MIDI com bytes e timestamp explícitos; conversões para `MidiMsg` da CAMD ficam
na integração AROS, para evitar erros de endian e o limite de três bytes de
`PutMidi`. SysEx usa caminho próprio (`PutSysEx`/`GetSysEx`).

### Primeira entrega verificável

1. Testes host para codecs AppleMIDI e RTP-MIDI: pacotes válidos, truncados,
   comprimentos, running status, timestamps, ordem de bytes e journals
   recebidos. Vetores fixos da documentação e capturas de tráfego real devem
   ser separados por proveniência.
2. Sessão manual de um par em LAN: iniciar/aceitar convite nas duas portas,
   sincronizar, trocar Note On/Off e Control Change nos dois sentidos, sair
   com `BY`, lidar com timeout e convites repetidos.
3. Integração CAMD que anuncia clusters de entrada/saída, recebe eventos da
   rede e envia eventos dos aplicativos sem eco circular. Validar a semântica
   das ligações `MLTYPE_Sender` e `MLTYPE_Receiver` em um teste nativo.
4. Construir como componente MetaMake para todos os targets AROS, usando os
   mesmos serviços CAMD e de rede, sem condicionais de arquitetura no núcleo
   ou no pacote;
   inicializar por registro em `ENVARC:SYS/Packages/aros-midihub`. Fazer teste
   de execução em AROS com `bsdsocket.library` e um par conhecido (macOS Network
   MIDI ou outro peer AppleMIDI). Validar compilação e execução em targets com
   endian e largura de ponteiros diferentes à medida que houver ambientes de
   teste disponíveis.

Critério de aceite: duas direções de Note On/Off atravessam a rede e aparecem
em clientes CAMD, com sessões estabelecidas e encerradas sem recursos órfãos.
Medir latência e jitter antes de fixar metas numéricas.

## Empacotamento

O layout desejado para a integração é `AROS/contrib/extras/aros-midihub/`
(checkout ou link para este repositório). O `mmakefile.src` do projeto deve
registrar um alvo `contrib-aros-midihub`, sem caminhos absolutos. Como no
`aros-bluzing`, o destino de instalação será `SYS:Extras/aros-midihub/`, com
executável em `C/`, `S/Package-Startup` e arquivo de registro em
`ENVARC:SYS/Packages/aros-midihub`. O registro aponta para o diretório do
pacote; o startup cria `MIDIHUB:` e acrescenta `MIDIHUB:C` ao `Path`.

O repositório já contém `mmakefile.src`, `Package-Startup` e o arquivo de
registro. Há um link local em `~/AROS/contrib/extras/aros-midihub`. O MetaMake
da árvore Linux hosted descobriu o arquivo aninhado e gerou o alvo
`contrib-aros-midihub`. A biblioteca, `Package-Startup`, `LICENSE` e o registro
foram gerados ou copiados para a imagem Linux hosted. O executável e o
autoteste foram ligados depois de construir a base do AROS. O
diretório local `build-aros-linux/` está ignorado pelo Git; como o MetaMake
examina a árvore de fontes independentemente do Git, foi necessário também
ignorá-lo em `mmake.config` desse build. Builds futuros ficam mais simples em
um diretório irmão do checkout. A integração inicial não exige
copiar executáveis para `SYS:C` nem modificar `camd.library`.

## Fases seguintes e dependências

| Recurso | Pré-condição prática |
| --- | --- |
| Descoberta mDNS | Sessão manual e rede UDP estáveis |
| Recovery journal | Interoperabilidade básica, métricas de perda e vetores de teste |
| Múltiplos peers/roteamento | Contrato de portas CAMD e prevenção de loops |
| USB MIDI | Teste da classe CAMD nos targets que a oferecem; é transporte opcional |
| TinySoundFont/SF2 | Saída PCM AROS escolhida e testada, política para arquivos SF2 |
| Serial/DIN e BLE MIDI | Transporte e hardware específicos disponíveis |
| MIDI 2.0/UMP | Contrato de eventos que não dependa de `MidiMsg` de 3 bytes |

## Código de terceiros e licença

- [TinySoundFont](https://github.com/schellingb/TinySoundFont) declara licença
  MIT. Se for incorporado, preservar seu aviso de licença. O projeto upstream
  também informa uma política que veda contribuições de código gerado por IA;
  a incorporação local e eventuais contribuições upstream devem ser tratadas
  separadamente.
- [librtpmidid](https://github.com/davidmoreno/rtpmidid) declara LGPL-2.1.
  Pode servir como referência de interoperabilidade, mas sua incorporação
  exige revisão das obrigações de distribuição e do modo de ligação.
- O README atual cita MIDIKit e cmidid. A licença exata do código ou versão a
  reutilizar deve ser confirmada no arquivo de licença do upstream escolhido;
  referências secundárias divergem. Para começar, implementar os codecs a
  partir de RFC 6295 e da documentação Apple, registrando a proveniência dos
  vetores de teste.

## Decisões ainda abertas

1. Ordem dos ambientes de validação disponíveis. Ela organiza os testes, mas
   não restringe os targets suportados pelo projeto.
2. Primeiro peer de interoperabilidade disponível em laboratório.
3. Se o pacote deve iniciar automaticamente ou só quando `MIDIHub` for
   executado. Para o primeiro marco, execução manual reduz variáveis.
