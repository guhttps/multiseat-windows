# 009 — laboratório cliente e recuperação externa para H1

Data: 2026-10-20. Estado: três verificações reais de recuperação aprovadas; H1 continua em pesquisa.

## Objetivo e resultado

Preparar uma VM descartável com Windows cliente para a investigação do [ADR 0009](../architecture/0009-native-core-and-session-research.md). O laboratório usa conta fictícia, não possui adaptador de rede e fica separado do Windows pessoal e do laboratório RDS.

A preparação confirmou acesso à conta, remoção das credenciais de login automático e dos arquivos de resposta, coleta dos binários do convidado em execução e coincidência com as imagens já analisadas. Um checkpoint Standard foi criado com o convidado desligado por encerramento normal.

## Verificações de recuperação

Cada cenário escreve um marcador depois do checkpoint. Um controlador externo independente restaura somente o checkpoint fixado daquela VM e confere o hash do arquivo de referência e a ausência dos marcadores posteriores.

| Cenário | Evidência | Resultado |
|---|---|---|
| Prazo esgotado | O controlador externo atuou no prazo e verificou o retorno ao estado original | Aprovado |
| Perda do coordenador | O processo de coordenação criado pelo teste foi encerrado; o controlador independente concluiu a recuperação | Aprovado |
| Tarefa sem resposta | Uma tarefa benigna no convidado aguarda sem responder; a recuperação não depende de sua cooperação | Aprovado |

Os três cenários terminaram com a VM em execução no estado original verificado. O último cenário simula uma tarefa bloqueada; não demonstra recuperação de um travamento real do kernel nem de falha do host ou do gerenciamento do Hyper-V.

As verificações inertes do roteiro também passaram: 49 asserções sobre identidade, caminhos, exclusividade, prazos e processos. Essas fixtures são distintas das execuções reais da tabela.

## Correções encontradas no ensaio

O primeiro ensaio identificou uma permissão insuficiente no disco virtual durante a restauração. A correção concede acesso somente à identidade daquela VM nos arquivos imediatos de sua pasta, com herança preservada durante o retorno do checkpoint.

A restauração também revelou uma referência de disco temporário antiga no processo de gerenciamento. A conferência completa da cadeia de discos ocorre no próximo processo independente, antes de iniciar a VM. Objetos antigos do Hyper-V não são usados como evidência de que o estado atual está correto.

Falhas anteriores permanecem separadas dos resultados aprovados. Nenhum processo crítico do Windows foi encerrado e nenhuma chamada privada de criação de sessão foi executada durante esta qualificação de recuperação. A solicitação posterior está registrada no [laboratório 010](010-native-session-creation.md).

## Limites e próximo passo

Este resultado prepara a recuperação do laboratório. Não demonstra segunda conta interativa nativa, terminal adicional, atribuição de monitor, isolamento de entrada, áudio ou League of Legends. A revisão de ABI e autorização e a observação do console exigidas para a solicitação seguinte foram realizadas no laboratório 010; novas rodadas continuam exigindo revisão e contenção próprias.

Scripts locais, credenciais protegidas, imagens, discos, símbolos e relatórios brutos permanecem fora do Git. Este registro publica somente resultados e limites generalizados.
