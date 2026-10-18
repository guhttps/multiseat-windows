# ADR 0007 — motor nativo: duas contas e dois postos físicos

Estado: decisão de pesquisa em 2026-10-16. Motor de produto ainda não aprovado. Este ADR substitui o enquadramento de Child Sessions como candidato a motor nos ADRs 0005/0006; os experimentos anteriores permanecem como histórico verificável.

## Decisão

O alvo continua sendo duas pessoas usando simultaneamente o mesmo computador com Windows, cada uma com sua conta Windows, monitor físico, teclado e mouse. O produto deve executar aplicativos comuns e jogos, com configuração e recuperação comparáveis à experiência pretendida com ASTER. A interface de configuração vem depois da demonstração do mecanismo que mantém esses ambientes independentes.

Child Sessions não será o motor do produto. A Microsoft descreve uma sessão loopback vinculada à sessão existente, login automático relacionado ao usuário, uma única filha ativa/conectada e ciclo de vida dependente do pai. Não há tela de login ou bloqueio própria. Isso não fornece o contrato de duas identidades e postos físicos que precisamos. Corrigir uma falha de conexão ou observar um segundo ID de sessão não basta para aprová-lo. Fonte: [Child Sessions](https://learn.microsoft.com/en-us/windows/win32/termserv/child-sessions).

O experimento de sessão filha pode ser conservado para reproduzir um resultado técnico específico, mas não é próximo passo obrigatório do usuário, prova de duas contas nem condição para avançar o produto. Novas execuções precisam de uma hipótese explícita que justifique seu alcance; repetir a mesma falha não aproxima a meta.

## Limites documentados e hipótese ainda aberta

No modelo padrão documentado pela Microsoft existe um único terminal de console ativo, ao qual pertencem os dispositivos locais. As saídas físicas WDDM estão associadas a esse terminal; sessões remotas usam outra via de apresentação. Assim, dois monitores estendidos no console não constituem dois postos. Fonte: [terminais, sessões e WDDM](https://learn.microsoft.com/en-us/windows/win32/termserv/consoles-vs-terminals).

| Caminho | Alcance comprovado pela documentação | Decisão para o produto |
|---|---|---|
| Raw Input e painéis próprios | Origem de eventos e tratamento dentro de um aplicativo | Manter o experimento existente; não expandir como substituto de isolamento de aplicativos externos |
| Desktops Win32 no mesmo console | Contextos de desktop, sem dois consoles físicos simultâneos demonstrados | Não selecionar isoladamente |
| Child Sessions | Sessão adicional loopback ligada ao usuário existente | Histórico de pesquisa; retirado como motor |
| IddCx de console | Adaptador/monitor indireto e imagens do desktop do console | Componente de display possível, sem mecanismo demonstrado de duas contas |
| IddCx remoto com provedor de protocolo | Apresentação de sessão remota dentro de uma pilha de protocolo apropriada | Não equivale a atribuir uma porta física da GPU a outra sessão |
| Windows Server/RDSH | Plataforma documentada para sessões remotas concorrentes | Pesquisa auxiliar somente com escopo próprio; não altera automaticamente o alvo Windows cliente nem demonstra jogos |
| VM por posto | Sistemas convidados separados | Laboratório auxiliar; não selecionar para LoL com Vanguard |
| Integração nativa especializada | ASTER declara postos independentes e associação de recursos | Hipótese aberta: falta identificar e demonstrar uma integração utilizável por esta implementação |

IddCx fornece recursos para monitores indiretos, incluindo displays virtuais, rede e adaptadores USB. Não é, por si só, um gerenciador de contas ou postos. Drivers de console e remotos são categorias distintas; um driver não pode suportar simultaneamente monitores das duas categorias. O IDD remoto exige um pacote que também contenha uma pilha de protocolo remoto. Fontes: [visão geral IddCx](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/indirect-display-driver-model-overview), [flags do adaptador](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/iddcx/ne-iddcx-iddcx_adapter_flags), [IddCx 1.4](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/iddcx1.4-updates).

O provedor `IWRdsProtocolManager` documenta suporte em Windows Server 2012 ou posterior e nenhum cliente Windows suportado. A existência do header no SDK não comprova suporte no Windows cliente. Windows Enterprise multi-session permite sessões concorrentes, mas a documentação restringe uso de produção ao serviço Azure Virtual Desktop; não é um atalho local para este projeto. Fontes: [IWRdsProtocolManager](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nn-wtsprotocol-iwrdsprotocolmanager), [FAQ multi-session](https://learn.microsoft.com/en-us/azure/virtual-desktop/windows-multisession-faq).

Esses limites não declaram impossível criar um concorrente. A documentação pública consultada não apresenta uma composição pronta que satisfaça o contrato no alvo. Não deduzir a arquitetura interna do ASTER apenas dos recursos que o fornecedor anuncia.

Revisão de 2026-10-17: [IWRdsProtocolConnection2](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nn-wtsprotocol-iwrdsprotocolconnection2) declara Windows cliente build 26100 e acrescenta credenciais serializadas para logon. Essa extensão não documenta duas estações físicas simultâneas nem substitui o contrato base do provider. Não generalizar a restrição de plataforma de uma interface para toda a família. A revisão de implementações e a decisão de laboratório estão no [ADR 0008](0008-backend-code-review.md).

## Comparação com ASTER e jogos

O fornecedor anuncia estações independentes e associação de periféricos. A versão 2.70.6, publicada em 2026-09-08, declara suporte a builds Windows 11 até 26500 e alterações relacionadas a políticas de segurança. Sua documentação também descreve compartilhamento de uma GPU e incompatibilidades de aplicativos. Portanto, segunda GPU não é requisito obrigatório estabelecido para nosso projeto, e a lista de recursos comerciais não comprova compatibilidade universal. Fontes: [produto](https://astermultiseat.com/), [versões do fornecedor](https://ibiksoft.com/download-aster-multiseat-software/), [limites de aplicativos](https://dokwiki.ibiksoft.com/en/v3/core/faq/faq_soft).

Compatibilidade com anti-cheat é um requisito separado da criação dos postos. A Riot documenta o erro VAN 9100 para VM ou ambiente virtualizado não suportado e orienta executar o jogo em instalação Windows comum. Isso exclui VM como plano de entrega de LoL; não significa que VBS no host transforme o jogo em execução dentro de uma VM. Não contornar Vanguard, alterar identificação de hardware ou desativar proteções para declarar o teste aprovado. Fonte: [VAN 9100](https://support.riotgames.com/en-us/riot/client/error-van-9100).

## Gate de aprovação do motor

Todos os itens precisam de evidência do mesmo protótipo, na configuração alvo. Um resultado ausente permanece não demonstrado.

| Área | Evidência mínima de aprovação |
|---|---|
| Identidade | Duas contas de teste distintas, com SIDs e IDs de sessão diferentes; perfis e dados associados à conta correta, sem persistir senhas no motor |
| Monitor físico | Os dois desktops e aplicativos externos visíveis e atualizando simultaneamente nos monitores físicos atribuídos; registrar o caminho efetivo de apresentação de cada sessão |
| Entrada e foco | Cada par de teclado/mouse controla apenas seu posto; digitação, movimento, teclas mantidas e troca de aplicativo em um posto não desviam a entrada do outro |
| GPU | Aplicativo gráfico externo renderiza em cada posto com aceleração de hardware verificada; registrar API, adaptador de renderização e forma de apresentação; receber imagens de desktop não prova desempenho de jogos |
| Segurança da interação | Login, bloqueio, UAC e encerramento respeitam a identidade e o posto correspondente, sem expor credenciais ou encaminhar entrada ao outro usuário |
| Recuperação | Falha ou encerramento de um posto mantém o outro utilizável; hotplug não reassocia silenciosamente periféricos; instalação e remoção do protótipo possuem retorno testado ao estado anterior |
| Áudio | Endpoints de reprodução/captura atribuídos e aplicativos externos validados sem troca involuntária de saída |
| Jogos | Após aprovação com aplicativos comuns: duas contas do jogo e sessões simultâneas, entrada correta, estabilidade e desempenho medidos com anti-cheat funcionando normalmente |

Um binário compilar, um driver carregar, `CreateProcessAsUser` iniciar um processo ou APIs enumerarem sessões/dispositivos não satisfazem o gate. Duas janelas do próprio projeto também não o satisfazem. Não marcar o motor como concluído antes das evidências funcionais.

Resultados brutos, SIDs, caminhos de perfil, identificadores de dispositivos e capturas com dados pessoais ficam em `docs/lab/results/`, ignorado pelo Git. A evidência pública deve relatar procedimento, resultado e limites sem identificar usuários ou máquinas.

## Próxima investigação executável

1. Identificar uma integração candidata concreta: API/ponto de extensão com suporte aplicável ao Windows alvo, ou motor de terceiro com direitos de integração e distribuição verificáveis. Registrar quais operações realmente cria para sessão, terminal, monitor, autenticação e entrada. Não selecionar apenas por nome de API ou presença de símbolos.
2. Produzir o fluxo completo da segunda conta até o monitor físico e de cada dispositivo até seu ambiente. Para cada passagem, apontar componente responsável, permissões, suporte de plataforma e estado ainda desconhecido. Qualquer lacuna mantém a candidata em pesquisa.
3. Somente com esse fluxo identificado, implementar o menor teste integrado que abra aplicativos externos comuns nas duas contas, mantenha as duas saídas físicas visíveis e permita interação simultânea. O teste precisa terminar e desfazer suas próprias alterações; não depende de interface de produto, jogos ou novas compras.
4. Executar o teste em ambiente recuperável adequado ao componente. A VM existente pode verificar lógica de conta/sessão; ela não comprova associação dos monitores físicos do host. Componentes de display/input precisam de validação física antes de aprovação.
5. Avaliar o gate acima, publicar a conclusão saneada e selecionar ou rejeitar a candidata. Sem candidata que complete o fluxo, o próximo resultado é uma lacuna técnica documentada, não outra demonstração de enumeração ou painel.

Se a candidata exigir driver, seu plano deve incluir WDK/SDK adequados, compatibilidade com integridade de memória, assinatura e testes correspondentes. A Microsoft recomenda HLK para drivers de produção; assinatura por attestation é apresentada para testes, requer certificado EV e não certifica compatibilidade ou funcionalidade. Esta etapa registra pré-requisitos; não autoriza compra, instalação ou alteração de boot. Fonte: [opções de assinatura Microsoft](https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/driver-signing-offerings).

Estado final desta decisão: meta nativa preservada, Child Sessions conservado como histórico, gate ainda aberto. Nenhuma compatibilidade simultânea com LoL demonstrada e nenhum comando adicional do usuário identificado como solução para a lacuna do motor.
