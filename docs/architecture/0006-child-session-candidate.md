# Sessão filha: experimento histórico

Estado revisto em 2026-10-16: não selecionado como motor do produto, pois não entrega as duas identidades Windows independentes exigidas. Mantido como experimento histórico; [ADR 0007](0007-native-engine-gate.md) define a próxima prova. Os testes anteriores não observaram segunda sessão e o transporte RDP permaneceu sem listener.

## Ponto de integração identificado

A Microsoft documenta [Child Sessions](https://learn.microsoft.com/en-us/windows/win32/termserv/child-sessions) desde Windows 8: sessão RDP loopback ligada à sessão existente, criada pelo controle ActiveX com ConnectToChildSession. Só uma sessão filha pode ficar ativa/conectada; termina quando a sessão pai termina. Há login automático relacionado ao usuário existente, com exceções descritas pela documentação. Não constitui prova de identidades Windows independentes.

A propriedade [ConnectToChildSession](https://learn.microsoft.com/en-us/windows/win32/termserv/imsrdpextendedsettings-property) direciona a conexão ao próprio computador. Isso permite investigar apresentação da sessão adicional numa janela do console; não transfere propriedade do HDMI para outra sessão. O cliente ainda precisa de entrada por dispositivo, sem retirar o foco dos aplicativos da estação principal.

As APIs [WTSIsChildSessionsEnabled](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/nf-wtsapi32-wtsischildsessionsenabled), [WTSEnableChildSessions](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/nf-wtsapi32-wtsenablechildsessions) e [WTSGetChildSessionId](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/nf-wtsapi32-wtsgetchildsessionid) documentam suporte em sistemas cliente desde Windows 8. Essa é uma hipótese distinta do provedor de protocolo Server anteriormente rejeitado para o alvo cliente.

## Implementação independente preparada

`ms-child-session-probe` consulta habilitação e ID, instancia o controle RDP e verifica leitura/escrita da propriedade no objeto desconectado. Não habilita nem conecta. Usa a type library instalada através de import MSVC.

`ChildSessionTest.cs` hospeda o controle em WinForms, usando .NET Framework já disponível. `--smoke` apenas hospeda e configura o objeto. `--test`:

1. Recusa sessão filha preexistente ou estado inicial desconhecido. Guarda a flag inicial.
2. Se necessário, tenta habilitar via API documentada, configura conexão local sem fornecer/gravar credenciais e inicia conexão.
3. Aguarda até 20 segundos; exige ID de sessão filha distinto do pai, conexão ativa e login completo observados de forma estável durante três segundos. O tempo é medido por relógio monotônico.
4. Desconecta, solicita logoff apenas do novo ID observado e restaura/verifica a flag inicial quando a alterou. Falha de limpeza retorna erro.

Este teste pode iniciar a sessão Windows e programas automáticos de login. Não abrir documentos ou jogos dentro dela: a sessão de teste será encerrada automaticamente. O critério de sucesso é a observação estável de ID/conexão/login, não certificação de desktop renderizado, dois focos, GPU, identidade separada ou compatibilidade com Vanguard. Um mutex global impede testes concorrentes; o cleanup revalida o ID antes de solicitar logoff e recusa identidade ambígua.

## Resultado local

Build x64, warnings tratados como erros. Consulta:

```text
Current session: 1
Child sessions enabled: no
Child session ID unavailable: error=1168
ConnectToChildSession property readable: yes
PASS: API/control capability only; no child session created.
```

O erro 1168 foi preservado como consulta de ID indisponível; não convertido em enumeração bem-sucedida. Host `--smoke` passou. Host `--test` falhou imediatamente:

```text
Parent session: 1; initial child flag: False
FAIL: InvalidOperationException: WTSEnableChildSessions failed: 5
```

Nova consulta confirmou flag desativada. Nenhuma conexão iniciada, sessão criada ou política alterada. A execução fora do sandbox não elevou o token Windows a administrador; o erro é da API, não rejeição de aprovação automática.

Usuário repetiu primeiro com erro 5. Em nova execução, a ativação passou, a chamada de conexão lançou TargetInvocationException e a restauração retornou True. Probe posterior confirmou recurso desativado; nenhum ID de sessão filha foi observado. A saída antiga não incluía a causa interna nem confirmação do papel de administrador, portanto não atribuir esse erro a política, licença ou credenciais sem diagnóstico.

Diagnóstico v2 preparado e compilado: imprime papel de administrador ativo, estado Connected antes da chamada e operação/HRESULT da exceção interna via GetBaseException. Recusa --test sem administrador antes de ativar; --smoke continua disponível sem elevação. Validação local: papel administrador False, Connected 0, smoke passou; --test retornou 2 sem ativação; probe posterior confirmou flag desativada. Nova execução elevada necessária para capturar o HRESULT da conexão que falhou no teste do usuário.

Usuário executou v2 com administrador True, Connected 0, habilitação True: IMsTscAx.Connect falhou com ArgumentException HRESULT 0x80070057 (E_INVALIDARG). Restauração True; retorno 1. Isso localiza a falha na conexão, não na permissão de ativação.

V3 configura explicitamente EnableCredSspSupport através de AdvancedSettings7 e verifica readback. A [propriedade Microsoft](https://learn.microsoft.com/en-us/windows/win32/termserv/imsrdpclientadvancedsettings6-enablecredsspsupport) controla CredSSP para aquela conexão; [AdvancedSettings7](https://learn.microsoft.com/en-us/windows/win32/termserv/imsrdpclient6-advancedsettings7) fornece a interface apropriada. Smoke local observou False antes, True depois, modo de sessão filha preservado e Connected 0. Suporte antes desativado é configuração ausente identificada; causalidade do E_INVALIDARG e conexão bem-sucedida ainda precisam da execução elevada. Não foram alteradas políticas de delegação, registro, firewall ou configuração de segurança do Windows.

Execução v3 pelo launcher autorizado: Connect retornou sem E_INVALIDARG; nenhum ID distinto apareceu no prazo. Flag inicial restaurada e probe passou. Em seguida, v4 acrescentou subscrição a cinco eventos RDP, com DISPIDs obtidos da type library instalada. Smoke validou registro/remoção dos handlers. Limpeza de objetos agora protege a tentativa de restaurar a flag mesmo quando Dispose falha.

V4 executada pelo agente usando o launcher UAC autorizado: OnDisconnected reason=516, Connected 0, ExtendedDisconnectReason 0. Segundo a [documentação de OnDisconnected](https://learn.microsoft.com/en-us/windows/win32/termserv/imstscaxevents-ondisconnected), 516 indica falha na conexão de socket. Nenhuma sessão filha observada; recurso restaurado para desativado, probe posterior retornou 0. Consulta local separada confirmou TermService Running, fDenyTSConnections=1, porta configurada 3389 e zero listeners TCP nela; firewall ativo nos três perfis. Esses dados sustentam investigar o transporte RDP; não demonstram que habilitá-lo basta ou que outro requisito não falhará depois.

Opção executada com autorização: `Invoke-ChildSessionTest.ps1 -TestLocalRdp`. Faz backup local de fDenyTSConnections e dos estados de SessionEnv/UmRdpService, inicia os auxiliares inicialmente parados, habilita temporariamente conexões RDP e executa o teste/probe somente se observar listener. Restaura configuração e serviços iniciados pelo launcher em finally, sem reiniciar TermService; registra restauração no summary.json. Mantém regras de firewall, políticas de credenciais e configuração de inicialização dos serviços. Cancelar/matar o processo pode impedir finally; backups ficam disponíveis no relatório local. Opção padrão continua sem alterar fDenyTSConnections.

Build do host C# incluído na CI; conexão que altera habilitação não é executada na CI. Acesso negado sem administrador continua retornando 2 antes de ativar.

## Teste temporário de transporte autorizado

Usuário autorizou habilitar temporariamente RDP mantendo o firewall atual. Duas execuções elevadas em 2026-10-08 UTC (2026-10-07 local) não observaram listener na porta 3389 em dez segundos: primeiro pela escrita do registro, depois pela interface documentada [SetAllowTSConnections](https://learn.microsoft.com/en-us/windows/win32/termserv/win32-terminalservicesetting-setallowtsconnections). O launcher passou a usar essa interface com ModifyFirewallException=0 na ativação e restauração, conferindo retorno do provedor. O teste de conexão não foi iniciado; os códigos de teste/probe nos relatórios são null.

Relatórios locais: child-admin-20261008-025212-68f9cc5e e child-admin-20261008-025528-8f8d2c99. Ambos confirmam administrador ativo e restauração True. Consulta independente posterior: fDenyTSConnections=1, AllowTSConnections=0, PolicySourceAllowTSConnections=0, fEnableWinStation=1, TermService Running, UmRdpService Stopped, zero listeners TCP 3389. Probe posterior retornou 0, sessões filhas desativadas e ID indisponível 1168. Não atribuir a ausência do listener ao serviço UmRdpService parado sem prova. Reinício de serviços e mudanças em políticas/firewall não foram executados. Próxima investigação: requisitos e diagnóstico de inicialização do listener neste Windows; repetir a mesma tentativa sem novos dados não agrega evidência.

## Launcher e execução manual

Investigação em 2026-10-08: `Get-RdpListenerDiagnostics.ps1` acrescentado e executado com token administrativo, em modo somente leitura. Separa consultas indisponíveis de eventos ausentes, coleta ACLs do registro via .NET e mantém resultados em diretório local ignorado. Get-Acl não conseguiu consultar chaves existentes neste ambiente; a consulta .NET resolveu essas ACLs, evitando interpretar erro de coleta como chave ausente. A chave de políticas existe, mas não contém fDenyTSConnections. Assinatura termsrv.dll válida. Eventos LocalSessionManager ID 17, correlacionados aos testes, informam 0x80070005 (acesso negado). Nenhum evento correspondente de Schannel/RemoteConnectionManager foi encontrado no período consultado; a causa específica ainda é desconhecida.

Testes adicionais com auxiliares iniciados: child-admin-20261008-030239-e8c3f978 e child-admin-20261008-030408-cb9efc7b. Ambos falharam antes de Connect e restauraram RDP/serviços. No segundo, diagnóstico foi capturado antes da restauração: fDenyTSConnections=0, TermService e UmRdpService Running, SessionEnv já Stopped com ExitCode=0 após ter iniciado, zero listeners 3389 e novo evento 17/0x80070005. Consulta independente posterior confirmou fDenyTSConnections=1 e os dois auxiliares Stopped; TermService permaneceu Running. Próximo passo: identificar a operação/recurso negado por rastreamento direcionado, antes de propor reparo de ACL, certificado ou serviço. O código genérico não justifica conceder permissões amplas.

Launcher com autorização UAC por execução: `powershell -NoProfile -File .\scripts\Invoke-ChildSessionTest.ps1`. Ele solicita administrador apenas para rodar `ms-child-session-test --test` e o probe posterior. Salva connection.log, after.log e summary.json em docs/lab/results/child-admin-<run> (ignorado pelo Git), permitindo ao agente ler a saída depois. Não instala serviço nem mantém processo privilegiado disponível para comandos futuros. Sintaxe verificada; fluxo UAC ainda depende da primeira execução autorizada pelo usuário. Se a autorização for cancelada ou não produzir relatório, o launcher informa a falha.

Abrir PowerShell **como administrador**, entrar na pasta do projeto e executar o host já compilado:

```powershell
Set-Location 'C:\projetos\multiseat-windows'
.\build\child-session\Release\ms-child-session-test.exe --test
$LASTEXITCODE
.\build\child-session\Release\ms-child-session-probe.exe
```

Essa execução tenta alterar temporariamente a habilitação de sessões filhas e criar uma sessão de teste. Não habilita servidor RDP comum, abre firewall, instala driver, altera segurança ou cria contas. Copiar saída do teste e da consulta posterior; se limpeza falhar, registrar o ID e estado sem tentar encerrar a sessão principal.

Reconstrução: Developer PowerShell, `cmake -S . -B build/child-session -A x64`, `cmake --build build/child-session --config Release --target ms-child-session-probe`, seguido de `powershell -NoProfile -File .\scripts\Build-ChildSessionTest.ps1`.

## Decisão após o teste

Atualização 2026-10-08, 151102-9f03dc40: testada lacuna da ordem anterior. Até aqui, o launcher habilitava RDP e abortava por ausência de listener antes de chamar o host, que habilita sessões filhas. A opção explícita -CombinedActivation permite que o host execute sua ativação/Connect enquanto RDP permanece temporariamente permitido, preservando todos os guardas de sessão preexistente e limpeza. Hipótese de interação entre as duas habilitações, não requisito adicional documentado pela Microsoft.

Resultado: administrador True, flag filha inicialmente False, WTSEnableChildSessions bem-sucedido, Connect retornou e OnDisconnected=516; nenhum ID distinto. Host restaurou a flag e probe retornou 0/desativado. Launcher restaurou RDP e auxiliares; ETW encerrado. 1.582 eventos (1.574 RPC), zero perdas, três chamadas TermService → ISessionManager/procedimento 3 com status 0x5, 18 pilhas analisadas. Os eventos cobrem ativação, tentativa e restauração; não atribuir as três chamadas ao mesmo estado das flags. Modo padrão continua abortando sem listener.

Download retomável completou o PDB de combase da captura 034748-a8267d5e: todos os cinco PDBs usados foram carregados. Frames `CMessageCall::CallI_RpcNegotiateTransferSyntax`, `ClientCall::NegotiateSyntaxWithTransportCall` e `CClientChannel::NegotiateSyntaxWithProxyIID` localizam a negociação COM/RPC; nomes não demonstram o AccessCheck/callback que recusou acesso nem que o método ISessionManager tenha sido executado.

Consulta somente leitura 151804-f0a889bf: CoGetSystemSecurityPermissions retornou os quatro descritores globais. Restrições de acesso/lançamento coincidem com o registro; padrão de acesso construído contém Principal Self, SYSTEM e Administrators. Isso não consulta a segurança processual do LSM, que pode ser configurada por CoInitializeSecurity. SID do processo TermService=S-1-5-20. Cópia do token para AccessCheck indisponível com erro 5, mesmo após ativação de SeDebug já existente no token do próprio diagnóstico; estado anterior restaurado em finally. Erro da coleta não é prova da causa RPC. Sem impersonação, attach ou mudança de descritores/proteção. A consulta de token passa a ser opcional -CheckServiceToken, evitando repetição automática nas falhas do launcher.

Conclusão atual: candidato bloqueado neste laboratório e não aprovado para P01–P08. A combinação de flags não resolveu. Retomar conexão somente com nova evidência de autorização efetiva ou ambiente de comparação; não há reparo global de ACL justificável pela coleta. T006/T007 permanecem abertas e LoL simultâneo não demonstrado.

Atualização 2026-10-08, captura -RpcStacks 034748-a8267d5e: 436 eventos totais, 430 RPC, zero perdas; pilhas e SIDs do emissor preservados. RDP/auxiliares restaurados e ETW encerrado. Consulta elevada, somente leitura, obteve mapas de módulos dos mesmos processos LSM/TermService. DbgHelp analisou seis pilhas da captura, sem attach nem leitura de memória. Símbolos públicos da Microsoft identificaram `termsrv!CRemoteConnectionManager::Initialize+0x1AD` no cliente e `RPCRT4!LRPC_SCALL::HandleRequest+0x711` → `SetFaultPacket+0xBB` no servidor. Nenhum frame de lsm.dll está presente nessa pilha de emissão da falha; isso não prova que um callback LSM/COM não tenha sido executado e retornado antes dela.

TermService escreve com SID S-1-5-20 e LSM com S-1-5-18. Esses dados são identidade do emissor ETW, não o token efetivo submetido ao AccessCheck. PDB de combase não concluído no limite de download; endereço e offset conservados. A regra de autorização continua desconhecida. Não há justificativa para mudar ACL global com base apenas nesses nomes. Próxima investigação: completar análise da captura existente e consultar evidência da configuração de segurança efetiva do servidor COM; não repetir habilitação RDP sem uma nova hipótese verificável. Ferramentas e limites: [laboratório 006](../lab/006-rdp-listener-diagnostics.md).

Atualização 2026-10-08, captura 032932-7e2bd196: modo -RpcCalls validado com aplicação dos filtros. 416 eventos totais (410 RPC), zero perdas; RPC limitado aos PIDs LSM/TermService e IDs 1/5/6/7/8. Início/fim cliente correlacionados por PID/thread/atividade identificam TermService → interface UUID 517f87fe-597a-4672-8555-6daf1c8c788d, procedimento 0x3, endpoint OLE dinâmico, status 0x5. Registro local identifica ISessionManager, sete métodos e proxy lsmproxy.dll. O servidor LSM registra a cadeia de erro no mesmo instante; não traduzir procedimento 3 em nome de método sem contrato documentado. AuthenticationLevel=6, AuthenticationService=20, ImpersonationLevel=2 foram preservados; esses valores não demonstram qual autorização rejeitou a chamada.

Restrições COM consultadas em modo somente leitura: MachineAccessRestriction possui concessão local a Everyone; DefaultAccessPermission ausente, políticas DCOM/RPC consultadas ausentes e nenhum evento DistributedCOM no intervalo examinado. A ausência de DefaultAccessPermission é um estado previsto pela [documentação Microsoft](https://learn.microsoft.com/en-us/windows/win32/com/defaultaccesspermission), que também distingue segurança definida pela aplicação/AppID. Não foi demonstrado que esse valor governa a interface LSM nesta instância; nenhuma alteração global é justificada pela evidência atual.

SFC /verifyfile dos quatro componentes (lsm.dll, lsmproxy.dll, termsrv.dll, combase.dll), execução 033624-f099faea: ausência de violações, códigos 0. Não houve reparo. Get-RdpListenerDiagnostics.ps1 recebeu modo de verificação fixa e metadados COM; Read-RpcFailures.ps1 automatiza a correlação da chamada real. RDP e auxiliares restaurados e sessão ETW encerrada. A falha antecede a execução de Connect do experimento. Próxima investigação: identificar a configuração/checagem de autorização efetiva de ISessionManager neste Windows, mantendo distinção entre hipótese e causa comprovada.

Captura final do modo padrão 031801-b5aec591: ETW iniciou/encerrou, tracerpt retornou 0 e rpc-errors.json preservou novamente a cadeia 0x5 em LSM/TermService. Configuração RDP e auxiliares restaurados; consulta independente confirmou estado inicial e sessão ETW ausente. Modo opcional -RpcCalls preparado com filtros PID/eventos e helper C# compilado; guarda de nomes de sessão e extração de erro validadas. A aplicação real desses filtros ainda não foi executada: UAC da tentativa 032055-2bd51f0d cancelado antes da coleta. Guia e comando no documento de laboratório 006. Nenhuma alteração Windows iniciada nessa tentativa cancelada.

Rastreamento em 2026-10-08: Trace-RdpListener.ps1 usa sessões ETW efêmeras com nomes exclusivos e ferramentas nativas logman/tracerpt, envolvendo o teste temporário autorizado. Captura inicial 031025-f7237628: seis eventos, incluindo erro LSM 17; sem localização do recurso negado. Captura com arquivo/registro 031233-fff537cf: 193.344 eventos, contador de perda zero; nenhuma ocorrência de Status 0xC0000022, 0x80070005 ou 0x5 nos campos Status de arquivo/registro. Esse resultado não exclui outras formas de verificação de acesso.

Captura RPC Error 031431-acf34e50: 29 eventos, contador de perda zero. EEInfo registra status 0x5 no processo LSM (PID 2124, locais 31/1320/1321) e no processo TermService (PID 38012, local 974) imediatamente antes do erro de inicialização. Associação dos PIDs foi verificada pelo CIM. Isso localiza uma falha RPC correlacionada entre os serviços; ainda não identifica interface, função ou regra de autorização. RPC 0x6BA do WMI e erros de outros processos também aparecem; não confundir com a falha investigada. [Microsoft](https://learn.microsoft.com/en-us/windows/win32/rpc/understanding-extended-error-information) informa que DetectionLocation depende da versão; números antigos não devem ser traduzidos como função atual sem prova.

Tentativa RPC Verbose 031523-33de3362 preencheu o ETL circular: 570.080 eventos e janela inicial sobrescrita, embora Events Lost=0. Não sustenta diagnóstico da chamada inicial. A configuração padrão foi restringida a RPC EEInfo/Error, com análise automática de XML pequeno e opção separada para arquivo/registro. Sessões ETW encerradas; testes restauraram RDP e auxiliares. Próxima investigação: filtrar eventos RPC por IDs/processos para obter interface/endpoint/chamada da negação, mantendo a janela inicial e a proteção atual do Windows.

Se aparecer uma sessão filha distinta com limpeza confirmada, avançar para comprovar apresentação e transporte de entrada por dispositivo em aplicativos comuns. Ainda verificar compartilhamento de identidade/perfil, limite de duas estações totais e dependência da sessão pai frente ao escopo completo. Se conexão falhar, guardar códigos e investigar requisitos do controle sem substituir o caminho por patches do sistema.

P01–P08 e T007 continuam pendentes. O alvo do produto não foi reduzido a duas estações com perfil compartilhado. LoL permanece não testado neste mecanismo.

Fontes consultadas em 2026-10-07. Projetos encontrados na busca que utilizam RDPWrap/patches não foram instalados ou incorporados; nenhum código do concorrente foi utilizado.
