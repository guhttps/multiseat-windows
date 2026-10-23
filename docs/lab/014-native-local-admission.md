# 014 — canal local para admissão no provedor nativo

Atualizado em 2026-10-23. Estado: endpoint, autenticação do cliente local e retorno de sucesso em OnConnected observados. A prontidão e o encerramento completo da admissão continuam pendentes. Nenhum segundo terminal utilizável foi demonstrado.

## Escopo e origem da evidência

O ensaio continua limitado à VM descartável sem rede do laboratório nativo, com conta fictícia, checkpoint limpo e recuperação externa obrigatória. A identidade das chamadas é conferida por processo do serviço, instante de criação, módulo carregado e marcador exclusivo da rodada. Ativação COM pelo próprio teste e testes locais de contrato não contam como chamadas reais do serviço.

O [laboratório 013](013-native-listener-start.md) confirmou a entrada em StartListen e, em uma rodada posterior, a observação saudável do console. A candidata seguinte acrescentou um canal local de uso único para exercitar a admissão. Seus controles verificam identidade e sessão reais do cliente, processo e instante de criação, imagem esperada e solicitação de formato fixo; a identidade não é aceita apenas porque consta na mensagem. Não há transporte de senha ou fila administrativa genérica.

## Resultados preservados por tentativa

- A primeira preparação foi recusada pela política de execução de arquivos do convidado. O componente não chegou à etapa de registro nessa tentativa. A correção posterior incorporou as funções de formato fixo ao comando remoto revisado, sem alterar a política de execução do convidado.
- A tentativa seguinte terminou por prazo esgotado ao iniciar o observador. Não aprovou carregamento, endpoint ou admissão nessa rodada; a causa exata do atraso não foi estabelecida.
- A coleta posterior confirmou chamadas do serviço, mas StartListen falhou porque a estratégia de transferência COM exigia um marshaler não registrado. Não foi registrado um proxy fictício para contornar essa falha.
- A candidata seguinte verificou explicitamente que origem e trabalhador pertenciam ao MTA do mesmo processo. Essa estratégia segue a documentação de [apartamentos MTA](https://learn.microsoft.com/en-us/windows/win32/com/multithreaded-apartments). O serviço criou o endpoint real e StartListen retornou sucesso. O cliente, porém, terminou ao abrir o canal, antes de enviar a solicitação ou ter sua identidade autenticada pelo servidor. O código de saída inicial identificava a etapa, mas não o erro Win32. Foram observadas 39 amostras saudáveis do console, sem amostras não saudáveis; isso não transforma a admissão malsucedida em sucesso.
- A rodada com diagnóstico imediato capturou o erro Win32 **5, acesso negado**, na única tentativa de abertura do canal. O coordenador encerrou a observação repetida assim que identificou o cliente finalizado com falha, registrou a evidência negativa e coletou o diagnóstico dentro do prazo. O console e os processos de sessão foram preservados. A causa específica da recusa de acesso ainda precisa ser identificada; não foi atribuída a permissões ou nível de integridade sem observar a segurança efetiva do objeto.

Todas as tentativas acima que entraram no experimento terminaram com restauração externa verificada, ausência dos elementos próprios, retorno da configuração original do serviço e das conexões remotas e, nas rodadas de admissão, retorno da segurança original do diretório do laboratório. Uma tentativa posterior recusou iniciar antes de qualquer alteração experimental porque a VM não estava em execução; essa recusa não é uma rodada de admissão nem uma nova prova de restauração.

## Próxima prova e limites

A leitura da segurança efetiva usou o handle já existente do servidor, sem abrir outra conexão ou alterar acessos. A lista de acesso correspondeu à política esperada; não havia rótulo explícito de integridade, portanto o nível efetivo observado foi médio, com a política normal de impedir escrita de nível inferior. A rodada continuou com acesso negado e terminou com preservação do console e restauração verificada. Esses resultados não justificaram reduzir o nível de integridade do canal.

Um teste separado de IPC comum reproduziu a abertura com objetos temporários próprios do aplicativo, sem carregar o provedor nem alterar o serviço do Windows. Nesta imagem, permitir apenas os direitos iniciais de dados recusou a abertura; acrescentar somente a leitura de atributos fez a mesma operação funcionar. A candidata seguinte acrescenta esse direito de metadados. O teste também recusou alteração da lista de acesso, troca de proprietário, acesso genérico de escrita e criação de segunda instância pela API de servidor. A última verificação usou capacidade livre no canal para distinguir recusa de acesso de falta de espaço. Os objetos temporários foram fechados ao final; esse teste não é evidência de conexão no serviço real.

A [documentação de segurança de named pipes](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights) distingue permissões do objeto e direitos solicitados pelo cliente. O [controle obrigatório de integridade](https://learn.microsoft.com/en-us/windows/win32/secauthz/mandatory-integrity-control) é uma verificação adicional. A necessidade específica de leitura de atributos foi medida no teste, sem deduzir compatibilidade universal.

## Integração com a correção de leitura de atributos

A rodada seguinte confirmou a correção no serviço real: endpoint pronto, solicitação recebida e identidade do cliente validada contra os controles esperados. O cliente terminou com código zero depois do fechamento do canal. A chamada real de [OnConnected](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nf-wtsprotocol-iwrdsprotocollistenercallback-onconnected), porém, devolveu **E_POINTER**. Não houve sucesso de conexão, prontidão ou a sequência planejada de recusa e fechamento pelo serviço. O fechamento do canal e a saída zero do cliente não foram contados como prova dessas etapas.

O console e os processos de sessão foram preservados, e a restauração externa conferiu todos os estados esperados, incluindo segurança original do diretório, serviço, configuração de conexões remotas e ausência dos elementos do experimento. O transporte local autenticado avançou; a tentativa completa de admissão permaneceu negativa nessa rodada.

## Retorno de sucesso com a interface de display adicional

A rodada posterior implementou a interface separada `IWRdsWddmIddProps1` definida no SDK, disponível somente depois da validação real do transporte. Ela preservou a identidade COM e retornou indisponibilidade nos métodos de hardware e carregamento. A interface antiga que transfere um handle continuou recusada. Nenhum identificador de driver ou handle fictício foi fornecido.

O serviço real consultou essa interface e `OnConnected` retornou **S_OK**. Processo, instante de criação e marcador da rodada coincidiram com a coleta independente. O console e seus processos de sessão foram preservados. Não foram observados sucesso em `OnReady`, recusa em `AcceptConnection`, fechamento pelo serviço ou saída zero do cliente nessa janela. O resultado completo da admissão continua negativo; o retorno de um método não comprova um terminal.

A restauração externa de dois minutos passou, com ausência dos arquivos, tarefas e registros próprios, retorno da segurança do diretório e configuração original do serviço e das conexões remotas. O coordenador próprio encerrou. Uma falha anterior de preparação e uma comparação incorreta entre hashes em maiúsculas/minúsculas permanecem registradas separadamente; a auditoria corrigida confirmou o estado restaurado sem fabricar um recibo anterior à execução.

O diagnóstico seguinte distingue ausência de chamada de recusa das verificações ou falha na gravação do registro. A [sequência documentada de OnReady](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nf-wtsprotocol-iwrdsprotocolconnectioncallback-onready) exige a chamada anterior de `GetLogonErrorRedirector`; esse requisito foi preservado. A candidata acrescentou espera limitada e comunicação explícita da tentativa incompleta pelo callback real de [BrokenConnection](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nf-wtsprotocol-iwrdsprotocolconnectioncallback-brokenconnection), fora de chamadas de entrada e sem alterar permissões. Ela passou em 100 ciclos locais de contrato e ciclo de vida, nas verificações de IPC próprio e na análise estática. Sua execução na VM ainda precisa de resultado próprio; esses testes não comprovam o fechamento pelo serviço.

A aprovação da admissão exigirá evidência própria de cliente autenticado, chamadas de conexão e prontidão, recusa explícita enquanto faltarem backends, encerramento real e preservação do console, seguida da recuperação obrigatória. Sucesso em StartListen não satisfaz esse conjunto.

Ainda não há login de segunda conta, terminal gráfico utilizável, monitor físico atribuído, entrada independente em aplicativos externos, áudio ou jogo comprovados por essa candidata. A presença de callbacks nesta imagem também não amplia o suporte documentado da plataforma. O [gate nativo](../architecture/0007-native-engine-gate.md) e a [lista de entregas pendentes](../REMAINING-WORK.md) continuam abertos. Código operacional, credenciais, identificadores e relatórios brutos permanecem privados.
