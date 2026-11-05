# Atualização do SDK FreeRDP em 2026-11-05

A receita do laboratório RDS passou de FreeRDP/WinPR 3.32.2 para **3.33.0**. Esta é a continuação do item DEP-01 da [revisão de segurança](SECURITY-REVIEW-20261105.md). A mudança afeta a compilação opcional do laboratório; não substitui o instalador público do painel nem atualiza automaticamente os binários congelados ou as VMs.

## Procedência e escopo

A [release oficial 3.33.0](https://github.com/FreeRDP/FreeRDP/releases/tag/3.33.0), publicada em 2026-11-02, anuncia correções de segurança e novas funcionalidades. A receita fixa o objeto da tag anotada `b4cf84109c0025b9e5c07925ac9d5775cb6d46d1` e seu commit `5a10d7b6b79a2fbc62e8eba77085cd356dc303a6`; ambos foram conferidos no repositório oficial. CMake exige FreeRDP e WinPR **3.33.0 EXACT** para impedir uma seleção silenciosa do SDK anterior.

Perl 5.44.0, OpenSSL 3.5.9 e zlib 1.3.2 mantêm os pins anteriores. As opções mínimas do laboratório foram preservadas. O cache da nova compilação confirmou servidor, canais e transporte UDP experimental desabilitados. A receita e todos os arquivos do prefixo de instalação têm hashes no recibo local; binários, fontes geradas e logs continuam ignorados pelo Git.

Não se alteraram fontes de terceiros para construir o SDK padrão. SDKs e recibos anteriores foram preservados para reproduzir os respectivos ensaios. A migração do adaptador privado e sua execução com o serviço real são qualificações distintas; não podem ser inferidas da simples mudança da receita pública.

## Compatibilidade com a proteção de busca de executáveis

A primeira construção local parou no makefile do Perl, que chama `miniperl` pelo nome após mudar de diretório. Com `NoDefaultCurrentDirectoryInExePath` ativo, o Windows recusa a busca implícita do executável no diretório atual.

O lançador agora aceita um diretório de ferramentas privado, absoluto e contido na árvore aprovada do SDK. Esse diretório é acrescentado ao PATH somente durante o comando necessário. O PATH anterior é restaurado em sucesso ou falha. A proteção de busca implícita permanece inalterada; diretórios externos, relativos, redirecionados e listas de caminhos são recusados antes de iniciar o processo.

As duas chamadas nmake do bootstrap do Perl usam seu próprio diretório recém-criado e verificado. Nenhuma configuração persistente de PATH, registro ou segurança do Windows é alterada. A tentativa incompleta foi preservada separadamente e a compilação válida usou uma árvore nova.

## Validação local

- SDK padrão reconstruído integralmente, com os quatro pins oficiais conferidos e 472 arquivos no prefixo de instalação registrados por hash.
- Cliente público configurado e compilado contra o novo SDK, incluindo o laboratório RDS e o host opcional de sessão filha.
- **18/18 testes CTest aprovados**, incluindo política offline e smoke test do cliente RDS. Nenhuma conexão RDP foi iniciada por esses testes.
- Fixtures do lançador aprovadas: prioridade do diretório privado no processo filho, manutenção da proteção de busca implícita, restauração do PATH após sucesso/falha e recusa de diretórios fora da árvore, relativos, redirecionados ou com injeção de lista de caminhos.

Essas verificações não comprovam autenticação de outra conta, comportamento de uma conexão remota real ou segundo terminal nativo. Não houve instalação de driver, modificação da VM ou desativação de proteções para validar esta receita.

## Avisos de segurança ainda exigem acompanhamento

A release cita 13 referências GHSA. Na coleta desta atualização, a lista oficial consultada retornou 325 advisories publicados, mas não incluiu as faixas afetadas dessas 13 referências; uma consulta direta de referência também retornou indisponível. Portanto, não atribuímos severidades ou aplicabilidade específicas sem os dados do mantenedor.

A atualização adota a base completa publicada pelo upstream, incluindo suas correções, enquanto o acompanhamento dos detalhes continua. Os testes upstream de todas as dependências permanecem desabilitados nesta receita mínima. Compilação, testes locais e ausência de alertas não certificam ausência de vulnerabilidades. Consulte os [advisories do FreeRDP](https://github.com/FreeRDP/FreeRDP/security/advisories) ao qualificar uma integração e antes de ampliar o uso do laboratório.

As instruções e os pins atuais ficam no [README do SDK](../experiments/008-rds-client/README-SDK.md). O bloqueio nativo de display continua descrito no [laboratório 019](lab/019-native-input-backend.md).
