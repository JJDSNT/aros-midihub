# Banco SoundFont para o sintetizador

O objetivo inicial é uma porta CAMD de saída que reproduza General MIDI (GM)
com um banco SF2. Assim o ScummVM e outros clientes CAMD podem enviar MIDI
para um sintetizador local. Jogos com trilhas próprias para MT-32 são um caso
distinto: o [ScummVM pode converter MT-32 para GM](https://docs.scummvm.org/en/v2.8.0/advanced_topics/understand_audio.html),
mas o resultado depende de cada jogo. Reproduzir MT-32 fielmente requer o
emulador e ROMs que o usuário tenha obtido do próprio módulo; um banco GM
não substitui isso.

## Candidatos verificados

| Banco | Licença e tamanho | Adequação inicial |
| --- | --- | --- |
| [FluidR3 GM](https://github.com/musescore/MuseScore/blob/main/share/sound/FluidR3Mono_License.md) | MIT; SF2 original com cerca de 141 MB | GM completo e licença clara, mas grande para clone, pacote e targets com pouca RAM. |
| [GeneralUser GS](https://github.com/mrbumpy409/GeneralUser-GS) | Licença própria permissiva; SF2 com cerca de 31 MB | GM/GS e menor; o autor [classifica TinySoundFont como inadequado](https://github.com/mrbumpy409/GeneralUser-GS/blob/main/documentation/README.md) para esse banco, pois faltam moduladores. |

GeneralUser GS e TinySoundFont estão fixados como submódulos neste repositório.
Depois de `git submodule update --init --recursive`, `make test-synth` carrega
`soundfonts/GeneralUser-GS/GeneralUser-GS.sf2` e grava uma nota em
`build/generaluser-test.wav`. Esse teste demonstra carga e renderização PCM.
**Não demonstra fidelidade sonora**: TinySoundFont ainda ignora `pmod` e `imod`.
A solução futura requer implementar e validar moduladores e depois comparar a
saída com os testes de conformidade publicados pelo autor do banco.

A [licença própria do GeneralUser GS](../soundfonts/GeneralUser-GS/documentation/LICENSE.txt)
autoriza uso em projetos de software, mas o autor declara incerteza sobre a
origem de algumas amostras. O submódulo serve como banco de teste; a escolha
de um banco para distribuição padrão depende de uma decisão separada sobre
licença e procedência das amostras.

O [TinySoundFont](https://github.com/schellingb/TinySoundFont/blob/main/tsf.h)
é MIT e portátil, mas declara que ainda não implementa moduladores, reverb e
chorus. Portanto, escolher GeneralUser GS agora junto com TinySoundFont
criaria uma combinação conhecida por reproduzir vários instrumentos de forma
errada.

### SF3

SF3 mantém a estrutura musical de SF2 e comprime as amostras com Ogg Vorbis;
reduz o tamanho do arquivo, sem acrescentar recursos de síntese nem resolver a
falta de moduladores. A versão examinada do TinySoundFont já contém um caminho
SF3 condicionado à inclusão de `stb_vorbis`, cujo código pode ser usado sob
MIT. Portanto, SF3 é uma opção real para o pacote dentro do critério de
licença MIT/BSD.

O TinySoundFont decodifica as amostras durante a carga e as guarda como
`float`. Um SF3 menor em disco pode ocupar muita memória e exigir memória
temporária adicional ao abrir; isso precisa ser medido no 68k. O suporte a
SF3 deve ser opcional na compilação e testado com um banco concreto, inclusive
qualidade de áudio, tempo de carga e uso máximo de RAM. A decodificação Vorbis
não altera as limitações atuais de moduladores e portabilidade big endian.

### Escolha do motor

O critério para incorporar um motor ao pacote em `contrib/extras` é uma
licença MIT ou BSD, com procedência verificável. Apache-2.0 e LGPL não
atendem a esse objetivo, mesmo quando a distribuição como componente
separado possa ser permitida. Os ensaios abaixo ficaram em `/tmp`, fora do
repositório:

| Motor | Licença | Resultado da investigação |
| --- | --- | --- |
| [TinySoundFont](https://github.com/schellingb/TinySoundFont) | MIT | Portátil; precisa de implementação e teste de moduladores para GeneralUser GS. |
| [SF2Lib](https://github.com/bradhowes/SF2Lib) | MIT | Declara suporte a moduladores, mas usa C++17/23, componentes Apple e outra biblioteca do autor; exige um porte maior. |

FluidLite (LGPL) e SpessaSynth C (Apache-2.0) foram examinados, mas estão
descartados para incorporação no MIDIHub por esse critério. A compilação
experimental de SpessaSynth C ficou apenas em `/tmp`; não integra o pacote.

TinySoundFont sob MIT é a primeira opção: implementar e validar os moduladores
necessários e verificar a portabilidade para big endian. SF2Lib permanece uma
alternativa MIT caso o custo dessa implementação se mostre maior que seu porte.
TinySoundFont está incluído como submódulo e exercitado por um programa de
teste. A porta CAMD ainda não está ligada ao sintetizador.

## Distribuição

Nenhum banco deve entrar como submódulo obrigatório neste momento. O clone
base deve continuar pequeno e o pacote de rede deve funcionar sem síntese.
Quando houver um motor e saída PCM operacionais no AROS, escolheremos um
banco após um teste de timbres GM, percussão, SysEx e uso de memória nos
targets disponíveis. Nesse ponto um submódulo opcional, fixado em uma revisão
e com a licença e atribuições copiadas para o pacote, pode fornecer o banco
padrão. Também caberá um download ou pacote separado para bancos maiores.

A futura `MIDIHubPrefs` deverá aceitar qualquer SF2 escolhido pelo usuário,
manter um caminho padrão salvo em `ENVARC:` e reproduzir uma nota de prévia
pelo mesmo motor de áudio que atende a porta CAMD. O formato dessa preferência
será definido com a implementação do sintetizador, para não gravar uma opção
que ainda não tenha efeito.
