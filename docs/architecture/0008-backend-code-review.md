# ADR 0008 — revisão de motores e laboratório RDS separado

Data: 2026-10-17. Investigação de T006; motor nativo ainda não demonstrado. O alvo do produto e os critérios do [ADR 0007](0007-native-engine-gate.md) continuam vigentes.

## Resultado da revisão

A operação ainda ausente é manter uma segunda sessão interativa de outra conta, com apresentação no monitor físico atribuído e entrada própria, enquanto o primeiro posto continua utilizável. Criar um token, iniciar um processo ou salvar uma associação de dispositivo não fornece essa operação.

As fontes abaixo foram lidas; nenhum instalador, driver ou patch desses projetos foi executado. Os commits fixam o material revisado, sem afirmar que versões futuras terão o mesmo comportamento.

| Candidato e fonte revisada | Mecanismo encontrado | Decisão para o alvo nativo |
|---|---|---|
| [OpenMultiSeat: sessões](https://github.com/RKY2023/OpenMultiSeat/blob/29e98f2483044ceee640e07e5ca4dba8280c23bd/src/OpenMultiSeat.Sessions/SessionManager.cs), [entrada](https://github.com/RKY2023/OpenMultiSeat/blob/29e98f2483044ceee640e07e5ca4dba8280c23bd/src/OpenMultiSeat.InputIsolation/InputIsolationService.cs) e [display](https://github.com/RKY2023/OpenMultiSeat/blob/29e98f2483044ceee640e07e5ca4dba8280c23bd/src/OpenMultiSeat.Core/DisplayManager.cs) | Usa sessões existentes para obter token e lançar processo. A habilitação de isolamento e a atribuição de tela atualizam estado/configuração. | Não fornece criação nem associação de um segundo terminal físico. |
| [FreeRDP shadow para Windows](https://github.com/FreeRDP/FreeRDP/blob/f74bbdf9558447f1c36c9fc94ae0a89f064f3025/server/shadow/Win/win_shadow.c) | Captura desktop existente e injeta entrada. | Pode fornecer transporte; não cria o terminal Windows que falta. |
| [MultiSeat](https://github.com/vibesoftwarecoder/MultiSeat/blob/c966e5e0a8285e14242fe413712cee9eafd6c6e3/README.md) | Compõe sessões RDP, displays virtuais e streaming; depende de RDPWrap/TermWrap no Windows cliente. | A composição não é um motor nativo independente demonstrado. |
| [Hydra](https://github.com/kingsenglishvietnam/hydra/blob/e188b9d0bc6d063b7ea596d29acca2aae96b9931/hydra7-avc.ps1), [roteador](https://github.com/kingsenglishvietnam/hydra/blob/e188b9d0bc6d063b7ea596d29acca2aae96b9931/input/seat_router.c) e [agente](https://github.com/kingsenglishvietnam/hydra/blob/e188b9d0bc6d063b7ea596d29acca2aae96b9931/input/seatB_agent.c) | Requer alterações no TermService para concorrência. Apresenta RDP no segundo monitor do console; Interception captura/suprime dispositivos e um agente injeta a entrada na sessão B. | Referência experimental concreta, sem transferência demonstrada da saída WDDM física para B. Não foi instalado. |
| [Duo](https://github.com/DuoStream/Duo) e [TermWrap](https://github.com/DuoStream/TermWrap) | Documentam streaming e alterações por offsets no serviço de sessões. O repositório principal não fornece o motor completo como SDK. | Não foi identificado um SDK aplicável que complete o contrato de produto. |
| [OSBASE DirectDisplay](https://osbase.com/hello-world/) | Driver e presenter para MultiPoint Server e dispositivos zero client específicos. | O suporte anunciado não estabelece integração para o alvo Windows cliente. |

O [experimento de provider do Hydra](https://github.com/kingsenglishvietnam/hydra/blob/e188b9d0bc6d063b7ea596d29acca2aae96b9931/MODE4-PROVIDER.md) relata inicialização de remote IDD seguida de desconexão. A explicação causal é hipótese do autor; não foi reproduzida aqui. Uma etapa de driver bem-sucedida não estabelece o funcionamento do terminal inteiro.

## Contratos do Windows: distinções necessárias

A Microsoft descreve um console ativo e associação dos monitores locais WDDM a ele. Remote IDD exige um dispositivo criado pela pilha de desktop remoto; a flag de driver remoto não cria essa pilha. Fontes: [console e terminal](https://learn.microsoft.com/en-us/windows/win32/termserv/consoles-vs-terminals) e [remote IDD](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/iddcx1.4-updates-for-remote-idds).

`D3DKMTSetVidPnSourceOwner` controla posse de fontes de apresentação por dispositivo; seus parâmetros não documentam a criação de uma sessão ou terminal interativo de outra conta. Não foi selecionado como solução para essa lacuna. Fonte: [estrutura de parâmetros](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/ns-d3dkmthk-_d3dkmt_setvidpnsourceowner).

O contrato base de `IWRdsProtocolManager` e o método `GetInputHandles` documentam suporte em Server. Existe uma nuance: `IWRdsProtocolConnection2` declara Windows cliente build 26100 e acrescenta credenciais serializadas para logon. Portanto, não é correto afirmar que toda interface da família é indisponível no cliente. Essa extensão não documenta, por si só, dois terminais locais simultâneos nem a atribuição de uma saída física a outra sessão. Fontes: [manager](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nn-wtsprotocol-iwrdsprotocolmanager), [input](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nf-wtsprotocol-iwrdsprotocolconnection-getinputhandles) e [Connection2](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nn-wtsprotocol-iwrdsprotocolconnection2).

## Decisão de laboratório

Foi escolhido um laboratório auxiliar com Windows Server em uma VM nova e separada. Seu objetivo é desenvolver e testar o transporte de duas sessões reais, autenticadas com contas de teste distintas, até duas apresentações locais. O Windows cliente do host continua sendo o alvo do produto.

O teste usa RDS no convidado e um frontend local por posto. As saídas físicas permanecem no console do host, que apresenta as imagens remotas. Essa rota será registrada explicitamente; ela não demonstra o motor nativo, compatibilidade com LoL, aceleração de jogos ou os critérios completos de recuperação e segurança do produto.

O plano e a evidência executada ficam no [laboratório 008](../lab/008-rds-transport-comparison.md). Duas sessões autenticadas, duas janelas visíveis e uma entrada sintética bem-sucedida são resultados separados da interação física simultânea. Nenhum desses resultados será presumido a partir de uma compilação.

## Reutilização e distribuição

Não foi copiado código dos candidatos da tabela. Licenças de um projeto não cobrem automaticamente suas dependências nem os recursos da plataforma. O [Interception](https://github.com/oblitum/Interception#license), por exemplo, apresenta condições comerciais separadas. Antes de distribuir componentes de terceiros, revisar versão, licença, avisos, procedência e direitos de redistribuição de cada componente.

Este ADR não seleciona um driver ou patch para instalação no computador pessoal. O gate nativo permanece aberto.
