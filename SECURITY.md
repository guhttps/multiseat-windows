# Segurança e privacidade

Este projeto ainda é um protótipo de pesquisa. Não há serviço, driver de produto ou isolamento de aplicativos externos aprovado para uso diário.

## Executar os experimentos

Comece pelos diagnósticos e verificações sem elevação. Os scripts administrativos de sessão filha, RDP, Hyper-V e ETW são experimentos separados e podem alterar temporariamente estado do sistema; leia a documentação específica. Não executar testes administrativos em paralelo. Uma falha de restauração exige inspeção do relatório local antes de repetir.

Mantenha as proteções do Windows e do jogo. Não instale drivers sem procedência, não aplique patches em componentes do Windows e não forneça senhas ao projeto. O lançador de comparação em VM verifica o pacote contra o hash transferido, mas isso não constitui assinatura de software nem protege contra alguém que consiga substituir ambos os arquivos.

## Dados locais

Os relatórios são gravados em `docs/lab/results/` e os binários em `build/`, ignorados pelo Git. Esses arquivos podem conter caminhos, IDs de execução, contas de serviço, eventos e detalhes do computador. Não anexar relatórios, ETL, dumps, capturas de tela ou discos de VM diretamente a uma Issue pública. Crie uma descrição mínima, remova dados pessoais e revise-a antes de enviar.

A política de publicação bloqueia artefatos de laboratório, caminhos de perfis pessoais, credenciais e formatos comuns de segredos. A inspeção automatizada complementa a revisão humana; nenhum scanner oferece garantia absoluta de ausência de dados sensíveis.

## Atualizar o GitHub

Revise o diff e envie somente os arquivos da mudança. Antes do push, execute `scripts/Test-PublicContent.ps1 -History` e a varredura de segredos; os workflows repetem essas verificações. Relatórios dessa revisão permanecem locais. Uma configuração salva pelo painel também é um dado do equipamento e não deve ser anexada ao repositório.

A [política de autoria pública](.github/publication-policy.json) define os identificadores públicos dos mantenedores e os nomes aprovados para seus commits. A verificação do histórico confere autor e committer separadamente, inclusive quando o e-mail é GitHub `noreply`. Ela relata a regra e o commit, sem imprimir o nome ou e-mail recusado. Mudanças de mantenedores e nomes públicos exigem revisar essa política.

Use a identidade pública do projeto e o e-mail privado fornecido pelo GitHub. Configurações locais do Git não controlam os metadados de commits criados pelo site: merges e outras integrações também precisam de revisão. Um e-mail `noreply` sozinho não impede a publicação de um nome pessoal.

O GitHub também pode usar o nome da conta no commit temporário gerado ao abrir um PR. Considere esses metadados antes de iniciar uma integração pública; a revisão do commit local não controla essa geração no servidor.

Mantenha as verificações obrigatórias, secret scanning, push protection e a proteção da branch principal. Distribuições devem sair do workflow de release a partir de um commit revisado, com a lista permitida de arquivos, manifesto e hashes conferidos. O rascunho só deve ser publicado após revisar os artefatos; não substitua arquivos ou tags de uma versão já publicada.

O workflow CodeQL analisa C++ e C# em um runner temporário do GitHub, nos pushes, PRs e semanalmente. Ele envia somente os resultados de análise para Code Scanning, sem publicar bancos ou relatórios do computador pessoal. O resultado deve ser revisado na aba **Security**; zero alertas não prova ausência de vulnerabilidades.

O Dependabot propõe semanalmente atualizações das ações do GitHub fixadas por commit. Essas atualizações precisam passar pelos checks e pela revisão antes da integração; não há merge automático. As ferramentas baixadas manualmente, como Gitleaks e Inno Setup, continuam exigindo revisão de versão, procedência e checksums.

## Relatar uma vulnerabilidade

Use a opção privada **Security → Report a vulnerability** quando estiver habilitada no GitHub. Não publique uma credencial ou informação pessoal para demonstrar o problema. Se a opção privada não estiver disponível, abra uma Issue pedindo um canal privado sem incluir o conteúdo sensível.

Se uma credencial for publicada, revogue-a antes de limpar o histórico. Apagar uma linha ou reescrever commits não remove cópias, forks ou caches existentes. Consulte o [procedimento do GitHub](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/removing-sensitive-data-from-a-repository).
