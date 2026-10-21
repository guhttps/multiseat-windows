# ADR 0009 — núcleo nativo e investigação da segunda sessão

Data: 2026-10-19. Atualizado em 2026-10-21. Estado: núcleo de identidade/reservas iniciado e criação de sessão/subsistema H1 demonstrada em VM cliente; integração de terminal, vídeo e entrada ainda não selecionada. O [ADR 0007](0007-native-engine-gate.md) continua definindo a aprovação do motor.

## Primeira implementação

O código em [engine/native](../../engine/native/README.md) fornece operações reais de controle, sem instalar componentes no Windows:

| Componente | Comportamento implementado | Limite da evidência |
|---|---|---|
| Identidade | Consulta e cópia privada de token primário; compara SID, logon SID, AuthenticationId e sessão | Uma captura não autentica um pedido nem comprova terminal ou sessão ainda conectada |
| Reservas | Dois postos com usuários/sessões distintos, exclusividade de recursos e gerações monotônicas | As chaves são fatos internos do futuro adaptador; reservar não atribui hardware ao Windows |
| Ciclo de vida | Suspensão por lease esperado, expiração, invalidação de chegada e reassociação explícita | A política não executa login, apresentação nem encaminha entrada |
| Testes | Tokens reais do próprio processo e interleavings de política com identidades fictícias | Os dois usuários fictícios dos testes não são dois postos Windows nativos |

A identidade usa [GetTokenInformation](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-gettokeninformation) e uma cópia obtida por [DuplicateTokenEx](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-duplicatetokenex). `AuthenticationId` identifica o logon e pode ser compartilhado por várias cópias de token; `TokenId` identifica a instância do objeto. Essa distinção evita tratar uma duplicação como outro usuário. Fonte: [TOKEN_STATISTICS](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-token_statistics).

Uma reserva nunca assume estado ativo. Não existe backend que retorne sucesso sem criar o terminal, nem uma opção `nativeCapable` recebida de cliente. O futuro adaptador precisará produzir e reter um vínculo real de terminal, identidade, apresentação, endpoints de entrada e suas gerações. Esse contrato deverá ser verificado antes de introduzir uma API de ativação.

Retiradas são registradas mesmo antes da primeira reserva. Eventos antigos não invalidam uma chegada mais nova. Falha de alocação nesse registro suspende todas as reservas, mantém as reivindicações exclusivas e fecha a admissão até destruir o controlador; cancelamento continua disponível. Os testes usam um alocador que falha de forma determinística para conferir esse comportamento. Essas verificações de política não exercitam hardware ou um terminal Windows.

## Fronteira de segurança para a próxima etapa

O futuro broker deverá obter a identidade do cliente pelo token efetivo da conexão e aplicar ACL por logon, protocolo limitado e recusa de clientes remotos. Nome, SID ou sessão declarados em mensagem não autorizam uma operação. A biblioteca atual é interna ao processo e não implementa IPC. Fonte: [segurança de named pipes](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights).

Perda de desktop autorizado ou de conectividade deverá invalidar a permissão de envio, descartar filas e aposentar o canal antigo. Liberar estados mantidos não significa injetar key-ups no desktop que acabou de ficar ativo. Login e UAC continuam sob Winlogon/desktop seguro; a API de entrada de um processo não fornece autorização para atravessá-los. Fontes: [desktops](https://learn.microsoft.com/en-us/windows/win32/winstation/desktops), [SendInput/UIPI](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput).

Um filtro HID pode observar ou modificar pacotes, mas o callback de classe não recebe uma sessão de destino. Carregar um filtro não demonstra a ligação ao segundo terminal. Os samples Kbfiltr/Moufiltr também têm escopo de instalação próprio e não serão instalados diretamente nos periféricos pessoais. Fontes: [callback de classe](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/kbdmou/nc-kbdmou-pservice_callback_routine), [Kbfiltr](https://learn.microsoft.com/en-us/samples/microsoft/windows-driver-samples/keyboard-input-wdf-filter-driver-kbfiltr/), [Moufiltr](https://learn.microsoft.com/en-us/samples/microsoft/windows-driver-samples/mouse-input-wdf-filter-driver-moufiltr/).

## Investigação de sessão: hipótese H1

Uma candidata de pesquisa é o protocolo privado do Session Manager, com `RtlConnectToSm`, `RtlSendMsgToSm` e a solicitação `SmStartCsr`. O contrato reverso do [PHNT fixado em 53fbbdc](https://github.com/winsiderss/phnt/blob/53fbbdc5b5d2b08761db1c7b26bfa8c820924356/ntsmss.h) descreve ID, comando inicial e IDs de processos retornados. A [implementação independente do ReactOS, fixada em f53b1d1](https://github.com/reactos/reactos/blob/f53b1d1560120c529763b18963fdee68f39118af/sdk/lib/smlib/smclient.c), fornece uma hipótese sobre alocação de sessão e inicialização de CSRSS. Isso não confirma ABI, suporte ou funcionamento no Windows 11.

A inspeção inicial encontrou os nomes das duas exportações no ntdll. Depois da revisão da imagem, autorização e recuperação, o [laboratório 010](../lab/010-native-session-creation.md) realizou uma única solicitação no convidado. Criou sessão, CSRSS e comando inicial reais, preservou o console e terminou com restauração externa verificada. A enumeração WTS não apresentou essa sessão de processos como terminal utilizável. O chamador permanece privado e restrito ao laboratório; não existe implementação dessa chamada na biblioteca de produto.

Antes de escrever/executar um chamador, o laboratório deve satisfazer todos estes requisitos:

1. VM Windows cliente descartável, separada dos dados pessoais e da VM RDS; build e ABI fixados e conferidos com símbolos legítimos. Permissões e limites da operação documentados.
2. Checkpoint limpo, retorno externo conferido e controlador limitado à VM escolhida. Um timeout ou falha deve restaurar essa VM, sem depender de código do convidado continuar funcionando.
3. Baseline de sessões, console e processos reais. Uma única chamada no convidado, com prazo e privilégio mínimos necessários já revisados.
4. Resultado só positivo para esse subteste se houver nova sessão, CSRSS e comando inicial reais nela, preservando o console. Um ID adicional isolado não basta.
5. Restauração obrigatória do checkpoint; nunca encerrar CSRSS/Winlogon manualmente. `SmStopCsr` não é retorno comprovado: a [referência ReactOS o mantém como stub](https://github.com/reactos/reactos/blob/f53b1d1560120c529763b18963fdee68f39118af/base/system/smss/smloop.c).

O resultado positivo demonstrou somente alocação/subsistema naquela imagem. Ainda são necessárias autenticação da segunda conta e ligação do terminal com apresentação e entrada. H1 permanece uma API interna sem suporte de produto confirmado, sem motor completo escolhido e sem instrução para executar no computador pessoal. Novas rodadas continuam exigindo os mesmos limites, observação e restauração.

## Vídeo e critérios de avanço

O modelo documentado associa as saídas locais WDDM ao terminal de console. A revisão não encontrou uma operação que transforme posse de uma fonte VidPN em criação/ligação de outro terminal. `DisplayManager` fornece scanout de targets especializados, sem criar o desktop de outra conta. Portanto, um padrão colorido, IDD carregado ou novo monitor virtual não aprovará a etapa. Fontes: [console e terminais](https://learn.microsoft.com/en-us/windows/win32/termserv/consoles-vs-terminals), [posse VidPN](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/nf-d3dkmthk-d3dkmtsetvidpnsourceowner), [monitores especializados](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/specialized-monitors-compositor).

A passagem ainda a resolver é segunda conta → sessão interativa → terminal → saída física e entrada próprias, mantendo o console utilizável. Só depois de demonstrar esse fluxo no mesmo protótipo serão aprovados instalação de produto, áudio e testes de LoL. Nenhuma proteção de boot, driver ou configuração de rede foi alterada nesta etapa. Resultados brutos e metadados locais permanecem privados.
