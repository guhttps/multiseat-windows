# Comparação do candidato em um Windows limpo

Revisão de 2026-10-16: este roteiro é um laboratório auxiliar histórico. Child Sessions não atende duas contas Windows independentes e não é motor de produto selecionado. O [ADR 0007](../architecture/0007-native-engine-gate.md) define a prioridade atual; não é necessário repetir esta comparação para avançar na análise do motor.

Plano escolhido pelo usuário em 2026-10-08: uma VM neste computador, se houver recursos. Estado: usuário mostrou a área de trabalho do Windows instalado. VM Running, checkpoint pré-teste criado e pacote copiado. Edição/build do convidado e comparação de sessão filha ainda pendentes. T006 ativa, T007 bloqueada. Não retomar rastreamentos idênticos no host sem nova evidência.

## Pergunta do experimento

O teste documentado de Child Sessions consegue observar uma sessão filha em um Windows instalado do zero? Sucesso mostrará uma diferença entre ambientes, não a causa da falha atual. Uma VM não reproduz os HDMI físicos, a GPU nem a entrada de dois jogadores. Mesmo com sucesso, o candidato ainda tem limite de uma sessão filha conectada e dependência da sessão pai; não comprova contas Windows distintas. Fontes: [Child Sessions](https://learn.microsoft.com/en-us/windows/win32/termserv/child-sessions), [resultado atual](../architecture/0006-child-session-candidate.md).

Usar aplicativos simples neste laboratório. Vanguard não suporta execução em VM; LoL será validado futuramente em Windows físico. Fonte: [Riot VAN 9100](https://support.riotgames.com/en-us/riot/client/error-van-9100).

## Recursos e configuração proposta

Antes de preparar o laboratório, consulte localmente RAM disponível, espaço livre, virtualização e instalação do Hyper-V. Não publique valores exatos do computador pessoal. Win32_OptionalFeature usa InstallState=2 para desativado. Um campo SLAT retornando False com hypervisor já presente não comprova ausência de suporte; a instalação do recurso e a inicialização da VM validam a configuração. Fontes: [requisitos Hyper-V](https://learn.microsoft.com/en-us/windows-server/virtualization/hyper-v/host-hardware-requirements), [InstallState](https://learn.microsoft.com/en-us/windows/win32/cimwin32prov/win32-optionalfeature).

Proposta: VM `Multiseat-Comparison`, geração 2, 8 GiB de RAM, 4 processadores virtuais, disco virtual dinâmico de 80 GiB, Secure Boot e TPM virtual habilitados. Usar Windows 11 Pro x64 limpo, registrando edição/build/atualizações. A [ISO oficial](https://www.microsoft.com/en-us/software-download/windows11) não fornece automaticamente uma licença adicional. Se outra edição for usada, registrar a diferença; resultado nela não valida Windows Pro.

## 1. Ativar Hyper-V no host

Etapa concluída pelo usuário. O comando abaixo fica como histórico; não é necessário repeti-lo. Serviços vmms e vmcompute observados Running após o reinício.

Abra Windows PowerShell como administrador e execute:

```powershell
Enable-WindowsOptionalFeature -Online -FeatureName Microsoft-Hyper-V-All -All -NoRestart
```

Esse comando altera uma feature do host. Aguarde a conclusão, salve seu trabalho e reinicie manualmente quando for conveniente. Não executar testes da VM antes de concluir o reinício. A preparação pelo agente não executou esse comando. Referência: [instalação Microsoft](https://learn.microsoft.com/en-us/windows-server/virtualization/hyper-v/get-started/Install-Hyper-V).

## 2. Instalar o convidado

A VM `Multiseat-Comparison` já foi criada; não criar outra com esse nome. Execução elevada `vm-setup-20261008-160722-3f71785c`: geração 2, 8 GiB de RAM estática, 4 CPUs virtuais, VHDX dinâmico de 80 GiB, Secure Boot On, TPM habilitado, Default Switch, estado Off, sem ISO. Armazenamento em `build/hyper-v/Multiseat-Comparison`, ignorado pelo Git. Início automático desativado; nenhuma outra VM foi alterada. A inicialização do convidado ainda não foi validada.

`scripts/New-ComparisonVM.ps1` cria somente essa VM e pede UAC quando necessário. Recusa nome ou armazenamento preexistente, não substitui switches nem apaga arquivos; confirma configuração e grava relatório local mesmo em falha. Se houver falha parcial, preserva os recursos criados para inspeção. Sintaxe validada; criação real e leitura das propriedades passaram. Documentação dos comandos: [New-VM](https://learn.microsoft.com/en-us/powershell/module/hyper-v/new-vm), [Set-VMKeyProtector](https://learn.microsoft.com/en-us/powershell/module/hyper-v/set-vmkeyprotector).

Download concluído pelo usuário: `Windows11_Client_x64_pt-br_26300_9457.iso`, 8.731.574.272 bytes, em Downloads. SHA-256 `D9AC87C1E9E47E0EB34EED443E5691C31781074E67608A51AEE50D7C7039F684`, correspondente à tabela Português (Brasil), 64 bits da [página Microsoft](https://www.microsoft.com/pt-br/software-download/windows11), consultada em 2026-10-08. O nome do arquivo não comprova a edição instalada; confirmar no convidado depois.

Preparação elevada `vm-installer-20261008-161825-a750e590`: hash conferido novamente antes da alteração, ISO anexada com readback, DVD como primeiro dispositivo de boot e VMConnect iniciado. VM Off ao terminar; não foi executado setup.exe no host nem iniciado automaticamente o instalador. Relatório local em `docs/lab/results/`, ignorado pelo Git.

`scripts/Open-ComparisonInstaller.ps1` aceita caminho literal da ISO e SHA-256 esperado; pede UAC para operar somente na VM do projeto. Verifica ID no relatório de criação, armazenamento, único disco virtual esperado, estado Off e DVD sem mídia diferente. Recusa hash incorreto antes de alterar a VM. Abre console interativo sem reiniciar/desligar máquinas. Validação real de anexação e readback passou. Referências: [Set-VMDvdDrive](https://learn.microsoft.com/en-us/powershell/module/hyper-v/set-vmdvddrive), [VMConnect](https://learn.microsoft.com/en-us/windows-server/virtualization/hyper-v/virtual-machine-connection).

Na janela `Multiseat-Comparison`, clique Iniciar e pressione uma tecla quando solicitado para iniciar pelo DVD. Escolha Português (Brasil), teclado Português (Brasil ABNT2), Instalar Windows 11 e edição Pro. Se o instalador solicitar chave, forneça a chave apropriada ou use a opção Não tenho uma chave do produto quando oferecida; isso não concede uma licença. Selecione o espaço não alocado do único disco virtual de 80 GiB e avance. As confirmações e termos do instalador devem ser lidos/aceitos pelo usuário. Após a cópia, deixe iniciar pelo disco virtual; a sessão não precisa de nova tecla para boot no DVD a cada reinício.

Alternativa manual após o download: Gerenciador do Hyper-V → `Multiseat-Comparison` → Configurações → Unidade de DVD → Arquivo de imagem, selecione a ISO. Use Conectar → Iniciar e pressione uma tecla na janela da VM quando solicitado para iniciar pelo DVD. Instale o Windows somente no disco virtual. Referência: [criar VM](https://learn.microsoft.com/en-us/windows-server/virtualization/hyper-v/get-started/create-a-virtual-machine-in-hyper-v).

Conclua a instalação, registre edição/build e entre no desktop do convidado. Crie um checkpoint antes do teste. Mantenha as proteções padrão. Não copiar ACLs, registro ou políticas do computador atual para o convidado.

## 3. Transferir o pacote

Etapa concluída por `scripts/Prepare-ComparisonGuest.ps1`, execução elevada `vm-guest-preparation-20261008-164724-7f2de4d7`. Conferiu propriedade da VM pelo ID/relatório de criação e toda a cadeia VHDX/AVHDX dentro do armazenamento do projeto. VM Running. Criou checkpoint `BeforeChildSessionComparison-20261008-164724-7f2de4d7`, tipo observado Standard, antes de habilitar a interface de cópia. Checkpoint não foi restaurado automaticamente.

Manifesto dos cinco arquivos do pacote conferido no host. SHA-256 do ZIP transferido: `E8A9DBC99D695455B48B82A53FE04FD9318150D9EB8BA1166E768CC3D53E4875`. Copy-VMFile concluiu para `C:\Users\Public\comparison-kit.zip` no convidado; hash do arquivo no convidado ainda não lido. Guest Service Interface inicialmente desativada foi habilitada para a cópia e restaurada/verificada como desativada em finally. Sem credenciais, RDP ou execução de comandos no convidado. Fontes: [Copy-VMFile](https://learn.microsoft.com/en-us/powershell/module/hyper-v/copy-vmfile), [integração Hyper-V](https://learn.microsoft.com/en-us/windows-server/virtualization/hyper-v/manage/manage-hyper-v-integration-services).

Pacote preparado localmente: `build/comparison-kit.zip`. Inclui apenas três executáveis, o launcher com restauração, este roteiro e hashes SHA-256; não inclui relatórios, credenciais ou símbolos. Probes C++ foram compilados com runtime estático e dependem somente de DLLs do Windows; host C# usa .NET Framework. Não é instalador de produto.

Alternativa manual com credenciais locais, caso a interface de cópia não esteja disponível em outro laboratório: com a VM ligada e um usuário administrador configurado nela, execute no PowerShell administrativo do host, a partir da pasta do projeto:

```powershell
$comparisonSession = New-PSSession -VMName 'Multiseat-Comparison' -Credential (Get-Credential)
try {
    Copy-Item -LiteralPath '.\build\comparison-kit.zip' -Destination 'C:\Users\Public\comparison-kit.zip' -ToSession $comparisonSession
} finally {
    Remove-PSSession -Session $comparisonSession
}
```

Digite as credenciais do usuário da VM na janela local; não enviá-las ao agente. PowerShell Direct transporta arquivos sem depender de RDP/rede. Não executar o host gráfico de teste dentro da sessão PowerShell Direct: ele precisa do desktop interativo do convidado. Fonte: [PowerShell Direct](https://learn.microsoft.com/en-us/windows-server/virtualization/hyper-v/powershell-direct).

## 4. Executar uma comparação delimitada

### Execução pelo launcher, sem copiar comandos

Usuário informou dificuldade para copiar/colar texto no console básico. `scripts/Run-ComparisonGuest.cmd` foi transferido separadamente para `C:\Users\Public\Executar-Teste.cmd`, junto de `comparison-kit.sha256`, por `Prepare-ComparisonGuest.ps1 -LauncherOnly`. Essa opção preserva ZIP e checkpoint existentes, valida o pacote no host e restaura a interface de cópia. Transferência real concluída; não executa comandos no convidado.

Dentro da VM, pressione Win+R, digite `C:\Users\Public`, pressione Enter e abra `Executar-Teste.cmd`. Aceite o UAC do convidado. O launcher recusa modelo diferente de Virtual Machine antes de extração/elevação, verifica o ZIP com o hash transferido, extrai para MultiseatComparison, registra edição/build e sessões, executa smoke e só então a tentativa padrão de sessão filha. Não usa script PS1 nem modifica a política de execução. Não executa a variante combinada automaticamente.

O usuário mostrou a recusa por pasta MultiseatComparison já existente; essa invocação não iniciou conexão. O launcher foi corrigido e transferido novamente: confere os cinco arquivos extraídos contra o manifesto do ZIP validado e reutiliza a pasta somente se corresponderem. Arquivo ausente, diferente ou caminho redirecionado interrompe sem sobrescrever. Não apagar a pasta para repetir o teste.

Se encontrar `guest-child-default.log` na pasta de resultados ou em uma subpasta `guest-attempt-*`, mostra a tentativa mais recente e não inicia outra conexão. Um log existente pode ser de execução interrompida; avaliar seu conteúdo antes de decidir repetir. Sem log de conexão anterior, guarda a nova tentativa em uma subpasta exclusiva de `C:\Users\Public\MultiseatComparison\docs\lab\results`, incluindo edição/build e código de saída. A janela usa `cmd /k` para permanecer aberta após o término.

Verificações locais passaram para extração nova, reutilização de pacote válido, leitura sem alteração de log anterior e recusa de arquivo diferente. Sintaxe PowerShell, limite de linha CMD e recusa no host físico também verificados; nenhum teste nativo de sessão filha foi executado nessas verificações. Nova transferência substituiu somente launcher/hash auxiliar, preservou ZIP/checkpoint e restaurou a interface de cópia. Resultado da sessão filha no convidado ainda aguarda a saída do usuário.

### Alternativa manual

O diálogo de escolha de resolução mostrado pelo usuário pertence à sessão aprimorada, que usa RDP. Feche-o pelo X para conservar o console básico; o ícone de sessão aprimorada na barra do VMConnect também permite alternar para básico. Não alterar a configuração global do host nem habilitar RDP convencional apenas para copiar arquivos. Fonte: [sessão aprimorada](https://learn.microsoft.com/en-us/windows-server/virtualization/hyper-v/enhanced-session-mode).

Dentro da VM, abra Windows PowerShell como administrador no desktop interativo. Se Ctrl+V não funcionar em modo básico, use Área de Transferência → Digitar texto da área de transferência no menu do VMConnect, ou digite os comandos. Referência: [recursos locais VMConnect](https://learn.microsoft.com/en-us/windows-server/virtualization/hyper-v/use-local-resources-virtual-machine-connection).

Primeiro confira o ZIP no convidado e registre edição/build:

```powershell
$guestHash = (Get-FileHash -LiteralPath 'C:\Users\Public\comparison-kit.zip' -Algorithm SHA256).Hash
if ($guestHash -ne 'E8A9DBC99D695455B48B82A53FE04FD9318150D9EB8BA1166E768CC3D53E4875') { throw 'O pacote no convidado nao corresponde a transferencia registrada.' }
Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' | Select-Object EditionID, DisplayVersion, CurrentBuild, UBR
```

Em seguida, execute os programas diretamente; o teste padrão não precisa de script PS1 nem de mudança da política de execução:

```powershell
Expand-Archive -LiteralPath 'C:\Users\Public\comparison-kit.zip' -DestinationPath 'C:\Users\Public\MultiseatComparison'
Set-Location 'C:\Users\Public\MultiseatComparison'
.\build\child-session\Release\ms-session-probe.exe
.\build\child-session\Release\ms-child-session-test.exe --smoke
if ($LASTEXITCODE -ne 0) { throw 'Smoke falhou; nao iniciar a conexao.' }
.\build\child-session\Release\ms-child-session-test.exe --test
$LASTEXITCODE
```

Primeiro testar o caminho documentado, sem habilitar o RDP convencional. Se falhar com transporte indisponível, executar uma única comparação adicional:

```powershell
powershell -NoProfile -File .\scripts\Invoke-ChildSessionTest.ps1 -TestLocalRdp -CombinedActivation
```

A segunda variante habilita temporariamente RDP e sessões filhas no convidado, preserva firewall e restaura configurações/serviços. O launcher recusa sessão filha preexistente/estado desconhecido. O teste encerra apenas a nova sessão observada; não abrir jogos ou documentos nela. Guardar `summary.json`, logs e edição/build em `docs/lab/results/`, sem publicar dados locais. Se a política bloquear o script, registrar o erro; não alterar políticas globais para contorná-lo.

## Decisão após a comparação

- **ID distinto e restauração confirmada:** documentar a diferença e preparar um teste separado de apresentação e foco com aplicativos comuns. Não concluir isolamento físico, contas distintas ou compatibilidade com LoL.
- **Mesma falha nas duas variantes:** suspender este candidato como próxima implementação; registrar a matriz de ambientes e voltar à investigação de integração nativa. Não iniciar outra rodada igual de rastreamento sem hipótese nova.
- **Falha de preparação ou limpeza:** interromper a comparação, registrar o erro e usar o checkpoint do convidado para recuperação. Não interpretar isso como falha comprovada do mecanismo.

Sucesso completo do produto continua condicionado a P01–P08 em [PRODUCT-SCOPE](../PRODUCT-SCOPE.md). Uma comparação bem-sucedida não libera automaticamente T007 ou M2.
