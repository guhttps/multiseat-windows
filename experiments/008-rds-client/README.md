# Cliente auxiliar para o laboratório Windows Server

Este experimento apresenta duas sessões RDP de uma VM Windows Server em duas janelas do host. Ele não cria dois postos nativos no Windows cliente, não instala drivers e não demonstra compatibilidade com League of Legends. A VM, as duas contas locais não administrativas e a rede interna isolada são preparadas separadamente.

O cliente usa duas instâncias reais do FreeRDP 3.32.2 com buffers GDI e canais de teclado/mouse por contexto. Os pacotes de Raw Input são associados a um dos contextos. Não há hooks globais, injeção de DLL ou roteamento por `PostMessage` para aplicativos externos.

## Build opcional

O build comum mantém `MULTISEAT_BUILD_RDS_LAB=OFF` e não baixa dependências. O [builder do SDK](README-SDK.md) usa fontes oficiais fixadas e mantém a instalação na pasta de build. O tag anotado `3.32.2` do FreeRDP tem objeto `c7e6848d15e613fda3d3ee7ebca36146088802a2` e commit `97eff6ddfbbfaa8e42a126d362686bd001ff8bdc`. As licenças e a procedência dos componentes do SDK precisam ser preservadas.

```powershell
cmake -S . -B build/rds-client -A x64 `
  -DMULTISEAT_BUILD_RDS_LAB=ON `
  -DCMAKE_PREFIX_PATH='<prefixo-local-do-SDK>' `
  -DMULTISEAT_RDS_RUNTIME_DIR='<prefixo-local-do-SDK>/bin'
cmake --build build/rds-client --config Release
ctest --test-dir build/rds-client -C Release --output-on-failure
```

As DLLs do SDK precisam estar disponíveis ao processo do cliente. A opção de runtime modifica apenas o ambiente do teste CTest; não altera o PATH do Windows. Não há instalador nem distribuição pública deste experimento nesta etapa. O observador C# do guest é compilado também no build comum, para ser analisado sem o SDK C++; seu teste automático é exclusivamente offline.

## Configuração privada

O caminho padrão é `%LOCALAPPDATA%\MultiseatPrototype\Lab\rds-client.json`. Também é possível fornecer `--config` com o caminho de outro JSON local. Nunca coloque esse arquivo no repositório ou nos artefatos de publicação. Ele contém os endereços e as credenciais protegidas do laboratório.

```json
{
  "schema_version": 1,
  "server_address": "10.73.12.2",
  "server_port": 3389,
  "certificate_sha256": "<64 caracteres hexadecimais do certificado leaf DER>",
  "seats": [
    {
      "seat_id": 1,
      "username": "LabSeatA",
      "domain": "MSRDSLAB",
      "password_dpapi": "<base64 do DPAPI CurrentUser aplicado aos bytes UTF-8 da senha>",
      "monitor_index": 0,
      "desktop_width": 1280,
      "desktop_height": 720
    },
    {
      "seat_id": 2,
      "username": "LabSeatB",
      "domain": "MSRDSLAB",
      "password_dpapi": "<base64 do DPAPI CurrentUser aplicado aos bytes UTF-8 da senha>",
      "monitor_index": 1,
      "desktop_width": 1280,
      "desktop_height": 720
    }
  ]
}
```

O endereço deve ser um IPv4 literal RFC1918 da rede interna criada para a VM. Somente as contas de teste `MSRDSLAB\LabSeatA` e `MSRDSLAB\LabSeatB` são aceitas. O pin é SHA-256 do certificado completo leaf em DER, obtido pela preparação autenticada via PowerShell Direct a partir do certificado efetivamente vinculado ao listener RDP. Não use hash SHA-1 do binding como pin SHA-256, nem aceite automaticamente o primeiro certificado apresentado pela rede.

O cliente exige NLA e TLS 1.2 ou posterior; valida o pin e a validade temporal do certificado antes de aceitar a conexão. Mudança de certificado, endpoint redirecionado ou gateway são recusados. Não redireciona clipboard, discos, impressoras, portas, smartcards ou áudio. Senhas são descriptografadas em memória com DPAPI para o usuário atual e nunca são argumentos de linha de comando ou campos de log. A biblioteca também mantém credenciais em sua memória durante a conexão; este experimento não promete proteção contra outro processo com direitos de depuração.

Os índices dos monitores seguem a enumeração atual do Windows. Confira-os após mudar cabos ou a disposição; eles não são identidade física persistente.

## Execução e evidência

`--check-config` lê e valida JSON, pin e DPAPI sem conectar. `--self-test` executa fixtures offline de configuração, certificado efêmero, DPAPI e roteamento. `--smoke` cria duas janelas ocultas e contextos FreeRDP, sem conexão ou registro de Raw Input. Nenhum desses modos abre Notepad ou modifica a VM.

O modo normal, sem esses argumentos, conecta as duas sessões. Ative primeiro uma janela do cliente pela barra de título; depois pressione F6 em cada teclado e clique direito em cada mouse, na ordem dos postos. Cada painel mostra separadamente se seu teclado e mouse já foram associados; o roteamento só começa com os dois pares completos. A mira vermelha indica o ponteiro remoto de cada posto. Enquanto o roteamento físico está ativo, o cursor compartilhado do host fica preso e oculto dentro do próprio frontend. F7 tenta reconectar os postos indisponíveis; F8 libera o cursor, solta as entradas pressionadas e reinicia a associação; F9 desconecta somente o posto associado àquele teclado, com os quatro dispositivos já associados; F12 encerra os dois painéis. Perda de foco, mudança de desktop de entrada ou remoção de dispositivo associado também devem liberar a captura. Fora das janelas do cliente, o host recebe os periféricos normalmente. Teclas Windows e a sequência de segurança do host permanecem locais.

A captura usa [ClipCursor](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-clipcursor) e [SetCursor](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setcursor) somente no modo físico, com ao menos uma conexão pronta para entrada e os quatro dispositivos associados. A implementação recusa uma restrição de cursor preexistente de outro aplicativo e preserva uma restrição mais recente ao liberar a sua. Os testes offline usam funções inertes para verificar aquisição, falhas e liberação por destruição; não prendem o cursor real. A captura do frontend não cria dois cursores independentes do sistema Windows nem aprova o motor nativo.

Para diagnosticar a entrada, o cabeçalho mostra contadores de teclas encaminhadas por posto, cliques esquerdos e teclas sem rota. O terminal imprime somente esses totais ao pausar ou encerrar a captura, sem conteúdo digitado, scan codes ou IDs de dispositivos. Um contador de encaminhamento confirma a aceitação na fila do cliente; o relatório do Notepad no guest continua necessário para comprovar a entrega.

Cada conexão normal tem prazo de 60 segundos para preparar protocolo, entrada e primeiro frame. Falha ou timeout deixa seu painel indisponível e libera somente suas entradas mantidas. O outro transporte continua funcionando; F7 recria apenas os transportes indisponíveis com a mesma configuração, pin e NLA. O painel indisponível apresenta seu estado em vez de uma imagem antiga. As associações são preservadas durante essa reconexão; hotplug ainda reinicia a associação dos dois pares. Fechar um painel ou usar F12 continua encerrando o cliente inteiro.

A prontidão exige protocolo ativo e entrada não suspensa, além do framebuffer. O [FreeRDP pode ignorar uma chamada de entrada enquanto inativo](https://github.com/FreeRDP/FreeRDP/blob/3.32.2/libfreerdp/core/input.c); por isso o retorno isolado da API não basta. O worker trata também o pedido de sincronização de foco conforme o [cliente Windows do FreeRDP](https://github.com/FreeRDP/FreeRDP/blob/3.32.2/client/Windows/wf_client.c). As flags de Caps Lock, Num Lock e Scroll Lock são mantidas por conexão e preservadas na reconexão, sem copiar o estado compartilhado do teclado do host para os dois postos. Uma entrada que se torne inválida na fila encerra aquele transporte, sem reproduzi-la mais tarde.

`--transport-check` é um teste sintético explícito: exige as duas conexões com framebuffer e uma janela própria em primeiro plano. Aguarda dez segundos após as conexões e, se estiver em segundo plano, mostra uma instrução para ativar um painel. O prazo total para preparar o envio é de 120 segundos; nenhuma fixture é enviada enquanto o foco exigido estiver ausente. Envia clique e scan codes para os textos exatos `rdp-seat-a`/`rdp-seat-b`, sem Enter ou comandos, e mantém as conexões e janelas por mais 45 segundos para a coleta externa. O Notepad externo de cada conta deve ter sido preparado pelo [observador do guest](guest-observer/README.md). O retorno do envio só confirma a aceitação pela API de transporte. O coletor separado precisa verificar texto observado, token SID, SessionID, PID, RunId e atualização recente nos relatórios privados de cada sessão. Esse teste não demonstra independência dos teclados e mouses físicos.

O argumento adicional `--unattended-fixtures` só é aceito junto com `--transport-check` e dispensa exclusivamente o foco do **host** para esse envio sintético direto aos dois contextos RDP. Raw Input permanece desativado; o modo físico e o teste sintético padrão mantêm suas regras de foco. O modo autônomo aguarda 30 segundos após as duas conexões com framebuffer, mantendo o prazo total pré-envio de 120 segundos e os 45 segundos após o envio. Antes do envio, o controlador do laboratório deve conferir relatórios recentes dos dois observadores após a reconexão: RunId, identidade/token, sessão, PID, janela/Edit pertencentes ao Notepad esperado, foco no Notepad dentro do guest e documento vazio preparado para a fixture. O controlador deve encerrar o cliente se essa conferência falhar. O argumento não altera certificado fixado, NLA, contas permitidas ou textos enviados, e não confirma por si só a propriedade do Notepad no guest.

`--recovery-cycle` exige os dois argumentos `--transport-check --unattended-fixtures`. Depois dos 30 segundos de preparação, desconecta somente o posto 1 e envia a fixture do posto 2. Mantém esse estado por 20 segundos para a coleta externa e reconecta o posto 1 com prazo de 60 segundos. Quando protocolo, entrada e frame estiverem prontos, envia somente o clique de ativação do editor e aguarda dez segundos antes das teclas. Nesse intervalo o controlador deve revalidar processo, sessão, documento vazio, foco no Notepad e ponto de clique dentro do Edit. Depois do envio das teclas, mantém as conexões por mais 45 segundos e encerra. O controlador deve conferir primeiro o posto 1 desconectado com documento vazio e o posto 2 ativo com texto exato; ao final deve exigir os dois textos exatos, sessões ativas e os mesmos PIDs, SIDs e IDs de sessão do preparo. Esse teste sintético não registra Raw Input nem substitui a recuperação com periféricos físicos ou o gate nativo.

A prova física exige escrever e operar os dois Notepads com os dois pares, simultaneamente e sem trocar o foco entre janelas do host, e conferir os relatórios independentes das sessões. Áudio, desempenho, recuperação física e jogos continuam fora desta evidência inicial.
