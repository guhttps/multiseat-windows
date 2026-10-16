# Pesquisa inicial de isolamento — M1

Estado: pesquisa antecipada, sem arquitetura aprovada. Não encerra M0 nem tarefas dependentes. Origem de entrada já observada; dois ambientes independentes ainda não demonstrados.

| Caminho | Evidência disponível | Lacuna para nosso produto |
|---|---|---|
| Dois desktops em WinSta0 | Microsoft documenta apenas um desktop ativo por vez | CreateDesktop sozinho não fornece duas estações visíveis e interativas simultaneamente |
| Raw Input na sessão atual | Teste físico distinguiu dois teclados e dois mouses | Não fornece por si só foco independente para aplicativos arbitrários |
| Sessões Windows distintas | Cada sessão RDS tem sua WinSta0; Microsoft oferece Enterprise multi-session em Azure Virtual Desktop | Caminho suportado no Windows 11 Pro local, associação a HDMI/input e compatibilidade LoL desconhecidos |
| Virtualização | Riot orienta executar fora de VM em erro VAN 9100 | Não é caminho aprovado para o cenário prioritário de LoL |
| Compositor ou driver especializado | Nenhuma prova neste projeto | Display, foco, entrada, segurança, compatibilidade e manutenção por build desconhecidos |

## Próximo experimento: desktop separado, sem alternância

Criar um desktop Win32 temporário e executar nosso diagnóstico nele, mantendo o desktop atual visível. Registrar desktop do processo filho e desktop de entrada ativo; observar que criar outro desktop não atribui esse desktop ao segundo monitor. Não chamar SwitchDesktop nessa primeira prova.

Critérios: processo filho confirma desktop diferente de Default, desktop de entrada permanece o atual, handles e processo encerram corretamente; falhas têm códigos registrados. Isso demonstra separação de objetos desktop e seu limite, não multiseat. Não lançar LoL nessa prova. Implementado em experiments/003-desktop-probe; build Release x64 e teste local pelo agente passaram com retorno 0. Usuário repetiu com filho em MultiseatProbe_39732, desktop de entrada Default antes/depois, handle fechado, PASS e retorno 0. Não houve teste gráfico ou alternância de desktops.

## Fontes consultadas em 2026-10-07

- [Desktops](https://learn.microsoft.com/en-us/windows/win32/winstation/desktops).
- [Window stations](https://learn.microsoft.com/en-us/windows/win32/winstation/window-stations).
- [Criação de desktops](https://learn.microsoft.com/en-us/windows/win32/winstation/window-station-and-desktop-creation).
- [Azure Virtual Desktop](https://learn.microsoft.com/en-us/azure/virtual-desktop/overview).
- [Riot VAN 9100](https://support.riotgames.com/en-us/riot/client/error-van-9100).

Uma hipótese refutada ou uma lacuna identificada evita escolher uma arquitetura sem mecanismo demonstrado. Manter a decisão aberta até o gate do ADR 0001.
