# ADR 0001 — validar isolamento antes de escolher o motor
Estado: proposto. Produto alvo: multiseat completo; arquitetura ainda não aprovada.

## Correções do roteiro anterior
- CreateDesktop cria um desktop, não uma estação física independente. Em WinSta0 apenas um desktop fica ativo por vez.
- Raw Input distingue a origem dos eventos para um aplicativo. Receber eventos de dois dispositivos não prova isolamento de entrada para programas arbitrários; RIDEV_NOLEGACY não é um filtro global.
- Enumerar saídas DXGI não fornece por si só uma operação para anexar um desktop Win32 a uma porta HDMI.
- Um serviço executa em sessão 0; UI deve viver em um processo do usuário e usar IPC com controle de acesso. LocalSystem exige justificativa, não deve ser padrão de todos os componentes.
- Endpoints de áudio podem ser enumerados, mas forçar roteamento de todos os processos precisa de pesquisa e testes por aplicação/API.

## Alternativas a investigar em M1
1. Sessões Windows: verificar caminhos suportados, simultaneidade local, displays e direitos de uso da edição exata.
2. Sessão compartilhada e compositor próprio: demonstrar foco, cursores, input e compatibilidade; janelas próprias não bastam para jogos arbitrários.
3. Virtualização: avaliar GPU/dispositivos, latência, licenças e hardware; pode exigir requisitos diferentes do produto pretendido.
4. Driver/composição especializada: somente após demonstrar a necessidade e o caminho viável; medir risco de manutenção por build.

Registrar cada opção como demonstrada, refutada ou desconhecida; incluir método, hardware/build, fontes, limitações e custo. Não assumir a arquitetura interna proprietária do ASTER.

## Gate M1
Escolher um caminho somente com evidências de dois ambientes visíveis simultaneamente, foco independente, possibilidade de separar input e mecanismo de display. Se não houver evidência, continuar pesquisa ou renegociar escopo. Não construir UI/driver esperando que isso resolva uma lacuna arquitetural.

## Requisitos para versão de produto
Contas e dados separados; entrada, display e áudio atribuíveis; login/logout por estação; UAC/lock; hotplug; recuperação de falhas; instalação e desinstalação reversíveis; matriz de Windows/GPU/aplicações; diagnóstico com privacidade; atualizações assinadas; direitos de distribuição/uso revisados para a configuração escolhida.
