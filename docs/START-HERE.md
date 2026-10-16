# Comece por aqui

Atualizado em 2026-10-16. Este é um protótipo de pesquisa para um produto multiseat independente. O estado real está em [STATUS.md](STATUS.md); duas estações completas para aplicativos externos ainda não foram demonstradas.

## Usar a base atual

1. Instale Visual Studio Build Tools com C++ desktop, Windows SDK e CMake 3.24 ou superior.
2. Abra o Developer PowerShell na raiz do repositório.
3. Compile e execute as verificações:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\ms-diagnostic.exe
```

O CMake também compila o host C# do experimento de sessão filha. O CTest usa somente verificações de leitura, smoke e testes sintéticos; não conecta sessões filhas nem habilita RDP.

Para testar a associação de dois teclados e dois mouses aos painéis próprios, siga o [laboratório 005](lab/005-seat-demo.md). Esse experimento não isola jogos ou aplicativos externos.

## Continuar o desenvolvimento

A prioridade é o [gate do motor nativo](architecture/0007-native-engine-gate.md): contas Windows distintas, monitor físico, foco e entrada independentes, aceleração gráfica e recuperação. Child Sessions e criação de desktops separados ficam como experimentos auxiliares; não resolvem esse conjunto de requisitos.

O [roteiro de comparação em VM](lab/007-clean-windows-comparison.md) preserva o laboratório anterior. A VM não é a solução escolhida para League of Legends. Não é necessário repetir os testes anteriores para compilar ou estudar esta base.

Veja [DEVELOPMENT-NEXT.md](DEVELOPMENT-NEXT.md) para retomar o trabalho a partir da última entrega. Mantenha relatórios e informações do seu computador em `docs/lab/results/`, fora do Git; consulte [SECURITY.md](../SECURITY.md) antes de publicar evidências.
