# Diagnóstico do listener RDP

Este passo investiga a falha de inicialização que impede o experimento de sessão filha. O diagnóstico salva dados locais sem alterar configurações do Windows.

Na pasta do projeto:

```powershell
powershell -NoProfile -File .\scripts\Get-RdpListenerDiagnostics.ps1
```

O Windows solicita UAC para ler o estado administrativo. O script imprime o caminho do summary.json em docs/lab/results/rdp-diagnostic-<run>. Esse diretório é ignorado pelo Git; contém eventos locais e ACLs, destinados à análise no computador.

O relatório distingue `ok`, `no_matching_events` e `unavailable`. Valores ausentes de políticas são acompanhados de indicadores explícitos de presença. ACLs são consultadas pela interface .NET do registro. Coleta configuração, serviços, listeners, assinatura da DLL e eventos das últimas 24 horas; nenhum certificado ou chave privada é exportado.

O launcher `Invoke-ChildSessionTest.ps1 -TestLocalRdp`, já autorizado neste laboratório, inicia temporariamente os auxiliares que estavam parados e restaura seus estados, junto da configuração RDP. Quando falha, chama o diagnóstico antes da restauração. Não reinicia o TermService nem muda o firewall. Backups e estado de restauração ficam no relatório do teste.

Resultado atual: evento LocalSessionManager 17 com 0x80070005 durante a ativação, mesmo com RDP permitido e UmRdpService em execução. Sessão filha não criada. O próximo diagnóstico precisa identificar o recurso ou operação negado para definir uma correção específica. Evidência detalhada: [ADR 0006](../architecture/0006-child-session-candidate.md).

## Rastreamento ETW direcionado

```powershell
powershell -NoProfile -File .\scripts\Trace-RdpListener.ps1
```

Usa logman/tracerpt do Windows para capturar LocalSessionManager, RemoteConnectionManager e RdpCoreTS em nível Verbose, além de erros RPC pelo keyword EEInfo. Limita o arquivo ETL circular a 64 MiB e executa o teste temporário de RDP autorizado. O teste roda em processo separado para que seu código de saída não encerre a limpeza do rastreamento. A sessão ETW tem nome exclusivo, é iniciada com `-ets` e encerrada em finally; não cria coletor agendado nem altera a habilitação persistente dos canais de eventos.

Resultados locais em docs/lab/results/rdp-trace-<run>: listener.etl, events.xml, trace-summary.txt, logs e summary.json. O código de saída do rastreamento mede captura/decodificação; o resultado da conexão fica separado em experiment_exit_code e no relatório child-admin correspondente. Uma captura bem-sucedida pode documentar uma conexão que falhou.

O resumo `rpc-errors.json` contém processo, PID temporário, status, localização de detecção e horário. Para XML acima de 16 MiB, a análise automática é omitida com `skipped_large_xml`, permitindo processamento por streaming sem carregar o arquivo inteiro em memória. A opção `-FileRegistryOperations` acrescenta aberturas/criação/consulta de segurança de chaves e eventos de nomes/abertura/conclusão de arquivos. Não captura valores do registro, conteúdo de arquivos nem pacotes. Esse modo observa operações do computador durante a janela; os arquivos locais podem conter caminhos de outros aplicativos.

O limite de 64 MiB vale para ETL, enquanto a conversão XML pode ser muito maior. Como o ETL é circular, eventos iniciais podem ser sobrescritos mesmo quando o contador Events Lost é zero. A tentativa RPC Verbose sem filtro gerou volume excessivo e perdeu a janela inicial; o modo padrão usa apenas EEInfo em nível Error. Os demais eventos não permitem atribuir causa a um certificado, ACL ou papel RDS sem evidência específica.

Se o processo for interrompido e a sessão ETW permanecer ativa, seu nome aparece no relatório ou pode ser consultado com `logman query -ets`. Encerre apenas a sessão `MultiseatRdp_<run>` criada por esse teste com `logman stop MultiseatRdp_<run> -ets`, em PowerShell administrativo. Os backups do launcher permitem verificar separadamente a restauração de RDP e serviços.

## Identificadores da chamada RPC

```powershell
powershell -NoProfile -File .\scripts\Trace-RdpListener.ps1 -RpcCalls
```

Este modo opcional usa RpcTraceFilter.cs para aplicar filtros de PID (LSM/TermService) e IDs de eventos 1/5/6/7/8 na sessão ETW exclusiva do projeto. Usa ControlTrace QUERY e EnableTraceEx2; consulta o handle da sessão criada pelo próprio launcher, sem alterar autorização ou configuração do serviço RPC. Requer processo x64 e PIDs ativos dos dois serviços. Se o filtro falhar, encerra ETW sem reproduzir o teste de RDP.

Resumo rpc-calls.json inclui interface UUID, número do procedimento, endpoint, atividade/thread e nível de autenticação quando presentes no evento. rpc-errors.json contém somente registros com status não zero. O XML original conserva a cadeia completa de dados estendidos.

Validação em 2026-10-08: captura -RpcCalls executada com sucesso em 032932-7e2bd196. Os 410 eventos RPC pertencem apenas aos dois PIDs selecionados e aos IDs 1/5/6/7/8; 416 eventos totais, zero perdas, XML de aproximadamente 538 kB. ETW encerrado e configuração/auxiliares restaurados. Encontrada chamada recusada do TermService à interface registrada ISessionManager, procedimento 0x3. A tentativa anterior 032055-2bd51f0d foi cancelada no UAC, sem iniciar coleta.

`Read-RpcFailures.ps1 -RunId <run>` correlaciona início/fim da chamada cliente pela atividade, processo e thread, usando o início mais próximo. Gera rpc-failures.json, preserva falhas sem início correspondente e consulta somente o nome registrado da interface. Também é chamado automaticamente ao terminar a captura -RpcCalls. Correlação validada com o registro real: uma chamada do TermService, ISessionManager/procedimento 3, status 0x5. Nome de método e regra exata de autorização continuam desconhecidos.

## Pilhas RPC e símbolos públicos

```powershell
powershell -NoProfile -File .\scripts\Trace-RdpListener.ps1 -RpcStacks
```

Implica -RpcCalls e acrescenta SID do emissor e pilha de EventWrite por EnableProperty. Conserva os mesmos filtros de processos/eventos; não altera autorização RPC. Captura também rpc-modules.json após o teste para relacionar endereços às DLLs. Consulta somente LSM e TermService, verifica PID e início do processo; falha de leitura é registrada, sem desativar proteção de processos. `Get-RpcModuleMap.ps1 -RunId <run>` permite completar essa consulta separadamente, com UAC, sem repetir o teste RDP.

Análise local, sem abrir processos:

```powershell
powershell -NoProfile -File .\scripts\Read-RpcStacks.ps1 -RunId <run> -DownloadSymbols
```

Lê XML até 16 MiB, mapa de módulos e chamadas correlacionadas. Usa DbgHelp com handle próprio, sem attach ou leitura de memória. A opção -DownloadSymbols baixa somente PDBs das DLLs Windows selecionadas do [servidor público da Microsoft](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/microsoft-public-symbols), por identidade RSDS/GUID/age do PE instalado. Cache em docs/lab/results/symbols, ignorado pelo Git. Downloads HTTPS têm limite total de 60 segundos e 16 MiB por arquivo; -MaxSymbolMegabytes aceita até 128 para arquivos maiores. Transferências parciais são retomadas pelo mesmo URL GUID/age, respeitando o limite escolhido, sem reiniciar o download. Somente transferência concluída é promovida ao cache. Falhas preservam endereços/módulos e informam símbolos indisponíveis. Sem -DownloadSymbols, utiliza apenas o cache existente. Nomes são símbolos de diagnóstico, não contratos de API para o produto.

Validação 034748-a8267d5e, em 2026-10-08: 436 eventos, 430 RPC, zero perdas; XML de 890.153 bytes. ETW encerrado e configuração RDP/auxiliares restaurados. Os dois mapas de módulos foram consultados com sucesso; análise de seis pilhas reais passou. TermService registra SID S-1-5-20; LSM registra S-1-5-18. SID do emissor não demonstra o token do cliente examinado pela autorização COM.

Símbolos de ntdll, RPCRT4, KERNEL32 e termsrv carregados: cliente em `CRemoteConnectionManager::Initialize+0x1AD`; servidor em `LRPC_SCALL::HandleRequest+0x711` → `SetFaultPacket+0xBB`, status 0x5. A captura mostra o processamento da falha, sem identificar ainda a regra que recusou a chamada. O PDB de combase (aproximadamente 83 MB) não foi concluído no limite de tempo; seus frames conservam módulo/offset. Não repetir o teste Windows para completar símbolos da mesma captura.

Referências: [EnableProperty/SID/stack](https://learn.microsoft.com/en-us/windows/win32/api/evntrace/ns-evntrace-enable_trace_parameters), [SymInitializeW](https://learn.microsoft.com/en-us/windows/win32/api/dbghelp/nf-dbghelp-syminitializew), [SymLoadModuleExW](https://learn.microsoft.com/en-us/windows/win32/api/dbghelp/nf-dbghelp-symloadmoduleexw), [SymFromAddr](https://learn.microsoft.com/en-us/windows/win32/api/dbghelp/nf-dbghelp-symfromaddr).

## Segurança COM em execução

O diagnóstico padrão consulta [CoGetSystemSecurityPermissions](https://learn.microsoft.com/en-us/windows/win32/api/objbase/nf-objbase-cogetsystemsecuritypermissions) para os quatro tipos COMSD, além do registro. Descritores retornados são globais; não expõem a autorização específica do processo LSM, que pode definir parâmetros próprios em [CoInitializeSecurity](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-coinitializesecurity).

Consulta opcional do token atual do TermService:

```powershell
powershell -NoProfile -File .\scripts\Get-RdpListenerDiagnostics.ps1 -CheckServiceToken
```

ComSecurityDiagnostics.cs consulta o SID com TOKEN_QUERY e tenta uma cópia SecurityIdentification para [AccessCheck](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-accesscheck), máscara específica 3 (COM_EXECUTE + LOCAL). Não impersona o serviço nem lê sua memória. Se TOKEN_DUPLICATE retornar erro 5, tenta SeDebug já existente no token do próprio diagnóstico e restaura seu estado em finally. Não concede direitos no sistema nem altera políticas/descritores/proteção. Esse modo é explícito; o diagnóstico padrão não ajusta privilégios. TokenCheckStatus distingue not_requested, unavailable e checked; AccessAllowed permanece null quando a coleta falha, acompanhada do código Win32. DebugPrivilegeRestored registra a limpeza.

Execução elevada 151804-f0a889bf: quatro descritores lidos; restrições globais coincidem com o registro. SID TermService S-1-5-20 confirmado, TOKEN_DUPLICATE recusado com erro 5 mesmo na tentativa com SeDebug; privilégio restaurado. AccessCheck desse serviço não executado. Teste local com token do próprio processo passou; PID inexistente produz unavailable/erro 87, sem falso resultado de acesso. Não traduzir a falha de coleta em causa da recusa RPC.

## Hipótese de ativação combinada

```powershell
powershell -NoProfile -File .\scripts\Trace-RdpListener.ps1 -RpcStacks -CombinedActivation
```

Modo padrão aborta se não observar listener. A opção explícita permite ao host habilitar sessão filha e tentar Connect enquanto RDP já está temporariamente permitido, mesmo sem listener observado. Verifica uma hipótese de interação entre as duas habilitações; não assume exigência documentada. Guardas do host e restauração permanecem iguais; opção direta do launcher requer -TestLocalRdp. Nenhum firewall, conta ou descritor COM é alterado.

Execução 151102-9f03dc40: ambas as habilitações foram tentadas, Connect retornou, OnDisconnected 516 e nenhum ID distinto. Host restaurou a flag, probe retornou 0/desativado, RDP/auxiliares restaurados e ETW encerrado. 1.582 eventos, 1.574 RPC, zero perdas, três chamadas recusadas para ISessionManager e 18 pilhas analisadas com os cinco PDBs completos. A hipótese não resolveu o bloqueio. Repetir conexão depende de nova evidência; análise adicional deve usar os relatórios existentes.

## Integridade dos componentes

```powershell
powershell -NoProfile -File .\scripts\Get-RdpListenerDiagnostics.ps1 -VerifyCoreFiles
```

Executa [SFC /verifyfile](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/sfc) somente para lsm.dll, lsmproxy.dll, termsrv.dll e combase.dll. Essa opção verifica e não repara arquivos. Logs locais são necessários para interpretar o resultado; não inferir integridade apenas do código de saída. Execução 033624-f099faea retornou 0 nos quatro arquivos e informou ausência de violações de integridade. Captura de texto foi ajustada para UTF-16 do SFC, evitando acentos corrompidos; decodificação conferida com a resposta nativa de ausência de administrador, sem repetir a verificação.

Referências: [logman create trace](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/logman-create-trace), [rastreamento ETW de RemoteConnectionManager](https://learn.microsoft.com/en-us/troubleshoot/windows-server/system-management-components/event-tracing-for-windows-simplified), [tracerpt](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/tracerpt), [EVENT_FILTER_DESCRIPTOR](https://learn.microsoft.com/en-us/windows/win32/api/evntprov/ns-evntprov-event_filter_descriptor), [ControlTraceW](https://learn.microsoft.com/en-us/windows/win32/api/evntrace/nf-evntrace-controltracew).
