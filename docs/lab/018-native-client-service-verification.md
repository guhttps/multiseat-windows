# Laboratório 018: identidade do serviço e dados do cliente nativo

Atualizado em 2026-10-29. **Motor incompleto; segundo terminal nativo ainda não demonstrado.**

## Problema observado

A rodada mais recente com o serviço real do Windows recebeu a solicitação local e validou a identidade do cliente, mas o cliente encerrou com código 32 ao conferir o processo servidor. A falha ocorreu antes da negociação de capacidades, de `OnConnected`, de `OnReady` e de `GetClientData` nessa rodada. Os resultados anteriores desses callbacks continuam sendo provas de outras revisões.

O código 32 agrupava quatro verificações: abertura do processo servidor com direitos de consulta e sincronização, obtenção do diretório do sistema, consulta do executável e comparação com o executável esperado. O código de saída sozinho não identifica qual delas falhou. Não atribuir essa falha à geometria nem presumir acesso negado sem a medição específica.

A recuperação externa e a verificação interna registraram restauração do laboratório, ausência dos arquivos próprios e retorno das permissões e configurações originais. Uma auditoria adicional não chegou a consultar o convidado: o supervisor havia colocado seu recibo fora da raiz permitida da execução. A correção manteve a restrição de caminhos, gravou o recibo dentro da raiz correspondente e passou na execução independente antes da nova rodada.

## Diagnóstico executado

Uma cópia diagnóstica do cliente real conserva as decisões e os códigos de saída. Ela registra separadamente a primeira verificação que falhou, o erro Win32 capturado imediatamente e o resultado de consultas auxiliares somente de leitura. Handles abertos pelas consultas auxiliares são fechados e não substituem o handle exigido pelo canal de produção.

O registro usa um arquivo de destino fixo, precriado pelo laboratório, com DACL protegida, proprietário administrativo e acesso de anexação limitado à conta fictícia do cliente. O cliente não cria, substitui ou trunca o arquivo. Tamanho, tipo do arquivo, links e caracteres permitidos são limitados. Os registros excluem caminhos, nomes de conta ou computador, SIDs, tokens, credenciais, endereços de rede e identificadores de autorização. Os resultados brutos permanecem privados.

O coletor vincula cada registro ao PID e ao instante de criação do processo lançado pelo coordenador. Uma falha na verificação inicial produz somente o registro de diagnóstico; o registro final de saída existe apenas quando a verificação inicial passou. A interpretação foi corrigida para distinguir esses dois formatos, com 21 casos locais aprovados, incluindo rejeição de identidade, sequência e tipos incorretos. Essa interpretação é metadado de diagnóstico, nunca autorização de uma conexão.

Os hashes dos 14 arquivos de fonte e dos quatro binários da entrega foram conferidos contra o recibo de compilação. Autoteste do cliente, testes do registrador e testes auxiliares encerraram com código zero. O registrador mediu as permissões de anexação e compartilhamento necessárias. A análise estática não registrou achados nessa compilação.

A rodada protegida executou após confirmação real do UAC. O registro foi vinculado ao processo lançado e identificou `OpenProcess` como a primeira falha: erro 5, acesso negado. As consultas separadas também receberam erro 5, tanto para consulta limitada quanto para sincronização. Portanto, não é uma negação exclusiva de sincronização. O PID do servidor do pipe coincidiu com o processo informado pelo gerenciador de serviços para o serviço esperado.

O experimento preservou a recusa de conexão e encerrou com código 32, como pretendido para medir a causa. A restauração obrigatória e a auditoria independente posterior passaram: arquivos próprios ausentes, permissões originais restauradas e configurações de serviço e conexão preservadas. Isso aprova a coleta e a recuperação; o vínculo do cliente com o processo do serviço ainda precisa ser corrigido.

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

Esses 16 cenários validam o fluxo de fatos entre processos próprios. Sua ligação à autorização real do serviço e ao consumidor do provedor continua pendente. A validação do handle do serviço foi concluída na revisão descrita acima; ela ainda não promove o fluxo completo de fatos a integração de produção ou a segundo terminal.

O próximo incremento deve integrar os fatos medidos pelo próprio cliente ao canal que já negocia com o serviço real, combinando a verificação do peer com a autorização existente do serviço e o prazo. Também deve qualificar o encerramento após a negociação quando o serviço morre e outro processo mantém o pipe aberto. Os dados do provedor devem continuar recusados enquanto sua configuração real estiver ausente.

O direito de duplicação exigido sobre o processo destinatário foi restrito ao helper próprio e descartável. Ele não é um simples direito de leitura: pode permitir acesso mais amplo ao destinatário, conforme os [direitos de acesso a processos](https://learn.microsoft.com/en-us/windows/win32/procthread/process-security-and-access-rights). O handle do serviço entregue ao cliente continua limitado a consulta e sincronização. A DACL do serviço e os aplicativos externos não receberam essa concessão.

Após identificar e corrigir a falha do cliente sem reduzir essas verificações, faltam concluir os dados reais do provedor e demonstrar a cadeia completa de segunda conta, sessão interativa, terminal, display e entrada. O observador atual continua recusando dados incompletos; não fornece credenciais, não conclui licenciamento, não autentica uma segunda conta nem anuncia admissão de uma sessão.

## Critério de avanço

A medição do código 32, a correção do acesso ao processo e as auditorias de recuperação foram concluídas. O próximo resultado necessário é integrar os fatos reais do cliente e a configuração legítima do provedor, mantendo autorização, recusa de dados incompletos e restauração. A conclusão do motor depende do [gate nativo](../architecture/0007-native-engine-gate.md) e da [lista de trabalho restante](../REMAINING-WORK.md), incluindo segunda conta, terminal, aplicativos externos, isolamento, apresentação física, entrada, áudio e recuperação.
