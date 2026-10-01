# Configuração e futura aplicação de Preferences

O programa de rede lê `ENV:MidiHub/Network` no AROS e, se o arquivo não
existir, tenta `ENVARC:MidiHub/Network`. É um arquivo texto UTF-8 com uma
opção `nome=valor` por linha; linhas vazias e iniciadas por `#` ou `;` são
ignoradas. Também se pode selecionar um arquivo com `MIDIHub --config caminho`.
O arquivo explícito precisa existir. Argumentos posicionais substituem as
portas e o endereço do arquivo. Um exemplo:

```text
local_port=5004
peer_ip=192.168.1.20
peer_port=5004
session_name=AROS MIDIHub
```

`local_port` e `peer_port` são portas de controle entre 1 e 65534; a porta
seguinte é usada para dados. Para apenas receber convites, omita `peer_ip` e
`peer_port` juntos. Sem arquivo, o programa escuta na porta 5004. O nome da
sessão pode ter até 63 bytes. Um arquivo inválido impede o início do programa
e não altera uma configuração já carregada.

## Preferences

Uma futura `MIDIHubPrefs` deve editar os mesmos ajustes usando os botões
`Use` (grava em `ENV:`), `Save` (grava em `ENV:` e `ENVARC:`) e `Cancel`.
Assim, CLI e GUI compartilham o comportamento, e o serviço não depende da
interface gráfica. A primeira página mostra nome da sessão, porta local,
par remoto e estado da conexão. Um botão de teste poderá tentar uma sessão e
mostrar convite, sincronização e mensagens recebidas. A implementação do
serviço precisa expor esse estado à GUI antes desse botão existir.

Quando houver síntese, uma página SoundFont escolherá o arquivo `.sf2` com
um requester, verificará se ele abre no motor de síntese e permitirá ajustar
ganho, banco e programa padrão. A configuração de áudio deve usar o caminho
comum de áudio do AROS e não presumir hardware ou target. O motor deve abrir
uma cópia do SoundFont para validação antes de substituir o instrumento em
uso; falhas não devem interromper notas já tocando.

A prévia deve enviar Note On/Off ao mesmo sintetizador e caminho de áudio
usados pelos aplicativos CAMD. Uma pequena tecla virtual ou um botão `Testar`
é suficiente inicialmente, com escolha de nota e intensidade. Isso verifica
SoundFont, roteamento e áudio de uma vez. A página e sua prévia só devem ser
ativadas quando o motor de síntese e a saída de áudio existirem. Não há
SoundFont nem prévia implementados neste incremento.

O nome `MIDIHub` identifica o pacote. `RTP-MIDI` identifica o transporte,
enquanto `AppleMIDI` identifica a negociação de sessão sobre esse transporte;
eles formam uma única conexão e não duas portas CAMD. Se for criado um driver
CAMD para portas disponíveis na inicialização, `rtpmidi` é um nome adequado
para ele. A forma de comunicação entre esse driver e o serviço de rede ainda
precisa ser definida e testada dentro do AROS.
