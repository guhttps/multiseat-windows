# 012 — carregamento do provedor de terminal

Data: 2026-10-21. Estado: carregamento real observado; segundo terminal utilizável pendente.

## O que foi demonstrado

Na VM descartável com Windows cliente, o serviço real do Windows carregou o componente experimental e chamou sua fábrica, inicialização, criação do listener e consulta de configurações. O módulo foi conferido dentro do processo que executava o serviço, com identidade e instante de criação verificados separadamente. Uma ativação COM feita pelo próprio teste não foi usada como evidência.

O console original permaneceu saudável e seus processos foram preservados nessa rodada. A restauração externa obrigatória conferiu o arquivo de referência, a ausência dos arquivos, chaves e tarefas do experimento e a configuração original do serviço. Os detalhes operacionais, credenciais e relatórios brutos continuam privados.

## Contrato de configurações e novo boot

A versão seguinte implementou as configurações de segurança do listener e passou em cem verificações de ciclo de vida, liberação de memória, argumentos inválidos e permissões restritivas. A propriedade do buffer foi conferida no chamador correspondente à imagem real. A entrada de produção recusou execução no host.

Duas tentativas dessa versão pararam antes do registro porque o encerramento normal de um serviço dependente não concluiu dentro dos respectivos prazos. Não foram contadas como testes da implementação do listener. Ambas terminaram com restauração e conferência dos nomes próprios ausentes.

Uma rodada posterior registrou o componente e reiniciou normalmente apenas a VM, sem forçar o encerramento de serviços. Após o novo boot e o login físico da conta fictícia, o serviço carregou essa versão e chamou a consulta de configurações. A chamada de início do listener não foi observada. Os testes locais do contrato não substituem essa etapa ainda pendente.

O console ficou saudável no intervalo medido após o novo boot; seus processos foram preservados dentro desse intervalo. Não se afirma continuidade através da reinicialização intencional. A rodada também terminou com restauração externa verificada, nomes próprios ausentes e configuração original do serviço preservada.

## Limite do resultado e próximo passo

As versões observadas não aceitam conexões e não criaram outro terminal. Carregar o componente não demonstra login de outro usuário, saída de vídeo, entrada independente, áudio ou jogos. A documentação da interface base não lista Windows cliente suportado; o comportamento observado nesta imagem é uma capacidade experimental. Veja [IWRdsProtocolManager](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nn-wtsprotocol-iwrdsprotocolmanager) e a [sequência de inicialização](https://learn.microsoft.com/en-us/windows/win32/termserv/start-sequence).

A investigação posterior registrou o retorno real das configurações e a entrada de início do listener; os resultados e a coleta do console ainda inconclusiva ficam no [laboratório 013](013-native-listener-start.md). As rodadas acima preservam seus resultados históricos. Conexão, autenticação e apresentação/entrada de outro usuário continuam pendentes. A [criação de sessão de processos](010-native-session-creation.md) e a [observação gráfica](011-native-graphics-context.md) continuam sendo evidências distintas.

Não foram alterados os serviços do computador pessoal, configurações de licenciamento, inicialização ou políticas de segurança. O provedor não abriu porta de rede nem instalou drivers.
