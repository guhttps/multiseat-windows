# ADR 0004 — limite do console nativo e gate de arquitetura

Estado: decisão de pesquisa; nenhuma arquitetura de motor aprovada. Não declara impossibilidade de produtos multiseat com integração especializada.

## O que mudou

A documentação da Microsoft sobre [console e terminais remotos](https://learn.microsoft.com/en-us/windows/win32/termserv/consoles-vs-terminals) esclarece duas restrições do modelo padrão: existe um único console ativo; os monitores locais de uma GPU WDDM pertencem a esse console. Uma sessão remota usa terminal e driver de display remoto próprios.

Consequência inferida: o plano de criar uma segunda sessão e simplesmente anexar a ela o segundo HDMI via APIs usuais de desktop não tem um mecanismo documentado nessas fontes. Consultar IDs de sessão e enumerar monitores não realiza essa operação.

## Interfaces investigadas

| Interface/caminho | O que documenta | O que não resolve sozinho |
|---|---|---|
| CreateDesktop | Contexto de desktop distinto, com um desktop de entrada ativo por WinSta0 | Segunda estação local simultânea |
| WTSConnectSession | Conectar uma sessão existente a outra e rotear sua saída | Criar um novo terminal físico ou atribuir HDMI por dispositivo |
| IDD de console | Monitores indiretos e processamento de imagens do desktop | Segunda sessão independente só por adicionar monitor virtual |
| IDD remoto | Configuração de display para uma sessão remota; dispositivo vinculado à pilha remota do sistema | Criar livremente um dispositivo remoto a partir de um IDD local |
| Provedor de protocolo RDS | Integração de cliente, terminal remoto e serviço RDS | Evidência de duas sessões locais simultâneas no Windows 11 Pro com LoL |

O [IddCx remoto](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/iddcx1.4-updates-for-remote-idds) verifica se o dispositivo foi criado pela pilha de desktop remoto; marcar a flag de driver remoto num dispositivo comum não basta. [WTSConnectSession](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/nf-wtsapi32-wtsconnectsessionw) é conexão de sessões existentes, não uma API de criação de estações.

A documentação da interface [IWRdsProtocolManager](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nn-wtsprotocol-iwrdsprotocolmanager) lista suporte a Windows Server 2012 como mínimo e nenhum sistema cliente suportado. Portanto, um provedor desse tipo não é uma integração documentada como suportada para nosso Windows 11 Pro. Existência de headers no SDK não equivale a suporte de runtime/plataforma.

Assim, a pesquisa encontrou uma limitação documentada de plataforma para esse caminho, além da lacuna de associação física. Isso não prova que uma extensão especializada equivalente ao concorrente seja impossível; impede aprovar o provedor RDS documentado como motor do alvo atual.

## Comparação com o concorrente

O [histórico público do ASTER](https://dokwiki.ibiksoft.com/en/v3/core/history) menciona componentes de driver, alterações de gerenciamento de sessões/GPU e adaptações por build do Windows. Isso indica, por inferência, uma integração além dos pequenos experimentos Win32 deste projeto; o histórico não revela seu mecanismo interno.

A [FAQ de compatibilidade em inglês](https://dokwiki.ibiksoft.com/en/v3/core/faq/faq_soft) registra limitações de software quando estações compartilham uma GPU. Não é comprovação de funcionamento de duas partidas de LoL. Não usar relatos de fórum como certificado de compatibilidade.

## Decisão de pesquisa

1. Não aprovar um motor baseado apenas em Raw Input, troca de foco, CreateDesktop ou monitor virtual.
2. Manter como investigação uma integração de terminais/sessões/display especializada. Antes de implementá-la, exigir API/plataforma identificadas e uma prova reproduzível com aplicativos comuns.
3. RDS/AVD é alternativa documentada para aplicações gerais em suas plataformas próprias, não substituto já demonstrado do objetivo local Windows 11 Pro. Não mudar o alvo do produto automaticamente.
4. Compatibilidade de LoL/Vanguard permanece requisito não demonstrado. Uma instância funcionando e eventos físicos distinguíveis não sustentam uma promessa de duas partidas.

Gate continua aberto. O progresso autorizado agora inclui consolidar provas, especificar componentes e investigar a integração; instalar um driver sem mecanismo identificado não fecha o gate.

## Verificação repetível

`scripts/Test-Lab.ps1` descobre as ferramentas C++, cria uma pasta de build nova, compila e executa os quatro diagnósticos. Logs e summary.json ficam em docs/lab/results, ignorados pelo Git. O resultado valida o laboratório; os testes físicos já realizados permanecem evidências separadas.

Verificação local concluída: configure, build, environment, input-registration, desktop-context e sessions-displays retornaram 0. Reexecução final da automação também passou. Nenhuma instalação de componente de sessão/display foi realizada.

## Fontes adicionais

- [Visão geral de IDD](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/indirect-display-driver-model-overview).
- [Provedor de protocolo RDS](https://learn.microsoft.com/en-us/windows/win32/termserv/custom-remote-desktop-protocols).
- [Windows Enterprise multi-session](https://learn.microsoft.com/en-us/azure/virtual-desktop/windows-multisession-faq).

Consultadas em 2026-10-07. Não foram alterados RDP, contas, registro, drivers, boot ou políticas de segurança do laboratório.
