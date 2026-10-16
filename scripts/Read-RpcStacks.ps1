[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^[0-9]{8}-[0-9]{6}-[a-f0-9]{8}$')][string]$RunId,
    [switch]$DownloadSymbols,
    [ValidateRange(1,128)][int]$MaxSymbolMegabytes = 16
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$directory = Join-Path $root ('docs/lab/results/rdp-trace-' + $RunId)
$xmlPath = Join-Path $directory 'events.xml'
if ((Get-Item -LiteralPath $xmlPath).Length -gt 16MB) { throw 'XML too large; streaming analysis required.' }
[xml]$capture = Get-Content -Raw -Encoding utf8 -LiteralPath $xmlPath
$maps = (Get-Content -Raw -Encoding utf8 -LiteralPath (Join-Path $directory 'rpc-modules.json') | ConvertFrom-Json).process_maps
$failures = (Get-Content -Raw -Encoding utf8 -LiteralPath (Join-Path $directory 'rpc-failures.json') | ConvertFrom-Json).failures
$activities = @($failures | ForEach-Object activity_id)
$selected = @($capture.Events.Event | Where-Object {
    $_.System.Provider.Name -eq 'Microsoft-Windows-RPC' -and
    (($_.EventData.Data | Where-Object { $_.Name -eq 'Status' -and $_.InnerText -eq '0x5' }) -or
    ([string]$_.System.EventID -eq '5' -and $activities -contains $_.System.Correlation.ActivityID))
})
Add-Type -Path (Join-Path $PSScriptRoot 'RpcStackSymbols.cs')
$cache = Join-Path $root 'docs/lab/results/symbols'
New-Item -ItemType Directory -Path $cache -Force | Out-Null
$allowed = @('ntdll.dll','rpcrt4.dll','combase.dll','termsrv.dll','kernel32.dll','lsm.dll','lsmproxy.dll')
$symbols = @{}
$downloads = @()
$wanted = @{}
foreach ($event in $selected) {
    $map = $maps | Where-Object { $_.status -eq 'ok' -and [string]$_.process_id -eq $event.System.Execution.ProcessID } | Select-Object -First 1
    foreach ($address in $event.System.Stack.Address) {
        $value = [Convert]::ToUInt64($address.Substring(2),16)
        foreach ($module in $map.modules) {
            $base = [Convert]::ToUInt64($module.base_address.Substring(2),16)
            if ($value -ge $base -and $value -lt $base + $module.size -and $allowed -contains $module.name.ToLowerInvariant()) {
                $wanted[$module.name.ToLowerInvariant()] = $module
            }
        }
    }
}
foreach ($name in @($wanted.Keys | Sort-Object)) {
    $path = Join-Path $env:windir ('System32/' + $name)
    $identity = [RpcStackSymbols]::PdbIdentity($path)
    $folder = Join-Path $cache ($identity[0] + '/' + $identity[1])
    $pdb = Join-Path $folder $identity[0]
    $download = [ordered]@{ module = $name; pdb_identity = ($identity -join '/'); status = 'not_cached'; error = $null }
    if (-not (Test-Path -LiteralPath $pdb) -and $DownloadSymbols) {
        New-Item -ItemType Directory -Path $folder -Force | Out-Null
        $url = 'https://msdl.microsoft.com/download/symbols/' + $identity[0] + '/' + $identity[1] + '/' + $identity[0]
        $temporary = $pdb + '.partial'
        try {
            if ((Test-Path -LiteralPath $temporary) -and (Get-Item -LiteralPath $temporary).Length -gt $MaxSymbolMegabytes * 1MB) {
                throw 'Partial symbol file exceeds the requested size limit.'
            }
            # curl bounds the entire transfer; PowerShell 5's TimeoutSec may not bound body reads.
            # The GUID/age URL is immutable; resume the matching partial file instead of restarting.
            $curl = Join-Path $env:windir 'System32/curl.exe'
            & $curl --fail --silent --show-error --location --proto '=https' --proto-redir '=https' --connect-timeout 10 --max-time 60 --max-filesize ($MaxSymbolMegabytes * 1MB) --continue-at - --output $temporary $url
            if ($LASTEXITCODE -ne 0) { throw ('Symbol download failed: curl exit ' + $LASTEXITCODE) }
            if ((Get-Item -LiteralPath $temporary).Length -gt $MaxSymbolMegabytes * 1MB) { throw 'Completed symbol file exceeds the requested size limit.' }
            Move-Item -LiteralPath $temporary -Destination $pdb -Force
        } catch { $download.error = $_.Exception.Message }
    }
    if (Test-Path -LiteralPath $pdb) { $download.status = 'cached'; $symbols[$name] = $pdb }
    $downloads += $download
}
$results = @()
# All DbgHelp calls remain sequential in this process. No target process is opened.
foreach ($map in $maps) {
    if ($map.status -ne 'ok') { continue }
    $handler = [RpcStackSymbols]::new($cache)
    $loaded = @{}
    try {
        foreach ($event in $selected | Where-Object { $_.System.Execution.ProcessID -eq [string]$map.process_id }) {
            $fields = @{}
            foreach ($field in $event.EventData.Data) { $fields[$field.Name] = $field.InnerText }
            $frames = @()
            foreach ($address in $event.System.Stack.Address) {
                $value = [Convert]::ToUInt64($address.Substring(2),16)
                $frame = [ordered]@{ address = $address; module = $null; offset = $null; symbol = $null }
                foreach ($module in $map.modules) {
                    $base = [Convert]::ToUInt64($module.base_address.Substring(2),16)
                    if ($value -lt $base -or $value -ge $base + $module.size) { continue }
                    $frame.module = $module.name
                    $frame.offset = '0x' + ($value - $base).ToString('X')
                    $name = $module.name.ToLowerInvariant()
                    if ($symbols.ContainsKey($name)) {
                        if (-not $loaded.ContainsKey($name)) {
                            $handler.Load($symbols[$name], $name, $base, [uint32]$module.size)
                            $loaded[$name] = $true
                        }
                        $frame.symbol = $handler.Resolve($value)
                    }
                    break
                }
                $frames += $frame
            }
            $results += [ordered]@{
                service = $map.service; event_id = [string]$event.System.EventID; process_id = $map.process_id
                thread_id = $event.System.Execution.ThreadID; activity_id = $event.System.Correlation.ActivityID
                time = $event.System.TimeCreated.SystemTime; status = $fields.Status; detection_location = $fields.DetectionLocation
                event_writer_sid = $event.System.Security.UserID; frames = $frames
            }
        }
    } finally { $handler.Dispose() }
}
[ordered]@{ schema_version = 1; analyzed_utc = [DateTimeOffset]::UtcNow.ToString('o'); symbols = $downloads; events = $results } |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $directory 'rpc-stacks.json') -Encoding utf8
Write-Host ('Pilhas analisadas: ' + $results.Count + '; relatorio: ' + (Join-Path $directory 'rpc-stacks.json'))
