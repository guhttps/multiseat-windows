# Continuidade do desenvolvimento

Atualizado em 2026-10-16. Este arquivo registra o ponto técnico de retomada; não agenda execução nem contorna limites de uso do aplicativo.

## Objetivo

Produto multiseat nativo para Windows, com experiência de configuração comparável ao ASTER e implementação independente. Cada estação precisa de conta Windows, monitor, teclado, mouse e áudio próprios; League of Legends com duas contas é um cenário de aceitação ainda não validado.

## Entrega em andamento

- Preparação de repositório público saneado, preservando histórico e resultados anteriores como privados.
- Correções de Raw Input, conexão/cleanup do experimento C#, validação do pacote e códigos de saída.
- CMake/CTest integrados, verificações de privacidade e segredos em CI.
- Compilação Release e nove testes locais aprovados; verificar também o CI da cópia pública antes de anunciar a publicação concluída.

## Retomar sem repetir trabalho

1. Leia STATUS.md e verifique a branch principal, o diff local e os workflows Windows build e Public content safety no GitHub.
2. Preserve relatórios locais e a base privada anterior. Não faça merge do histórico antigo na base pública; ele contém um caminho de perfil pessoal em versões antigas.
3. Se os testes falharem, corrija a falha e execute somente as verificações afetadas, seguidas das verificações obrigatórias.
4. Siga o ADR 0007: investigar o ponto de integração que cria dois terminais utilizáveis por contas distintas e apresenta cada um no seu monitor físico. Uma nova enumeração de periféricos ou outro painel próprio não satisfaz essa prova.
5. Antes de introduzir serviço/driver, documente o mecanismo, a fronteira de privilégio, autenticação, display/input, recuperação e ambiente de teste. Não usar o PC pessoal como primeiro alvo de um driver experimental.
6. Só depois da prova com aplicativos comuns, validar áudio e duas instâncias do jogo com os critérios P01–P08. Não marcar o produto como concluído enquanto esses critérios estiverem pendentes.

## Limites conhecidos

- Child Sessions não entrega identidades Windows independentes e não é motor de produto selecionado.
- O Windows cliente não fornece, nas APIs investigadas, um caminho completo já demonstrado de dois consoles físicos independentes. Isso é uma lacuna de engenharia desta implementação, não uma declaração de impossibilidade universal.
- Nenhum driver de produto foi instalado ou aprovado. A assinatura e a distribuição dependem do mecanismo real a desenvolver.
- O pacote de VM transferido anteriormente é uma versão de laboratório; não supor que foi atualizado por alterações no código-fonte.
- A licença e o modelo de contribuições externas permanecem pendentes. Não conceder uma licença automaticamente ao publicar o código.
