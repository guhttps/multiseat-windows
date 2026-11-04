# Laboratório 018: identidade do serviço e dados do cliente nativo

Atualizado em 2026-11-02. **Motor incompleto; segundo terminal nativo ainda não demonstrado.**

## Problema observado

A rodada que motivou o diagnóstico com o serviço real do Windows recebeu a solicitação local e validou a identidade do cliente, mas o cliente encerrou com código 32 ao conferir o processo servidor. A falha ocorreu antes da negociação de capacidades, de `OnConnected`, de `OnReady` e de `GetClientData` nessa rodada. Os resultados anteriores desses callbacks continuam sendo provas de outras revisões.

O código 32 agrupava quatro verificações: abertura do processo servidor com direitos de consulta e sincronização, obtenção do diretório do sistema, consulta do executável e comparação com o executável esperado. O código de saída sozinho não identifica qual delas falhou. Não atribuir essa falha à geometria nem presumir acesso negado sem a medição específica.

A recuperação externa e a verificação interna registraram restauração do laboratório, ausência dos arquivos próprios e retorno das permissões e configurações originais. Uma auditoria adicional não chegou a consultar o convidado: o supervisor havia colocado seu recibo fora da raiz permitida da execução. A correção manteve a restrição de caminhos, gravou o recibo dentro da raiz correspondente e passou na execução independente antes da nova rodada.

## Diagnóstico executado

Uma cópia diagnóstica do cliente real conserva as decisões e os códigos de saída. Ela registra separadamente a primeira verificação que falhou, o erro Win32 capturado imediatamente e o resultado de consultas auxiliares somente de leitura. Handles abertos pelas consultas auxiliares são fechados e não substituem o handle exigido pelo canal de produção.

O registro usa um arquivo de destino fixo, precriado pelo laboratório, com DACL protegida, proprietário administrativo e acesso de anexação limitado à conta fictícia do cliente. O cliente não cria, substitui ou trunca o arquivo. Tamanho, tipo do arquivo, links e caracteres permitidos são limitados. Os registros excluem caminhos, nomes de conta ou computador, SIDs, tokens, credenciais, endereços de rede e identificadores de autorização. Os resultados brutos permanecem privados.

O coletor vincula cada registro ao PID e ao instante de criação do processo lançado pelo coordenador. Uma falha na verificação inicial produz somente o registro de diagnóstico; o registro final de saída existe apenas quando a verificação inicial passou. A interpretação foi corrigida para distinguir esses dois formatos, com 21 casos locais aprovados, incluindo rejeição de identidade, sequência e tipos incorretos. Essa interpretação é metadado de diagnóstico, nunca autorização de uma conexão.

Os hashes dos 14 arquivos de fonte e dos quatro binários da entrega foram conferidos contra o recibo de compilação. Autoteste do cliente, testes do registrador e testes auxiliares encerraram com código zero. O registrador mediu as permissões de anexação e compartilhamento necessárias. A análise estática não registrou achados nessa compilação.

A rodada protegida executou após confirmação real do UAC. O registro foi vinculado ao processo lançado e identificou `OpenProcess` como a primeira falha: erro 5, acesso negado. As consultas separadas também receberam erro 5, tanto para consulta limitada quanto para sincronização. Portanto, não é uma negação exclusiva de sincronização. O PID do servidor do pipe coincidiu com o processo informado pelo gerenciador de serviços para o serviço esperado.

O experimento preservou a recusa de conexão e encerrou com código 32, como pretendido para medir a causa. A restauração obrigatória e a auditoria independente posterior passaram: arquivos próprios ausentes, permissões originais restauradas e configurações de serviço e conexão preservadas. Esse resultado aprovou a coleta e a recuperação. A correção do vínculo com o processo do serviço foi validada na rodada descrita adiante.

## Proteção da execução administrativa

A revisão conjunta encontrou uma janela entre conferir o hash da fonte e executá-la com privilégios. O novo inicializador lê e verifica seus bytes em memória e mantém handles sobre 42 arquivos e 32 diretórios da cadeia revisada. Escrita, substituição e troca dos caminhos são bloqueadas; as leituras compartilhadas e a criação dos resultados continuam permitidas. A descoberta de módulos é restrita às pastas do sistema no processo auxiliar.

Testes com processos próprios confirmaram que os bloqueios sobrevivem à morte do inicializador e do coordenador enquanto o processo de recuperação continua vivo, e são liberados quando o último detentor encerra. A rodada administrativa real terminou com código zero e todos os hashes preservados. A cópia dos artefatos para a VM também funcionou sob esses bloqueios. O auxiliar é finito e não aceita novos comandos após o ensaio.

## Correção validada no serviço real

Uma cópia nova substituiu a abertura direta negada por uma concessão explícita do próprio serviço, usando [DuplicateHandle](https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-duplicatehandle). A oferta tem tamanho fixo, versão, identificadores da solicitação e prazo. O cliente confere o objeto real recebido, seus direitos, vida, imagem e vínculo com o gerenciador de serviços antes de confirmar a oferta. O valor numérico transmitido não serve como identidade.

A revisão independente encontrou um caminho em que uma oferta válida já enfileirada poderia ser aceita após o prazo. A segunda cópia corrigiu a verificação antes da leitura, após as conclusões de I/O e antes da adoção. Doze cenários locais passaram, incluindo prazo vencido com oferta enfileirada, morte real com o pipe ainda aberto, objeto ou PID errado, direitos adicionais, oferta inválida e ausência de confirmação. Compilação com avisos tratados como erros, autotestes dos adaptadores e análise estática passaram. Foram conferidos os hashes de 17 fontes, cinco binários e nove origens preservadas.

O ensaio posterior na VM passou no serviço real: confirmação da oferta observada, abertura do processo destinatário somente com `PROCESS_DUP_HANDLE` e comparação do mesmo objeto antes e depois da duplicação. O cliente validou o handle mínimo e concluiu a negociação. `OnConnected`, `OnReady` e a continuação condicional de `AcceptConnection` retornaram sucesso; `GetClientData` observou as políticas e manteve `E_NOTIMPL`. O fechamento foi observado e o cliente encerrou com código zero.

A restauração e a auditoria independente posterior passaram, sem arquivos próprios remanescentes e com permissões e configurações originais restauradas. O inicializador protegido dessa rodada verificou 38 arquivos e 33 diretórios e encerrou com código zero e hashes preservados. **O bloqueio de acesso foi resolvido; segunda conta e segundo terminal ainda não foram demonstrados.**

## Dados do cliente e trabalho seguinte

O módulo de dados do cliente tem validação local de geometria, teclado, fuso e políticas. Uma prova separada entre processos exercitou a transferência de geometria e fatos do cliente, mas usava um evento local como substituto da vida do peer e uma conferência contínua sempre verdadeira. Isso não prova a segurança do vínculo com o serviço real e não pode ser promovido a integração de produção.

Uma nova revisão substituiu esse evento por um handle do processo efetivamente autenticado. A compilação com avisos tratados como erros e análise estática passou, com zero achados registrados. Dezesseis cenários com processos separados passaram: saída normal, término abrupto, morte do peer enquanto outro processo mantém o canal aberto, morte durante leitura pendente, fechamento do canal com o peer ainda vivo, desconexão, revogação, expiração e recusas de identidade. As leituras posteriores à invalidação são recusadas e os dados aceitos são limpos; os caminhos de encerramento conferiram a ausência de I/O pendente. Os hashes dos 14 arquivos de fonte, dos três binários e das nove origens preservadas foram conferidos independentemente.

Essa revisão também corrigiu a ordem da conferência do token: o token próprio é aberto antes da impersonação em nível de identificação; o token do cliente é obtido em seguida, a impersonação termina imediatamente e a comparação de usuário usa os dois tokens já abertos. Um diagnóstico separado confirmou o erro anterior, sem ampliar o nível de impersonação ou retirar a comparação de usuário. A revisão anterior e seus resultados parciais foram preservados.

Esses 16 cenários validaram o fluxo entre processos próprios. A integração seguinte ligou os fatos à autorização real do serviço: sessão esperada da conta autenticada, PID e instante de criação do processo, identidade do cliente do pipe, transporte conectado, autorização de uso único, contexto do serviço, encerramento e prazo. A comparação do token continua usando a conta autorizada do cliente, e não a conta do serviço.

A geometria e os fatos usam o mesmo canal e o mesmo identificador de negociação, com uma geração nova. O próprio cliente mede teclado, fuso e presença de mouse. O serviço mantém esses dados em um leitor que revalida a conexão e encerra as leituras antes de liberar o estado. Os registros contêm somente categorias e validade, sem valores de fuso, layout, nomes de conta ou identificadores de autorização.

A espera após a negociação agora compartilha a mesma implementação no cliente real e no cliente de teste. Ela observa o processo real do serviço além do evento de leitura e cancela e drena a operação pendente se o processo morrer ou o prazo vencer. Dezessete cenários passaram, incluindo morte com o pipe retido por outro processo, término abrupto, identificador ou geração incorretos, revogação que limpa os dados, prazo durante leitura e dados inesperados. Os autotestes dos adaptadores passaram; cinco compilações com avisos tratados como erros e análise estática terminaram sem achados. Foram conferidos 22 arquivos de fonte, cinco binários e 54 origens preservadas.

A rodada na VM confirmou a oferta do handle mínimo, o vínculo com o mesmo processo do cliente e a recepção dos fatos pelo serviço real. Os callbacks de conexão concluíram, GetClientData manteve E_NOTIMPL e o cliente encerrou com código zero. A restauração obrigatória e a auditoria independente passaram, com ausência dos arquivos próprios e retorno das permissões e configurações originais. O inicializador verificou 38 arquivos e 33 diretórios, terminou em código zero e preservou os hashes. Isso comprova essa integração na VM; não comprova uma segunda sessão interativa nem o segundo terminal.

O incremento seguinte integrou a configuração própria do provedor e projetou os fatos vivos em GetClientData, preservando as políticas fornecidas pelo Windows. O getter recusa quando faltar configuração ou quando o vínculo for invalidado. Os resultados estão abaixo.

O direito de duplicação exigido sobre o processo destinatário foi restrito ao helper próprio e descartável. Ele não é um simples direito de leitura: pode permitir acesso mais amplo ao destinatário, conforme os [direitos de acesso a processos](https://learn.microsoft.com/en-us/windows/win32/procthread/process-security-and-access-rights). O handle do serviço entregue ao cliente continua limitado a consulta e sincronização. A DACL do serviço e os aplicativos externos não receberam essa concessão.

A correção do acesso ao processo e a projeção de dados são etapas da cadeia de segunda conta, sessão interativa, terminal, display e entrada. Elas não fornecem credenciais, não concluem licenciamento nem demonstram admissão de uma sessão.

## Projeção dos dados e sequência observada em 31/10

A nova candidata lê a configuração da chave própria do listener, conferindo classe, nome e tipo do protocolo. A seleção do tipo é experimental: não representa uma faixa oficialmente atribuída nem garante suporte no Windows cliente. A resposta é construída zerada, preserva as seis políticas recebidas e inclui somente os fatos disponíveis. Em qualquer falha, todos os bytes da saída original permanecem intactos. Presença de mouse não é entrada habilitada; `fMouse` permanece falso enquanto não existir roteamento. Esses campos e as políticas estão descritos em [WTS_CLIENT_DATA](https://learn.microsoft.com/en-us/windows/win32/api/wtsdefs/ns-wtsdefs-wts_client_data).

A autorização composta é revalidada antes de `OnConnected` e da publicação dos dados. Passaram 315 verificações de configuração/projeção, incluindo as 64 combinações das seis políticas, recusa de valores inválidos e alterações durante a leitura. Os 17 cenários entre processos e os autotestes do cliente/DLL também passaram. Seis compilações usaram avisos como erros e análise estática; houve uma exceção explícita e limitada ao aviso C6553 nas declarações do SDK, cuja anotação de `RegOpenKeyExW` marca um parâmetro escalar como opcional. Não foi desabilitada a análise do código do projeto. Foram conferidos 26 arquivos de fonte, seis binários e 77 origens preservadas.

O avaliador de resultados passou em 62 verificações locais. Seu critério exigia entrega de dados, dados de monitores, recusa em `GetUserCredentials` e encerramento. A ordem publicada para [IWRdsProtocolConnection](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nn-wtsprotocol-iwrdsprotocolconnection) coloca credenciais antes da interface de licenciamento; a execução desta imagem do Windows seguiu outra sequência.

| Chamada observada no serviço real | Resultado |
|---|---|
| AcceptConnection | S_OK: continuação da solicitação |
| GetClientData | S_OK; preparação dos dados registrada e chamada seguinte observada |
| GetClientMonitorData | S_OK |
| GetUserCredentials | Não observado nessa rodada |
| GetLicenseConnection | E_NOTIMPL retornado pelo nosso componente, com ponteiro nulo |
| Close e fim do trabalhador | Encerramento observado; cliente terminou em código zero |

**O experimento completo permaneceu reprovado**, por receber um método fora da sequência autorizada pelo avaliador. O critério e o resultado originais foram preservados. Uma análise posterior separada vinculou os eventos ao processo real do serviço e confirmou a entrega dos dados, a solicitação da interface de licenciamento e o encerramento. Essa constatação não equivale a licença concedida, segunda conta autenticada ou sessão admitida; também não é uma evidência de rejeição de licença pelo Windows, pois a interface ainda não foi fornecida pelo nosso código.

O console permaneceu saudável. A restauração obrigatória e a auditoria independente passaram: recursos próprios ausentes, permissões originais restauradas e configurações de serviço e conexão preservadas. O inicializador elevado concluiu com código zero e hashes preservados. A aprovação da recuperação não transforma o experimento em aprovado.

O próximo componente é o transporte e o objeto de [IWRdsProtocolLicenseConnection](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nn-wtsprotocol-iwrdsprotocollicenseconnection), mantendo as decisões reais do serviço. O parser do cliente testado isoladamente não prova essa integração. A interface documenta suporte em Windows Server; a observação experimental na VM cliente não amplia esse suporte. Continuam pendentes autenticação, sessão utilizável, display, entrada e a validação do produto.

## Critério de avanço

### Transporte de mensagens verificado em 03/11

Uma candidata privada separada implementou quadros de tamanho limitado, vinculados a nonce, geração e sequência, sem interpretar uma confirmação de transporte como conclusão do licenciamento. Um quadro inválido, repetido ou de direção incorreta encerra o receptor. O nonce vincula os dados à negociação; não substitui autenticação do processo.

Passaram 203 verificações, incluindo 20 cenários com pipes locais reais. Foram exercitados o modo de leitura por mensagem e por bytes, limite máximo, mensagens truncadas ou excedentes, fragmentação, prazo, revogação e exceção no guard, morte de um processo filho próprio e cancelamento de leitura e escrita pendentes. A operação assíncrona é cancelada e sua conclusão é aguardada antes de liberar evento ou buffer. O guard de autorização usado nessa suíte é uma fixture, sem prova nova de identidade de produção ou de comunicação com o serviço do Windows.

A compilação final usou `/W4 /WX /analyze`, sem achados nem supressões adicionadas. Um aviso anterior de uso excessivo de pilha foi corrigido movendo o buffer de caminho do teste para o heap; a compilação reprovada permanece registrada. Quatro fontes e 103 origens congeladas foram conferidas. Não houve UAC, execução de VM, alteração de serviço, driver, rede ou Parsec nessa etapa.

Essa primeira etapa limitou-se ao enquadramento e transporte. O parser isolado e esses testes não comprovam uma negociação real de licenciamento. Os resultados da rodada anterior continuam inalterados.

### Parser e identidade ligados em 03/11

O parser raw do cliente passou a receber PDUs pelo transporte local e devolver a resposta pelo mesmo canal. O canal conserva a autorização recebida e acrescenta conferências do PID/sessão atestados pelo pipe, handle real do processo e instante de criação. A identidade e o prazo são revalidados antes de invocar o parser e antes de publicar a resposta. Uma confirmação de transporte não equivale a `ProtocolComplete` nem a licença concedida.

A suíte passou em **136 verificações e 16 cenários entre processos próprios reais**. Além do PID, criação e imagem esperados, o laboratório verifica o token primário do processo, o mesmo logon e o contexto conectado por impersonação. A reversão é conferida, sem deixar token na thread. Essa política de mesmo logon pertence ao laboratório; não substitui a política do serviço real para o cliente convidado.

O caminho positivo devolveu o `NEW_LICENSE_REQUEST` gerado pelo parser a partir de uma solicitação e certificado efêmeros do próprio teste, sem servidor Windows conectado. Passaram também recusa do parser, nonce/geração/direção incorretos, repetição, mensagem fora de fase, PID/criação/imagem incorretos, cliente anônimo, encerramento concorrente, revogação, prazo e morte do processo autenticado. Nesse último cenário, um processo auxiliar conservou o handle do pipe: o endpoint recusou a continuidade e cancelou/drenou a leitura mesmo com o transporte ainda aberto. Ao final, não restaram processos do teste ou operações pendentes.

A entrada do cliente descartável da VM foi adaptada em candidata separada. Ela preserva as verificações iniciais de ambiente, arquivos protegidos, token, hash da imagem, lançamento e handle do serviço. O trabalhador do parser continua sob o vínculo real de processo/pipe e a consulta ao SCM. A ligação usa o nonce da negociação autenticada anterior e uma geração não nula derivada dele; esse vínculo não concede autoridade. Requer um diretório privado de cache previamente preparado pelo coordenador da VM. Os resultados do trabalhador são categorias de encerramento, sem afirmar sucesso de licenciamento.

Essa entrada compilou com `/W4 /WX /analyze`, passou em 11 verificações de contexto/configuração e no autoteste de política. Fora do ambiente permitido, retornou código 10 antes de alterar ACL ou abrir o canal. Foram conferidos cinco arquivos da integração, quatro da entrada adaptada, duas imagens desta última e 707 dependências; as fontes anteriores permaneceram intactas. A análise estática cobre o código próprio, com cabeçalhos externos excluídos. Os testes do parser usaram nomes fictícios e não publicaram PDUs, identificadores ou caminhos locais.

**Limite atual:** o caminho entre processos próprios foi executado; a entrada adaptada foi compilada e validada localmente, mas ainda não executada na VM. O objeto do servidor solicitado por `GetLicenseConnection`, sua autoridade de serviço/permit e a negociação real continuam pendentes. Não houve UAC, mudança de serviço, driver ou rede nesta etapa. O motor não oferece dois terminais nativos utilizáveis.

### Próxima integração

A correção do acesso ao processo, a recepção dos fatos reais do cliente e a entrega de dados ao serviço foram observadas. O próximo bloqueio é a interface de licenciamento ainda não implementada. Preserve o resultado completo reprovado da rodada de 31/10 e sua recuperação aprovada. A conclusão do motor depende do [gate nativo](../architecture/0007-native-engine-gate.md) e da [lista de trabalho restante](../REMAINING-WORK.md), incluindo segunda conta, terminal, aplicativos externos, isolamento, apresentação física, entrada, áudio e recuperação.
