# Identificar a origem dos eventos de teclado e mouse

Também identifica mouses: clique com o botão direito dentro da janela, alternando os dois mouses. Compare `Mouse 1` e `Mouse 2` no terminal. Os IDs de teclado e mouse são independentes; números iguais não associam dispositivos a uma estação. O cursor do Windows continua compartilhado.

Experimento exploratório antecipado de Raw Input; não conclui inventário M2 nem isolamento M3.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
.\build\Release\ms-keyboard-probe.exe
```

Clique na janela do teste e pressione F6, soltando a tecla, em cada teclado sem trocar de janela. Compare os números no título ou no terminal. IDs diferentes demonstram origens distintas nos eventos recebidos; não demonstram controle independente do Bloco de Notas ou de jogos. Se houver tecla Fn, talvez seja necessário Fn+F6.

Feche a janela para encerrar. Só F6 e cliques direitos são exibidos, somente enquanto a janela recebe entrada em primeiro plano. Nenhum arquivo de eventos é gerado; os IDs são temporários, atribuídos por ordem de recebimento. Um dispositivo composto pode expor múltiplas origens; o teste não determina sozinho quantos dispositivos físicos existem. Reconexões podem alterar os IDs.

`--smoke` verifica criação da janela e registro Raw Input, sem testar eventos físicos.

Referências: [Raw Input](https://learn.microsoft.com/en-us/windows/win32/inputdev/about-raw-input), [WM_INPUT](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-input). O experimento mantém processamento padrão dos eventos e não filtra nem redireciona a entrada de outros aplicativos.
