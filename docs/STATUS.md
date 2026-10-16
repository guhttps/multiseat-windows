# Estado do projeto

Atualizado em 2026-10-16. Protótipo de pesquisa: nenhuma segunda estação independente para aplicativos externos foi demonstrada. O objetivo continua sendo um concorrente completo do ASTER, com implementação independente.

## O que funciona e foi verificado

- Diagnóstico Win32 de sessão, desktop e monitores, sem alterar configuração.
- Identificação de dois teclados e dois mouses e roteamento para painéis próprios.
- Associação, uso simultâneo e recuperação de periféricos nos painéis próprios confirmados em laboratório.
- Build Release C++ e C# integrado ao CMake; nove verificações seguras via CTest passaram localmente.
- Validação de tamanho/tipo de Raw Input e aposentadoria de IDs de dispositivos removidos.
- Teste de sessão filha exige observação estável de conexão/login, impede concorrência na mesma sessão e revalida a identidade antes de encerrar a sessão criada pelo teste.
- Pacote de comparação validado antes da extração; fixtures verificam corrupção, caminhos fora da pasta, duplicatas, arquivos extras, junctions e preservação de resultados anteriores.
- Falhas de lançamento e do experimento rastreado deixam de aparecer como sucesso por código de saída antigo.

Essas verificações não criam duas estações e não demonstram compatibilidade com duas instâncias de um jogo.

## Publicação e privacidade

A preparação da edição pública revisou o conteúdo, os 33 commits privados, nomes de arquivos, autoria e inventário remoto. Uma varredura Gitleaks adicional não encontrou segredos no histórico privado. Um caminho pessoal foi removido do conteúdo atual; detalhes do computador foram generalizados. A publicação usa uma base nova e revisada, preservando o repositório anterior como arquivo privado, para não expor versões antigas ou logs do Actions.

Resultados brutos, pacotes, mídias e discos de VM permanecem fora do Git. Há política de publicação, varredura de segredos, ações fixadas em commits e permissões mínimas nos workflows. Estado da nova publicação e do CI ainda em verificação. Nenhuma ferramenta fornece garantia absoluta de ausência de informações sensíveis; veja [SECURITY.md](../SECURITY.md).

## O que impede o produto completo

T006 investiga o motor; T007 permanece bloqueada pela prova de isolamento externo. Dois painéis próprios não equivalem a duas contas, foco independente ou dois consoles físicos.

Child Sessions foi um experimento auxiliar, com falha de conexão no ambiente anterior. Mesmo que funcione em uma VM limpa, sua sessão vinculada ao pai e login herdado não atendem às duas contas Windows independentes exigidas pelo produto. Por isso, o experimento será mantido como referência, sem tratá-lo como motor aprovado. Veja [gate nativo](architecture/0007-native-engine-gate.md).

A comparação em VM foi preparada, mas não há resultado confirmado da conexão no convidado. Não repetir esse teste como se ele, isoladamente, desbloqueasse a arquitetura. A VM permanece ferramenta de pesquisa para aplicativos comuns.

## Próximo resultado necessário

Demonstrar uma integração de terminal/sessão, display físico e entrada que mantenha dois usuários Windows distintos interagindo ao mesmo tempo, com aplicativos comuns e recuperação de falhas. Áudio, jogos, desempenho, instalação e distribuição vêm depois dessa prova. Os critérios P01–P08 estão em [PRODUCT-SCOPE.md](PRODUCT-SCOPE.md).

O estado de retomada e a sequência de trabalho ficam em [DEVELOPMENT-NEXT.md](DEVELOPMENT-NEXT.md). Não instalar drivers ou comprar certificados apenas para demonstrar que um componente carrega; primeiro identificar o mecanismo e o plano de teste recuperável.
