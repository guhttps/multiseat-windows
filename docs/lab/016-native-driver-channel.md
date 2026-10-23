# 016 — Canal de diagnóstico do driver nativo

Atualizado em 2026-10-23. O protocolo reutilizável foi implementado e testado; a comunicação com um driver carregado ainda não foi demonstrada. O motor nativo permanece indisponível.

## Código público

O módulo [engine/native/channel](../../engine/native/channel/README.md) define mensagens com tamanho fixo, versão, sessão, admissão, dispositivo e sequência. As respostas distinguem configuração do adaptador, inicialização do worker e leitura concluída de imagem. A geração do fluxo permite reiniciar os contadores após uma troca legítima, recusando regressões dentro do mesmo fluxo.

O teste `native-status-protocol` passou com 223 verificações de dados fictícios em memória: truncamento, campos contraditórios, mensagens repetidas, identificadores incorretos, troca de fluxo e resultado vazio após recusa. A configuração comum passou nos 15 testes CTest em Release. O módulo não registra uma interface de dispositivo nem autentica processos.

## Pacote e permissões na VM

Um pacote próprio foi assinado e aceito no DriverStore da VM isolada. A origem e as cópias armazenadas de INF, DLL e catálogo coincidiram. O verificador conferiu o hash integral e o certificado efetivamente validado para DLL e catálogo. A chave foi criada como não exportável somente no convidado. A restauração externa removeu pacote, certificados, tarefas e arquivos temporários; os coordenadores próprios encerraram. Não houve criação de dispositivo ou carregamento do driver.

A verificação como LocalService confirmou acesso às assinaturas, mas recebeu acesso negado ao consultar diretamente o processo real de TermService. Essa falha permanece registrada. O candidato passou a usar a [impersonação por requisição do UMDF](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/handling-client-impersonation-in-umdf-drivers), com conferência de identidade e acesso limitado a consulta e sincronização do processo chamador. A ACL do serviço/processo não foi ampliada.

Um ensaio separado usou uma cópia temporária do token real do serviço dentro de uma tarefa própria da VM. Confirmou usuário permitido, SID de serviço habilitado e abertura do processo sob essa identidade; instantes de criação e hashes incorretos foram recusados. O ensaio distinguiu os contextos de [OpenThreadToken](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-openthreadtoken): abrir o token para inspeção como o próprio processo funcionou; abri-lo como o cliente recebeu acesso negado. Essa opção não muda a identidade da operação seguinte.

Essa tarefa executou como SYSTEM no convidado. Seu resultado não comprova a entrega de um token pelo UMDF a um driver LocalService ou a autenticação de uma requisição WDF. O token do serviço não foi alterado, nenhum token foi exportado e nenhum privilégio foi ajustado. A restauração e a ausência dos recursos próprios foram verificadas.

## Testes isolados e limites

Os candidatos de serviço e driver compilaram com avisos tratados como erros e passaram na análise estática. Os testes isolados somam 515 verificações: protocolo e interpretação de tokens, permissões de arquivos, ciclo de vida, snapshots gráficos, recusa de contexto e conclusão de I/O pendente. Os snapshots usaram 400 texturas geradas em WARP com leitores concorrentes. Cancelamento, timeout e corrida de conclusão foram exercitados em canais locais próprios, mantendo buffers e handles até a conclusão efetiva. Nenhum teste recebeu frames de desktop de outra sessão.

O INF passou em InfVerif `/w` e `/h`, e o pacote passou no teste de requisitos de assinatura. Código operacional, autorizações, credenciais, identidades do ambiente, binários e relatórios brutos permanecem fora do Git.

## Próxima integração

Falta confirmar a sequência real dos callbacks, vincular a admissão à sessão criada pelo Windows e observar o dispositivo remoto correto. A prova precisa carregar o driver, autenticar a requisição WDF, obter monitor ativo e frames reais e encerrar com recuperação verificada. A última chamada real de `OnConnected` continua com `E_POINTER`; os ensaios de pacote e token não repetiram nem resolveram essa chamada.

Segunda conta autenticada, entrada independente, monitores físicos, áudio, jogos e instalador do motor continuam pendentes. O download experimental existente continua sendo painel e diagnósticos.
