# Laboratório 025: imagem e login da segunda conta na sessão 2, e o limite de sessão ativa única

Atualizado em 2026-11-21. **Marco: em VM com Windows 11 Pro, uma segunda conta Windows fez login interativo na tela de login da sessão 2, criada pelo nosso provedor, com usuário e senha digitados por um agente local. O Windows autenticou a conta. Em seguida, a arbitragem padrão de sessões do próprio Windows recusou manter as duas sessões ativas.** Com uma conta comum, o Windows negou o acesso à sessão. Com uma conta administradora, ofereceu desconectar o usuário do console. Isso confirma que o Windows 11 Pro mantém **uma única sessão interativa ativa por vez**. Nenhuma rodada aprovou um terminal utilizável. Todas foram restauradas, com auditoria independente.

## Resumo das rodadas

| Rodada | Mudança | Resultado |
| --- | --- | --- |
| V52 | Agente local executado como SYSTEM na própria sessão 2, na área de trabalho da tela de login: captura a tela, envia uma tecla e captura de novo | O agente roda na sessão certa e a entrada é aceita, mas as capturas saem totalmente pretas |
| V53 | Agente v2 detalha as saídas de vídeo, lê a tela de login por acessibilidade (UI Automation) e digita o usuário e a senha de uma conta fictícia criada só para a rodada | A tela de login aparece como "Outro usuário"; a conta é **autenticada** (logon remoto interativo) e `IsUserAllowedToLogon` é aceito; depois da arbitragem, a tela mostra "Acesso negado à sessão requisitada" |
| V54 | Igual à V53, com a conta fictícia também no grupo Administradores da VM | A tela de login mostra "Outro usuário está conectado. Se você continuar, ele será desconectado." O agente **não** responde, e o console é preservado |

As três rodadas seguem o mesmo padrão:
- plano novo fixado por hash;
- guarda externo de 180 segundos;
- restauração obrigatória;
- auditoria independente;
- console e processos centrais das sessões existentes verificados.

Na V54, o primeiro pedido de elevação não foi aceito. Ele foi registrado como não iniciado e repetido uma única vez, a pedido do usuário, com o mesmo plano.

## O que foi demonstrado

- **Imagem da sessão 2.** A sessão 2 ainda não tem saída de vídeo ativa: o driver de display remoto da caixa carrega sem monitor, a duplicação DXGI não encontra saídas e a captura GDI sai preta. Mesmo assim, a janela da tela de login renderiza o próprio conteúdo quando capturada diretamente, e a primeira imagem da tela de login da sessão 2 foi obtida assim.
- **Entrada na sessão 2.** A árvore de acessibilidade da tela de login foi lida de dentro da sessão: campos de usuário e senha, botão de envio, acessibilidade e energia. As teclas injetadas chegaram aos campos certos.
- **Autenticação da segunda conta.** O Windows registrou um logon remoto interativo bem-sucedido da conta fictícia enquanto o usuário do console continuava conectado.
- **Origem da recusa.** Depois da autenticação, o serviço chamou `SessionArbitrationEnumeration`. O provedor respondeu `E_NOTIMPL`, que segundo a documentação da Microsoft é o caminho para usar a arbitragem padrão. O gerenciador de sessões registrou o início da arbitragem, e a recusa veio dela, não do provedor.

## O que não foi demonstrado

- Duas contas Windows ativas ao mesmo tempo. No Windows 11 Pro, a arbitragem padrão aceita uma única sessão interativa ativa: uma conta comum é negada e uma administradora recebe a oferta de derrubar o console.
- Imagem contínua da área de trabalho da sessão 2, apresentação no segundo monitor físico, áudio e terminal utilizável.
- O nosso driver IDD continua falhando antes de `DeviceAdd` (`STATUS_WDF_BUSY`). O INF dele já tem as mesmas diretivas essenciais do driver remoto da caixa.

## Consequência para o produto

Contornar a arbitragem exigiria alterar o serviço de Área de Trabalho Remota, o licenciamento ou os limites de sessão do Windows. O projeto não faz isso: é uma regra do projeto e também uma restrição dos termos de licença do Windows. O caminho de duas sessões nativas no Windows 11 Pro fica, portanto, bloqueado nesta forma.

A escolha da arquitetura seguinte está pendente com o responsável pelo projeto. As alternativas em avaliação respeitam essa regra:
- uma máquina virtual por posto adicional;
- uma edição do Windows licenciada para várias sessões.

Os componentes já provados continuam úteis em qualquer caminho: o agente na sessão, a leitura e a entrada na tela de login, a recuperação e as auditorias.

Fontes, binários experimentais, capturas e resultados brutos continuam privados. A CI pública não compila a candidata privada. Consulte o [trabalho restante](../REMAINING-WORK.md).
