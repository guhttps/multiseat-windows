[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$IsoPath,
    [Parameter(Mandatory)]
    [ValidatePattern('^[a-fA-F0-9]{64}$')]
    [string]$ExpectedSha256,
    [switch]$Elevated,
    [ValidatePattern('^[0-9]{8}-[0-9]{6}-[a-f0-9]{8}$')]
    [string]$RunId
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$iso = Get-Item -LiteralPath $IsoPath -ErrorAction Stop
if ($iso.PSIsContainer -or $iso.Extension -ne '.iso' -or $iso.FullName.Contains('"')) {
    throw 'A local ISO file is required.'
}
$IsoPath = $iso.FullName
if (-not $RunId) {
    $RunId = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
}
$resultsRoot = Join-Path $projectRoot 'docs/lab/results'
$reportDirectory = Join-Path $resultsRoot ('vm-installer-' + $RunId)
New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null
$reportPath = Join-Path $reportDirectory 'summary.json'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
try { $admin = ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) }
finally { $identity.Dispose() }
if (-not $admin) {
    if ($Elevated) { throw 'The process did not receive an administrator token.' }
    $powershell = Join-Path $env:windir 'System32/WindowsPowerShell/v1.0/powershell.exe'
    # -File receives literal arguments, not a generated PowerShell expression.
    $arguments = '-NoProfile -File "' + $PSCommandPath + '" -IsoPath "' + $IsoPath +
        '" -ExpectedSha256 ' + $ExpectedSha256 + ' -Elevated -RunId ' + $RunId
    try {
        $process = Start-Process -FilePath $powershell -Verb RunAs -WindowStyle Hidden -ArgumentList $arguments -PassThru
    } catch {
        throw 'UAC elevation did not complete. This launcher did not attach installation media.'
    }
    # Start-Process -Wait also waits for descendant VMConnect. Wait only for the helper.
    $process.WaitForExit()
    if (-not (Test-Path -LiteralPath $reportPath -PathType Leaf)) {
        throw ('Installer preparation produced no report; elevated exit code: ' + $process.ExitCode)
    }
    Get-Content -LiteralPath $reportPath -Raw -Encoding UTF8 | Write-Output
    exit $process.ExitCode
}

$vmName = 'Multiseat-Comparison'
$storageRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build/hyper-v/Multiseat-Comparison'))
$diskPath = Join-Path $storageRoot 'system.vhdx'
$report = [ordered]@{
    administrator = $admin
    vm_name = $vmName
    iso_path = $IsoPath
    iso_sha256 = $null
    expected_sha256 = $ExpectedSha256
    hash_verified = $false
    iso_attached = $false
    viewer_opened = $false
    vm_state = $null
    started = $false
    error = $null
}
$exitCode = 1
try {
    Import-Module Hyper-V -ErrorAction Stop
    $vm = Get-VM -Name $vmName -ErrorAction Stop
    if ($vm.State.ToString() -ne 'Off') { throw 'The comparison VM must be off; it was not stopped or changed.' }
    $actualStorage = [IO.Path]::GetFullPath($vm.Path).TrimEnd('\')
    if ($actualStorage -ne $storageRoot -and
        -not $actualStorage.StartsWith($storageRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The VM storage does not belong to this project.'
    }
    $owned = $false
    foreach ($setupDirectory in Get-ChildItem -LiteralPath $resultsRoot -Directory -Filter 'vm-setup-*') {
        $setupPath = Join-Path $setupDirectory.FullName 'summary.json'
        if (Test-Path -LiteralPath $setupPath -PathType Leaf) {
            $setup = Get-Content -LiteralPath $setupPath -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($setup.configured -and $setup.vm_name -eq $vmName -and $setup.vm_id -eq $vm.Id.ToString()) {
                $owned = $true
                break
            }
        }
    }
    if (-not $owned) { throw 'No successful creation report matches this VM ID.' }
    $disks = @(Get-VMHardDiskDrive -VM $vm)
    if ($disks.Count -ne 1 -or -not $disks[0].Path -or
        [IO.Path]::GetFullPath($disks[0].Path) -ne $diskPath) {
        throw 'Expected only the project virtual system disk.'
    }
    $dvd = @(Get-VMDvdDrive -VM $vm)
    if ($dvd.Count -ne 1) { throw 'Expected exactly one virtual DVD drive.' }
    if ($dvd[0].Path -and $dvd[0].Path -ne $IsoPath) { throw 'Different DVD media is already attached; it was not replaced.' }
    $hash = (Get-FileHash -LiteralPath $IsoPath -Algorithm SHA256).Hash
    $report.iso_sha256 = $hash
    if ($hash -ne $ExpectedSha256) { throw 'ISO SHA-256 does not match the expected download.' }
    $report.hash_verified = $true
    Set-VMDvdDrive -VMDvdDrive $dvd[0] -Path $IsoPath
    $attached = Get-VMDvdDrive -VM $vm
    if ($attached.Path -ne $IsoPath) { throw 'DVD media readback did not match the ISO.' }
    $report.iso_attached = $true
    Set-VMFirmware -VM $vm -FirstBootDevice $attached
    $vmconnect = Join-Path $env:windir 'System32/vmconnect.exe'
    # Interactive viewer: the user controls boot and the Windows installation wizard.
    Start-Process -FilePath $vmconnect -ArgumentList @('localhost', $vmName) -WindowStyle Normal | Out-Null
    $report.viewer_opened = $true
    $report.vm_state = (Get-VM -Id $vm.Id).State.ToString()
    $exitCode = 0
} catch {
    $report.error = $_.Exception.Message
} finally {
    $report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $reportPath -Encoding UTF8
}
Write-Output ('Installer preparation report: ' + $reportPath)
exit $exitCode
