# Multiseat Windows

Implementação independente em pesquisa para permitir que várias pessoas usem um computador Windows ao mesmo tempo, cada uma com monitor, teclado, mouse, áudio e conta próprios. O objetivo é oferecer uma experiência de configuração e uso comparável ao ASTER, sem vínculo com seu fabricante.

**Este repositório contém um protótipo de pesquisa. Ainda não oferece estações independentes para aplicativos externos e jogos.**

| Recurso | Estado |
|---|---|
| Diagnóstico de sessão, desktop e monitores | Implementado |
| Identificação de dois teclados e dois mouses | Implementada via Raw Input |
| Associação e contagem separada nos painéis próprios | Demonstrada em laboratório |
| Recuperação de periféricos nos painéis próprios | Demonstrada em laboratório |
| Contas Windows e foco independentes para aplicativos externos | Ainda não demonstrados |
| Atribuição exclusiva de monitor, entrada e áudio por estação | Em pesquisa |
| Duas contas de League of Legends simultâneas | Não validado |
| Serviço, driver assinado, instalador e atualização do produto | Planejados após a prova do motor |

## Compilar e verificar

Requer Windows x64, Visual Studio Build Tools com C++, Windows SDK e CMake 3.24 ou superior. Abra o Developer PowerShell na raiz do repositório:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\ms-diagnostic.exe
```

As verificações automáticas não criam estações independentes nem substituem testes físicos de periféricos, foco, áudio e jogos. Não é necessário executar como administrador para o diagnóstico inicial.

O teste interativo dos painéis próprios está em [laboratório 005](docs/lab/005-seat-demo.md). Os experimentos de sessão filha e os scripts de rastreamento são ferramentas de pesquisa separadas; leia seus limites antes de executar opções administrativas.

## Desenvolvimento

- [Comece aqui](docs/START-HERE.md): caminho de uso e próximos passos.
- [Estado verificado](docs/STATUS.md): o que foi demonstrado e o que falta.
- [Escopo e aceitação](docs/PRODUCT-SCOPE.md): requisitos para duas estações e jogos.
- [Roadmap](ROADMAP.md) e [Issues](https://github.com/guhttps/multiseat-windows/issues): trabalho por evidência.
- [Investigação do motor](docs/architecture/0005-backend-investigation.md): candidatos e limitações.
- [Segurança e privacidade](SECURITY.md): execução, relatórios e publicação.

Não envie senhas, tokens, nomes de usuário, caminhos pessoais, relatórios brutos, imagens de tela ou identificadores do equipamento para Issues e commits. Resultados de laboratório ficam em `docs/lab/results/`, fora do Git.

## Distribuição

O código está disponível para consulta pública. A licença de distribuição e a política de contribuições externas continuam pendentes; publicar o repositório não define uma licença automaticamente. Veja [CONTRIBUTING.md](CONTRIBUTING.md) e a [revisão de publicação](docs/PUBLICATION-REVIEW.md).

ASTER e League of Legends são referências de produto e compatibilidade; este projeto não é afiliado a seus fabricantes. Compatibilidade com Windows, drivers, jogos e mecanismos antitrapaça exige validação própria. O projeto não fornece patches de componentes do Windows ou formas de contornar o Vanguard.
