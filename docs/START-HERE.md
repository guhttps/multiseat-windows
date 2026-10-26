# Comece por aqui

Atualizado em 2026-10-26. Este é um protótipo de pesquisa para um produto multiseat independente. O estado real está em [STATUS.md](STATUS.md); duas estações completas para aplicativos externos ainda não foram demonstradas. Veja a [lista de entregas pendentes](REMAINING-WORK.md) para acompanhar o que falta até o produto completo.

## Abrir o painel

Versão disponível: [**v0.1.0-experimental.1**](https://github.com/guhttps/multiseat-windows/releases/tag/v0.1.0-experimental.1). Você pode usar o painel sem instalar ferramentas de desenvolvimento.

1. [Baixe o instalador Windows x64](https://github.com/guhttps/multiseat-windows/releases/download/v0.1.0-experimental.1/MultiseatPrototype-0.1.0-experimental.1-windows-x64-installer.exe), abra o arquivo e conclua a instalação por usuário. Confira integridade, requisitos e a opção ZIP em [Download e instalação](DOWNLOAD-INSTALL.md).
2. Abra **Multiseat Windows Prototype** pelo menu Iniciar, ou `MultiseatPrototype.exe` na pasta extraída.
3. Clique em **Identificar monitores** e escolha as telas no plano das duas estações.
4. Em cada estação, clique em **Associar teclado + mouse**. Mantenha o painel em foco, pressione **F6** no teclado desejado e clique com o botão direito do mouse desejado dentro do painel.
5. Selecione as saídas de áudio desejadas e clique em **Salvar configuração**. Isso guarda um rascunho; o áudio e os aplicativos ainda não são redirecionados.
6. Para observar os dois pares funcionando nos painéis do protótipo, clique em **Testar dois pares** e siga as instruções da janela de teste. A associação desse teste é feita novamente, separada do plano salvo.

O botão **Iniciar postos** permanece desabilitado porque a integração do motor independente ainda não foi concluída. O painel pode ser usado para organizar e testar os dispositivos existentes; não inicia duas contas Windows nem jogos separados.

## Compilar o código

1. Instale Visual Studio Build Tools com C++ desktop, Windows SDK e CMake 3.24 ou superior.
2. Abra o Developer PowerShell na raiz do repositório.
3. Compile e execute as verificações:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\MultiseatPrototype.exe
```

O CMake compila o painel, os diagnósticos e o host C# do experimento de sessão filha. O CTest usa verificações de leitura, smoke e testes sintéticos; não conecta sessões filhas nem habilita RDP. Para abrir apenas o diagnóstico de ambiente, execute `.\build\Release\ms-diagnostic.exe`.

A base de [leitura gráfica nativa](../engine/native/display/README.md) também tem teste automático: `ctest --test-dir build -C Release -R native-frame-readback --output-on-failure`. Ele usa texturas geradas em software, sem capturar a tela ou instalar driver. O [laboratório 015](lab/015-native-display-foundation.md) explica o resultado e a integração pendente.

Para testar a associação de dois teclados e dois mouses aos painéis próprios, siga o [laboratório 005](lab/005-seat-demo.md). Esse experimento não isola jogos ou aplicativos externos.

## Suas seleções e privacidade

O rascunho fica somente no perfil Windows atual, em `%LOCALAPPDATA%\MultiseatPrototype\settings.json`. Ele contém identificadores técnicos de dispositivos em forma de hashes; não contém senhas ou gravação de digitação. Mesmo usando o ZIP, salvar o plano usa essa pasta local.

Revise as seleções depois de trocar cabos, dispositivos ou topologia de telas. Esta versão não garante identidade permanente dos monitores nem aplica automaticamente o plano a aplicativos. Não envie o arquivo de configuração ou relatórios brutos ao GitHub.

## Continuar o desenvolvimento

A prioridade é o [gate do motor nativo](architecture/0007-native-engine-gate.md): contas Windows distintas, monitor físico, foco e entrada independentes, aceleração gráfica e recuperação. Child Sessions e criação de desktops separados ficam como experimentos auxiliares; não resolvem esse conjunto de requisitos.

Na pesquisa em VM, o provedor já concluiu um ciclo de recusa controlada: OnConnected e OnReady retornaram sucesso, AcceptConnection recusou a continuação por falta dos backends e o serviço fechou a conexão. Os controles e limites estão no [laboratório 014](lab/014-native-local-admission.md). Uma candidata isolada de entrada depois carregou na VM e concluiu leituras e cancelamentos das filas próprias com dados de teste, conforme o [laboratório 017](lab/017-native-input-integration.md). A ligação à segunda sessão continua pendente. As rodadas concluídas restauraram o laboratório. Nenhuma delas demonstrou dois postos nativos utilizáveis.

O [roteiro de comparação em VM](lab/007-clean-windows-comparison.md) preserva o laboratório anterior. A VM não é a solução escolhida para League of Legends. Não é necessário repetir os testes anteriores para compilar ou estudar esta base.

Veja [DEVELOPMENT-NEXT.md](DEVELOPMENT-NEXT.md) para retomar o trabalho a partir da última entrega. Mantenha relatórios e informações do seu computador em `docs/lab/results/`, fora do Git; consulte [SECURITY.md](../SECURITY.md) antes de publicar evidências.
