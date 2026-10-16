[CmdletBinding()]
param(
    [switch]$Elevated,
    [Parameter(Mandatory)][ValidatePattern('^[0-9]{8}-[0-9]{6}-[a-f0-9]{8}$')][string]$RunId
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$directory = Join-Path $root ('docs/lab/results/rdp-trace-' + $RunId)
$trace = Get-Content -Raw -Encoding utf8 -LiteralPath (Join-Path $directory 'summary.json') | ConvertFrom-Json
if ($trace.rpc_process_ids.Count -ne 2) { throw 'Two captured service process IDs required.' }
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
try { $admin = ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) }
finally { $identity.Dispose() }
if (-not $admin) {
    if ($Elevated) { throw 'Administrator token unavailable.' }
    $powershell = Join-Path $env:windir 'System32/WindowsPowerShell/v1.0/powershell.exe'
    $arguments = '-NoProfile -File "' + $PSCommandPath + '" -Elevated -RunId ' + $RunId
    $process = Start-Process -FilePath $powershell -Verb RunAs -WindowStyle Hidden -ArgumentList $arguments -Wait -PassThru
    exit $process.ExitCode
}
$results = @()
$names = @('LSM','TermService')
for ($index = 0; $index -lt $names.Count; $index++) {
    $serviceName = $names[$index]
    $entry = [ordered]@{ service = $serviceName; process_id = $trace.rpc_process_ids[$index]; status = 'unavailable'; process_start_utc = $null; modules = @(); error = $null }
    $process = $null
    try {
        $service = Get-CimInstance Win32_Service -Filter ("Name='" + $serviceName + "'") -ErrorAction Stop
        if ($service.ProcessId -ne $entry.process_id) { throw 'Service PID changed after capture; mapping refused.' }
        $process = [Diagnostics.Process]::GetProcessById($service.ProcessId)
        $entry.process_start_utc = $process.StartTime.ToUniversalTime().ToString('o')
        if ($process.StartTime.ToUniversalTime() -gt [DateTimeOffset]::Parse($trace.captured_utc).UtcDateTime) { throw 'Process started after capture; mapping refused.' }
        $entry.modules = @($process.Modules | ForEach-Object {
            [ordered]@{ name = $_.ModuleName; path = $_.FileName; base_address = ('0x{0:X16}' -f $_.BaseAddress.ToInt64()); size = $_.ModuleMemorySize }
        })
        if ($entry.modules.Count -eq 0) { throw 'No module map returned.' }
        $entry.status = 'ok'
    } catch { $entry.error = $_.Exception.GetBaseException().Message }
    finally { if ($process) { $process.Dispose() } }
    $results += $entry
}
[ordered]@{ schema_version = 1; captured_utc = [DateTimeOffset]::UtcNow.ToString('o'); process_maps = $results } |
    ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $directory 'rpc-modules.json') -Encoding utf8
if (@($results | Where-Object status -eq 'ok').Count -ne 2) { exit 1 }
exit 0
