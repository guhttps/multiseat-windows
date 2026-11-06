# Laboratório 019: canais de entrada para o consumidor nativo

Atualizado em 2026-11-04. **Canais reais entregues ao serviço; leitura pelo consumidor nativo ainda não comprovada. O motor não entrega dois terminais nativos utilizáveis.**

## Problema e implementação

No [laboratório 018](018-native-client-service-verification.md), o serviço real notificou uma sessão e nosso provedor leu essa atribuição sob autoridade viva. `GetInputHandles` ainda recusava fornecer dispositivos. Identificar a sessão não bastava para entregar entrada.

A nova candidata fornece canais de arquivo separados de teclado, mouse e controle por um driver UMDF da classe System. Ela não se registra como teclado/mouse/HID físico e não instala filtros nos periféricos. O trabalhador autenticado abre os três canais antes do callback de conexão. O getter só pode publicar duas referências reais após vincular os canais à sessão efetivamente notificada, com geração imutável e exclusão do console original. O canal de beep permanece ausente.

O dispositivo aceita as consultas padrão de atributos de teclado e mouse. O perfil descreve uma fonte virtual mínima de teste, sem se passar pelos periféricos do usuário. Uma leitura nativa autorizada pode receber um único lote fixo de dados tipados; depois, a leitura fica pendente até cancelamento ou encerramento. Não existe comando público de enfileiramento arbitrário nem captura de entrada física nessa candidata.

## Verificação local concluída

| Verificação | Resultado | Limite |
|---|---|---|
| Driver e auxiliar de criação do dispositivo | Compilação com avisos como erros e análise estática aprovadas | Não comprova carregamento |
| Permissões, hardlinks e encerramento da ponte local | 28 verificações aprovadas | Testes locais, sem consumidor kernel |
| Proteção de execução do auxiliar no host | Recusa esperada aprovada | Não modifica o Windows principal |
| INF nos modos de validação do WDK | Aprovado | Não comprova instalação |
| Signabilidade do pacote, sem criar catálogo | Aprovada, sem erros ou avisos | Nenhum certificado instalado no host |
| Pacote e dispositivo próprios na VM | Assinatura no convidado, cópias verificadas e dispositivo/interface iniciados | Não comprova entrega de handles nem leitura pelo Windows |
| Provedor integrado e cliente | Compilação e análise estática aprovadas | A compilação não comprova consumo kernel |
| Canal autenticado entre processos próprios | 365 verificações em 26 cenários aprovadas | Não comprova terminal |
| Registro da sessão e integração do provedor | 26 verificações aprovadas | Não autentica outra conta |
| Contexto do cliente | 11 verificações aprovadas | Não demonstra display |
| Classificação da próxima rodada | 16 verificações aprovadas | São testes do observador, não evidência de execução |

## Autorização e ciclo de vida

Cada abertura exige a identidade real do serviço, incluindo processo retido, instante de criação, imagem e token autorizado. O arquivo de autorização e o diretório têm permissões explícitas e protegidas; arquivos redirecionados e vínculos inesperados são recusados. Prazo, imagem do driver e vínculo permanecem sujeitos à revalidação.

Uma requisição em modo kernel não ganha autorização apenas por esse modo: precisa usar o mesmo objeto de arquivo já autorizado e o vínculo vivo da sessão. O fechamento do controle revoga a associação e cancela as leituras. A ponte é encerrada e drenada antes da destruição dos contextos emprestados pelo trabalhador; uma referência COM retida não deve reativar esses contextos.

O código não fabrica uma sessão nem transforma o valor opaco recebido por `NotifySessionId` em handle de dispositivo. As referências publicadas têm origem nas aberturas reais do driver. O fechamento foi observado na via de recusa descrita abaixo; o ciclo completo com leituras nativas pendentes ainda exige comprovação.

## Próxima prova e critérios separados

As duas primeiras rodadas de preparação ficaram reprovadas. A primeira exigia um marcador que a rotina integrada ainda não criava; a segunda parou na verificação do arquivo do serviço. Ambas restauraram a VM e passaram na auditoria independente. A revisão identificou que a regra de vínculo único usada para arquivos privados precisava distinguir a leitura do executável fixo do Windows, que utiliza hardlinks do sistema. A nova candidata mantém caminho exato e hash do mesmo handle retido; arquivos privados continuam recusando múltiplos vínculos. Essa distinção passou também nos testes locais.

A rodada seguinte confirmou a preparação real: pacote assinado somente no convidado, cópias conferidas no DriverStore e dispositivo próprio iniciado com interface disponível. A coleta posterior foi recusada por aplicar a margem reservada para alterações a uma operação somente de leitura. O teste completo ficou reprovado; restauração e auditoria independente passaram. A coleta foi corrigida em uma nova rodada, mantendo o watchdog de 120 segundos e a margem original de todas as ações que alteram a VM. Não há resultado de consumo nativo nessa tentativa anterior.

## Entrega observada no serviço real

A última rodada capturou uma troca autenticada com o processo e instante de criação do serviço conferidos independentemente. `NotifySessionId` foi aceito, `GetInputHandles` leu a mesma atribuição viva e retornou `S_OK` com os dois canais reais e beep ausente. O driver registrou o vínculo e as consultas padrão feitas em modo usuário pelo provedor. O fechamento dos arquivos de controle, teclado e mouse foi observado na via de recusa; o cliente encerrou com código zero. Console e processos centrais permaneceram preservados, e restauração e auditoria independente passaram.

O próximo método foi `GetHardwareId`, recusado com `ERROR_NOT_READY` porque falta a integração de display. `GetVideoHandle` não foi chamado. Não houve consulta de atributos em modo kernel, leitura nativa de registros ou cancelamento de uma leitura nativa pendente nessa coleta. Portanto, a devolução e o fechamento dos canais foram observados, enquanto a entrada funcional continua sem prova.

O ensaio completo permanece **reprovado**: a coleta final excedeu sua janela e o veredito preparado esperava a recusa em `GetVideoHandle`, diferente da sequência real. Uma análise separada verificou a observação completa já capturada, sua identidade, ordem dos callbacks, estado do cliente e cleanup; ela não modifica o resultado original. Também preserva um erro anterior ao início da troca autenticada, sem misturá-lo aos callbacks bem-sucedidos dessa troca.

O roteiro de VM foi preparado com pacote e fontes fixados por hash, criação de dispositivo com duração limitada, watchdog externo e restauração obrigatória do checkpoint. Assinatura de teste, instalação do pacote e tarefas administrativas ficam exclusivamente na VM isolada. A auditoria posterior também procura resíduos do novo pacote, dispositivo, tarefas e certificado.

O resultado deve distinguir:

1. O serviço recebeu os handles reais e chegou à próxima recusa explícita.
2. O consumidor nativo consultou os atributos e leu os lotes tipados dos dois canais.
3. As leituras e todos os objetos de arquivo foram encerrados corretamente.

Nenhum desses critérios substitui os outros. A observação confirma essa dependência: a ausência da integração de display interrompeu a conexão antes da leitura nativa. O próximo trabalho é `GetHardwareId`, carregamento e vínculo do driver de display; depois, comprovar o consumidor dos dois canais na conexão gráfica correspondente. O retorno de handles não é entrada funcional.

## Pendências do produto

A identificação de display posterior já tem candidata e testes locais descritos no [laboratório 020](020-native-display-identification.md). Isso não altera a observação nem o resultado completo desta rodada de entrada; carregamento gráfico e execução real da nova candidata continuam pendentes.

Continuam pendentes a autenticação efetiva da segunda conta Windows, apresentação do desktop, saída física, roteamento de periféricos reais, recuperação integrada, áudio, desempenho e compatibilidade com jogos. Esta candidata privada não foi incorporada ao instalador público.

A documentação de [GetInputHandles](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nf-wtsprotocol-iwrdsprotocolconnection-getinputhandles) declara suporte de servidor, sem suporte em Windows cliente. O experimento em VM cliente não amplia esse contrato. Os [requisitos de clientes kernel em UMDF](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/supporting-kernel-mode-clients-in-umdf-drivers) também precisam ser satisfeitos por operações, objetos e níveis de execução reais; compilar a configuração correspondente não comprova isso.
