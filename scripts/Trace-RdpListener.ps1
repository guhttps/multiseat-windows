[CmdletBinding()]
param(
    [switch]$Elevated,
    [switch]$FileRegistryOperations,
    [switch]$RpcCalls,
    [switch]$RpcStacks,
    [switch]$CombinedActivation,
    [ValidatePattern('^[0-9]{8}-[0-9]{6}-[a-f0-9]{8}$')][string]$RunId
)
$ErrorActionPreference = 'Stop'
if ($RpcStacks) { $RpcCalls = $true }
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $RunId) { $RunId = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8) }
$directory = Join-Path $projectRoot ('docs/lab/results/rdp-trace-' + $RunId)
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$reportPath = Join-Path $directory 'summary.json'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
try { $admin = ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) }
finally { $identity.Dispose() }
$powershell = Join-Path $env:windir 'System32/WindowsPowerShell/v1.0/powershell.exe'
if (-not $admin) {
    if ($Elevated) { throw 'Administrator token unavailable.' }
    Write-Host 'UAC: este rastreamento executa um teste temporario de RDP e registra dados locais de diagnostico.'
    $arguments = '-NoProfile -File "' + $PSCommandPath + '" -Elevated -RunId ' + $RunId
    if ($FileRegistryOperations) { $arguments += ' -FileRegistryOperations' }
    if ($RpcCalls) { $arguments += ' -RpcCalls' }
    if ($RpcStacks) { $arguments += ' -RpcStacks' }
    if ($CombinedActivation) { $arguments += ' -CombinedActivation' }
    try {
        $process = Start-Process -FilePath $powershell -Verb RunAs -WindowStyle Hidden -ArgumentList $arguments -Wait -PassThru
    } catch {
        $cause = $_.Exception.GetBaseException()
        if ($cause -is [ComponentModel.Win32Exception] -and $cause.NativeErrorCode -eq 1223) {
            [ordered]@{ schema_version = 1; status = 'uac_cancelled'; trace_started = $false; windows_test_started = $false } |
                ConvertTo-Json | Set-Content -LiteralPath $reportPath -Encoding utf8
            Write-Host 'UAC cancelado: nenhuma captura ou alteracao do Windows iniciada.'
            exit 2
        }
        throw
    }
    if (-not (Test-Path -LiteralPath $reportPath)) { throw 'Trace did not produce a report.' }
    Write-Host ('Relatorio: ' + $reportPath)
    exit $process.ExitCode
}

$sessionName = 'MultiseatRdp_' + $RunId
$providerFile = Join-Path $directory 'providers.txt'
# Fixed providers; no packets or persistent channel changes.
$providers = @(
    'Microsoft-Windows-TerminalServices-LocalSessionManager 0xffffffffffffffff 5',
    'Microsoft-Windows-TerminalServices-RemoteConnectionManager 0xffffffffffffffff 5',
    'Microsoft-Windows-RemoteDesktopServices-RdpCoreTS 0xffffffffffffffff 5',
    'Microsoft-Windows-RPC 0x8000000000000000 2'
)
if ($FileRegistryOperations) {
    # Opens/security queries and file names/completions, without value/content payloads.
    $providers += 'Microsoft-Windows-Kernel-Registry 0x3002 4', 'Microsoft-Windows-Kernel-File 0xd0 4'
}
$providers | Set-Content -LiteralPath $providerFile -Encoding ascii
$logman = Join-Path $env:windir 'System32/logman.exe'
$tracerpt = Join-Path $env:windir 'System32/tracerpt.exe'
$etlPath = Join-Path $directory 'listener.etl'
$started = $false
$stopped = $false
$testExit = $null
$decodeExit = $null
$traceError = $null
$rpcAnalysis = 'not_decoded'
$rpcProcessIds = @()
try {
    & $logman create trace $sessionName -ets -pf $providerFile -o $etlPath -f bincirc -max 64 -nb 8 32 -bs 64 2>&1 |
        Out-File -LiteralPath (Join-Path $directory 'start.log') -Encoding utf8
    if ($LASTEXITCODE -ne 0) { throw ('ETW start failed: ' + $LASTEXITCODE) }
    $started = $true
    if ($RpcCalls) {
        $rpcProcessIds = @('LSM','TermService' | ForEach-Object {
            (Get-CimInstance Win32_Service -Filter ("Name='" + $_ + "'") -ErrorAction Stop).ProcessId
        })
        Add-Type -Path (Join-Path $PSScriptRoot 'RpcTraceFilter.cs') -ErrorAction Stop
        [RpcTraceFilter]::Apply($sessionName, [uint32[]]$rpcProcessIds, [bool]$RpcStacks)
    }
    # Separate process protects the trace finally block from the experiment's exit.
    $experimentArguments = @('-NoProfile','-File',(Join-Path $PSScriptRoot 'Invoke-ChildSessionTest.ps1'),'-Elevated','-TestLocalRdp','-RunId',$RunId)
    if ($CombinedActivation) { $experimentArguments += '-CombinedActivation' }
    & $powershell @experimentArguments 2>&1 |
        Out-File -LiteralPath (Join-Path $directory 'experiment.log') -Encoding utf8
    $testExit = $LASTEXITCODE
} catch { $traceError = $_.Exception.Message }
finally {
    if ($started) {
        try {
            & $logman stop $sessionName -ets 2>&1 | Out-File -LiteralPath (Join-Path $directory 'stop.log') -Encoding utf8
            $stopped = ($LASTEXITCODE -eq 0)
            if (-not $stopped) { $traceError = 'ETW session stop failed.' }
        } catch { $traceError = $_.Exception.Message }
    }
}
if ($stopped -and (Test-Path -LiteralPath $etlPath)) {
    & $tracerpt $etlPath -o (Join-Path $directory 'events.xml') -of XML -summary (Join-Path $directory 'trace-summary.txt') -y 2>&1 |
        Out-File -LiteralPath (Join-Path $directory 'decode.log') -Encoding utf8
    $decodeExit = $LASTEXITCODE
    if ($decodeExit -eq 0) {
        $xmlPath = Join-Path $directory 'events.xml'
        if ((Get-Item -LiteralPath $xmlPath).Length -gt 16MB) {
            $rpcAnalysis = 'skipped_large_xml'
        } else {
            try {
                [xml]$events = Get-Content -Raw -Encoding utf8 -LiteralPath $xmlPath
                $rpcEvents = @($events.Events.Event | Where-Object { $_.System.Provider.Name -eq 'Microsoft-Windows-RPC' } | ForEach-Object {
                    $fields = @{}
                    foreach ($field in $_.EventData.Data) { $fields[$field.Name] = $field.InnerText }
                    [ordered]@{
                        process = $fields.ImageName
                        process_id = $_.System.Execution.ProcessID
                        status = $fields.Status
                        detection_location = $fields.DetectionLocation
                        time = $_.System.TimeCreated.SystemTime
                        event_id = [string]$_.System.EventID
                        thread_id = $_.System.Execution.ThreadID
                        activity_id = $_.System.Correlation.ActivityID
                        interface_uuid = $fields.InterfaceUuid
                        procedure = $fields.ProcNum
                        endpoint = $fields.Endpoint
                        protocol = $fields.Protocol
                        authentication_level = $fields.AuthenticationLevel
                        authentication_service = $fields.AuthenticationService
                        event_writer_sid = $_.System.Security.UserID
                        stack_addresses = @($_.System.Stack.Address | Where-Object { $_ })
                    }
                })
                ConvertTo-Json -InputObject $rpcEvents -Depth 5 | Set-Content -LiteralPath (Join-Path $directory 'rpc-calls.json') -Encoding utf8
                $rpcErrors = @($rpcEvents | Where-Object { $_.status -and $_.status -ne '0x0' })
                ConvertTo-Json -InputObject $rpcErrors -Depth 4 | Set-Content -LiteralPath (Join-Path $directory 'rpc-errors.json') -Encoding utf8
                $rpcAnalysis = 'written'
            } catch { $rpcAnalysis = 'unavailable' }
        }
    }
}
[ordered]@{
    schema_version = 1
    captured_utc = [DateTimeOffset]::UtcNow.ToString('o')
    session_name = $sessionName
    file_registry_operations_requested = [bool]$FileRegistryOperations
    rpc_process_ids = $rpcProcessIds
    rpc_calls_requested = [bool]$RpcCalls
    rpc_stacks_requested = [bool]$RpcStacks
    combined_activation_requested = [bool]$CombinedActivation
    trace_started = $started
    trace_stopped = $stopped
    experiment_exit_code = $testExit
    decode_exit_code = $decodeExit
    rpc_analysis = $rpcAnalysis
    trace_error = $traceError
    experiment_report = '../child-admin-' + $RunId + '/summary.json'
} | ConvertTo-Json | Set-Content -LiteralPath $reportPath -Encoding utf8
if ($RpcCalls -and $rpcAnalysis -eq 'written') {
    try { & (Join-Path $PSScriptRoot 'Read-RpcFailures.ps1') -RunId $RunId }
    catch { $_.Exception.Message | Out-File -LiteralPath (Join-Path $directory 'correlation-error.log') -Encoding utf8 }
}
if ($RpcStacks -and $rpcAnalysis -eq 'written') {
    # Separate process keeps the diagnostic's exit from terminating this launcher.
    & $powershell -NoProfile -File (Join-Path $PSScriptRoot 'Get-RpcModuleMap.ps1') -Elevated -RunId $RunId 2>&1 |
        Out-File -LiteralPath (Join-Path $directory 'module-map.log') -Encoding utf8
}
# Successful capture is distinct from a successful child-session experiment.
if (-not $started -or -not $stopped -or $decodeExit -ne 0 -or $traceError -or $null -eq $testExit -or $testExit -ne 0) { exit 1 }
exit 0
