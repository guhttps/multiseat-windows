# Sessões e monitores — diagnóstico de viabilidade

```powershell
cmake -S . -B build/session-test -A x64
cmake --build build/session-test --config Release
.\build\session-test\Release\ms-session-probe.exe
$LASTEXITCODE
```

Lista IDs/estados das sessões que a conta consegue consultar, contexto do processo e console físico. Consulta protocolo (0 console, 2 RDP) quando permitido. Em seguida enumera os monitores do desktop atual e seus retângulos. Não exibe usuários, endereços de rede, títulos de janelas, caminhos de dispositivos ou seriais. Não inicia/desconecta sessões, altera monitores ou habilita acesso remoto.

Nenhuma contagem fixa é exigida. Sessões de serviço/desconectadas/listeners não são estações de usuário demonstradas. `Active` é um estado WTS, não uma prova de pessoas usando monitores físicos distintos. Erros de consulta de protocolo são indicados como indisponível, sem converter em console ou RDP.

Protocolo 0 não identifica sozinho a sessão física: a sessão de serviços 0 também pode retornar esse valor. O marcador `[physical console]` deriva de WTSGetActiveConsoleSessionId, independentemente do protocolo.

Retorno 0 confirma sucesso da consulta principal, não multiseat. A enumeração de monitores é relativa ao desktop do processo e pode incluir monitores virtuais; ela não informa a qual sessão de usuário cada porta HDMI pode ser atribuída. Resultados de CI não substituem o hardware físico.

Fontes: [WTSEnumerateSessions](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/nf-wtsapi32-wtsenumeratesessionsw), [WTS_INFO_CLASS](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/ne-wtsapi32-wts_info_class), [EnumDisplayMonitors](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumdisplaymonitors).
