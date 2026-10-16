# Multiseat Windows

Implementação independente em pesquisa para permitir que várias pessoas usem um computador Windows ao mesmo tempo, cada uma com monitor, teclado, mouse, áudio e conta próprios. O objetivo é oferecer uma experiência de configuração e uso comparável ao ASTER, sem vínculo com seu fabricante.

**Este repositório contém um protótipo de pesquisa. Ainda não oferece estações independentes para aplicativos externos e jogos.**

## Interface e instalação experimental

O painel permite visualizar monitores, teclados, mouses e saídas de áudio, organizar um plano local para duas estações e abrir os testes do protótipo. Salvar esse plano ainda não ativa postos independentes.

A primeira distribuição com interface está sendo preparada como **`v0.1.0-experimental.1`**. Consulte [Releases](https://github.com/guhttps/multiseat-windows/releases): o instalador e o ZIP estarão disponíveis somente após a publicação dessa versão.

Veja [Download, instalação e primeiro uso](docs/DOWNLOAD-INSTALL.md) para os passos simples, requisitos e limites. O instalador planejado é por usuário, sem administrador; o ZIP deve ser extraído por inteiro antes de abrir `MultiseatPrototype.exe`.

| Recurso | Estado |
|---|---|
| Painel gráfico e plano local de duas estações | Em preparação para a distribuição experimental |
| Diagnóstico de sessão, desktop e monitores | Implementado |
| Identificação de dois teclados e dois mouses | Implementada via Raw Input |
| Associação e contagem separada nos painéis próprios | Demonstrada em laboratório |
| Recuperação de periféricos nos painéis próprios | Demonstrada em laboratório |
| Contas Windows e foco independentes para aplicativos externos | Ainda não demonstrados |
| Atribuição exclusiva de monitor, entrada e áudio por estação | Em pesquisa |
| Duas contas de League of Legends simultâneas | Não validado |
| Instalador e ZIP do painel de pesquisa | Em preparação; publicação deve ser confirmada em Releases |
| Motor de postos, serviço, driver aplicável e atualização do produto | Dependem da prova de integração nativa |

## Compilar e verificar

Requer Windows x64, Visual Studio Build Tools com C++, Windows SDK e CMake 3.24 ou superior. Abra o Developer PowerShell na raiz do repositório:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\MultiseatPrototype.exe
```

Para executar só o diagnóstico de ambiente, use `.\build\Release\ms-diagnostic.exe`. O painel e os diagnósticos iniciais funcionam sem administrador.

As verificações automáticas não criam estações independentes nem substituem testes físicos de periféricos, foco, áudio e jogos. Usuários do instalador ou ZIP não precisam instalar as ferramentas de desenvolvimento.

O teste interativo dos painéis próprios está em [laboratório 005](docs/lab/005-seat-demo.md). Os experimentos de sessão filha e os scripts de rastreamento são ferramentas de pesquisa separadas; leia seus limites antes de executar opções administrativas.

## Desenvolvimento

- [Comece aqui](docs/START-HERE.md): caminho de uso e próximos passos.
- [Download e instalação](docs/DOWNLOAD-INSTALL.md): painel experimental, configuração local e remoção.
- [Estado verificado](docs/STATUS.md): o que foi demonstrado e o que falta.
- [Escopo e aceitação](docs/PRODUCT-SCOPE.md): requisitos para duas estações e jogos.
- [Roadmap](ROADMAP.md) e [Issues](https://github.com/guhttps/multiseat-windows/issues): trabalho por evidência.
- [Investigação do motor](docs/architecture/0005-backend-investigation.md): candidatos e limitações.
- [Segurança e privacidade](SECURITY.md): execução, relatórios e publicação.

Não envie senhas, tokens, nomes de usuário, caminhos pessoais, relatórios brutos, imagens de tela ou identificadores do equipamento para Issues e commits. Resultados de laboratório ficam em `docs/lab/results/`, fora do Git.

## Distribuição

O código está disponível para consulta pública. A licença de distribuição e a política de contribuições externas continuam pendentes; publicar o repositório não define uma licença automaticamente. Veja [CONTRIBUTING.md](CONTRIBUTING.md) e a [revisão de publicação](docs/PUBLICATION-REVIEW.md).

ASTER e League of Legends são referências de produto e compatibilidade; este projeto não é afiliado a seus fabricantes. Compatibilidade com Windows, drivers, jogos e mecanismos antitrapaça exige validação própria. O projeto não fornece patches de componentes do Windows ou formas de contornar o Vanguard.
