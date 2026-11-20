# Laboratório 023: motivo da desconexão da nova sessão

Atualizado em 2026-11-20. **Ensaio limitado V37 aprovado.** O serviço real informou o motivo da desconexão que encerra a nova sessão: `0x11`, ou seja, `ERRINFO_CLOSE_STACK_ON_DRIVER_FAILURE`. O driver de display da sessão remota não concluiu a inicialização. A rodada passou por restauração e auditoria independente. Ainda não há segundo terminal utilizável.

## Problema

No [laboratório 022](022-native-command-process-notification.md), depois de reconhecido `NotifyCommandProcessCreated`, o Windows criou CSRSS e Winlogon na sessão 2 e logo desconectou com PreDisconnect, SetErrorInfo e Close. A candidata anterior recebia o motivo, mas não o gravava. A rodada V36c também foi interrompida porque o coordenador não esperava novos processos de sessão.

## Mudanças

- **Provedor:** passa a gravar o motivo recebido em PreDisconnect, DisconnectNotify e Close, o valor de SetErrorInfo, o identificador de cada propriedade consultada em QueryProperty e as opções de CreateVirtualChannel. Todos os valores de retorno continuam iguais aos da candidata anterior.
- **Coordenador:** passa a aceitar CSRSS e Winlogon de uma sessão ausente da linha de base e registra esses processos como evidência. A rodada ainda falha se qualquer processo central das sessões existentes mudar ou se surgir outro processo. A janela de inicialização de 40 segundos e o guarda de 120 segundos continuam iguais.

## Verificações locais

| Verificação | Resultado |
| --- | --- |
| Compilação com avisos como erros e análise estática | Aprovadas |
| Notificações e método COM real | 62 verificações, zero falhas |
| Identificação do display | 71 verificações, zero falhas |
| Permissões do registro | 23 verificações, zero falhas |
| Formato do produtor e consumidor | 20 verificações, zero falhas |
| Atribuição da sessão | 26 verificações, zero falhas |
| Referências congeladas | 1007 conferidas |

Uma primeira preparação da V37 falhou antes de qualquer ação na VM. Os trechos do coordenador eram lidos de um arquivo JSON, e o PowerShell 5.1 agrupou a lista inteira em um único item. A tentativa foi preservada, e a preparação foi gerada novamente com a correção.

## Execução real

A V37 passou no escopo definido:
- identificador do display retornado e os dois reconhecimentos de notificação em ordem;
- fechamento real pelo serviço e saída normal do cliente;
- console e processos centrais preservados;
- restauração e auditoria independente aprovadas.

A revisão reaplicou o veredito a todos os eventos originais, sem filtragem. Nesta rodada, as amostras do observador não registraram os processos da nova sessão, que provavelmente existiram por menos tempo que o intervalo entre amostras.

Sequência observada:

| Momento relativo | Chamada |
| --- | --- |
| cerca de 140 ms antes | `NotifySessionId` e `GetInputHandles` |
| cerca de 50 ms antes | `GetHardwareId` |
| 0 ms | `NotifyCommandProcessCreated` reconhecido |
| cerca de 150 ms depois | `PreDisconnect` com motivo `0x11` |
| logo em seguida | `SetErrorInfo(0x11)`, `DisconnectNotify` e `Close` |

Antes da desconexão, o serviço consultou quatro propriedades por QueryProperty e tentou criar dois canais virtuais, um estático e um dinâmico. Todos receberam E_NOTIMPL. Não houve GetVideoHandle nem OnDriverLoad.

## Interpretação e limites

O código `0x11` aponta para o driver de display da sessão. Isso é coerente com o estado atual: o pacote do driver de display próprio está preparado, mas a autorização de início, de propósito, ainda não é produzida. O driver exige uma autorização de 160 bytes com a sessão realmente atribuída, a identidade do serviço, a imagem do driver e prazos curtos, gravada por SYSTEM em uma pasta com permissões restritas.

A V37 não demonstra carregamento do driver, imagem, login de outra conta ou entrada. O motivo informado também não prova sozinho qual verificação interna do driver falhou.

## Próximos passos

1. Produzir a autorização de início do driver com a sessão real informada em `NotifySessionId`, dentro dos cerca de 300 ms observados e sem adivinhar o número da sessão.
2. Tratar `OnDriverLoad` com a instância e a sessão reais e observar se o serviço mantém a sessão.
3. Depois, monitor ativo e frames, autenticação da segunda conta e entrada.

Fontes e binários experimentais e resultados brutos continuam privados. A CI pública não compila a candidata privada. Consulte o [trabalho restante](../REMAINING-WORK.md).
