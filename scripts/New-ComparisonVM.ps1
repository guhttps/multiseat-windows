[CmdletBinding()]
param(
    [switch]$Elevated,
    [ValidatePattern('^[0-9]{8}-[0-9]{6}-[a-f0-9]{8}$')]
    [string]$RunId
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vmName = 'Multiseat-Comparison'
$storageRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build/hyper-v/Multiseat-Comparison'))
$allowedRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build')) + [IO.Path]::DirectorySeparatorChar
if (-not $storageRoot.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'VM storage must remain inside the project build directory.'
}
if (-not $RunId) {
    $RunId = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
}
$reportDirectory = Join-Path $projectRoot ('docs/lab/results/vm-setup-' + $RunId)
New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null
$reportPath = Join-Path $reportDirectory 'summary.json'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
try { $admin = ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) }
finally { $identity.Dispose() }

if (-not $admin) {
    if ($Elevated) { throw 'The process did not receive an administrator token.' }
    $powershell = Join-Path $env:windir 'System32/WindowsPowerShell/v1.0/powershell.exe'
    $arguments = '-NoProfile -File "' + $PSCommandPath + '" -Elevated -RunId ' + $RunId
    try {
        $process = Start-Process -FilePath $powershell -Verb RunAs -WindowStyle Hidden -ArgumentList $arguments -Wait -PassThru
    } catch {
        throw 'UAC elevation did not complete. This launcher did not start VM creation.'
    }
    if (-not (Test-Path -LiteralPath $reportPath -PathType Leaf)) {
        throw ('VM setup produced no report; elevated exit code: ' + $process.ExitCode)
    }
    Get-Content -LiteralPath $reportPath -Raw -Encoding UTF8 | Write-Output
    exit $process.ExitCode
}

$report = [ordered]@{
    administrator = $admin
    vm_name = $vmName
    storage_path = $storageRoot
    created = $false
    configured = $false
    started = $false
    iso_attached = $false
    network_switch = $null
    vm_id = $null
    configuration = $null
    error = $null
}
$exitCode = 1
try {
    Import-Module Hyper-V -ErrorAction Stop
    if ((Get-Service vmms).Status -ne 'Running') { throw 'Hyper-V management service is not running.' }
    # Enumerate successfully before interpreting absence; an access failure must not mean no VM.
    $existing = @(Get-VM -ErrorAction Stop | Where-Object Name -EQ $vmName)
    if ($existing.Count -ne 0) { throw 'A VM with this name already exists; it was not modified.' }
    if (Test-Path -LiteralPath $storageRoot) { throw 'VM storage already exists; it was not overwritten.' }
    $defaultSwitch = @(Get-VMSwitch -ErrorAction Stop | Where-Object {
        $_.SwitchType -eq 'Internal' -and $_.Name -in @('Default Switch', ('Comutador Padr' + [char]0x00e3 + 'o'))
    })
    if ($defaultSwitch.Count -gt 1) { throw 'More than one default switch candidate was found.' }
    $create = @{
        Name = $vmName
        Generation = 2
        MemoryStartupBytes = 8GB
        Path = $storageRoot
        NewVHDPath = Join-Path $storageRoot 'system.vhdx'
        NewVHDSizeBytes = 80GB
        ErrorAction = 'Stop'
    }
    if ($defaultSwitch.Count -eq 1) {
        $create.SwitchName = $defaultSwitch[0].Name
        $report.network_switch = $defaultSwitch[0].Name
    }
    New-Item -ItemType Directory -Path $storageRoot | Out-Null
    $vm = New-VM @create
    $report.created = $true
    $report.vm_id = $vm.Id.ToString()
    Set-VM -VM $vm -ProcessorCount 4 -AutomaticStartAction Nothing -AutomaticStopAction Save
    Set-VMMemory -VM $vm -DynamicMemoryEnabled $false
    Set-VMFirmware -VM $vm -EnableSecureBoot On -SecureBootTemplate MicrosoftWindows
    Set-VMKeyProtector -VM $vm -NewLocalKey
    Enable-VMTPM -VM $vm
    $dvd = Add-VMDvdDrive -VM $vm -Passthru
    Set-VMFirmware -VM $vm -FirstBootDevice $dvd
    $memory = Get-VMMemory -VM $vm
    $processor = Get-VMProcessor -VM $vm
    $firmware = Get-VMFirmware -VM $vm
    $security = Get-VMSecurity -VM $vm
    $disk = Get-VHD -Path $create.NewVHDPath
    $state = (Get-VM -Id $vm.Id).State.ToString()
    $report.configuration = [ordered]@{
        generation = $vm.Generation
        memory_bytes = $memory.Startup
        cpu_count = $processor.Count
        secure_boot = $firmware.SecureBoot.ToString()
        tpm_enabled = $security.TpmEnabled
        disk_type = $disk.VhdType.ToString()
        disk_capacity_bytes = $disk.Size
        state = $state
    }
    if ($state -ne 'Off' -or $vm.Generation -ne 2 -or $memory.Startup -ne 8GB -or
        $processor.Count -ne 4 -or $firmware.SecureBoot.ToString() -ne 'On' -or
        -not $security.TpmEnabled -or $disk.VhdType.ToString() -ne 'Dynamic' -or $disk.Size -ne 80GB) {
        throw 'Created VM configuration does not match the comparison plan.'
    }
    $report.configured = $true
    $exitCode = 0
} catch {
    # Preserve newly created assets for inspection; never delete or alter another VM.
    $report.error = $_.Exception.Message
} finally {
    $report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding UTF8
}
Write-Output ('VM setup report: ' + $reportPath)
exit $exitCode
