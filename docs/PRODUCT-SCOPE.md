# Escopo do produto — rascunho

Objetivo confirmado: produto multiseat completo para Windows, concorrente do ASTER, com implementação independente. Jogos são prioritários; o produto também deve atender aplicativos comuns.

## Primeiro cenário de aceitação

Duas pessoas jogando League of Legends simultaneamente no mesmo computador, cada uma com sua própria conta Riot, monitor, teclado e mouse. Esse é um requisito a demonstrar, não uma funcionalidade existente.

- Cada jogador controla somente seu jogo, inclusive quando o outro muda de janela.
- Os dois jogos ficam visíveis e respondem simultaneamente em monitores distintos.
- Login e credenciais Riot separados, sem armazenar senhas no nosso software.
- Entrada independente em teclado e mouse; receber eventos de dois dispositivos não basta.
- Áudio independente será validado com endpoints e dispositivos de reprodução adequados.
- Medir FPS, latência de entrada, CPU/GPU/RAM e estabilidade; resolução, qualidade, metas e duração ainda precisam ser definidos.

## Requisitos do produto completo

Contas e dados Windows separados, atribuição de monitores/entrada/áudio, lifecycle por estação, hotplug, recuperação de falhas, painel de configuração, instalador/desinstalador, atualizações, diagnóstico e matriz de compatibilidade. Quantidade máxima de estações e hardware mínimo ainda estão em aberto.

## Viabilidade de League of Legends

Na consulta de 2026-10-07, a [FAQ oficial do Vanguard](https://support.riotgames.com/en-us/league-of-legends/performance/riot-vanguard-faq-league-of-legends) informa que ele precisa estar ativo durante o jogo e com o cliente aberto. A orientação para [VAN 9100](https://support.riotgames.com/en-us/riot/client/error-van-9100) é executar jogo e Vanguard em Windows convencional, fora de máquina virtual ou ambiente virtualizado não suportado.

Consequência para a pesquisa: não escolher virtualização como solução do cenário LoL sem evidência de suporte. As fontes não confirmam que duas instâncias com contas distintas funcionam simultaneamente no nosso cenário multiseat. Essa compatibilidade permanece desconhecida e deve ser investigada antes de anunciar suporte.

Atualização de pesquisa: o [anúncio Vanguard On-Demand](https://www.riotgames.com/en/news/vanguard-on-demand) descreve um modo opcional com requisitos adicionais de segurança. Não confundir esses requisitos com obrigatoriedade universal para LoL nem inferir modo instalado a partir do estado do serviço. Compatibilidade do futuro motor com as proteções e assinatura de drivers será avaliada conforme [ADR 0005](architecture/0005-backend-investigation.md).

## Próximas evidências

1. Confirmar que uma instância de LoL funciona no PC atual.
2. Investigar restrições do cliente e do Vanguard para sessões/instâncias simultâneas.
3. Demonstrar isolamento simultâneo de display e entrada com aplicações simples, conforme M1.
4. Validar LoL na arquitetura demonstrada e registrar resultados e limitações.

Arquitetura não aprovada. T004 depende de T003; este documento antecipa a captura dos requisitos, sem concluir M0/M1.

## Critérios de aceitação do primeiro protótipo

Critérios de pesquisa, não funcionalidades entregues nem promessa comercial. Os tempos e metas abaixo são valores iniciais de engenharia; podem mudar conforme medições.

| ID | Prova | Critério inicial |
|---|---|---|
| P01 | Duas aplicações comuns | Uma janela de teste por ambiente, visíveis e responsivas simultaneamente durante 5 minutos |
| P02 | Entrada independente | 100 pressões deliberadas por teclado e 100 cliques por mouse; zero eventos recebidos pelo ambiente errado; repetir enquanto o outro usuário alterna janelas |
| P03 | Foco independente | Cada estação alterna entre duas aplicações; a estação oposta mantém seu alvo e suas teclas pressionadas |
| P04 | Identidades Windows | Dois usuários de teste separados; registrar sessão/processos e acesso a perfis sem credenciais em logs |
| P05 | Encerramento e recuperação | Encerrar uma estação não encerra a outra; liberar recursos e voltar ao estado inicial de forma verificável |
| P06 | Áudio | Duas fontes diferentes em dois endpoints/dispositivos independentes, sem cruzamento; validar quando houver dois dispositivos disponíveis |
| P07 | LoL | Duas contas Riot distintas em partidas simultâneas por 30 minutos, Vanguard funcionando normalmente; medir erros e desconexões |
| P08 | Desempenho | Registrar resolução/configuração de cada estação e FPS/CPU/GPU/RAM de uma e duas instâncias; alvo inicial de 60 FPS por estação, sujeito à medição |

Dois painéis de uma aplicação cooperativa podem auxiliar a engenharia, mas não substituem P01/P03 com aplicações comuns. Não usar partidas competitivas para a primeira validação de estabilidade. Contas sintéticas distintas foram criadas somente na VM do laboratório RDS; os critérios P01–P08 no motor nativo continuam pendentes.

Issue de acompanhamento: [T004](https://github.com/guhttps/multiseat-windows/issues/4). Gate de arquitetura: [T007](https://github.com/guhttps/multiseat-windows/issues/7).
