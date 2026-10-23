# Protocolo experimental de status

`status-protocol.h` define mensagens de diagnóstico entre os futuros componentes do motor. Não abre dispositivos, autentica processos, instala drivers nem cria uma estação Windows. O backend nativo continua indisponível.

O formato usa requisições de 56 bytes e respostas de 104 bytes, com versão, sessão, identificador da admissão, identificador do dispositivo e sequência. Respostas incluem uma geração do fluxo de imagem para distinguir reinícios de regressões nos contadores. Nenhuma mensagem transporta imagens, credenciais, caminhos, ponteiros ou handles.

A política deste experimento reserva os números de sessão 0, 1 e `0xFFFFFFFF`. Essa comparação não consulta qual sessão ocupa o console físico: o integrador precisa verificar a sessão real e sua autorização antes de usar o protocolo.

`Server` controla a ordem das mensagens que o chamador já autenticou por outro meio. `Exchange` controla uma única requisição pendente e encerra o estado após falha ambígua, resposta incompatível ou regressão dos contadores. Ambos exigem acesso serializado pelo proprietário. Seus identificadores e checksums não substituem a autenticação pelo sistema operacional.

As flags descrevem configuração, inicialização do worker e conclusão de leitura de imagem separadamente. Uma resposta de diagnóstico válida sem essas flags não indica que um terminal esteja pronto. Os limites atuais de imagem, 1280 × 720, pertencem ao experimento.

O teste CTest `native-status-protocol` cobre comprimentos truncados, mensagens contraditórias, sequência repetida, identificadores incorretos, reinício do fluxo, concorrência indevida de requisições e limpeza dos resultados recusados. Usa somente dados fictícios em memória. Nenhum resultado desse teste comprova comunicação com um driver carregado ou dois usuários independentes.
