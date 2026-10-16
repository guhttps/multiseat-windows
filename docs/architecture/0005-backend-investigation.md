# Investigação do motor de estações — sequência após o experimento 005

Estado: pesquisa ativa de T006. Alvo permanece Windows 11 Pro, dois monitores locais na mesma GPU, dois usuários e LoL. Nenhum motor aprovado. Testes básicos do experimento cooperativo 005 concluídos; não ampliar esse experimento para aparentar isolamento externo.

## Comparação para decisão

| Caminho | Evidência e plataforma | Custo técnico/comercial a esclarecer | Lacuna e decisão |
|---|---|---|---|
| Raw Input + painéis próprios | Associação, simultaneidade e recuperação físicas confirmadas | Código pequeno em modo usuário; sem compra específica neste experimento | Só encaminha eventos internos. Experimento encerrado; não selecionado como motor externo |
| Desktops no mesmo console | Processo filho em desktop separado, entrada permanece Default | APIs Win32 disponíveis no SDK; não necessita driver para criar contexto | Não cria dois terminais locais simultâneos. Não selecionar sozinho |
| Provedor RDS documentado | Interfaces IWRdsProtocolManager/Connection com suporte documentado em Server, nenhum cliente suportado | Implementação COM/protocolo, infraestrutura de sessão; edição e licenças da plataforma precisam de análise própria | Não aprovar como backend suportado no Windows 11 Pro |
| Windows Server RDSH | Sessões/terminais remotos simultâneos documentados | Ambiente separado, configuração RDS, licenciamento e apresentação local; GPU depende de configuração/vendor | Pode servir de referência para aplicativos comuns. Não altera automaticamente o alvo e não demonstra LoL |
| VM por usuário | Sistemas separados; Riot VAN 9100 orienta sair de VM | RAM/disco, sistema convidado, licenças, GPU e transporte de imagem/entrada | Não selecionar para o cenário LoL. VBS ativo no host não significa jogo executando dentro de VM |
| Integração nativa especializada | Concorrente publica recursos e limitações; mecanismo interno desconhecido | Pesquisa de sessão/display/input, manutenção por build, eventual WDK/HLK e assinatura | Única hipótese nativa ainda aberta nesta comparação; falta identificar ponto de integração e demonstrar uma estação adicional |

Fontes de plataforma: [console/terminal/WDDM](https://learn.microsoft.com/en-us/windows/win32/termserv/consoles-vs-terminals), [GetInputHandles](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nf-wtsprotocol-iwrdsprotocolconnection-getinputhandles), [RDS suportado](https://learn.microsoft.com/en-us/windows-server/remote/remote-desktop-services/rds-supported-config), [VAN 9100](https://support.riotgames.com/en-us/riot/client/error-van-9100). Custos monetários não estimados; não comprar licença/certificado antes de identificar mecanismo viável.

## Resultado da investigação adicional

`IWRdsProtocolConnection::GetInputHandles` parece relevante pelo nome, mas integra um provedor de protocolo: entrega handles de teclado I8042prt e mouse Mouclass, com suporte documentado em Server. Isso não documenta atribuição de um handle Raw Input USB a uma nova estação no Windows 11 Pro. Handles das duas interfaces não são intercambiáveis por pressuposição.

A documentação pública do concorrente descreve incompatibilidades com integridade de memória e Secure Boot. São declarações do fornecedor sobre o produto dele, não requisito universal do Windows nem instrução para configurar este laboratório. Não revelam uma API utilizável por nossa implementação. Fonte: [limitações ASTER](https://dokwiki.ibiksoft.com/en/v3/core/faq/faq_limitations).

## Segurança e Vanguard: distinções necessárias

A Riot anunciou Vanguard On-Demand em junho de 2026. Esse modo é opcional e exige Windows 11 25H2 ou superior, Secure Boot, TPM 2.0, VBS/HVCI e IOMMU. O mesmo artigo distingue LoL de VALORANT: não exige Secure Boot para LoL em Windows 11 da mesma forma. Portanto, não declarar todo esse conjunto obrigatório para qualquer execução atual de LoL, nem concluir que ausência de HVCI torna nosso teste de uma conta inválido. Também não afirmar qual modo está instalado a partir do estado do serviço. Fonte: [anúncio da Riot](https://www.riotgames.com/en/news/vanguard-on-demand).

Objetivo de engenharia: um futuro motor deve funcionar com as proteções do sistema e com Vanguard operando normalmente. Driver carregar com sucesso não comprova compatibilidade com anti-cheat ou duas instâncias. Modo de teste de assinatura e alterações de boot não constituem caminho de distribuição do produto.

A Microsoft documenta compatibilidade de drivers com HVCI e políticas recentes de assinatura. Não inferir política efetiva só do número de build. O planejamento de distribuição precisa considerar testes e assinatura adequados à política em vigor. Fontes: [compatibilidade HVCI](https://learn.microsoft.com/en-us/windows-hardware/test/hlk/testref/driver-compatibility-with-device-guard), [Windows Driver Policy](https://support.microsoft.com/en-us/windows/hardware/drivers/the-windows-driver-policy).

## Ferramenta de preparação entregue

`scripts/Get-EngineReadiness.ps1` consulta edição/build, Device Guard, Secure Boot, TPM, headers de kernel no diretório padrão do kit e serviço vgc. Salva JSON local ignorado pelo Git; não coleta identidades, números de série, credenciais ou estado de contas Riot. Consultas indisponíveis são `unknown`, nunca convertidas em recurso desativado.

Execução no laboratório em 2026-10-07, sem alterações de configuração:

| Observação | Resultado |
|---|---|
| Windows | Windows 11 Pro, 10.0.26300 |
| VBS | Status 2: habilitado e em execução |
| Integridade de memória | Ausente de SecurityServicesRunning; array retornado [0] |
| Secure Boot / TPM | Sem confirmação: PermissionDenied |
| Headers km/ntddk.h no kit padrão | Nenhuma versão encontrada; não é inventário de todas as instalações possíveis |
| Serviço vgc | Presente, parado no instante da consulta; não determina saúde/modo do Vanguard |

Consulta dentro do sandbox tratou CIM/firmware como desconhecidos. Consulta fora dele leu CIM do Windows/Device Guard; firmware/TPM continuaram indisponíveis. Esse comportamento exercitou o tratamento de acesso negado sem interromper o relatório. Não pedir ao usuário para repetir diagnósticos de segurança apenas para preencher campos que não desbloqueiam o motor.

Referência de interpretação: [Win32_DeviceGuard](https://learn.microsoft.com/en-us/windows/security/hardware-security/enable-virtualization-based-protection-of-code-integrity).

## Próximo spike nativo: contrato de prova

Antes de escrever serviço ou driver, a investigação deve produzir uma ficha com:

1. API/ponto de extensão identificado, documentação de plataforma e forma de criar ou anexar um segundo terminal sem retirar o console principal.
2. Fluxo de autenticação em dois usuários de teste, sem persistir senhas no motor; sessões distintas verificadas.
3. Caminho de saída da sessão adicional até o segundo monitor, considerando que o HDMI físico continua pertencendo ao console no modelo padrão.
4. Caminho de entrada de cada dispositivo até seu terminal, que preserve foco e teclas pressionadas enquanto o outro usuário troca de aplicativo.
5. Modelo de recuperação: falha/encerramento de uma estação mantém a outra utilizável e desfaz associações do protótipo.
6. Estratégia de assinatura/compatibilidade quando houver componente de driver, seguida de P01–P05 com aplicativos comuns antes de P07 com LoL.

Resultado mínimo executável: duas janelas de aplicativos comuns em ambientes distintos, visíveis e interativas ao mesmo tempo, com IDs de sessão e evidência de foco independente. Uma API de enumerar dispositivos, outra janela própria ou um driver que apenas inicia não satisfazem esse contrato.

**Lacuna atual:** nenhuma fonte investigada fornece o conjunto para o alvo nativo. Não há comando adicional do usuário que resolva essa lacuna. T006 permanece ativa para investigar a integração especializada; T007 continua sem aprovação. RDS em laboratório separado só deve ser escolhido explicitamente como pesquisa auxiliar, com escopo e limites definidos antes de instalar componentes.

Atualização 2026-10-08: [ADR 0006](0006-child-session-candidate.md) identifica Child Sessions como candidato documentado em sistemas cliente. Testes elevados e habilitação combinada foram executados; nenhuma sessão filha foi observada. Candidato bloqueado neste ambiente, sem arquitetura aprovada. O usuário escolheu comparação em uma VM com Windows limpo para aplicativos comuns; [laboratório 007](../lab/007-clean-windows-comparison.md) define alcance, preparação e critérios de parada. A VM é ferramenta de pesquisa, sem adoção como motor para LoL.

Fontes consultadas em 2026-10-07. Esta decisão não declara impossível um concorrente completo; registra o que falta provar nesta implementação.

Revisão de 2026-10-16: Child Sessions não será motor do produto, independentemente do resultado da comparação, pois sua identidade vinculada ao pai não satisfaz duas contas Windows próprias. O [ADR 0007](0007-native-engine-gate.md) substitui a prioridade anterior por um gate integrado de terminal, conta, saída física e entrada.
