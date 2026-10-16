# Revisão da primeira publicação pública

Data: 2026-10-16. Repositório: [guhttps/multiseat-windows](https://github.com/guhttps/multiseat-windows).

## Histórico e conteúdo

O histórico privado anterior foi preservado em repositório privado e arquivado. A base pública foi criada com um commit inicial revisado, sem ancestrais privados, seguido somente das correções dessa publicação. Isso evita tornar públicos os objetos e logs anteriores, inclusive versões com um caminho pessoal que foi removido do código atual.

A revisão incluiu as 85 fontes iniciais, nomes de arquivos, autoria, mensagem do commit, links e textos das sete tarefas. A autoria pública usa nome genérico do projeto e e-mail GitHub noreply. Foram generalizados dados do computador; nenhuma senha, chave ou token foi encontrado nas fontes. O identificador público da conta proprietária continua visível no endereço do GitHub.

Builds, pacotes, relatórios, ETL, dumps, discos de VM, mídias e cache de auditoria não foram publicados. Os relatórios privados da revisão e o backup local permanecem ignorados pelo Git. O acesso público foi verificado sem autenticação; uma consulta a um commit privado antigo foi recusada no novo repositório.

## Verificações

- Varredura Gitleaks com binário obtido da distribuição oficial e checksum SHA-256 conferido; nenhum segredo detectado na base pública.
- Política de conteúdo público aplicada à árvore e ao histórico alcançável: artefatos privados, caminhos de perfil, chaves, tokens, SIDs pessoais e e-mails que exigem revisão.
- Revisão independente de privacidade do snapshot inicial, sem achados sensíveis.
- Compilação C++/C# Release, nove verificações CTest e fixtures inertes de validação do pacote aprovadas localmente e no GitHub.
- Corrigidas diferenças encontradas no CI: arquivos ocultos no Linux e código de saída herdado de testes negativos.

Os workflows [Windows build](https://github.com/guhttps/multiseat-windows/actions/workflows/windows-build.yml) e [Public content safety](https://github.com/guhttps/multiseat-windows/actions/workflows/security.yml) repetem as verificações nos próximos commits. Eles não publicam os relatórios locais como artefatos.

## Proteções e limites

Secret scanning, push protection, alertas de vulnerabilidades e relatos privados de vulnerabilidades foram habilitados no GitHub. Workflows recebem permissões de leitura, não persistem credenciais no checkout e usam ação oficial fixada por SHA. Contribuições externas precisam de autorização do mantenedor para executar workflows.

Essas medidas não oferecem garantia absoluta contra dados sensíveis ou falhas futuras. Não importar histórico, branches ou arquivos do arquivo privado para a base pública. Revise manualmente evidências antes de anexá-las, mesmo quando o scanner não encontrar nada.

A licença ainda não foi escolhida. Esta publicação não afirma equivalência funcional com ASTER: isolamento externo, duas contas Windows, áudio por estação, instalação e jogos continuam dependentes do [gate do motor](architecture/0007-native-engine-gate.md).
