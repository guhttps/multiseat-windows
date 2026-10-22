# 013 — entrada no início do listener nativo

Data: 2026-10-21. Atualizado em 2026-10-22. Estado: entrada real em StartListen, observação do console e restauração verificadas. Esse subteste não aprova um listener operacional nem um segundo terminal.

## Configurações e origem das chamadas

O diagnóstico do [laboratório 012](012-native-terminal-provider.md) confirmou que GetSettings retornou HRESULT zero, com nível solicitado 1 e nível devolvido 1. A origem das chamadas foi conferida no serviço real, combinando identidade e instante de criação do processo, marcador exclusivo da rodada e presença do módulo esperado. Ativação COM pelo próprio teste não foi contada como evidência do serviço.

A investigação identificou a configuração global normal de permissão de conexões remotas como um controle anterior ao início do listener nesta imagem. A Microsoft documenta essa configuração como permissão ou recusa de conexões de Área de Trabalho Remota; isso não constitui um contrato público para criar dois postos físicos no Windows cliente. O ensaio foi limitado à VM descartável sem rede. Veja [fDenyTSConnections](https://learn.microsoft.com/en-us/windows-hardware/customize/desktop/unattend/microsoft-windows-terminalservices-localsessionmanager-fdenytsconnections).

## Primeira entrada observada

Com a opção de início ajustada no laboratório, o serviço chamou StartListen com callback não nulo. A identidade das chamadas e o módulo foram conferidos. O componente devolveu deliberadamente ERROR_NOT_READY: não havia um backend completo para aceitar clientes, e nenhuma conexão ou endpoint foi aberto.

Receber a chamada é um resultado intermediário. O contrato de [StartListen](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nf-wtsprotocol-iwrdsprotocollistener-startlisten) exige retornar sucesso somente quando a operação tiver sucesso; observar a entrada no método não demonstra que o listener esteja escutando ou que uma conexão possa ser aceita.

O observador do console produziu **zero amostras nessa rodada**. Portanto, essa execução não aprova a continuidade do console, mesmo com a entrada real no listener. Ausência de observação também não demonstra que o console tenha falhado; a lacuna exige uma nova coleta.

A restauração externa terminou com verificação do estado original, remoção dos elementos próprios do experimento e retorno da configuração global ao bloqueio de conexões remotas. O computador pessoal não recebeu essas alterações.

## Instrumentação e verificação com observador explícito

Foi corrigida a instrumentação da cota de eventos, mas essa correção não tem causalidade demonstrada para o avanço: as coletas anteriores tinham 30 e 38 eventos, ambas abaixo do limite de 64. A entrada observada não deve ser atribuída a essa correção sem evidência adicional.

Em 2026-10-22, uma nova rodada iniciou o observador explicitamente depois do login e da disponibilidade do desktop. A coleta reuniu sete amostras de referência e 14 amostras finais, sem amostras não saudáveis. O serviço, módulo, instante de criação e marcador coincidiram; GetSettings retornou sucesso e StartListen recebeu callback não nulo. Os processos originais do console foram preservados no intervalo observado após o boot. A restauração externa verificou o estado de referência, ausência dos elementos do experimento e retorno da configuração de conexões remotas. Essa rodada aprova esse subteste; a tentativa anterior sem amostras permanece inconclusiva.

## O que continua sem demonstração

Nenhum listener operacional, endpoint de conexão, conexão aceita, login de uma segunda conta, segundo terminal utilizável, monitor físico atribuído, entrada independente, áudio ou jogo foi demonstrado neste ensaio. A documentação de StartListen não lista Windows cliente suportado; callbacks experimentais nesta imagem não ampliam o suporte documentado da plataforma.

A [sessão adicional de processos](010-native-session-creation.md), o [contexto gráfico](011-native-graphics-context.md) e o início do listener são provas separadas. O próximo resultado necessário é validar uma admissão local autenticada, com callbacks e ciclo de vida reais, mantendo recusa explícita enquanto faltarem os backends de terminal. O [gate nativo](../architecture/0007-native-engine-gate.md) e a [lista de entregas pendentes](../REMAINING-WORK.md) continuam abertos.

Relatórios brutos, identificadores, código operacional e detalhes administrativos permanecem privados. Este documento registra resultados e limites, sem instruções para alterar o computador do leitor.
