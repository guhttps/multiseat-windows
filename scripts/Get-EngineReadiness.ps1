[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$runId = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$resultDirectory = Join-Path $projectRoot ('docs/lab/results/readiness-' + $runId)
New-Item -ItemType Directory -Path $resultDirectory -Force | Out-Null

function Read-Observation {
    param([scriptblock]$Query)
    try {
        $value = & $Query
        if ($null -eq $value) {
            return [ordered]@{ status = 'unknown'; reason = 'No observation returned.' }
        }
        return [ordered]@{ status = 'observed'; value = $value }
    } catch {
        # Do not log full exceptions, which can contain local paths or host names.
        return [ordered]@{ status = 'unknown'; error_category = [string]$_.CategoryInfo.Category }
    }
}

$observations = [ordered]@{
    windows = Read-Observation {
        $os = Get-CimInstance -ClassName Win32_OperatingSystem
        [ordered]@{ edition = $os.Caption; version = $os.Version; build = $os.BuildNumber }
    }
    device_guard = Read-Observation {
        $guard = Get-CimInstance -Namespace root/Microsoft/Windows/DeviceGuard -ClassName Win32_DeviceGuard
        if ($null -eq $guard -or $null -eq $guard.VirtualizationBasedSecurityStatus -or
            $null -eq $guard.SecurityServicesConfigured -or $null -eq $guard.SecurityServicesRunning) {
            throw 'Device Guard observation incomplete.'
        }
        [ordered]@{
            vbs_status = [int]$guard.VirtualizationBasedSecurityStatus
            services_configured = @($guard.SecurityServicesConfigured)
            services_running = @($guard.SecurityServicesRunning)
            memory_integrity_running = (@($guard.SecurityServicesRunning) -contains 2)
        }
    }
    secure_boot = Read-Observation { [bool](Confirm-SecureBootUEFI) }
    tpm = Read-Observation {
        $tpm = Get-CimInstance -Namespace root/CIMV2/Security/MicrosoftTpm -ClassName Win32_Tpm
        if ($null -ne $tpm) {
            [ordered]@{
                specification = $tpm.SpecVersion
                enabled_initial = [bool]$tpm.IsEnabled_InitialValue
                activated_initial = [bool]$tpm.IsActivated_InitialValue
            }
        }
    }
    driver_headers = Read-Observation {
        $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/Include'
        $versions = @()
        if (Test-Path -LiteralPath $kits) {
            $versions = @(Get-ChildItem -LiteralPath $kits -Directory | Where-Object {
                Test-Path -LiteralPath (Join-Path $_.FullName 'km/ntddk.h') -PathType Leaf
            } | Select-Object -ExpandProperty Name)
        }
        [ordered]@{ km_header_versions = $versions; complete_wdk_verified = $false }
    }
    vanguard_service = Read-Observation {
        $service = Get-Service -Name vgc -ErrorAction SilentlyContinue
        [ordered]@{
            present = ($null -ne $service)
            state = $(if ($null -ne $service) { [string]$service.Status } else { 'not_present' })
        }
    }
}

$report = [ordered]@{
    schema_version = 1
    captured_utc = [DateTimeOffset]::UtcNow.ToString('o')
    observations = $observations
    decision = 'native_backend_unproven'
    limitations = @(
        'Read-only host queries; no configuration, account, driver or boot changes.',
        'Unknown observations are not interpreted as disabled features.',
        'Service state does not establish Vanguard mode, health or game compatibility.',
        'Installed headers do not establish a supported mechanism for creating physical seats.'
    )
}
$reportPath = Join-Path $resultDirectory 'readiness.json'
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding utf8
foreach ($name in $observations.Keys) {
    Write-Host ($name + ': ' + $observations[$name].status)
}
Write-Host ('Relatorio local (ignorado pelo Git): docs/lab/results/readiness-' + $runId + '/readiness.json')
Write-Host 'Motor nativo ainda nao demonstrado. Este relatorio nao aprova uma arquitetura.'
