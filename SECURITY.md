# Segurança e privacidade

Este projeto ainda é um protótipo de pesquisa. Não há serviço, driver de produto ou isolamento de aplicativos externos aprovado para uso diário.

## Executar os experimentos

Comece pelos diagnósticos e verificações sem elevação. Os scripts administrativos de sessão filha, RDP, Hyper-V e ETW são experimentos separados e podem alterar temporariamente estado do sistema; leia a documentação específica. Não executar testes administrativos em paralelo. Uma falha de restauração exige inspeção do relatório local antes de repetir.

Mantenha as proteções do Windows e do jogo. Não instale drivers sem procedência, não aplique patches em componentes do Windows e não forneça senhas ao projeto. O lançador de comparação em VM verifica o pacote contra o hash transferido, mas isso não constitui assinatura de software nem protege contra alguém que consiga substituir ambos os arquivos.

## Dados locais

Os relatórios são gravados em `docs/lab/results/` e os binários em `build/`, ignorados pelo Git. Esses arquivos podem conter caminhos, IDs de execução, contas de serviço, eventos e detalhes do computador. Não anexar relatórios, ETL, dumps, capturas de tela ou discos de VM diretamente a uma Issue pública. Crie uma descrição mínima, remova dados pessoais e revise-a antes de enviar.

A política de publicação bloqueia artefatos de laboratório, caminhos de perfis pessoais, credenciais e formatos comuns de segredos. A inspeção automatizada complementa a revisão humana; nenhum scanner oferece garantia absoluta de ausência de dados sensíveis.

## Relatar uma vulnerabilidade

Use a opção privada **Security → Report a vulnerability** quando estiver habilitada no GitHub. Não publique uma credencial ou informação pessoal para demonstrar o problema. Se a opção privada não estiver disponível, abra uma Issue pedindo um canal privado sem incluir o conteúdo sensível.

Se uma credencial for publicada, revogue-a antes de limpar o histórico. Apagar uma linha ou reescrever commits não remove cópias, forks ou caches existentes. Consulte o [procedimento do GitHub](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/removing-sensitive-data-from-a-repository).
