[CmdletBinding()]
param(
    [switch]$Elevated,
    [switch]$LauncherOnly,
    [ValidatePattern('^[0-9]{8}-[0-9]{6}-[a-f0-9]{8}$')]
    [string]$RunId
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vmName = 'Multiseat-Comparison'
$storageRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build/hyper-v/Multiseat-Comparison'))
$source = Join-Path $projectRoot 'build/comparison-kit.zip'
$launcherSource = Join-Path $PSScriptRoot 'Run-ComparisonGuest.cmd'
$hashSource = Join-Path $projectRoot 'build/comparison-kit.sha256'
$destination = 'C:\Users\Public\comparison-kit.zip'
if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw 'Comparison kit is missing.' }
if (-not (Test-Path -LiteralPath $launcherSource -PathType Leaf)) { throw 'Guest launcher is missing.' }
if (-not $RunId) {
    $RunId = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
}
$resultsRoot = Join-Path $projectRoot 'docs/lab/results'
$reportDirectory = Join-Path $resultsRoot ('vm-guest-preparation-' + $RunId)
New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null
$reportPath = Join-Path $reportDirectory 'summary.json'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
try { $admin = ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) }
finally { $identity.Dispose() }
if (-not $admin) {
    if ($Elevated) { throw 'The process did not receive an administrator token.' }
    $powershell = Join-Path $env:windir 'System32/WindowsPowerShell/v1.0/powershell.exe'
    $arguments = '-NoProfile -File "' + $PSCommandPath + '" -Elevated -RunId ' + $RunId
    if ($LauncherOnly) { $arguments += ' -LauncherOnly' }
    try {
        $process = Start-Process -FilePath $powershell -Verb RunAs -WindowStyle Hidden -ArgumentList $arguments -PassThru
    } catch { throw 'UAC elevation did not complete; preparation was not started by this launcher.' }
    $process.WaitForExit()
    if (-not (Test-Path -LiteralPath $reportPath -PathType Leaf)) { throw 'Elevated preparation produced no report.' }
    Get-Content -LiteralPath $reportPath -Raw -Encoding UTF8 | Write-Output
    exit $process.ExitCode
}

$report = [ordered]@{
    administrator = $admin
    vm_name = $vmName
    vm_state = $null
    checkpoint_name = 'BeforeChildSessionComparison-' + $RunId
    checkpoint_created = $false
    checkpoint_type = $null
    package_sha256 = $null
    package_manifest_verified = $false
    destination = $destination
    copied = $false
    launcher_only = [bool]$LauncherOnly
    launcher_copied = $false
    hash_file_copied = $false
    guest_service_initially_enabled = $null
    guest_service_restored = $null
    error = $null
    cleanup_error = $null
}
$guestService = $null
$serviceChanged = $false
$exitCode = 1
try {
    Import-Module Hyper-V -ErrorAction Stop
    $vm = Get-VM -Name $vmName -ErrorAction Stop
    $report.vm_state = $vm.State.ToString()
    if ($report.vm_state -ne 'Running') { throw 'The comparison VM must be running; it was not started or resumed.' }
    $vmPath = [IO.Path]::GetFullPath($vm.Path).TrimEnd('\')
    if ($vmPath -ne $storageRoot -and -not $vmPath.StartsWith($storageRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'VM storage does not belong to this project.'
    }
    $owned = $false
    foreach ($directory in Get-ChildItem -LiteralPath $resultsRoot -Directory -Filter 'vm-setup-*') {
        $setupPath = Join-Path $directory.FullName 'summary.json'
        if (Test-Path -LiteralPath $setupPath -PathType Leaf) {
            $setup = Get-Content -LiteralPath $setupPath -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($setup.configured -and $setup.vm_name -eq $vmName -and $setup.vm_id -eq $vm.Id.ToString()) { $owned = $true; break }
        }
    }
    if (-not $owned) { throw 'No successful creation report matches this VM ID.' }
    $disks = @(Get-VMHardDiskDrive -VM $vm)
    if ($disks.Count -ne 1 -or -not $disks[0].Path) { throw 'Expected one virtual disk, without physical disk passthrough.' }
    # Automatic checkpoints may have introduced AVHDX files; validate the entire parent chain.
    $diskPath = $disks[0].Path
    $baseFound = $false
    for ($depth = 0; $depth -lt 32; $depth++) {
        $diskPath = [IO.Path]::GetFullPath($diskPath)
        if (-not $diskPath.StartsWith($storageRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Virtual disk chain leaves the project storage.'
        }
        $disk = Get-VHD -Path $diskPath
        if (-not $disk.ParentPath) {
            $baseFound = $diskPath -eq (Join-Path $storageRoot 'system.vhdx') -and $disk.Size -eq 80GB
            break
        }
        $diskPath = $disk.ParentPath
    }
    if (-not $baseFound) { throw 'Expected the project system disk at the base of the virtual disk chain.' }
    $services = @(Get-VMIntegrationService -VM $vm | Where-Object {
        $_.Name -eq 'Guest Service Interface' -or $_.Name -match '^Interface.*Convidad'
    })
    if ($services.Count -ne 1) { throw 'Could not identify exactly one guest file-copy integration service.' }
    $guestService = $services[0]
    $report.guest_service_initially_enabled = [bool]$guestService.Enabled
    & (Join-Path $PSScriptRoot 'Get-ComparisonKitManifest.ps1') -Path $source | Out-Null
    $report.package_sha256 = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    $report.package_manifest_verified = $true
    $report.package_sha256 | Set-Content -LiteralPath $hashSource -Encoding ASCII
    if (-not $LauncherOnly) {
        $checkpoint = Checkpoint-VM -VM $vm -SnapshotName $report.checkpoint_name -Passthru
        $report.checkpoint_created = $true
        $report.checkpoint_type = $checkpoint.SnapshotType.ToString()
    } else {
        $report.checkpoint_name = $null
        if (-not @(Get-VMSnapshot -VM $vm | Where-Object Name -Like 'BeforeChildSessionComparison-*').Count) {
            throw 'Launcher transfer requires the previously prepared comparison checkpoint.'
        }
    }
    if (-not $guestService.Enabled) {
        $serviceChanged = $true
        Enable-VMIntegrationService -VMIntegrationService $guestService
        Start-Sleep -Seconds 2
    }
    if (-not $LauncherOnly) {
        Copy-VMFile -VM $vm -SourcePath $source -DestinationPath $destination -FileSource Host -CreateFullPath
        $report.copied = $true
    }
    # Only the fixed launcher and hash companion are replaceable; preserve the kit and guest results.
    Copy-VMFile -VM $vm -SourcePath $hashSource -DestinationPath 'C:\Users\Public\comparison-kit.sha256' -FileSource Host -CreateFullPath -Force
    $report.hash_file_copied = $true
    Copy-VMFile -VM $vm -SourcePath $launcherSource -DestinationPath 'C:\Users\Public\Executar-Teste.cmd' -FileSource Host -CreateFullPath -Force
    $report.launcher_copied = $true
    $exitCode = 0
} catch {
    $report.error = $_.Exception.Message
} finally {
    if ($guestService) {
        try {
            if ($serviceChanged) { Disable-VMIntegrationService -VMIntegrationService $guestService }
            $currentService = Get-VMIntegrationService -VM $vm -Name $guestService.Name
            $report.guest_service_restored = [bool]$currentService.Enabled -eq $report.guest_service_initially_enabled
            if (-not $report.guest_service_restored) { throw 'Guest file-copy service did not return to its initial setting.' }
        } catch { $report.cleanup_error = $_.Exception.Message; $exitCode = 1 }
    }
    $report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $reportPath -Encoding UTF8
}
Write-Output ('Guest preparation report: ' + $reportPath)
exit $exitCode
