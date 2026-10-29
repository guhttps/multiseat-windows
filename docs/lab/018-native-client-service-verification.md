# Laboratório 018: identidade do serviço e dados do cliente nativo

Atualizado em 2026-10-29. **Motor incompleto; segundo terminal nativo ainda não demonstrado.**

## Problema observado

A rodada mais recente com o serviço real do Windows recebeu a solicitação local e validou a identidade do cliente, mas o cliente encerrou com código 32 ao conferir o processo servidor. A falha ocorreu antes da negociação de capacidades, de `OnConnected`, de `OnReady` e de `GetClientData` nessa rodada. Os resultados anteriores desses callbacks continuam sendo provas de outras revisões.

O código 32 agrupava quatro verificações: abertura do processo servidor com direitos de consulta e sincronização, obtenção do diretório do sistema, consulta do executável e comparação com o executável esperado. O código de saída sozinho não identifica qual delas falhou. Não atribuir essa falha à geometria nem presumir acesso negado sem a medição específica.

A recuperação externa e a verificação interna registraram restauração do laboratório, ausência dos arquivos próprios e retorno das permissões e configurações originais. Uma auditoria adicional não chegou a consultar o convidado: o supervisor havia colocado seu recibo fora da raiz permitida da execução. A correção mantém a restrição de caminhos e passa a gravar o recibo dentro da raiz correspondente; a nova auditoria ainda precisa executar.

## Incremento preparado

Uma cópia diagnóstica do cliente real conserva as decisões e os códigos de saída. Ela registra separadamente a primeira verificação que falhou, o erro Win32 capturado imediatamente e o resultado de consultas auxiliares somente de leitura. Handles abertos pelas consultas auxiliares são fechados e não substituem o handle exigido pelo canal de produção.

O registro usa um arquivo de destino fixo, precriado pelo laboratório, com DACL protegida, proprietário administrativo e acesso de anexação limitado à conta fictícia do cliente. O cliente não cria, substitui ou trunca o arquivo. Tamanho, tipo do arquivo, links e caracteres permitidos são limitados. Os registros excluem caminhos, nomes de conta ou computador, SIDs, tokens, credenciais, endereços de rede e identificadores de autorização. Os resultados brutos permanecem privados.

O coletor vincula cada registro ao PID e ao instante de criação do processo lançado pelo coordenador. Uma falha na verificação inicial produz somente o registro de diagnóstico; o registro final de saída existe apenas quando a verificação inicial passou. A interpretação foi corrigida para distinguir esses dois formatos, com 21 casos locais aprovados, incluindo rejeição de identidade, sequência e tipos incorretos. Essa interpretação é metadado de diagnóstico, nunca autorização de uma conexão.

Os hashes dos 14 arquivos de fonte e dos quatro binários da entrega foram conferidos contra o recibo de compilação. Autoteste do cliente, testes do registrador e testes auxiliares encerraram com código zero. O registrador mediu as permissões de anexação e compartilhamento necessárias. A análise estática não registrou achados nessa compilação. Esses testes locais não identificam ainda a causa na VM.

Uma rodada finita foi preparada para auditar a recuperação anterior, executar esse cliente na VM isolada, aguardar a restauração obrigatória e realizar uma nova auditoria independente. A revisão conjunta identificou a necessidade de impedir a alteração das fontes entre a conferência do hash e a execução administrativa; essa proteção está em implementação antes de solicitar outro UAC. A execução também depende da confirmação real do Windows. Não há resultado aprovado dessa nova rodada neste registro.

## Dados do cliente e trabalho seguinte

O módulo de dados do cliente tem validação local de geometria, teclado, fuso e políticas. Uma prova separada entre processos exercitou a transferência de geometria e fatos do cliente, mas usava um evento local como substituto da vida do peer e uma conferência contínua sempre verdadeira. Isso não prova a segurança do vínculo com o serviço real e não pode ser promovido a integração de produção.

Uma nova revisão substituiu esse evento por um handle do processo efetivamente autenticado. A compilação com avisos tratados como erros e análise estática passou, com zero achados registrados. Dezesseis cenários com processos separados passaram: saída normal, término abrupto, morte do peer enquanto outro processo mantém o canal aberto, morte durante leitura pendente, fechamento do canal com o peer ainda vivo, desconexão, revogação, expiração e recusas de identidade. As leituras posteriores à invalidação são recusadas e os dados aceitos são limpos; os caminhos de encerramento conferiram a ausência de I/O pendente. Os hashes dos 14 arquivos de fonte, dos três binários e das nove origens preservadas foram conferidos independentemente.

Essa revisão também corrigiu a ordem da conferência do token: o token próprio é aberto antes da impersonação em nível de identificação; o token do cliente é obtido em seguida, a impersonação termina imediatamente e a comparação de usuário usa os dois tokens já abertos. Um diagnóstico separado confirmou o erro anterior, sem ampliar o nível de impersonação ou retirar a comparação de usuário. A revisão anterior e seus resultados parciais foram preservados.

A prova nova usa o papel de servidor próprio do teste. O vínculo positivo com o processo real do serviço Windows, sua autorização e o consumidor do provedor continuam pendentes. O vínculo do cliente ao processo do serviço recebeu somente uma prova negativa: recusa de um servidor de teste fora da Sessão 0. A autorização do lado do serviço não foi exercitada. Não promover esses resultados a autenticação de produção ou a segundo terminal.

O cliente dessa revisão exige um handle do processo do serviço com consulta e sincronização, justamente a abertura incluída no código 32. A próxima medição precisa mostrar se esse direito foi negado ou se outra verificação falhou. Preservar o vínculo ao processo real; não substituí-lo por um evento nem omitir a sincronização para fazer o teste passar.

Após identificar e corrigir a falha do cliente sem reduzir essas verificações, faltam concluir os dados reais do provedor e demonstrar a cadeia completa de segunda conta, sessão interativa, terminal, display e entrada. O observador atual continua recusando dados incompletos; não fornece credenciais, não conclui licenciamento, não autentica uma segunda conta nem anuncia admissão de uma sessão.

## Critério de avanço

O resultado imediato necessário é uma medição vinculada à execução real que explique o código 32, com restauração e auditoria independentes verificadas. Passar nessa etapa ainda não demonstra dois postos. A conclusão do motor depende do [gate nativo](../architecture/0007-native-engine-gate.md) e da [lista de trabalho restante](../REMAINING-WORK.md), incluindo aplicativos externos, isolamento, apresentação física, entrada, áudio e recuperação.
