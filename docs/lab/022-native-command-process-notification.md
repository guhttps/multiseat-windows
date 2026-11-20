# Laboratório 022: notificação do processo de comando

Atualizado em 2026-11-20. **`NotifyCommandProcessCreated` foi reconhecido no serviço real, e o Windows criou `csrss.exe` e `winlogon.exe` em uma nova sessão (sessão 2) enquanto a sessão do console continuava preservada.** As três rodadas do dia (V36, V36b e V36c) estão registradas como **reprovadas**. Todas foram restauradas e passaram pela auditoria independente. Não houve login de outra conta, imagem, entrada ou terminal utilizável.

## Problema e comportamento

No [laboratório 021](021-native-connection-notification.md), o componente reconheceu `ConnectNotify` e recusou a chamada seguinte, `NotifyCommandProcessCreated`, com `ERROR_NOT_READY`. A nova candidata reconhece essa notificação somente para a mesma conexão e a mesma sessão já conferidas, depois de `ConnectNotify` e dentro dos prazos originais.

Segundo o [contrato da Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nf-wtsprotocol-iwrdsprotocolconnection-notifycommandprocesscreated), a notificação informa que o Winlogon da sessão foi criado. A chamada deve retornar imediatamente, sem chamadas diretas ou indiretas a APIs RDS. A interface continua documentada para Windows Server, sem suporte declarado em Windows cliente. Este trabalho segue como pesquisa em VM isolada.

## Implementação e limites

- O reconhecimento reaproveita o controle limitado da etapa anterior. Ele exige a mesma sessão conferida, o reconhecimento anterior de `ConnectNotify`, os prazos originais do pacote, o mesmo processo do cliente e nenhum sinal de cancelamento.
- Ordem errada, sessão diferente, prazo vencido, perda do peer, cancelamento, saturação e conexão revogada fazem a chamada ser recusada. Repetições idênticas são idempotentes e não renovam o prazo.
- O resultado efetivo, o instante da captura e a ordem ficam em um buffer fixo. A gravação acontece depois, no encerramento normal, como evento 112. O evento 110 continua registrando `ConnectNotify`.
- A chamada não consulta nem cria processos, não autentica usuários e não concede acesso ao driver. Reconhecer o aviso, sozinho, não prova que existe um processo autenticado de outra conta.

## Verificações locais

| Verificação | Resultado | Alcance |
| --- | --- | --- |
| Notificações e método COM real da candidata | 62 verificações, zero falhas | Ambas as ordens, sessão divergente, revogação e expiração entre fases, repetições e saturação |
| Identificação do display | 71 verificações, zero falhas | Pacote de fixture separado e recusa real de produção no host |
| Permissões do registro | 23 verificações, zero falhas | Descritores em memória e avaliação Authz |
| Formato do produtor e consumidor | 20 verificações, zero falhas | ABI PowerShell/C++ e recusa de execução no host |
| Atribuição da sessão | 26 verificações, zero falhas | Estado e contratos locais, sem criar sessão |
| Veredito da execução | 45 verificações, zero falhas | Observações sintéticas válidas e adulteradas |
| Referências congeladas | 981 conferidas | Fontes e binários anteriores intactos |

## Execuções reais

As três rodadas usaram a mesma candidata e o mesmo veredito, com planos novos, arquivos fixados por hash, restauração obrigatória e auditoria independente. O guarda externo de 120 segundos não foi alterado.

**V36 e V36b: reprovadas antes do ensaio.** Depois do reinício do convidado, o coordenador tinha 25 segundos para confirmar o login de verificação, e nas duas vezes esse prazo esgotou. O registro de falha alcançou o convidado cerca de dois segundos depois. Nesse momento, a inicialização do provedor depois do reinício coincidia com a da V35. A análise mostrou que uma tentativa de login iniciada durante a inicialização ficava bloqueada por até 8 segundos e consumia o fim da janela. O hospedeiro tinha recebido atualizações do Windows depois da V35. A V36b repetiu a V36 sem nenhuma alteração.

**V36c: alteração aprovada apenas na espera de inicialização.** A janela passou de 25 para 40 segundos, e o limite de cada tentativa de login caiu de 8 para 5 segundos. O guarda de 120 segundos, os demais prazos, a candidata e o veredito ficaram iguais. A confirmação veio em 19 segundos.

A V36c observou o provedor carregado, o listener, o endpoint pronto, `OnConnected` e `OnReady` com sucesso e a verificação do peer. Depois vieram o identificador do display, `ConnectNotify` e `NotifyCommandProcessCreated`, ambos reconhecidos com S_OK. A observação seguinte encontrou `csrss.exe` e `winlogon.exe` na sessão 2. Os processos centrais das sessões 0 e 1 e o console continuaram os mesmos, com amostras saudáveis.

O coordenador tratava qualquer mudança nos processos centrais de sessão como fora do escopo, então interrompeu o ensaio com `ObserverScopeSessionCoreChanged`, e a rodada ficou **reprovada**. A revisão posterior reaplicou o veredito fixado a todos os eventos capturados, sem filtragem. O resultado foi aprovado: identificador real, dois reconhecimentos em ordem e fechamento real, sem driver. Essa reaplicação não muda a reprovação da rodada.

Em até 235 ms depois do reconhecimento, o serviço consultou `GetProtocolStatus`, `GetLastInputTime`, `CreateVirtualChannel`, `QueryProperty` e `GetShadowConnection`. Todos retornaram E_NOTIMPL. Em seguida vieram `PreDisconnect`, `SetErrorInfo`, `DisconnectNotify` e `Close`. Não houve `GetVideoHandle` nem `OnDriverLoad`. O código de erro informado em `SetErrorInfo` não era gravado pela candidata, então o motivo da desconexão ainda não é conhecido.

## O que isso comprova e o que não comprova

- **Comprova, nesta VM:** o serviço real do Windows cliente criou o par CSRSS/Winlogon de uma nova sessão durante uma conexão do nosso provedor, sem encerrar a sessão do console.
- **Não comprova:** autenticação de outra conta, área de trabalho, imagem, entrada ou terminal utilizável. Também não mostra se o limite de sessões do Windows cliente permitirá o login simultâneo de uma segunda conta.

## Próximos passos

1. Gravar o código recebido em `SetErrorInfo` e os identificadores consultados em `QueryProperty`, para explicar a desconexão.
2. Ajustar o coordenador para esperar e registrar o surgimento de uma nova sessão como evidência, mantendo a exigência de preservar o console e os processos das sessões existentes.
3. Implementar o que o serviço realmente exigir depois da notificação. Os candidatos são os canais virtuais e as propriedades, depois `GetVideoHandle`/`OnDriverLoad` com a autorização separada do driver de display e, por fim, a autenticação da segunda conta.

Fontes e binários experimentais e resultados brutos continuam privados. A documentação pública não instala essa candidata nem a inclui no download experimental; sua CI não compila o código privado. Consulte o [trabalho restante](../REMAINING-WORK.md).
