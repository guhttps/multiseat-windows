# Revisão de segurança de 2026-11-05

Base revisada: commit `588112bb35243005d368182cd69a03379488095f` do repositório público `guhttps/multiseat-windows`. A coleta começou em 2026-11-04 e foi concluída em 2026-11-05. Este documento contém somente resultados sanitizados; relatórios brutos, caminhos locais, identidades de contas, configurações de laboratório e evidências privadas não acompanham a publicação.

**Resultado:** as verificações abaixo não detectaram segredos ou artefatos privados no conteúdo público inspecionado. Não foi confirmada uma vulnerabilidade explorável na revisão das partes de código descritas. Isso não certifica ausência de vulnerabilidades, não prova que nunca houve divulgação e não aprova o motor para uso diário. Existem pendências explícitas nesta revisão.

## Publicação e cadeia de distribuição

| Verificação | Evidência e limite |
|---|---|
| Histórico Git | Gitleaks 8.30.1, com procedência e SHA-256 conferidos, não encontrou segredos na varredura `--all`. A coleta foi ampliada de 86 para 88 commits alcançáveis ao incluir 31 referências de heads/merges de PRs disponíveis no servidor. Referências já apagadas, cópias externas e caches não são cobertos. |
| Conteúdo e autoria | `Test-PublicContent.ps1 -History` passou: 170 arquivos atuais, árvores históricas, caminhos proibidos e metadados de autor/committer. Os testes de regressão da política também passaram. A identidade pública aprovada do projeto é intencionalmente pública. |
| Issues e PRs | 63 registros de descrições e comentários de Issues/PRs, incluindo revisões, passaram pelas buscas de segredos e padrões de privacidade. Não havia anexos nas URLs de anexos do GitHub procuradas nessa coleta. Isso não cobre conversas privadas ou links externos arbitrários. |
| Release publicada | Os quatro arquivos de `v0.1.0-experimental.1` corresponderam aos digests do GitHub e aos manifestos/checksums aplicáveis. Os dez arquivos do ZIP corresponderam à lista permitida e às verificações de conteúdo, PE x64 e ausência de padrões privados. Nenhum executável baixado foi instalado ou executado nesta inspeção. |
| Testes do empacotamento | Foram recusadas fixtures com arquivo extra, conteúdo alterado e diretório redirecionado. Hashes, caminhos do ZIP, conteúdo e manifesto `asInvoker` do instalador passaram. Um hash confere integridade em relação à referência consultada; não substitui assinatura ou análise de comportamento. |
| Antivírus nos arquivos públicos | O Microsoft Defender concluiu duas varreduras personalizadas, nos downloads e no conteúdo extraído, com saída zero e sem indicação de detecção. A remediação foi desabilitada somente nessas chamadas de inspeção; as proteções do Windows permaneceram ativas. Isso não é uma prova de ausência de malware. |
| Alertas do GitHub | Nenhum alerta aberto de Secret Scanning, Dependabot ou Code Scanning no instante da consulta do projeto. Dependências compiladas manualmente têm cobertura diferente, descrita adiante. |

O conteúdo público do repositório e o pacote publicado não incluem os discos, credenciais, relatórios ou binários experimentais do motor. Dados ignorados pelo Git continuam exigindo proteção local e revisão antes de qualquer compartilhamento.

## Código e testes

O inventário público compreende 24 arquivos C++, 16 headers, nove arquivos C#, 28 scripts PowerShell, seis arquivos YAML e uma definição Inno Setup. A revisão combinou inspeção dos caminhos de maior risco, testes de comportamento e consulta aos resultados de análise estática existentes. Não foi uma demonstração formal de correção de cada linha, nem uma auditoria independente dos sistemas de terceiros.

| Área | O que foi conferido | Limite |
|---|---|---|
| Identidade e reservas | Tokens primários reais, cópia limitada de SIDs, exclusão de identidades de serviço, vínculo a logon/sessão, gerações e suspensão de reservas | Uma identidade capturada não demonstra que a sessão continua viva nem cria uma estação. |
| Transporte local público | Pipe local com instância única, acesso mínimo sem criação de instâncias adicionais, política de logon, identidade do processo antes do payload, reversão de impersonação e mensagens limitadas | Não é uma fronteira de isolamento entre programas do mesmo usuário. A autenticação do peer no servidor não equivale a autenticação mútua ou autorização de comandos. |
| Backend privado atual | Integridade das fontes e binários congelados; ACL explícita, recusa de redirecionamentos/hardlinks fora da exceção restrita, processo/token autorizado, prazo e encerramento da ponte | Não houve instalação de driver ou ensaio administrativo novo nesta revisão. A leitura pelo consumidor nativo permanece sem comprovação. |
| Entrada, vídeo e painel | Tamanhos de pacotes, buffers, dimensões, propriedade de texturas, descarte de eventos antigos e configuração local limitada/validada | As primitivas operam nos seus próprios contextos; não comprovam isolamento de duas contas. |
| Cliente RDS de laboratório | Destino IPv4 privado validado, duas contas fixas de laboratório, DPAPI, limpeza de buffers de senha, pin de certificado e recusa de certificado desconhecido/alterado | A dependência FreeRDP exige a triagem de atualização abaixo. Não foram feitas conexões novas a um servidor remoto. |
| Scripts administrativos e instalador | Lançadores com argumentos delimitados, guardas de laboratório, caminhos de restauração, verificações de pacote e bootstrap com hash/publicador | Scripts elevados continuam exigindo checkout confiável e autorização do Windows. Não constituem um auxiliar administrativo genérico de produto. |

Verificações executadas novamente:

- Compilação limpa da base pública e **16/16 testes CTest**, incluindo o caminho positivo de autenticação do canal local neste ambiente.
- **28 verificações** do backend privado, sem falhas e sem carregar driver.
- **365 verificações em 26 cenários** do canal privado, **26 verificações** de atribuição e **11 verificações** de contexto, sem falhas. A integridade de 884 referências de fontes, dependências e binários da integração foi conferida antes da execução.
- Suítes offline de segurança da comparação, do convidado RDS, da descoberta do SDK, da política de publicação e do pacote publicado aprovadas.

A análise CodeQL existente usa `security-extended` para C++ e C#, incluindo o cliente RDS opcional e os helpers C#. PowerShell, INF, configuração de implantação, dependências de terceiros e fontes privadas não são automaticamente cobertos por esse resultado do repositório público. Os scripts têm verificações sintáticas e fixtures específicas; isso não substitui sua revisão.

## Proteções do GitHub

Secret Scanning, Push Protection e reporte privado de vulnerabilidades estavam habilitados. A branch principal exige checks atualizados, histórico linear e resolução de conversas, aplica a proteção a administradores e recusa exclusão/force push. O token padrão dos workflows tem leitura e não pode aprovar PRs; não havia runners próprios, secrets ou variables de Actions configurados no projeto na coleta.

Nesta revisão foi habilitada a política do repositório **`sha_pinning_required`**. As nove referências de ações remotas dos workflows existentes já usavam SHA completo. A configuração foi lida novamente e a lista de ações permitidas permaneceu inalterada. Nenhum check obrigatório foi removido ou enfraquecido. Consulte a [API oficial de permissões de Actions](https://docs.github.com/en/rest/actions/permissions).

## Pendências e restrições de uso

1. **Revisar e qualificar a atualização do FreeRDP do laboratório.** O SDK está fixado em 3.32.2; a [release oficial 3.33.0](https://github.com/FreeRDP/FreeRDP/releases/tag/3.33.0), de 2026-11-02, anuncia correções de segurança. A coleta de 325 advisories publicados do upstream ainda não apresentou as faixas afetadas das 13 referências GHSA citadas nessa release. Portanto, esta revisão não declara a candidata atual livre desses problemas. É necessário acompanhar a publicação dos detalhes, conferir aplicabilidade, atualizar os pins e reconstruir/testar o SDK e a integração correspondente antes de aprovar uso além do laboratório isolado. FreeRDP não faz parte do download experimental do painel. Não se substituíram binários congelados de ensaios anteriores.
2. **Qualificar a segurança do motor integrado.** Ainda faltam autorização por comando e recurso, autenticação dos componentes conforme o modelo de ameaça, revogação de autoridade, isolamento entre contas, desktop seguro/UAC e recuperação sob falhas reais. O fechamento observado na via de recusa não aprova leituras nativas pendentes nem dois postos completos. Os limites e testes não realizados estão nos [documentos do IPC](../engine/native/ipc/README.md) e do [backend nativo](lab/019-native-input-backend.md).
3. **Assinar e qualificar a distribuição antes de tratá-la como produto.** O instalador publicado permanece experimental e sem assinatura Authenticode. A inspeção de integridade não muda essa classificação. A [política de segurança](../SECURITY.md) e os [critérios de conclusão](REMAINING-WORK.md) continuam aplicáveis.

Os dados desta revisão são um retrato datado. Nenhuma proteção de Windows, rede ou jogo foi desativada para obter estes resultados. Uma suspeita nova exige investigação; uma credencial efetivamente exposta exige revogação, mesmo que depois seja removida do Git.
