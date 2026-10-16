[CmdletBinding()]
param(
    [switch]$Elevated,
    [switch]$VerifyCoreFiles,
    [switch]$CheckServiceToken,
    [ValidatePattern('^[0-9]{8}-[0-9]{6}-[a-f0-9]{8}$')][string]$RunId
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $RunId) { $RunId = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8) }
$resultDirectory = Join-Path $projectRoot ('docs/lab/results/rdp-diagnostic-' + $RunId)
New-Item -ItemType Directory -Path $resultDirectory -Force | Out-Null
$reportPath = Join-Path $resultDirectory 'summary.json'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
try { $admin = ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) }
finally { $identity.Dispose() }
if (-not $admin) {
    if ($Elevated) { throw 'Administrator token unavailable; diagnostic refused.' }
    $arguments = '-NoProfile -File "' + $PSCommandPath + '" -Elevated -RunId ' + $RunId
    if ($VerifyCoreFiles) { $arguments += ' -VerifyCoreFiles' }
    if ($CheckServiceToken) { $arguments += ' -CheckServiceToken' }
    Write-Host 'UAC: diagnostico somente leitura; nenhuma configuracao do Windows sera alterada.'
    $process = Start-Process -FilePath (Join-Path $env:windir 'System32/WindowsPowerShell/v1.0/powershell.exe') -Verb RunAs -WindowStyle Hidden -ArgumentList $arguments -Wait -PassThru
    if (-not (Test-Path -LiteralPath $reportPath)) { throw 'Diagnostic did not produce a report.' }
    Write-Host ('Relatorio: ' + $reportPath)
    exit $process.ExitCode
}

function Read-Diagnostic {
    param([scriptblock]$Read)
    try { return [ordered]@{ status = 'ok'; data = @(& $Read) } }
    catch {
        $status = if ($_.FullyQualifiedErrorId -like 'NoMatchingEventsFound*') { 'no_matching_events' } else { 'unavailable' }
        return [ordered]@{ status = $status; error = $_.Exception.Message }
    }
}

$terminalKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\Terminal Server'
$policyKey = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows NT\Terminal Services'
$report = [ordered]@{
    schema_version = 1
    captured_utc = [DateTimeOffset]::UtcNow.ToString('o')
    administrator_role_active = $admin
    read_only = $true
    configuration = Read-Diagnostic { Get-ItemProperty -LiteralPath $terminalKey | Select-Object fDenyTSConnections }
    policy = Read-Diagnostic {
        $policyNode = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($policyKey.Substring(6))
        try {
            [ordered]@{
                key_exists = ($null -ne $policyNode)
                deny_value_present = ($null -ne $policyNode -and $policyNode.GetValueNames() -contains 'fDenyTSConnections')
                fDenyTSConnections = $(if ($policyNode) { $policyNode.GetValue('fDenyTSConnections', $null) })
            }
        } finally { if ($policyNode) { $policyNode.Dispose() } }
    }
    listener = Read-Diagnostic { Get-ItemProperty -LiteralPath ($terminalKey + '\WinStations\RDP-Tcp') | Select-Object fEnableWinStation,PortNumber,SecurityLayer,UserAuthentication }
    services = Read-Diagnostic { Get-CimInstance Win32_Service -Filter "Name='TermService' OR Name='UmRdpService' OR Name='SessionEnv'" | Select-Object Name,State,StartMode,StartName,ExitCode,ServiceSpecificExitCode }
    tcp_listeners = Read-Diagnostic { Get-NetTCPConnection -State Listen | Where-Object LocalPort -eq 3389 | Select-Object LocalAddress,LocalPort }
    service_signature = Read-Diagnostic { Get-AuthenticodeSignature (Join-Path $env:windir 'System32/termsrv.dll') | Select-Object Status,StatusMessage }
    registry_acls = @()
    machinekeys_acl = Read-Diagnostic { (Get-Acl -LiteralPath (Join-Path $env:ProgramData 'Microsoft/Crypto/RSA/MachineKeys')).Sddl }
    event_logs = @()
}
foreach ($keyPath in @($terminalKey, ($terminalKey + '\WinStations\RDP-Tcp'), 'HKLM:\SYSTEM\CurrentControlSet\Services\TermService', 'HKLM:\SOFTWARE\Microsoft\SystemCertificates\Remote Desktop')) {
    $entry = Read-Diagnostic {
        $registryNode = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($keyPath.Substring(6))
        if (-not $registryNode) { throw 'Registry key absent.' }
        try { $registryNode.GetAccessControl().GetSecurityDescriptorSddlForm([System.Security.AccessControl.AccessControlSections]::Access) }
        finally { $registryNode.Dispose() }
    }
    $entry['path'] = $keyPath
    $report.registry_acls += $entry
}
foreach ($logName in @('Microsoft-Windows-TerminalServices-LocalSessionManager/Operational', 'Microsoft-Windows-TerminalServices-RemoteConnectionManager/Operational', 'Microsoft-Windows-TerminalServices-RemoteConnectionManager/Admin', 'Microsoft-Windows-RemoteDesktopServices-RdpCoreTS/Operational')) {
    $entry = Read-Diagnostic { Get-WinEvent -FilterHashtable @{ LogName = $logName; Level = 2,3; StartTime = (Get-Date).AddDays(-1) } -MaxEvents 20 | Select-Object Id,TimeCreated,Message }
    $entry['log_name'] = $logName
    $report.event_logs += $entry
}
$report['tls_events'] = Read-Diagnostic { Get-WinEvent -FilterHashtable @{ LogName = 'System'; ProviderName = 'Schannel','Microsoft-Windows-TerminalServices-RemoteConnectionManager'; StartTime = (Get-Date).AddDays(-1) } -MaxEvents 20 | Select-Object Id,ProviderName,TimeCreated,Message }
$report['core_file_verification_requested'] = [bool]$VerifyCoreFiles
$report['session_manager_interface'] = Read-Diagnostic {
    $node = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey('SOFTWARE\Classes\Interface\{517f87fe-597a-4672-8555-6daf1c8c788d}')
    try { [ordered]@{ registered = ($null -ne $node); name = $(if ($node) { $node.GetValue('') }) } }
    finally { if ($node) { $node.Dispose() } }
}
$report['com_access_defaults'] = Read-Diagnostic {
    $node = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey('SOFTWARE\Microsoft\Ole')
    try {
        foreach ($valueName in @('MachineAccessRestriction','MachineLaunchRestriction','DefaultAccessPermission','DefaultLaunchPermission')) {
            $value = $node.GetValue($valueName, $null)
            $sddl = $null
            if ($value -is [byte[]]) { $sddl = ([System.Security.AccessControl.RawSecurityDescriptor]::new($value, 0)).GetSddlForm([System.Security.AccessControl.AccessControlSections]::All) }
            [ordered]@{ name = $valueName; present = ($null -ne $value); sddl = $sddl }
        }
    } finally { $node.Dispose() }
}
if ($VerifyCoreFiles) {
    $report['core_files'] = @()
    foreach ($fileName in @('lsm.dll','lsmproxy.dll','termsrv.dll','combase.dll')) {
        $filePath = Join-Path $env:windir ('System32/' + $fileName)
        $logName = 'sfc-' + $fileName + '.log'
        $entry = Read-Diagnostic {
            $startInfo = New-Object Diagnostics.ProcessStartInfo
            $startInfo.FileName = Join-Path $env:windir 'System32/sfc.exe'
            $startInfo.Arguments = '/verifyfile="' + $filePath + '"'
            $startInfo.UseShellExecute = $false
            $startInfo.CreateNoWindow = $true
            $startInfo.RedirectStandardOutput = $true
            $startInfo.RedirectStandardError = $true
            $startInfo.StandardOutputEncoding = [Text.Encoding]::Unicode
            $startInfo.StandardErrorEncoding = [Text.Encoding]::Unicode
            $process = New-Object Diagnostics.Process
            try {
                $process.StartInfo = $startInfo
                if (-not $process.Start()) { throw 'SFC process did not start.' }
                $stdout = $process.StandardOutput.ReadToEndAsync()
                $stderr = $process.StandardError.ReadToEndAsync()
                $process.WaitForExit()
                ($stdout.Result + $stderr.Result) | Out-File -LiteralPath (Join-Path $resultDirectory $logName) -Encoding utf8
                [ordered]@{ file = $fileName; exit_code = $process.ExitCode; log = $logName }
            } finally { $process.Dispose() }
        }
        $report.core_files += $entry
    }
}
$report['com_system_permissions'] = @()
$report['com_service_token_check_requested'] = [bool]$CheckServiceToken
$report['com_permission_scope'] = 'Machine-wide descriptors returned by COM; raw AccessCheck mask 3 for the current TermService process token. Does not query LSM process-wide security or the authenticated RPC caller token.'
Add-Type -Path (Join-Path $PSScriptRoot 'ComSecurityDiagnostics.cs') -ErrorAction Stop
$termServiceProcessId = 0
$report['com_token_source'] = Read-Diagnostic {
    $service = Get-CimInstance Win32_Service -Filter "Name='TermService'" -ErrorAction Stop
    if ($service.ProcessId -eq 0) { throw 'TermService has no running process.' }
    $script:termServiceProcessId = [uint32]$service.ProcessId
    [ordered]@{ service = 'TermService'; process_id = $service.ProcessId }
}
foreach ($type in 0..3) {
    $entry = Read-Diagnostic { [ComSecurityDiagnostics]::Read($type, $(if ($CheckServiceToken) { $termServiceProcessId } else { 0 })) }
    $entry['descriptor_type'] = $type
    $report.com_system_permissions += $entry
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding utf8
Write-Host ('Relatorio: ' + $reportPath)
