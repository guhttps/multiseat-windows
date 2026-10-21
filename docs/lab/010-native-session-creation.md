# 010 — criação nativa de uma segunda sessão

Data: 2026-10-21. Estado: criação de sessão/subsistema aprovada em VM cliente; terminal interativo ainda pendente.

## Resultado observado

Uma única solicitação ao Session Manager criou uma nova sessão de processos no Windows cliente da VM descartável. A coleta independente encontrou CSRSS e o executável inicial de teste nessa nova sessão. Os identificadores retornados coincidiram com os processos reais e com o recibo do executável inicial. O processo de teste não estava marcado como crítico.

O console original permaneceu na mesma sessão. Seu observador continuou registrando WinSta0 visível, thread no desktop Default e desktop de entrada Default ativo. CSRSS, Winlogon e Explorer originais conservaram identidade e instante de criação durante a coleta.

| Verificação | Resultado |
|---|---|
| Mensagem limitada à imagem revisada e uma solicitação | Aprovada |
| Nova sessão com CSRSS e comando inicial reais | Aprovada |
| Coincidência entre retorno, processos e recibo com nonce | Aprovada |
| Continuidade do console original durante a coleta | Aprovada |
| Restauração externa obrigatória e conferência do estado original | Aprovada |
| Segunda conta autenticada e terminal utilizável | Não demonstrados |
| Monitor físico e entrada independentes | Não demonstrados |

A enumeração WTS continuou mostrando o console e o listener; ela não apresentou a nova sessão de processos como terminal utilizável. Essa diferença delimita o próximo trabalho: criar a sessão de processos não estabelece sozinho a ligação com login, terminal e apresentação.

## Contenção do ensaio

A preparação seguiu o [ADR 0009](../architecture/0009-native-core-and-session-research.md) e a [qualificação de recuperação](009-native-h1-recovery.md). Foram conferidos os binários reais do convidado, os símbolos correspondentes, o formato da mensagem, a seleção do caminho de alocação e a autorização da operação. A revisão é específica daquela imagem e solicitação; não constitui um contrato suportado do Windows.

O chamador recusou o ambiente do host no teste de proteção. A operação ocorreu somente na VM sem rede, usando uma tarefa LocalSystem limitada ao experimento. O observador do console executou em uma tarefa interativa sem elevação. Nenhum serviço, driver ou configuração de segurança do Windows pessoal foi alterado.

Um controlador externo independente estava armado antes da chamada. Ao esgotar o prazo, ele desligou somente a VM escolhida, restaurou o checkpoint fixado, iniciou o convidado e conferiu o hash de referência e a ausência dos marcadores posteriores. Nenhum CSRSS ou Winlogon foi encerrado manualmente. Não houve segunda tentativa dentro dessa rodada.

## Próxima integração

Investigar o contexto gráfico da sessão criada e a ligação ao terminal, com novas observações dentro da VM recuperável. A segunda conta deve ser autenticada por um fluxo real de logon. A apresentação e a entrada precisam demonstrar simultaneidade com o console original antes de aprovar o motor.

O instalador experimental continua distribuindo somente o painel e os diagnósticos já publicados. Este ensaio não acrescenta um motor utilizável à versão existente, nem aprova áudio, desempenho ou League of Legends.

Credenciais, scripts operacionais, binários do sistema, símbolos, discos, capturas e relatórios brutos permanecem privados. Este documento contém somente o resultado generalizado e seus limites.
