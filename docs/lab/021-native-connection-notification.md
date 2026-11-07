# Laboratório 021: notificação da conexão nativa

Atualizado em 2026-11-07. **ConnectNotify confirmado no serviço real; ensaio limitado V35 aprovado, com restauração e auditoria independente aprovadas.** Ainda não há segundo terminal nativo utilizável.

## Problema e comportamento

No [laboratório 020](020-native-display-identification.md), o serviço real recebeu o identificador do pacote de display e chamou `ConnectNotify`. O componente recusava essa chamada com `ERROR_NOT_READY`. A nova candidata trata a notificação para a mesma sessão já atribuída e conferida durante a identificação.

Segundo o [contrato da Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nf-wtsprotocol-iwrdsprotocolconnection-connectnotify), a notificação informa que a sessão foi inicializada e deve retornar imediatamente, sem chamadas diretas ou indiretas a APIs RDS. Essa interface continua documentada para Windows Server, sem suporte declarado em Windows cliente; este ensaio permanece pesquisa em VM isolada.

O retorno `S_OK` reconhece a notificação. Ele não autentica uma conta, concede acesso ao driver, demonstra imagem ou torna o terminal utilizável.

## Implementação e limites

- A preparação ocorre após a conferência completa existente de serviço, processo, transporte, atribuição e pacote. Copia a sessão realmente informada, mantém os prazos do registro e retém referências próprias aos objetos de cancelamento e ao processo do cliente, com direito somente de sincronização.
- A chamada de entrada usa tentativa de aquisição de lock, estado em memória, consulta de prazo e sondagens com tempo de espera zero. Não abre arquivos, consulta registro, escreve no pipe, cria threads ou faz chamadas COM/RDS.
- Sessão diferente, conexão revogada, processo encerrado, cancelamento, prazo vencido e capacidade esgotada recusam a chamada. Repetições idênticas são idempotentes; não renovam o prazo nem trocam a sessão.
- Um buffer fixo registra o resultado efetivamente calculado, instante da captura, ordem e categoria. O caminho normal de encerramento ou o trabalhador de saída grava esses dados depois, pela trilha autenticada existente. O instante da gravação não é apresentado como instante da chamada.
- Cancelamento, conflito na atribuição e encerramento do display revogam o estado. As referências permanecem válidas até a conclusão de seu uso; não há fechamento de handles emprestados.

A notificação verifica a identidade previamente conferida e retida, junto dos sinais vivos de cancelamento e prazo. Ela **não reabre a autorização nem refaz a autenticação completa dentro da chamada**. Qualquer operação posterior que conceda um recurso deverá revalidar a autoridade completa por conta própria. O carregamento do driver, a autenticação e a apresentação gráfica continuam indisponíveis nesta candidata.

## Verificações locais

| Verificação | Resultado | Alcance |
| --- | --- | --- |
| Notificação e método COM real da candidata | 46 verificações, zero falhas | Fixtures de eventos próprios; inclui expiração, conflito, revogação, concorrência e saturação |
| Identificação do display e vínculo com a notificação | 71 verificações, zero falhas | Pacote positivo de fixture separado; recusa real de produção no host |
| Permissões do registro | 23 verificações, zero falhas | Descritores em memória e avaliação Authz |
| Formato do produtor e consumidor | 20 verificações, zero falhas | ABI PowerShell/C++, serialização e recusa de execução no host |
| Atribuição da sessão | 26 verificações, zero falhas | Estado e contratos locais, sem criar sessão |
| Contratos herdados da DLL | Aprovados | Preservam recusas e comportamento das etapas anteriores |
| Compilação e análise estática | Aprovadas com avisos como erros | Não executam driver ou serviço real |
| Veredito da próxima execução | 36 verificações, zero falhas | Observações sintéticas válidas e adulteradas, sem alterar a VM |

As 955 referências de fontes e binários anteriores verificadas permaneceram íntegras. As tentativas locais inicialmente reprovadas foram preservadas: uma incompatibilidade de tipos no teste e uma expectativa antiga de retorno, atualizada para exigir o cancelamento permanente após falha de sessão. Nenhuma dessas tentativas foi executada na VM.

## Execução real e próximos passos

A rodada V35 usa plano novo, arquivos fixados por hash e restauração obrigatória seguida de auditoria independente. Mantém o cliente, os drivers de entrada e display, a validade de 20 segundos do registro, o controle de 75 segundos e a recuperação externa de 120 segundos, com as reservas anteriores.

O veredito exige o identificador real, a confirmação capturada da conexão e sua gravação posterior, a próxima recusa de `NotifyCommandProcessCreated`, fechamento real, saída do cliente e preservação do console/processos. Recusa eventos ausentes, duplicados, fora de ordem, capturas expiradas, saturação e sucesso inesperado de operações ainda não implementadas. Não filtra eventos para fazer o ensaio passar. O resultado reprovado da V34 permanece intacto.

Na execução real, a sequência observada foi: canais de entrada retornados, pacote conferido, identificador retornado, `ConnectNotify` reconhecido com `S_OK`, `NotifyCommandProcessCreated` recusado com `ERROR_NOT_READY`, gravação posterior do reconhecimento e fechamento real. A captura ocorreu depois do identificador e antes da próxima notificação; foi gravada cerca de 250 ms depois. Não houve saturação, contenção registrada ou chamada `OnDriverLoad`.

O veredito completo da V35 passou, com saída zero do cliente e preservação do console e dos processos centrais. A restauração e a auditoria independente confirmaram a ausência dos arquivos próprios e a recuperação das configurações e permissões verificadas. Supervisor e coordenador encerraram. Uma revisão posterior reaplicou o mesmo veredito a todos os eventos originais, sem filtragem, e reconferiu as fontes e binários congelados.

A próxima implementação é `NotifyCommandProcessCreated`, ainda recusada. Seu reconhecimento precisará corresponder à mesma conexão e sessão vivas e à ordem observada. Receber essa notificação não comprova, sozinho, processo autenticado de outra conta.

Depois dessa prova, continuam pendentes a autorização separada de início do driver, `OnDriverLoad`, imagem do desktop, consumo nativo de entrada e autenticação da segunda conta. Só a integração dessas partes permitirá validar os dois postos físicos. Consulte [trabalho restante](../REMAINING-WORK.md).

Fontes e binários experimentais e resultados brutos continuam privados. A documentação pública não instala essa candidata nem a inclui no download experimental; sua CI não compila o código privado.
