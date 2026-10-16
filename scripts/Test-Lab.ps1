[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$runId = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$buildDirectory = Join-Path $projectRoot ('build/lab-' + $runId)
$resultDirectory = Join-Path $projectRoot ('docs/lab/results/' + $runId)
New-Item -ItemType Directory -Path $resultDirectory -Force | Out-Null
$checks = [System.Collections.Generic.List[object]]::new()
$reportPath = Join-Path $resultDirectory 'summary.json'
$started = [DateTimeOffset]::UtcNow.ToString('o')

function Invoke-LabCheck {
    param([string]$Name, [string]$Executable, [string[]]$Arguments)
    if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
        throw ('Executavel nao encontrado para ' + $Name + ': ' + $Executable)
    }
    $logPath = Join-Path $resultDirectory ($Name + '.log')
    Write-Host ('Executando: ' + $Name)
    # Preserve native stderr as log output; evaluate success by its exit code.
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $global:LASTEXITCODE = $null
        $output = & $Executable @Arguments 2>&1
        $commandExit = $global:LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    $output | Out-File -LiteralPath $logPath -Encoding utf8
    $checks.Add([pscustomobject]@{
        name = $Name
        exit_code = $commandExit
        log = $Name + '.log'
    })
    if ($null -eq $commandExit -or $commandExit -ne 0) {
        throw ('Falha em ' + $Name + '. Consulte ' + $logPath)
    }
}

Push-Location $projectRoot
try {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'Visual Studio Installer/vswhere nao encontrado.'
    }
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $installation) {
        throw 'Instalacao com ferramentas C++ nao encontrada.'
    }
    $developerShell = Join-Path $installation 'Common7/Tools/Launch-VsDevShell.ps1'
    & $developerShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $cmake = (Get-Command cmake -ErrorAction Stop).Source
    Invoke-LabCheck -Name 'configure' -Executable $cmake -Arguments @('-S', $projectRoot, '-B', $buildDirectory, '-A', 'x64')
    Invoke-LabCheck -Name 'build' -Executable $cmake -Arguments @('--build', $buildDirectory, '--config', 'Release')
    $release = Join-Path $buildDirectory 'Release'
    Invoke-LabCheck -Name 'environment' -Executable (Join-Path $release 'ms-diagnostic.exe') -Arguments @()
    Invoke-LabCheck -Name 'input-registration' -Executable (Join-Path $release 'ms-keyboard-probe.exe') -Arguments @('--smoke')
    Invoke-LabCheck -Name 'desktop-context' -Executable (Join-Path $release 'ms-desktop-probe.exe') -Arguments @()
    Invoke-LabCheck -Name 'sessions-displays' -Executable (Join-Path $release 'ms-session-probe.exe') -Arguments @()
    $report = [ordered]@{
        schema_version = 1
        started_utc = $started
        finished_utc = [DateTimeOffset]::UtcNow.ToString('o')
        status = 'passed'
        build = $buildDirectory
        checks = @($checks.ToArray())
        limitations = @('Physical keyboard and mouse events are not retested by this script.', 'Passing does not demonstrate simultaneous independent stations or League of Legends compatibility.')
    }
    $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding utf8
    Write-Host ('PASS: build limpo e quatro diagnosticos. Relatorio local: ' + $reportPath)
} catch {
    $failure = [ordered]@{
        schema_version = 1
        started_utc = $started
        finished_utc = [DateTimeOffset]::UtcNow.ToString('o')
        status = 'failed'
        error = $_.Exception.Message
        checks = @($checks.ToArray())
    }
    $failure | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding utf8
    throw
} finally {
    Pop-Location
}
