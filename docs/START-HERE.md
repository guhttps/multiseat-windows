# Comece por aqui

Atualizado em 2026-10-16. Este é um protótipo de pesquisa para um produto multiseat independente. O estado real está em [STATUS.md](STATUS.md); duas estações completas para aplicativos externos ainda não foram demonstradas.

## Abrir o painel

A distribuição planejada é **`v0.1.0-experimental.1`**. Confira a disponibilidade em [Releases](https://github.com/guhttps/multiseat-windows/releases). Enquanto a versão não estiver publicada, o caminho disponível é compilar o código abaixo.

Quando os arquivos estiverem publicados:

1. Siga [Download e instalação](DOWNLOAD-INSTALL.md) para escolher instalador ou ZIP.
2. Abra **Multiseat Windows Prototype** pelo menu Iniciar, ou `MultiseatPrototype.exe` na pasta extraída.
3. Clique em **Identificar monitores** e escolha as telas no plano das duas estações.
4. Em cada estação, clique em **Associar teclado + mouse**. Mantenha o painel em foco, pressione **F6** no teclado desejado e clique com o botão direito do mouse desejado dentro do painel.
5. Selecione as saídas de áudio desejadas e clique em **Salvar configuração**. Isso guarda um rascunho; o áudio e os aplicativos ainda não são redirecionados.
6. Para observar os dois pares funcionando nos painéis do protótipo, clique em **Testar dois pares** e siga as instruções da janela de teste. A associação desse teste é feita novamente, separada do plano salvo.

O botão **Iniciar postos** permanece desabilitado porque o motor independente ainda não está implementado. O painel pode ser usado para organizar e testar os dispositivos existentes; não inicia duas contas Windows nem jogos separados.

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

Para testar a associação de dois teclados e dois mouses aos painéis próprios, siga o [laboratório 005](lab/005-seat-demo.md). Esse experimento não isola jogos ou aplicativos externos.

## Suas seleções e privacidade

O rascunho fica somente no perfil Windows atual, em `%LOCALAPPDATA%\MultiseatPrototype\settings.json`. Ele contém identificadores técnicos de dispositivos em forma de hashes; não contém senhas ou gravação de digitação. Mesmo usando o ZIP, salvar o plano usa essa pasta local.

Revise as seleções depois de trocar cabos, dispositivos ou topologia de telas. Esta versão não garante identidade permanente dos monitores nem aplica automaticamente o plano a aplicativos. Não envie o arquivo de configuração ou relatórios brutos ao GitHub.

## Continuar o desenvolvimento

A prioridade é o [gate do motor nativo](architecture/0007-native-engine-gate.md): contas Windows distintas, monitor físico, foco e entrada independentes, aceleração gráfica e recuperação. Child Sessions e criação de desktops separados ficam como experimentos auxiliares; não resolvem esse conjunto de requisitos.

O [roteiro de comparação em VM](lab/007-clean-windows-comparison.md) preserva o laboratório anterior. A VM não é a solução escolhida para League of Legends. Não é necessário repetir os testes anteriores para compilar ou estudar esta base.

Veja [DEVELOPMENT-NEXT.md](DEVELOPMENT-NEXT.md) para retomar o trabalho a partir da última entrega. Mantenha relatórios e informações do seu computador em `docs/lab/results/`, fora do Git; consulte [SECURITY.md](../SECURITY.md) antes de publicar evidências.
