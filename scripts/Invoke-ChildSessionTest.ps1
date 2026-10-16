[CmdletBinding()]
param(
    [switch]$Elevated,
    [switch]$TestLocalRdp,
    [switch]$CombinedActivation,
    [ValidatePattern('^[0-9]{8}-[0-9]{6}-[a-f0-9]{8}$')]
    [string]$RunId
)

$ErrorActionPreference = 'Stop'
if ($CombinedActivation -and -not $TestLocalRdp) { throw 'CombinedActivation requires TestLocalRdp.' }
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$testExecutable = Join-Path $projectRoot 'build/child-session/Release/ms-child-session-test.exe'
$probeExecutable = Join-Path $projectRoot 'build/child-session/Release/ms-child-session-probe.exe'
foreach ($executable in @($testExecutable, $probeExecutable)) {
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw ('Executable not found: ' + $executable) }
}
if (-not $RunId) {
    $RunId = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
}
$resultDirectory = Join-Path $projectRoot ('docs/lab/results/child-admin-' + $RunId)
New-Item -ItemType Directory -Path $resultDirectory -Force | Out-Null
$reportPath = Join-Path $resultDirectory 'summary.json'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
try { $admin = ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) }
finally { $identity.Dispose() }

if (-not $admin) {
    if ($Elevated) { throw 'Administrator approval did not provide an administrator token.' }
    Write-Host 'Autorize o UAC para executar somente o teste de sessao filha e a verificacao posterior.'
    # Fixed script and fixed experiment; no arbitrary command parameter or persistent helper.
    $arguments = '-NoProfile -File "' + $PSCommandPath + '" -Elevated -RunId ' + $RunId
    if ($TestLocalRdp) { $arguments += ' -TestLocalRdp' }
    if ($CombinedActivation) { $arguments += ' -CombinedActivation' }
    $powershell = Join-Path $env:windir 'System32/WindowsPowerShell/v1.0/powershell.exe'
    try {
        $process = Start-Process -FilePath $powershell -Verb RunAs -WindowStyle Hidden -ArgumentList $arguments -Wait -PassThru
    } catch { throw 'A elevacao nao foi concluida. Nenhum teste administrativo foi iniciado por este launcher.' }
    if (-not (Test-Path -LiteralPath $reportPath)) { throw ('Relatorio nao produzido. Processo elevado retornou ' + $process.ExitCode) }
    $report = Get-Content -Raw -Encoding utf8 -LiteralPath $reportPath | ConvertFrom-Json
    Write-Host ('Teste: ' + $report.status + '; codigo: ' + $report.test_exit_code)
    if ($report.launcher_error) { Write-Host ('Motivo: ' + $report.launcher_error) }
    if ($report.local_rdp_test_requested) { Write-Host ('Configuracao RDP restaurada: ' + $report.rdp_listener_restored) }
    Write-Host ('Relatorio: ' + $reportPath)
    exit $process.ExitCode
}

function Invoke-LoggedExperiment {
    param([string]$Executable, [string[]]$Arguments, [string]$Log)
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        # A launch failure must not inherit a successful exit code from an earlier process.
        $LASTEXITCODE = $null
        $output = & $Executable @Arguments 2>&1
        $code = $LASTEXITCODE
        $output | Out-File -LiteralPath (Join-Path $resultDirectory $Log) -Encoding utf8
        if ($null -eq $code) { throw ('Experiment did not produce an exit code: ' + $Log) }
        return $code
    } finally { $ErrorActionPreference = $previousPreference }
}

$transportKey = 'HKLM:/SYSTEM/CurrentControlSet/Control/Terminal Server'
$transportInitial = $null
$transportChanged = $false
$transportRestored = $true
$transportSetting = $null
$startedServices = @()
$serviceStates = @()
$servicesRestored = $true
$testExit = $null
$probeExit = $null
$launcherError = $null
$listenerReady = $null
try {
    if ($TestLocalRdp) {
        $transportInitial = (Get-ItemProperty -LiteralPath $transportKey -Name fDenyTSConnections -ErrorAction Stop).fDenyTSConnections
        if ($transportInitial -notin @(0, 1)) { throw 'Unknown RDP listener state; test refused.' }
        [ordered]@{ registry_value = 'fDenyTSConnections'; initial_value = $transportInitial } |
            ConvertTo-Json | Set-Content -LiteralPath (Join-Path $resultDirectory 'transport-backup.json') -Encoding utf8
        foreach ($serviceName in @('SessionEnv', 'UmRdpService')) {
            $service = Get-Service -Name $serviceName -ErrorAction Stop
            if ($service.Status -notin @('Running', 'Stopped')) { throw ('Unstable auxiliary service state: ' + $serviceName) }
            $serviceStates += [ordered]@{ name = $serviceName; initial_state = $service.Status.ToString() }
        }
        $serviceStates | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $resultDirectory 'service-backup.json') -Encoding utf8
        foreach ($serviceState in $serviceStates) {
            if ($serviceState.initial_state -eq 'Stopped') {
                $startedServices += $serviceState.name
                Start-Service -Name $serviceState.name -ErrorAction Stop
                (Get-Service -Name $serviceState.name).WaitForStatus('Running', [TimeSpan]::FromSeconds(10))
            }
        }
        if ($transportInitial -ne 0) {
            $transportSetting = Get-CimInstance -Namespace root/cimv2/terminalservices -ClassName Win32_TerminalServiceSetting -ErrorAction Stop
            if ($transportSetting.AllowTSConnections -ne (1 - $transportInitial)) { throw 'RDP provider and registry disagree; test refused.' }
            $transportChanged = $true
            $enableResult = Invoke-CimMethod -InputObject $transportSetting -MethodName SetAllowTSConnections -Arguments @{ AllowTSConnections = [uint32]1; ModifyFirewallException = [uint32]0 } -ErrorAction Stop
            if ($enableResult.ReturnValue -ne 0) { throw ('RDP provider enable failed: ' + $enableResult.ReturnValue) }
        }
        $rdpPort = (Get-ItemProperty -LiteralPath ($transportKey + '/WinStations/RDP-Tcp') -Name PortNumber -ErrorAction Stop).PortNumber
        $listenerDeadline = [DateTime]::UtcNow.AddSeconds(10)
        $listenerReady = $false
        do {
            $listenerReady = (@(Get-NetTCPConnection -State Listen -ErrorAction Stop | Where-Object LocalPort -eq $rdpPort).Count -gt 0)
            if (-not $listenerReady) { Start-Sleep -Milliseconds 250 }
        } while (-not $listenerReady -and [DateTime]::UtcNow -lt $listenerDeadline)
        if (-not $listenerReady -and -not $CombinedActivation) { throw 'RDP listener did not start within 10 seconds; connection test not started.' }
        if (-not $listenerReady) {
            Write-Host 'Combined activation hypothesis: proceed to child enable/Connect while RDP is temporarily allowed, despite no TCP listener.'
        }
    }
    $testExit = Invoke-LoggedExperiment -Executable $testExecutable -Arguments @('--test') -Log 'connection.log'
    $probeExit = Invoke-LoggedExperiment -Executable $probeExecutable -Arguments @() -Log 'after.log'
} catch {
    $launcherError = $_.Exception.Message
    # Capture the failed state before restoration; this helper only reads Windows.
    if ($TestLocalRdp) {
        try {
            & (Join-Path $PSScriptRoot 'Get-RdpListenerDiagnostics.ps1') -Elevated -RunId $RunId |
                Out-File -LiteralPath (Join-Path $resultDirectory 'listener-diagnostic.log') -Encoding utf8
        } catch {
            $_.Exception.Message | Out-File -LiteralPath (Join-Path $resultDirectory 'listener-diagnostic.log') -Encoding utf8
        }
    }
}
finally {
    if ($transportChanged) {
        try {
            $restoreResult = Invoke-CimMethod -InputObject $transportSetting -MethodName SetAllowTSConnections -Arguments @{ AllowTSConnections = [uint32](1 - $transportInitial); ModifyFirewallException = [uint32]0 } -ErrorAction Stop
            if ($restoreResult.ReturnValue -ne 0) { throw ('RDP provider restore failed: ' + $restoreResult.ReturnValue) }
            $transportRestored = ((Get-ItemProperty -LiteralPath $transportKey -Name fDenyTSConnections -ErrorAction Stop).fDenyTSConnections -eq $transportInitial)
        } catch { $transportRestored = $false; $launcherError = 'RDP listener configuration restoration failed.' }
    }
    for ($serviceIndex = $startedServices.Count - 1; $serviceIndex -ge 0; $serviceIndex--) {
        try {
            $service = Get-Service -Name $startedServices[$serviceIndex] -ErrorAction Stop
            if ($service.Status -ne 'Stopped') {
                Stop-Service -Name $service.Name -ErrorAction Stop
                $service.WaitForStatus('Stopped', [TimeSpan]::FromSeconds(10))
            }
        } catch { $servicesRestored = $false; $launcherError = 'Auxiliary service state restoration failed.' }
    }
}
$status = if ($testExit -eq 0 -and $probeExit -eq 0 -and $transportRestored -and $servicesRestored -and -not $launcherError) { 'passed' } else { 'failed' }
[ordered]@{
    schema_version = 1
    captured_utc = [DateTimeOffset]::UtcNow.ToString('o')
    status = $status
    administrator_role_active = $admin
    test_exit_code = $testExit
    probe_exit_code = $probeExit
    local_rdp_test_requested = [bool]$TestLocalRdp
    combined_activation_requested = [bool]$CombinedActivation
    tcp_listener_observed_before_child_test = $listenerReady
    rdp_listener_initial_value = $transportInitial
    rdp_listener_restored = $transportRestored
    auxiliary_service_initial_states = $serviceStates
    auxiliary_services_restored = $servicesRestored
    launcher_error = $launcherError
    connection_log = $(if (Test-Path -LiteralPath (Join-Path $resultDirectory 'connection.log')) { 'connection.log' } else { $null })
    after_log = $(if (Test-Path -LiteralPath (Join-Path $resultDirectory 'after.log')) { 'after.log' } else { $null })
} | ConvertTo-Json | Set-Content -LiteralPath $reportPath -Encoding utf8
if ($status -eq 'passed') { exit 0 }
exit 1
