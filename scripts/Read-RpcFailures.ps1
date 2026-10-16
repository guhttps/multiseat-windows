[CmdletBinding()]
param([Parameter(Mandatory)][ValidatePattern('^[0-9]{8}-[0-9]{6}-[a-f0-9]{8}$')][string]$RunId)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$directory = Join-Path $projectRoot ('docs/lab/results/rdp-trace-' + $RunId)
$callsPath = Join-Path $directory 'rpc-calls.json'
if ((Get-Item -LiteralPath $callsPath).Length -gt 16MB) { throw 'Call summary too large; streaming analysis required.' }
$calls = Get-Content -Raw -Encoding utf8 -LiteralPath $callsPath | ConvertFrom-Json
$trace = Get-Content -Raw -Encoding utf8 -LiteralPath (Join-Path $directory 'summary.json') | ConvertFrom-Json
$latestStart = @{}
$failures = @()
foreach ($call in $calls) {
    # Match client start/stop by activity, process and thread; keep the nearest start.
    $key = $call.process_id + '/' + $call.thread_id + '/' + $call.activity_id
    if ($call.event_id -eq '5') { $latestStart[$key] = $call; continue }
    if ($call.event_id -ne '7') { continue }
    $start = $latestStart[$key]
    $latestStart.Remove($key)
    if (-not $call.status -or $call.status -eq '0x0') { continue }
    $interfaceName = $null
    if ($start -and $start.interface_uuid) {
        $node = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey('SOFTWARE\Classes\Interface\' + $start.interface_uuid)
        try { if ($node) { $interfaceName = $node.GetValue('') } }
        finally { if ($node) { $node.Dispose() } }
    }
    $role = 'unknown'
    if ($trace.rpc_process_ids.Count -eq 2) {
        if ($call.process_id -eq [string]$trace.rpc_process_ids[0]) { $role = 'LSM' }
        if ($call.process_id -eq [string]$trace.rpc_process_ids[1]) { $role = 'TermService' }
    }
    $failures += [ordered]@{
        client_service = $role
        client_process_id = $call.process_id
        thread_id = $call.thread_id
        activity_id = $call.activity_id
        status = $call.status
        start_matched = ($null -ne $start)
        interface_uuid = $(if ($start) { $start.interface_uuid })
        interface_name_from_current_registry = $interfaceName
        procedure = $(if ($start) { $start.procedure })
        endpoint = $(if ($start) { $start.endpoint })
        authentication_level = $(if ($start.authentication_level) { $start.authentication_level.Trim() })
        authentication_service = $(if ($start.authentication_service) { $start.authentication_service.Trim() })
        start_time = $(if ($start) { $start.time })
        stop_time = $call.time
    }
}
$reportPath = Join-Path $directory 'rpc-failures.json'
[ordered]@{ schema_version = 1; analyzed_utc = [DateTimeOffset]::UtcNow.ToString('o'); failures = $failures } |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding utf8
Write-Host ('Chamadas RPC com falha: ' + $failures.Count)
Write-Host ('Relatorio: ' + $reportPath)
