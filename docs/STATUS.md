# Estado do projeto

Atualizado em 2026-10-18. Protótipo de pesquisa: o motor nativo para duas estações no Windows cliente ainda não foi demonstrado. O laboratório auxiliar em Windows Server validou duas contas e entrada sintética em aplicativos externos. O objetivo continua sendo um concorrente completo do ASTER, com implementação independente.

## O que funciona e foi verificado

- Diagnóstico Win32 de sessão, desktop e monitores, sem alterar configuração.
- Identificação de dois teclados e dois mouses e roteamento para painéis próprios.
- Associação, uso simultâneo e recuperação de periféricos nos painéis próprios confirmados em laboratório.
- Build Release C++ e C# integrado ao CMake; 14 verificações seguras via CTest passaram na configuração local com o laboratório RDS opcional, incluindo os testes comuns, painel, observador e política do cliente. O build comum conta 12 testes; sem o host C# de sessão filha, conta dez.
- Laboratório RDS separado: duas contas não administrativas, duas sessões ativas distintas e dois Notepads com propriedade verificada receberam suas fixtures sintéticas corretas. A repetição partiu de documentos vazios e confirmou encerramento do cliente com código zero e stderr vazio. O cliente usa NLA e certificado fixado. O usuário confirmou digitação física e manutenção do alvo ao trocar aplicativos no outro posto; os contadores registraram encaminhamento nos dois postos. A contagem quantitativa de eventos e a coleta completa da recuperação física continuam pendentes. Áudio, jogos e motor nativo permanecem pendentes. Veja [laboratório 008](lab/008-rds-transport-comparison.md).
- Recuperação RDS sintética: o posto 2 recebeu sua fixture enquanto o posto 1 estava desconectado; após reconectar, os mesmos aplicativos e sessões receberam os dois textos corretos. O cliente encerrou com código zero e stderr vazio. O modo normal oferece F9 para desconectar o posto daquele teclado e F7 para reconectar transportes indisponíveis. O usuário confirmou o teste físico nos dois sentidos; a coleta disponível registrou a desconexão do posto 1 e continuidade de entrada no posto 2. A conferência automatizada da sequência física completa continua pendente.
- Remoção de periféricos no cliente RDS: as fixtures offline passaram com liberação somente do posto afetado, preservação do par e das entradas mantidas pelo outro, reassociação explícita e descarte de entrada antiga. No primeiro ensaio físico, o usuário confirmou digitação no posto 2 com o teclado do posto 1 desconectado; a coleta observou aumento apenas no tamanho do documento do posto 2, nos mesmos aplicativos e sessões. A conferência completa das associações e da reconexão física ainda está pendente.
- Avisos de periféricos continuam registrados durante a pausa por perda de foco, sem solicitar entrada em segundo plano. Chegadas repetidas após atualizar o registro preservam os pares. Compilação e 14 testes locais passaram; essa correção ainda exige conferência no ciclo físico.
- Validação de tamanho/tipo de Raw Input e aposentadoria de IDs de dispositivos removidos.
- Teste de sessão filha exige observação estável de conexão/login, impede concorrência na mesma sessão e revalida a identidade antes de encerrar a sessão criada pelo teste.
- Pacote de comparação validado antes da extração; fixtures verificam corrupção, caminhos fora da pasta, duplicatas, arquivos extras, junctions e preservação de resultados anteriores.
- Falhas de lançamento e do experimento rastreado deixam de aparecer como sucesso por código de saída antigo.

Essas verificações não aprovam duas estações nativas no Windows cliente nem demonstram compatibilidade com duas instâncias de um jogo.

## Publicação e privacidade

A preparação da edição pública revisou o conteúdo, os 33 commits privados, nomes de arquivos, autoria e inventário remoto. Uma varredura Gitleaks adicional não encontrou segredos no histórico privado. Um caminho pessoal foi removido do conteúdo atual; detalhes do computador foram generalizados. A publicação usa uma base nova e revisada, preservando o repositório anterior como arquivo privado, para não expor versões antigas ou logs do Actions.

Resultados brutos, pacotes locais, mídias e discos de VM permanecem fora do Git. Publicação pública confirmada, inclusive sem autenticação; commit privado antigo recusado no novo repositório. Os workflows [Windows build](https://github.com/guhttps/multiseat-windows/actions/runs/38933836616) e [Public content safety](https://github.com/guhttps/multiseat-windows/actions/runs/38933836603) passaram no código publicado. Secret scanning, push protection e relatos privados de vulnerabilidades habilitados. Há política de publicação, ações fixadas em commits e permissões mínimas nos workflows. Nenhuma ferramenta fornece garantia absoluta de ausência de informações sensíveis; veja [revisão de publicação](PUBLICATION-REVIEW.md) e [SECURITY.md](../SECURITY.md).

## O que impede o produto completo

T006 investiga o motor; T007 permanece bloqueada pela prova de isolamento externo. Dois painéis próprios não equivalem a duas contas, foco independente ou dois consoles físicos.

Child Sessions foi um experimento auxiliar, com falha de conexão no ambiente anterior. Mesmo que funcione em uma VM limpa, sua sessão vinculada ao pai e login herdado não atendem às duas contas Windows independentes exigidas pelo produto. Por isso, o experimento será mantido como referência, sem tratá-lo como motor aprovado. Veja [gate nativo](architecture/0007-native-engine-gate.md).

A comparação em VM foi preparada, mas não há resultado confirmado da conexão no convidado. Não repetir esse teste como se ele, isoladamente, desbloqueasse a arquitetura. A VM permanece ferramenta de pesquisa para aplicativos comuns.

## Painel e distribuição experimental

O painel da versão **v0.1.0-experimental.1** organiza duas estações, lista os dispositivos disponíveis e guarda um plano no perfil local. A associação de teclado e mouse usa F6 e clique direito dentro do próprio painel. O plano ainda não aplica isolamento a aplicativos externos. Alterações de conexões ou da topologia dos monitores exigem revisar as seleções.

A [distribuição experimental publicada](https://github.com/guhttps/multiseat-windows/releases/tag/v0.1.0-experimental.1) oferece instalador por usuário e pacote portátil para Windows x64. Ela inclui o painel e quatro ferramentas de diagnóstico/teste. Conteúdo permitido, hashes, privacidade, instalação/desinstalação e preservação de configurações passaram no [workflow de distribuição](https://github.com/guhttps/multiseat-windows/actions/runs/38933842227). Uma revisão independente dos artefatos não identificou dados pessoais ou segredos. Os quatro downloads públicos foram conferidos sem autenticação. Instalação por usuário e inicialização do painel também passaram no ambiente alvo, sem reinicialização. Consulte [download e instalação](DOWNLOAD-INSTALL.md) para os passos de uso e a ausência de assinatura de código desta versão.

## Próximo resultado necessário

A revisão de código dos candidatos está no [ADR 0008](architecture/0008-backend-code-review.md). Nenhum motor completo reutilizável foi identificado. Foi escolhido um laboratório auxiliar com Windows Server em uma VM separada para desenvolver o transporte de duas sessões reais; a [preparação e as provas pendentes](lab/008-rds-transport-comparison.md) distinguem esse teste da meta nativa.

Demonstrar uma integração de terminal/sessão, display físico e entrada que mantenha dois usuários Windows distintos interagindo ao mesmo tempo, com aplicativos comuns e recuperação de falhas. Áudio, jogos, desempenho e distribuição do motor dependem dessa prova. A instalação do painel experimental é uma entrega separada e não aprova o motor. Os critérios P01–P08 estão em [PRODUCT-SCOPE.md](PRODUCT-SCOPE.md).

O estado de retomada e a sequência de trabalho ficam em [DEVELOPMENT-NEXT.md](DEVELOPMENT-NEXT.md). Não instalar drivers ou comprar certificados apenas para demonstrar que um componente carrega; primeiro identificar o mecanismo e o plano de teste recuperável.
