# SDK local do laboratório RDS

`scripts/Build-RdsLabSdk.ps1` compila dependências oficiais fixadas para o cliente experimental. Não configura RDP, cria contas, inicia VM ou instala ferramentas globalmente. O SDK não constitui um motor multiseat para Windows cliente.

Pré-requisitos já instalados: Windows x64, PowerShell x64, Git, CMake, Ninja e Visual Studio 2022/2026 com ferramentas C++ x64 e Windows SDK. O script encontra o Visual Studio por `vswhere`, abre o ambiente de desenvolvimento e restaura as variáveis do processo ao terminar. Nenhuma dependência é baixada de um pacote binário reempacotado.

```powershell
.\scripts\Build-RdsLabSdk.ps1
cmake -S . -B build/rds-client -A x64 -DMULTISEAT_BUILD_RDS_LAB=ON -DCMAKE_PREFIX_PATH="$PWD/build/rds-sdk/install"
```

O destino local é `build/rds-sdk`; fontes, builds, Perl e logs ficam somente nessa árvore ignorada. Em um runner hospedado do GitHub, `-BuildRoot "$env:RUNNER_TEMP/rds-sdk"` permite colocar todas as fontes de terceiros fora do checkout. Para CodeQL, termine o SDK antes de inicializar a análise de C/C++. O prefixo CMake será `<BuildRoot>/install`, com `FreeRDPConfig.cmake` e `WinPRConfig.cmake`; os pacotes usam `find_package(FreeRDP 3.32.2 EXACT CONFIG)` e `find_package(WinPR 3.32.2 EXACT CONFIG)`.

| Fonte oficial | Tag | Commit fixado |
| --- | --- | --- |
| [Perl/perl5](https://github.com/Perl/perl5) | `v5.44.0` | `e634cc838fb1785dfafb5d13b52807ea16b5c52c` |
| [openssl/openssl](https://github.com/openssl/openssl) | `openssl-3.5.9` | `45e844fa2a14ec92d146bd8f5778ac130b6625fb` |
| [madler/zlib](https://github.com/madler/zlib) | `v1.3.2` | `da607da739fa6047df13e66a2af6b8bec7c2a498` |
| [FreeRDP/FreeRDP](https://github.com/FreeRDP/FreeRDP) | `3.32.2` | `97eff6ddfbbfaa8e42a126d362686bd001ff8bdc` |

O script também fixa o SHA do objeto de cada tag anotada, verifica a resolução para o commit e rejeita HEAD diferente, checkout sujo e caminhos redirecionados. Não altera os fontes. Perl é compilado com `USE_NO_REGISTRY=define` e serve somente à compilação do OpenSSL. OpenSSL usa `VC-WIN64A shared no-asm no-tests no-docs` em um build separado. zlib fornece `zs.lib`; o conjunto mínimo de opções do FreeRDP não usa atualmente zlib, mas ela permanece disponível no SDK.

FreeRDP/WinPR e OpenSSL são bibliotecas compartilhadas com CRT `/MD`; zlib é uma biblioteca estática com o mesmo CRT. Áudio, canais, ferramentas, servidor FreeRDP e codecs externos ficam desabilitados para este experimento. O cliente é compilado separadamente. A receita fixa fontes e opções; versões do compilador/SDK e caminhos de build podem produzir binários diferentes.

Uma árvore existente só é reutilizada com recibo completo da mesma receita e correspondência de todos os arquivos/hash do prefixo. Árvores desconhecidas, incompletas, adulteradas ou com arquivos extras são recusadas. Preserve uma tentativa interrompida para inspeção e use uma árvore nova; o script não apaga builds automaticamente. O console mostra somente etapas. Logs brutos podem conter caminhos da máquina e permanecem privados em `<BuildRoot>/logs`.

Na revisão de 2026-10-18, o build tool Perl foi atualizado para a [versão estável 5.44.0](https://www.perl.org/get.html). O runtime usa a [release de segurança FreeRDP 3.32.2](https://github.com/FreeRDP/FreeRDP/releases/tag/3.32.2) e a atualização de segurança [OpenSSL 3.5.9 da série 3.5](https://openssl-library.org/news/openssl-3.5-notes/). Isso não é uma auditoria completa de vulnerabilidades; futuras atualizações exigem revisar os avisos oficiais e repetir a construção e os testes, sem mudar pins automaticamente.

As licenças originais são copiadas sem alterações: FreeRDP e OpenSSL usam [Apache-2.0](https://github.com/FreeRDP/FreeRDP/blob/97eff6ddfbbfaa8e42a126d362686bd001ff8bdc/LICENSE), [licença OpenSSL](https://github.com/openssl/openssl/blob/45e844fa2a14ec92d146bd8f5778ac130b6625fb/LICENSE.txt); zlib usa a [licença zlib](https://github.com/madler/zlib/blob/da607da739fa6047df13e66a2af6b8bec7c2a498/LICENSE). Perl oferece as alternativas [GPL](https://github.com/Perl/perl5/blob/e634cc838fb1785dfafb5d13b52807ea16b5c52c/Copying) e [Artistic](https://github.com/Perl/perl5/blob/e634cc838fb1785dfafb5d13b52807ea16b5c52c/Artistic). Esses termos pertencem aos componentes de terceiros e não definem a licença deste projeto nem concedem direitos adicionais sobre Windows/RDS.

Os testes upstream dessas dependências estão desabilitados nesta receita de laboratório. Compilar, ligar e estabelecer conexões são verificações separadas. Não publique o SDK, logs, fontes geradas ou binários locais como assets: esta receita não aprova redistribuição, auditoria completa de dependências, assinatura ou ausência de caminhos privados nos binários.
