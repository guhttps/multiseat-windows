# Laboratório 008 — duas sessões reais por RDS em VM separada

Estado em 2026-10-18: duas sessões autenticadas, transporte sintético até dois Notepads externos e ciclo sintético de desconexão/reconexão validados. O usuário confirmou digitação com os periféricos físicos, manutenção do alvo ao trocar aplicativos no outro posto e recuperação por F9/F7 nos dois sentidos. O primeiro ensaio de retirada de teclado teve relato e contagens compatíveis com continuidade no outro posto; o ciclo físico completo de recuperação/hotplug e a medição quantitativa continuam pendentes. Este laboratório é auxiliar e não aprova o motor nativo nem duas instâncias de LoL. A decisão está no [ADR 0008](../architecture/0008-backend-code-review.md).

## Escopo e ambiente

Uma VM nova executa Windows Server 2025 Evaluation com Desktop Experience. Ela contém somente dados e contas sintéticas do laboratório. A VM de comparação anterior e as contas do host não são reutilizadas.

A mídia vem do [Microsoft Evaluation Center](https://www.microsoft.com/en-us/evalcenter/download-windows-server-2025). A avaliação tem prazo e condições próprios; o período de carência do RDS não dispensa o cumprimento dos termos aplicáveis. Não usar este laboratório como configuração de produção nem alterar períodos de avaliação. Referências: [avaliação oficial](https://www.microsoft.com/en-us/evalcenter/evaluate-windows-server-2025) e [licenciamento RDS](https://learn.microsoft.com/en-us/windows-server/remote/remote-desktop-services/rds-client-access-license).

A preparação usa disco virtual novo, Secure Boot, switch Hyper-V Internal dedicado e sub-rede RFC1918 sem colisão com as rotas existentes. O switch não recebe NAT, bridge ou gateway; a regra RDP do convidado permite somente o endereço do host nessa ligação. Nenhuma regra RDP de entrada é aberta no host.

Administração do convidado por [PowerShell Direct](https://learn.microsoft.com/en-us/windows-server/virtualization/hyper-v/powershell-direct), sem habilitar WinRM ou alterar TrustedHosts no host. As senhas são aleatórias, exclusivas do laboratório, guardadas com DPAPI para o mesmo usuário local e nunca passadas pela linha de comando. Unattend não é criptografia: seus arquivos e caches precisam de acesso restrito e remoção após o primeiro boot.

## Sequência de execução

1. Verificar procedência, tamanho e hash da mídia; conferir a assinatura Microsoft do instalador e a edição antes de aplicar a imagem ao disco virtual novo.
2. Inicializar a VM, confirmar marcador exclusivo, edição Server e identidade do convidado; remover os arquivos de bootstrap com credenciais e criar checkpoint anterior ao RDS.
3. Executar [Initialize-RdsLabGuest.ps1](../../scripts/Initialize-RdsLabGuest.ps1) somente dentro dessa VM. O script exige marcador e ambiente válidos, instala a role RDS e prepara duas contas de teste sem privilégios administrativos. Se houver reinicialização pendente, retomá-lo após o boot.
4. Obter por PowerShell Direct o certificado efetivamente associado ao listener RDP. O cliente usa SHA256 desse certificado, NLA e duas conexões distintas. Não aceitar qualquer certificado nem instalar uma raiz de confiança no host.
5. Abrir aplicativos Windows externos nas duas contas. Observar o processo que realmente possui cada janela e comparar identidades/sessões, sem presumir que o PID inicial é o dono final da janela.
6. Encaminhar teclado e mouse de cada par para sua conexão. A captura inicial só atua com o próprio frontend em primeiro plano; perda de foco, remoção de dispositivo ou encerramento devem liberar as teclas e botões mantidos.
7. Fazer o desafio físico: interagir simultaneamente, trocar aplicativos em um posto enquanto o outro mantém digitação ou movimento, e inverter os papéis. Verificar as duas telas físicas atualizando e ausência de entrada cruzada. Entrada sintética automatizada não substitui esse passo.
8. Registrar falhas e retorno ao estado anterior; encerrar apenas conexões, contas e processos pertencentes a esse laboratório.

## Evidência necessária

| Prova | Resultado observado |
|---|---|
| VM separada, marcador e checkpoint verificados | Passou; assinatura Microsoft da mídia e identidade do convidado conferidas |
| Duas contas com identidades distintas | Passou; contas locais de teste não administrativas, tokens distintos |
| Duas sessões RDP simultaneamente conectadas | Passou; duas sessões distintas ativas na coleta WTS |
| Aplicativos externos com processo e conta corretos | Passou; dois Notepads com PID, token SID e SessionID revalidados |
| Duas imagens remotas apresentadas nos monitores locais | Dois framebuffers recebidos e janelas criadas para monitores distintos; conferência visual física pendente |
| Encaminhamento sintético por conexão | Passou; cada Notepad observou sua fixture exata, sem a fixture cruzada nas amostras |
| Encerramento do cliente após a prova sintética | Passou na repetição; código de saída zero e stderr vazio |
| Ponteiros dos dois mouses no frontend | Miras separadas confirmadas pelo usuário; captura e liberação do cursor compartilhado observadas em runtime |
| Digitação com os teclados físicos | Confirmada visualmente pelo usuário; contadores registraram encaminhamento nos dois postos sem teclas sem rota. Conferência automatizada dos textos exatos ainda pendente |
| Troca de aplicativo enquanto o outro posto mantém digitação | Confirmada pelo usuário nos dois sentidos; conferência qualitativa, sem a contagem quantitativa de P02 |
| Desconexão e reconexão do transporte de um posto | Passou em ciclo sintético: o outro recebeu texto durante a desconexão; os mesmos aplicativos e sessões foram preservados após reconectar; encerramento com código zero |
| Recuperação de transporte com periféricos físicos | Confirmada pelo usuário nos dois sentidos; registros disponíveis mostram desconexão do posto 1 e continuidade de entrada no posto 2. Conferência automatizada da sequência completa pendente |
| Remoção e reconexão de periféricos | Fixtures offline passaram nos dois sentidos; primeira retirada física de teclado com continuidade no posto oposto confirmada pelo usuário e contagens compatíveis. Preservação completa das associações e reconexão física ainda em conferência |
| Desativação e reativação de mouses por PnP | Passou nos dois postos, com avisos reais do Windows e fixtures sintéticas no roteador de produção; par intacto preservado e reassociação explícita exigida. Todos os dispositivos selecionados reabilitados; teste com cabos e aplicativos externos ainda pendente |
| Observação automática de cinco minutos | Passou em 22 amostras durante 303 segundos: duas sessões ativas, mesmos aplicativos com propriedade verificada e duas janelas locais responsivas; sem interação contínua ou medição de latência |
| Bloqueio e UAC | Pendentes; o ciclo sintético não aprova esses cenários |

Relatórios brutos, endereços, SIDs, certificados, identificadores e credenciais ficam somente em `docs/lab/results/` e `build/`, ignorados pelo Git. A evidência pública deve limitar-se a procedimento, resultado, limitações e booleanos revisados, sem anexar os arquivos brutos ou capturas do desktop pessoal.

O preparo do convidado passou após corrigir duas verificações: os métodos CIM podem devolver `ReturnValue` nulo, portanto o script exige também a leitura da configuração aplicada; os resolvers IPv6 padrão inativos não devem ser confundidos com DNS IPv4 configurado. A NIC exclusiva da VM usa somente IPv4, sem alterar IPv6 no host. As fixtures inertes cobrem falha das pós-condições e fechamento da entrada de rede.

O cliente precisou de inicialização explícita do Winsock, agora coberta por resolução numérica offline. Uma tentativa inicial apresentou erro genérico de TLS; a causa não foi isolada. As conexões posteriores passaram com o mesmo certificado fixado, NLA obrigatória e TLS mínimo 1.2, sem relaxar esses controles. O diagnóstico registra apenas etapas e códigos numéricos.

Na prova executada, o modo explícito `--transport-check --unattended-fixtures` manteve Raw Input desativado. Após os dois framebuffers, o controlador conferiu relatórios recentes, proprietários dos processos, Notepad em primeiro plano dentro de cada sessão e documentos vazios. O cliente enviou somente `rdp-seat-a`/`rdp-seat-b`, sem Enter. A coleta posterior exigiu `exact_fixture_now`, sessões ativas distintas, tokens esperados, janela/controle válidos, aplicativos ainda abertos e ausência de erro; não aceitou apenas um sinal de envio ou resultado antigo. A repetição também confirmou encerramento do cliente com código zero e stderr vazio. A captura física continua restrita ao próprio frontend em primeiro plano.

A verificação de documento vazio usa `document_empty_now` com consulta bem-sucedida. Apenas `exact_fixture_now=false` não basta: texto parcial de uma tentativa anterior também produz esse resultado. Tentativas que enviaram entrada exigem preparação limpa antes de repetir.

Compilação Release e 14 testes CTest passaram localmente, além das fixtures inertes do preparo do convidado. Esses testes e a prova sintética têm escopos diferentes. Os relatórios brutos continuam privados.

O frontend usa buffer de pintura por janela e mostra o progresso de associação de teclado e mouse de cada posto. Um único par não completa a associação dos dois postos; o teste físico requer os dois pares conectados. O smoke offline também exercita a pintura das duas janelas ocultas, sem capturar entrada nem conectar.

Após o relato de que os dois mouses moviam o cursor principal do host, o modo físico passou a prender e ocultar esse cursor durante a captura do frontend. O usuário confirmou movimento separado das miras; uma coleta somente de leitura também observou a captura e sua liberação com o cliente ainda aberto e stderr vazio. As fixtures inertes verificaram falhas de aquisição/liberação, destruição e preservação de uma restrição mais recente de outro aplicativo. São miras do frontend; dois cursores independentes do sistema Windows continuam fora do resultado demonstrado.

Na interação física posterior, o usuário confirmou que a digitação funcionou e esclareceu que as letras repetidas foram deliberadas. Os contadores privados registraram teclas encaminhadas para os dois contextos e nenhuma tecla sem rota. Em seguida, o usuário confirmou manutenção da digitação no posto correto ao alternar Notepad e Explorador no outro posto, nos dois sentidos. O resultado é qualitativo: a contagem de eventos, estabilidade e recuperação física continuam pendentes. A prova automatizada de texto exato permanece separada. O observador passou a fornecer também uma contagem de tamanho limitada, para acompanhar atividade manual sem registrar conteúdo digitado; essa atualização foi instalada somente no guest e as contagens esperadas foram conferidas nos relatórios privados.

Para associar os periféricos, ative primeiro um painel pela barra de título; depois pressione F6 nos dois teclados e dê clique direito com cada mouse dentro do painel. A associação do segundo mouse completou após essa sequência. A captura pausa ao voltar para outro aplicativo do host. F8 libera o cursor e reinicia as associações.

## Ciclo de recuperação do transporte

O cliente mantém um prazo e estado de entrada por conexão. Uma falha deixa o painel correspondente indisponível, enquanto o outro transporte continua ativo; F7 tenta reconectar somente os indisponíveis. Com os quatro periféricos associados, F9 desconecta o posto daquele teclado. As verificações offline cobrem liberação apenas das teclas e botões do posto suspenso, preservação das entradas mantidas pelo outro e ausência de reprodução de entrada antiga após reativar.

A prova executada com `--transport-check --unattended-fixtures --recovery-cycle` manteve Raw Input desativado. O coletor exigiu dois editores vazios e com propriedade validada antes do envio. Com o posto 1 desconectado, confirmou a sessão 2 ativa e sua fixture correta, enquanto o documento do posto 1 permaneceu vazio. Depois da reconexão, revalidou o editor do posto 1 antes das teclas e confirmou os dois textos exatos, sem fixture cruzada, nos mesmos processos, contas e IDs de sessão do preparo. O cliente encerrou com código zero e stderr vazio.

As tentativas iniciais reconectaram o transporte mas não entregaram a fixture imediata ao editor do posto 1. A prontidão de protocolo e o tratamento da sincronização de foco, sozinhos, não resolveram essa observação. O ciclo passou após separar o clique de ativação e as teclas, com uma nova conferência do editor entre essas etapas. Esse resultado é do teste sintético de transporte. O usuário confirmou F9/F7 com os periféricos nos dois sentidos, mas os registros disponíveis comprovam somente a desconexão do posto 1 com entrada mantida no posto 2. A conferência física automatizada completa, perda de rede, encerramento independente de janelas, hotplug e desktops de segurança continuam exigindo provas próprias.

O roteador foi alterado para que a remoção de um periférico libere somente as entradas do posto afetado. Ele preserva as associações e entradas mantidas pelo outro posto; o par incompleto aguarda reassociação explícita com F6 ou clique direito. Um reset por F8 ainda exige associar os quatro dispositivos novamente. As fixtures offline passaram com retirada de teclado e mouse nos dois sentidos, notificações duplicadas, IDs desconhecidos, continuidade do posto intacto e ausência de replay. Esse resultado não substitui o teste físico de hotplug.

Na primeira retirada física do teclado do posto 1, o usuário informou que conseguiu acrescentar uma letra no posto 2, mantendo o posto 1 inalterado. O coletor verificou os mesmos processos, proprietários e sessões ativos, com aumento apenas da contagem limitada do documento do posto 2. Uma captura posterior dos cabeçalhos encontrou todas as associações liberadas; a relação desse estado com a etapa de retorno ao host ainda está em conferência. Portanto, esse ensaio comprova apenas a continuidade observada da digitação, sem aprovar a preservação completa das associações ou o ciclo de reconexão. Os relatórios e as imagens continuam privados.

A revisão identificou que retirar o registro de Raw Input ao perder foco também suspendia os avisos de desconexão. O cliente passou a manter `RIDEV_DEVNOTIFY` durante a pausa, sem `RIDEV_INPUTSINK`, `RIDEV_EXINPUTSINK` ou roteamento em segundo plano. Somente a remoção aposenta uma associação. Um ensaio local em janela oculta verificou que atualizar as flags de registro repete chegadas dos dispositivos já presentes; tratar essas chegadas como remoções apagaria os pares ao trocar de foco. As fixtures agora cobrem essa preservação, além das transições de registro, falhas e limpeza. O ensaio não retirou hardware nem recebeu entrada em segundo plano; a prova física da mudança continua pendente.

## Observação automática de estabilidade

Uma execução com a revisão `95f3f44` observou o cliente normal durante 303 segundos, em 22 amostras. Cada coleta exigiu relatórios recentes, os mesmos processos e sessões de contas distintas, propriedade e controles dos dois Notepads válidos, ambas as sessões ativas e resposta das duas janelas locais a uma consulta limitada. O controlador não digitou texto nem registrou seu conteúdo. O cliente foi encerrado depois da observação, com código zero.

As amostras registraram entre 14 e 15 objetos GDI; a contagem de handles passou de 320 na primeira para 317 na última amostra. Esses números são observações dessa execução e não comprovam ausência de vazamentos em uso prolongado. O resultado também não mede atualização contínua da imagem, FPS ou latência e não aprova P01 no motor nativo.

Na mesma revisão, o ciclo `--transport-check --unattended-fixtures --recovery-cycle` passou novamente após preparar dois editores vazios. O coletor confirmou a fixture do posto 2 enquanto o posto 1 estava desconectado, revalidou o editor reconectado antes das teclas e confirmou as duas fixtures exatas nos mesmos aplicativos. O cliente terminou com código zero e stderr vazio.

Um ensaio separado e local verificou a identidade de quatro dispositivos de entrada individuais, em contêineres USB distintos, e preservou suas associações de teste durante mudanças reais das flags de registro. Esse preparo usou entrada sintética somente no roteador, sem encaminhá-la ao sistema ou aos aplicativos. Nenhum dispositivo foi desativado nessa etapa; ela não substitui o teste de remoção e reconexão.

## Desativação e reativação de mouses por PnP

Um ensaio posterior usou um receptor oculto com `InputRegistration` e `InputRouter` da revisão `95f3f44`. Os quatro dispositivos da fixture foram identificados por interface e instância PnP, com classe, serviço, pai, estado inicial e ausência de filhos conferidos. Somente os dois dispositivos individuais de mouse foram elegíveis para desativação, um por vez. Os teclados do ambiente não apresentavam `DN_DISABLEABLE`; uma tentativa inicial de desativação não gerou a remoção esperada e terminou com todos habilitados. A triagem passou a exigir essa flag antes de autorizar qualquer mudança.

O roteiro usou [`CM_Disable_DevNode`](https://learn.microsoft.com/en-us/windows/win32/api/cfgmgr32/nf-cfgmgr32-cm_disable_devnode) sem persistência após reinicialização, conferiu o código de problema 22, aguardou quatro segundos e reativou o dispositivo. Um processo separado de recuperação ficou pronto antes de cada desativação. O intervalo registrado entre a confirmação da desativação e a confirmação da reativação foi de aproximadamente 5,4 e 5,5 segundos, incluindo as consultas de verificação.

Nos dois sentidos, os avisos reais de remoção liberaram somente as teclas e botões sintéticos mantidos pelo posto afetado. O outro par manteve associações, entradas pressionadas e encaminhamento para seu sink de teste. A chegada do mouse reativado não o associou automaticamente: a fixture exigiu clique direito explícito no roteador antes de retomar. Nenhuma mensagem de entrada em segundo plano foi recebida. Os dois testes encerraram com código zero; os quatro dispositivos foram conferidos habilitados e os dois processos de recuperação terminaram.

Esse ensaio combina PnP real com entrada sintética interna ao roteador. A retirada física de cabos, a identificação dos pares pelo usuário e a continuidade da digitação em aplicativos externos durante o hotplug exigem uma prova integrada própria. Os planos, interfaces, instâncias e relatórios completos permanecem somente nos resultados locais ignorados pelo Git.

## Critério de conclusão

O laboratório só demonstra as etapas efetivamente observadas do transporte RDS. Para aprovar o produto no Windows cliente ainda será necessário cumprir o gate do [ADR 0007](../architecture/0007-native-engine-gate.md) no mesmo backend alvo, incluindo GPU, segurança da interação, recuperação, áudio e jogos.
