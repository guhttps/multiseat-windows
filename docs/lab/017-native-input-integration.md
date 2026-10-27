# 017 — Integração de entrada do motor nativo

Atualizado em 2026-10-26. Estado: candidata UMDF carregada na VM, com leituras e cancelamentos das filas próprias verificados; integração com a sessão nativa ainda pendente. **O segundo terminal nativo continua indisponível.**

## Implementação e evidência disponível

A investigação do consumidor de [GetInputHandles](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nf-wtsprotocol-iwrdsprotocolconnection-getinputhandles) identificou canais de arquivo separados para teclado e mouse, com consultas de atributos, leituras assíncronas e cancelamento. A observação é específica da imagem examinada. A documentação dessa interface não declara suporte em Windows cliente; o experimento não amplia esse suporte.

O módulo privado de entrada passou na compilação e análise estática. Implementa filas limitadas, validação dos pacotes e consultas de atributos condicionadas à autorização e a um perfil virtual explícito. Os 110 testes da política compartilhada passaram, incluindo combinações inválidas de flags, limites, perfis inconsistentes e preservação dos buffers quando uma consulta é recusada. Esses testes não carregam WDF, não autenticam um produtor de entrada e não demonstram isolamento de aplicativos.

A negociação de capacidades foi integrada à candidata do canal autenticado. Um teste com processos próprios confirmou a negociação válida. Outro manteve uma leitura assíncrona realmente pendente, provocou uma exceção no verificador de autorização e confirmou a recusa e a conclusão do cancelamento antes de liberar buffer, evento e estrutura da operação. A conexão com o serviço real do Windows e a tradução completa para as estruturas do protocolo continuam pendentes.

O método `GetClientMonitorData` da nova candidata passou a consultar essa negociação por uma ligação que permanece válida somente enquanto o cliente e a autorização estiverem ativos. O encerramento aguarda o fim dos leitores antes de liberar seu contexto. Negociação e revogação foram verificadas com processos próprios; o contrato do método e o encerramento com leitor concorrente também passaram. Esses testes não exercitam a chamada pelo serviço real do Windows, não atribuem um monitor físico e não implementam os demais métodos necessários para aceitar a conexão.

## Resultado na VM

Uma candidata UMDF independente foi preparada para testar o ciclo de vida das filas antes da ligação ao terminal. DLL e cliente compilaram com avisos tratados como erros e análise estática sem ocorrências. O INF passou em InfVerif `/w` e `/h`; o pacote passou no teste de requisitos de assinatura sem avisos. A candidata usa somente dados fixos do próprio ensaio, sem capturar periféricos ou encaminhar entrada ao console.

Na primeira execução, o pacote próprio foi assinado e armazenado na VM, com conferência das cópias e assinaturas. A criação do dispositivo falhou. Uma segunda rodada acrescentou diagnóstico e distinguiu a origem: [SwDeviceCreate](https://learn.microsoft.com/en-us/windows/win32/api/swdevice/nf-swdevice-swdevicecreate) retornou `E_INVALIDARG` (`0x80070057`) antes do callback, sem instância ou interface confirmada. Nenhuma leitura de fila WDF foi demonstrada. A causa do parâmetro recusado continua em investigação.

As duas rodadas terminaram com restauração externa verificada. A auditoria posterior confirmou o arquivo de referência e a ausência dos diretórios, pacotes próprios no DriverStore, certificados temporários, tarefas e registros próprios do dispositivo. Os coordenadores encerraram. A verificação do DriverStore enumerou o diretório com filtro explícito: uma verificação antiga que usava curinga com `LiteralPath` foi identificada como insuficiente e não foi reclassificada como evidência válida.

## Execução das filas após as correções

A definição completa de proprietário e grupo no descritor de segurança resolveu a recusa da criação: a instância e a interface próprias passaram a existir, com o dispositivo iniciado sem problema registrado. O acesso às filas ainda foi recusado. A candidata também corrigiu a exigência indevida de um identificador de iniciador: [WdfFileObjectGetInitiatorProcessId](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdffileobject/nf-wdffileobject-wdffileobjectgetinitiatorprocessid) pode retornar zero quando não existe essa associação. A identidade real do solicitante continuou obrigatória.

Uma coleta limitada ao provedor de eventos do próprio driver localizou a recusa seguinte. A inspeção da identidade e a abertura do processo funcionavam durante a impersonação da requisição, mas a consulta da sessão ocorria após o retorno ao contexto LocalService. [ProcessIdToSessionId](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-processidtosessionid) exige direitos de consulta sobre o processo. Essa consulta passou a ocorrer no contexto autorizado; o handle retido, o instante de criação, o caminho e o hash continuaram sujeitos às verificações posteriores. Nenhuma ACL de processo ou serviço foi ampliada.

A rodada corrigida carregou o driver e concluiu o teste real das filas WDF. O canal de teclado confirmou quatro consultas de status, uma leitura e um cancelamento; o de mouse confirmou três consultas, uma leitura e dois cancelamentos. Cada leitura conferiu seus dois registros fixos. Os dois arquivos continuaram utilizáveis após duplicar o handle e fechar o original. A instância própria deixou de estar presente depois de fechar o dispositivo; isso não foi contado como desinstalação do pacote.

O console e as identidades dos seus processos de sessão permaneceram iguais nas conferências anterior e posterior. O cliente e o coordenador encerraram com código zero. A restauração externa e a auditoria posterior passaram, incluindo ausência de pacote, arquivos, certificados, tarefas, registros próprios e sessão de coleta temporária.

Essa é uma prova de ciclo de vida e I/O de uma candidata isolada, com cliente próprio em sessão de serviço e dados sintéticos. Não executou o módulo nativo que exige outra sessão atribuída pelo Windows; não confirmou consultas nativas de atributos, consumidor em kernel, ligação ao RIM, periféricos físicos ou frames de desktop. O carregamento desta candidata de filas também não comprova carregamento do driver de display remoto.

## Bloqueio e próximo marco

O provedor do [laboratório 014](014-native-local-admission.md) ainda recusa a conexão por falta dos backends. Faltam o preenchimento completo e verdadeiro dos dados do cliente, o protocolo de licenciamento aplicável, a autenticação da segunda conta, a ligação à sessão realmente atribuída pelo Windows e a integração de entrada e display. A candidata de fila não resolve essas dependências por si só.

O próximo marco funcional é uma segunda sessão nativa aceita, com um aplicativo externo visível e controlável independentemente do console original, seguida de recuperação verificada. Compilar componentes, aprovar políticas ou armazenar um pacote não satisfaz esse marco. Não há previsão confiável de conclusão antes dessa prova.

Código operacional, binários, imagens do Windows, credenciais, identidades do ambiente e relatórios brutos permanecem privados. O [download experimental](../DOWNLOAD-INSTALL.md) continua contendo somente painel e diagnósticos, sem o motor.
