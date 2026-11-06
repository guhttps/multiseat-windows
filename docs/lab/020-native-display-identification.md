# Laboratório 020: identificação do pacote de display nativo

Atualizado em 2026-11-06. **GetHardwareId retornou S_OK no serviço real da VM, após a conferência do registro protegido e do pacote.** O ensaio completo ficou reprovado porque a sequência avançou para notificações fora do escopo previsto. A observação foi conferida separadamente, preservando o resultado original. Ainda não há carregamento de display, frames de desktop, segunda conta autenticada ou dois terminais nativos utilizáveis comprovados.

## Problema e mudança

O [laboratório 019](019-native-input-backend.md) observou a entrega dos dois canais reais de entrada ao serviço. A conexão parou na chamada seguinte, `GetHardwareId`, com `ERROR_NOT_READY`. Informar um texto constante sem conferir o pacote não demonstraria disponibilidade do driver nem segurança do vínculo.

A candidata liga `GetHardwareId` a uma ponte pertencente à conexão. Exige a autoridade composta autenticada do serviço e a atribuição viva recebida em `NotifySessionId`. Antes de publicar o identificador próprio, confere um registro protegido da implantação e o pacote efetivamente armazenado no DriverStore. Na V34, o serviço concluiu essa conferência, recebeu o identificador e chamou `ConnectNotify`, que o componente ainda recusa com `ERROR_NOT_READY`.

## Autorização e ciclo de vida

- Registro com esquema, versão, tamanho e campos reservados fixos; mesma execução e provisionamento; processo real do serviço, instante de criação, imagem, token e prazos conferidos. Não contém credenciais nem concede direitos no Windows.
- Diretório e arquivo protegidos, proprietário administrativo e leitura limitada ao serviço; recusa de redirecionamentos, múltiplos vínculos e substituição de caminhos. Os handles conferidos permanecem retidos durante o uso.
- INF publicado resolvido por SetupAPI para o pacote próprio; DLL e catálogo conferidos por hash integral e certificado fixado. O INF é hasheado pelo mesmo handle retido. A confiança usa o cache local do Windows e não comprova atualização de revogação de certificados.
- Sessão e autorização revalidadas durante a conferência. O identificador publicado é próprio do projeto; nenhum identificador de hardware pessoal é publicado.
- Capacidade do buffer medida em elementos `WCHAR`, incluindo o terminador. Em falha, somente o primeiro elemento disponível é limpo; buffer insuficiente não inicia a conferência do pacote.
- Encerramento sinaliza cancelamento antes de aguardar a operação e drena a conferência antes de liberar os contextos emprestados. Um descarregamento de outra sessão não revoga os recursos desta conexão.

A conferência é somente de leitura: não instala pacote, cria licença ou dispositivo, modifica certificados ou altera a segurança do host. Chamadas de confiança do Windows não são forçadamente interrompíveis; os ensaios na VM continuam exigindo recuperação externa.

## Contrato do display

A candidata usa `IWRdsWddmIddProps1` do SDK 10.0.26100.0. Seu `OnDriverLoad` recebe `ULONG` e **texto emprestado `LPCWSTR` da instância**, conforme o header efetivamente compilado. Esse texto não é um handle e não deve ser fechado ou reinterpretado como um. A [projeção da interface da Microsoft](https://microsoft.github.io/windows-docs-rs/doc/windows/Win32/System/RemoteDesktop/struct.IWRdsWddmIddProps1.html) também distingue esse contrato.

O carregamento continua recusado com `ERROR_NOT_READY`. Esta candidata ainda não fornece a autorização separada de início do driver nem o canal da instância ou frames. As [regras de IDDs remotos](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/iddcx1.4-updates-for-remote-idds) exigem que a criação pelo Windows e as opções do adaptador correspondam à sessão remota; criar um dispositivo no console não substitui essa condição.

## Verificações locais concluídas

| Verificação | Resultado | Limite |
| --- | --- | --- |
| Identificação, registro, buffers e ciclo de vida | 62 verificações, zero falhas | Sucessos de pacote usam fixture explícita em executável separado |
| Revogação, conflito e cancelamento em andamento | Aprovados na mesma suíte | Não comprovam o prazo de chamadas de confiança do Windows |
| Fábrica de produção no processo comum do host | Recusa aprovada | Nenhum pacote ou driver foi aberto positivamente no host |
| Contratos da DLL do provedor | Aprovados | Preservam as recusas quando a ponte não está vinculada |
| Permissões do registro e avaliação de ACLs do Windows | 23 verificações, zero falhas | Descritores em memória; não usa o token real do serviço |
| Produtor, formato PowerShell/C++ e recusa no host | 20 verificações, zero falhas | Não cria um registro na VM nem altera ACLs do host |
| Atribuição de sessão na DLL | 26 verificações, zero falhas | Teste local, separado da observação real do laboratório 019 |
| Compilação e análise estática | Aprovadas com avisos como erros | Não executam o driver |

O executável de testes usa I/O de fixture. A DLL fixa a fábrica de conferência real e não oferece uma chave para selecionar a fixture, um pacote falso ou um caminho arbitrário. O recibo registra fontes, dependências, binários, compilador e header RDS utilizados; referências congeladas foram reconferidas.

A [atualização para FreeRDP 3.33.0](../FREERDP-UPDATE-20261105.md) passou separadamente em um controle limitado com o serviço real e o provedor anterior congelado: identidade, fatos, atribuição e recusa esperada da entrada foram observados, com restauração e auditoria independente aprovadas. Esse controle de dependência **não executou o provedor deste laboratório** e não valida o display.

## Próxima prova

A revisão do produtor identificou que a política de leitura herdada do host do driver era para `LocalService`, enquanto o TermService do laboratório usa `NetworkService`. O registro de identificação agora tem política própria: SYSTEM e Administradores com controle total e **somente o SID específico do TermService com leitura**. A conta NetworkService genérica, LocalService, Everyone, escrita pelo serviço e ACEs adicionais são recusadas. Essa correção não altera a política separada de início do driver. As 23 verificações acima incluem a avaliação dessa regra pela API Authz do Windows em memória; essa avaliação não fabrica um token de serviço nem comprova acesso na conexão real.

O produtor verifica execução, baseline, fontes e provedor fixados, pacote assinado no DriverStore e processo real do serviço antes de criar exclusivamente o registro de 400 bytes. O arquivo tem vida curta, nenhuma credencial e não é a autorização de início do driver. O formato gerado pelo PowerShell foi aceito pelo consumidor C++ de teste, junto dos descritores de segurança. O programa de produção recusou execução no host antes de qualquer ação de convidado.

O roteiro divide a preparação: assinatura e armazenamento dos pacotes antes do reinício da VM; permissões vinculadas ao PID atual e registro de identificação depois dele. A assinatura do display reutiliza apenas o certificado não exportável criado para a mesma execução do pacote de entrada. O watchdog externo permanece em 120 segundos, com reserva de 20 segundos para mutações e 5 para a coleta somente de leitura. Os planos passaram na validação de arquivos e sintaxe, 22 verificações do veredito e 11 da janela de leitura.

## Execuções e correções medidas

Após um UAC inicialmente cancelado sem execução, novas autorizações permitiram sete rodadas concluídas. Os planos consumidos e seus resultados foram preservados; nenhuma falha foi convertida em aprovação.

| Rodada | Resultado observado | Correção decorrente |
| --- | --- | --- |
| V28 | Pacote de entrada preparado; preparação do display recusou a presença da árvore de política de certificados | A leitura passou a distinguir os três contêineres padrão vazios de uma política com conteúdo; nenhuma política foi removida ou alterada |
| V29 | A forma vazia permitida foi observada; o script falhou ao consultar o certificado preparado | Removida colisão com a variável automática `$input` do PowerShell; os dados capturados já continham o campo esperado |
| V30 | Ambos os pacotes próprios assinados e armazenados; vínculo de entrada recusou o caminho retornado pelo Windows | Comparação do caminho próprio passou a respeitar a semântica de maiúsculas/minúsculas do Windows; prefixo fechado, hashes, assinaturas e recusas de redirecionamento foram preservados |
| V31 | Interface de entrada disponível e registro protegido de 400 bytes criado; início do cliente recusado por tempo restante insuficiente | Preparada substituição das esperas fixas por observação de prontidão e remoção de consulta redundante de início de serviço, mantendo as margens |
| V32 | Cliente iniciado dentro da margem; serviço abriu o registro e chegou à sua validação, mas `GetHardwareId` retornou `E_ACCESSDENIED`; fechamento real e saída zero do cliente observados | Os ticks capturados comprovaram que o registro já estava expirado na chamada. Preparada criação imediatamente antes da publicação atômica que libera o cliente, com os mesmos 10 segundos de validade |
| V33 | Registro criado antes de liberar o cliente; chamada ainda ocorreu 359 ms após sua expiração | A ordem reduziu o atraso, mas a sequência medida exigiu mais que os 10 segundos disponíveis; o ensaio permaneceu reprovado |
| V34 | Registro com validade de 20 segundos; conferência do pacote e retorno real de `GetHardwareId` com `S_OK`, seguidos de recusa em `ConnectNotify` | O veredito estrito recusou a sequência nova. Uma análise independente confirmou somente as observações, sem remover eventos ou alterar o veredito original |

**Os sete ensaios completos permaneceram reprovados. Restauração obrigatória e auditoria independente passaram em todos.** Na V34, a revisão separada conferiu identidade e módulo do serviço, execução, ordem e unicidade dos eventos: canais de entrada, pacote preparado, identificador retornado, `ConnectNotify` recusado, notificação posterior cancelada e fechamento real. O cliente encerrou com código zero; console e processos centrais preservaram suas identidades. O mesmo veredito original, aplicado novamente a todos os eventos, continuou reprovado.

Na chamada bem-sucedida restavam aproximadamente 9,6 segundos de validade do registro. Sua vida foi explicitamente ajustada de 10 para 20 segundos para cobrir a sequência medida e a conferência do pacote. O consumidor continua recusando um registro que ultrapasse o prazo do controle autenticado ao qual pertence. O controle permanece limitado a 75 segundos e o watchdog externo a 120, com as reservas originais; não há renovação ou reaproveitamento do registro. Essa mudança não concede escrita ao serviço nem cria a autorização separada de início do driver.

O método seguinte observado foi `ConnectNotify`, recusado com `ERROR_NOT_READY`. `NotifyCommandProcessCreated` também apareceu, já retornando cancelamento; esse callback, isoladamente, não comprova criação de processo ou autenticação de outra conta. `OnDriverLoad` não foi observado nessa rodada. O próximo incremento precisa tratar a notificação da conexão e conferir a continuação real antes de anunciar carregamento do driver.

As regressões adicionais passaram: 16 verificações da forma da política, 7 do predicado real de certificado com metadados capturados e objetos sintéticos, 16 de fronteiras de caminho/prontidão e 8 da resposta autenticada de login. As 20 verificações de formato, consumidor C++ e recusa no host foram repetidas para o produtor atualizado. As 954 referências de fontes e binários congelados reconferidas permaneceram íntegras. Esses testes locais não alteram registro, certificados, contas ou drivers do host.

A V34 manteve os binários de provedor, cliente e drivers; alterou o produtor isolado e as verificações de seu recibo para os 20 segundos documentados. O registro é criado depois de preparar o lançamento do cliente limitado e antes de publicar seu arquivo de liberação. As fontes do produtor ficam abertas somente para leitura, com hashes conferidos; uma falha deixa a liberação ausente. A coleta exige amostras reais saudáveis do console, identidade dos processos e fechamento observado.

1. Implementar `ConnectNotify` com validação da identidade e sessão vivas, estado da conexão e cancelamento; conferir o contrato do parâmetro e a continuação real. Uma confirmação de notificação não equivale a conta autenticada ou terminal pronto.
2. Executar nova rodada finita com escopo previamente definido para essa continuação, preservando a V34 reprovada e sua análise separada. Reconfirmar pacote, identificador e isolamento do console.
3. Implementar a autorização do driver para a sessão efetivamente notificada, com produtor administrativo limitado e transporte autenticado. Preservar as permissões e condições de início do driver.
4. Integrar `OnDriverLoad` à observação real de instância, sessão, pacote e interface, usando canal próprio com cancelamento. Depois comprovar adapter/monitor, frames e consumo nativo da entrada.

Autenticação da segunda conta, saída física, áudio, jogos e distribuição do motor continuam pendentes no [gate nativo](../architecture/0007-native-engine-gate.md). Candidata, resultados brutos e binários não foram incluídos no instalador público; os checks públicos desta documentação não compilam o código privado.
