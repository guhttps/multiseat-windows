# 011 — contexto gráfico da sessão nativa

Data: 2026-10-21. Estado: observação concluída; terminal interativo continua pendente.

## O que foi medido

Depois da [criação nativa de sessão](010-native-session-creation.md), uma nova rodada executou um observador de contexto gráfico na sessão criada. A solicitação, CSRSS e processo inicial foram conferidos independentemente; o console original continuou saudável. A rodada terminou com restauração externa e conferência do estado original.

O observador não conseguiu obter nomes válidos para a window station e o desktop de sua thread. A abertura do desktop de entrada falhou com erro 110, sem desktop de entrada ativo confirmado. A enumeração de monitores também falhou. Isso não permite interpretar o contador inicializado em zero como uma contagem bem-sucedida de monitores.

Esses resultados delimitam a lacuna: o subsistema e o processo existem, mas essa composição ainda não oferece o contexto gráfico e de entrada necessário a um posto utilizável. Ela também não autentica uma segunda conta.

## Correções e tentativas separadas

Uma tentativa anterior foi recusada antes de enviar a solicitação porque a conexão avançada do Hyper-V havia criado uma sessão extra. A conexão foi fechada normalmente; o ensaio seguinte partiu do estado restaurado e de um boot sem essa interferência.

O primeiro observador gráfico falhou ao consultar as dimensões do monitor principal. O evento de falha e os símbolos correspondentes do binário real localizaram a exceção em `GetPrimaryMonitorRect`, dentro de USER32. Esse ensaio não foi contado como uma observação aprovada.

A correção removeu as consultas que presumiam um monitor principal. O observador passou a enumerar alvos existentes e registrar separadamente sucesso, erro e resultados. A versão corrigida concluiu e produziu o relatório descrito acima. Compilação com avisos tratados como erros e testes do contrato de argumentos passaram antes do ensaio.

Cada rodada manteve solicitação única, prazo externo e restauração obrigatória. Não foram criados desktops adicionais, encaminhados eventos de entrada ou instalados drivers. Capturas, credenciais, símbolos, binários e recibos brutos permanecem privados.

## Próximo ponto de integração

Investigar a ligação com o gerenciador de terminais, sem tratar uma sessão de processos como terminal pronto. Uma candidata é verificar se o serviço do Windows cliente instancia um protocol manager próprio. A documentação da interface base lista suporte para Windows Server, sem cliente suportado; portanto, esse comportamento precisa de ensaio isolado, não pode ser inferido da presença do cabeçalho ou da compilação. Fontes: [registro do protocol manager](https://learn.microsoft.com/en-us/windows/win32/termserv/registering-the-custom-protocol) e [IWRdsProtocolManager](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nn-wtsprotocol-iwrdsprotocolmanager).

Uma ativação COM isolada ainda não aprovaria conexões, login, apresentação, entrada ou o motor. A prova completa continua exigindo dois usuários reais interagindo simultaneamente, cada qual com seus recursos, mantendo o console utilizável.
