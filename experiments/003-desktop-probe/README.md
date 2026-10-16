# Desktop separado sem alternar a tela

```powershell
cmake -S . -B build/desktop-test -A x64
cmake --build build/desktop-test --config Release
.\build\desktop-test\Release\ms-desktop-probe.exe
$LASTEXITCODE
```

Executar em sessão Windows interativa. Cria um desktop temporário e inicia uma cópia do diagnóstico nesse desktop via STARTUPINFO.lpDesktop. O filho verifica o próprio desktop e compara com o desktop de entrada. O pai aguarda até 10 segundos, verifica retorno e contexto de entrada após a execução, e fecha os handles.

Não chama SwitchDesktop, não instala drivers e não inicia jogos. Esperado: desktop do filho MultiseatProbe_<PID>, input desktop inalterado (comumente Default), PASS e retorno 0. Nome/ID varia. O teste verifica contexto distinto, não prova dois ambientes visíveis simultaneamente ou input independente; também não testa interface gráfica no desktop secundário.

Se a sessão mudar ou for bloqueada durante a execução, a comparação pode falhar. Registrar erro e contexto. Desktop temporário fica sujeito ao ciclo de vida dos handles/processos do Windows.

Referências: [Desktops](https://learn.microsoft.com/en-us/windows/win32/winstation/desktops), [STARTUPINFO](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/ns-processthreadsapi-startupinfow).
