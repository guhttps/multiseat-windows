# Organização do GitHub

Endereço do projeto: [guhttps/multiseat-windows](https://github.com/guhttps/multiseat-windows). A branch principal é `main`. A publicação pública está condicionada à revisão de privacidade e às verificações de código; o estado confirmado fica em [STATUS.md](STATUS.md).

## Publicação segura

O histórico privado de desenvolvimento contém um caminho pessoal em versões antigas. Remover somente a linha atual não elimina essas versões. A preparação da primeira edição pública preserva o repositório antigo como arquivo privado e usa um repositório novo, com conteúdo revisado, no endereço original. Logs, relatórios, credenciais, builds, discos de VM e mídias permanecem locais ou no arquivo privado.

Não importar bundles, branches antigas, relatórios privados ou o histórico do arquivo privado para o repositório público. Use a cópia pública como base de novas contribuições. O identificador público da conta proprietária continuará visível no endereço e na atividade do GitHub.

Antes de publicar ou anexar um arquivo:

1. Execute `scripts/Test-PublicContent.ps1` e a varredura de segredos.
2. Revise o diff, os nomes dos arquivos, a autoria e o conteúdo das Issues.
3. Confirme que os workflows passaram e que nenhum artefato privado será incluído.
4. Mantenha os resultados brutos em `docs/lab/results/`, ignorado pelo Git.

Referências: [visibilidade e logs do Actions](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/managing-repository-settings/setting-repository-visibility), [limites da remoção de histórico sensível](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/removing-sensitive-data-from-a-repository).

## Trabalho e marcos

Os identificadores T001–T026 são estáveis e estão em [docs/backlog](backlog). As Issues representam resultados verificáveis; não encerrar uma tarefa apenas porque existe código. M0 reúne laboratório e base, M1 investiga viabilidade. Os demais marcos estão no [ROADMAP](../ROADMAP.md).

T006 investiga o motor; T007 exige a prova de isolamento externo antes de aprovar arquitetura. A configuração de um GitHub Project é opcional e não bloqueia o desenvolvimento. Use os estados Backlog, Ready, In progress, Blocked, Review e Done quando houver um quadro.

Permissões dos workflows devem ser mínimas. Não usar o computador pessoal como runner de contribuições públicas. Não colocar tokens em arquivos versionados ou em comandos copiados para Issues.
