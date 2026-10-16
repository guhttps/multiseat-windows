# Isolamento — resultado da pesquisa inicial

Estado: nenhuma arquitetura aprovada. Pesquisa de M1 antecipada enquanto organização administrativa de M0 permanece pendente. Fontes consultadas em 2026-10-07.

## Conclusão técnica

Os experimentos demonstraram identificação de entrada e execução de um processo em desktop separado. Ainda não há mecanismo demonstrado para entregar duas estações locais com aplicativos comuns e jogos. Construir um painel de configuração neste momento não resolve essa lacuna.

| Hipótese | Evidência | Decisão nesta fase |
|---|---|---|
| Criar dois desktops basta | Filho executou em desktop temporário; input desktop permaneceu Default. Microsoft documenta um desktop ativo por vez em WinSta0 | Refutada como solução completa por si só |
| Raw Input separa aplicativos comuns | Dois teclados e dois mouses distinguíveis. RIDEV_NOLEGACY afeta mensagens para a aplicação que se registra | Identificação demonstrada; isolamento externo não demonstrado |
| Alternar foco e usar SendInput oferece duas estações | SetForegroundWindow ativa uma janela; SendInput insere eventos no fluxo de entrada, com restrições de integridade | Não satisfaz o requisito de dois focos independentes por si só |
| Sessões distintas oferecem base de isolamento | Cada sessão RDS tem sua WinSta0; consulta local encontrou sessão 1 ativa e 0 desconectada | Investigar caminho de criação/login suportado, display e input locais; nenhuma segunda estação criada |
| VM resolve duas instâncias de LoL | Riot informa ausência de suporte ao Vanguard em VMs | Não selecionada para o cenário prioritário |
| Driver próprio resolve a lacuna | Nenhum mecanismo de criação de estações/display/foco demonstrado | Não iniciar driver apenas com essa hipótese |

## Snapshot local: experimento 004

Build Release x64 limpo com MSVC e /W4 /WX; execução pelo agente com retorno 0:

```text
Current process session: 1
Physical console session: 1
Session 0: Disconnected protocol=0
Session 1: Active [this process] [physical console] protocol=0
Enumerated sessions: 2 | state Active: 1
Monitor 1: desktop rectangle [0, 0, 1920, 1080] primary
Monitor 2: desktop rectangle [1920, 81, 3360, 981]
Monitors enumerated in this desktop: 2
```

Snapshot não prova restrição permanente a uma sessão nem descreve todas as capacidades do Windows. Apenas registra o estado observado. Sessão 0 não é a segunda estação; protocolo 0 sozinho não identifica o console físico. Monitores são enumerados no desktop atual; não estão atribuídos separadamente a sessões por este experimento. Usuário repetiu o diagnóstico e enviou saída coincidente, com $LASTEXITCODE 0.

## Próxima investigação

Priorizar sessões distintas como hipótese de isolamento para aplicações comuns. Antes de prototipar roteamento, identificar um caminho suportado de criação/login para duas sessões simultâneas no alvo escolhido e um mecanismo de apresentação/input local por sessão. A documentação de Windows Enterprise multi-session em Azure Virtual Desktop não constitui evidência de suporte equivalente em Windows 11 Pro local.

Gate de continuação: documentar APIs/plataforma e teste reproduzível para duas aplicações em sessões distintas com display e input independentes. Sem esse caminho, registrar a hipótese como não demonstrada e comparar plataforma alternativa ou integração especializada. Não substituir essa prova por dois painéis cooperativos de nossa própria aplicação.

LoL: uma instância funciona conforme usuário; duas contas simultâneas não testadas. FAQ de terceiros da Riot não concede compatibilidade automática a produtos multiseat. As páginas de ASTER consultadas apresentaram recursos gerais, sem comprovação técnica específica de LoL/Vanguard neste laboratório. Arquitetura interna do concorrente não foi determinada.

## Fontes

- [Desktops](https://learn.microsoft.com/en-us/windows/win32/winstation/desktops).
- [Window stations](https://learn.microsoft.com/en-us/windows/win32/winstation/window-stations).
- [RAWINPUTDEVICE](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawinputdevice).
- [SetForegroundWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow).
- [SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput).
- [Azure Virtual Desktop](https://learn.microsoft.com/en-us/azure/virtual-desktop/overview).
- [Riot VAN 9100](https://support.riotgames.com/en-us/riot/client/error-van-9100).
- [Vanguard e aplicativos de terceiros](https://www.riotgames.com/en/DevRel/vanguard-faq).
- [Recursos apresentados pelo ASTER](https://astermultiseat.com/features).
