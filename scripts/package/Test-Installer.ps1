[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$InstallerPath,
    [Parameter(Mandatory)][string]$Directory
)

# Integration test on an ephemeral hosted runner only. Never install on a personal PC.
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted' -or -not $env:RUNNER_TEMP) {
    throw 'Installer integration checks require an ephemeral GitHub-hosted Windows runner.'
}
$root = & (Join-Path $PSScriptRoot 'Assert-ContainedDirectory.ps1') -RootPath $env:RUNNER_TEMP -TargetPath $Directory -RequireNew
$installer = Get-Item -LiteralPath $InstallerPath -Force -ErrorAction Stop
if ($installer.PSIsContainer -or $installer.Attributes -band [IO.FileAttributes]::ReparsePoint -or
    $installer.Name -notmatch '^MultiseatPrototype-[0-9.]+-experimental\.[0-9]+-windows-x64-installer\.exe$') {
    throw 'Expected the built experimental installer.'
}
$release = Get-Content -LiteralPath (Join-Path $installer.DirectoryName 'release-manifest.json') -Raw | ConvertFrom-Json
$expected = @($release.files | Where-Object name -EQ $installer.Name)
if ($expected.Count -ne 1 -or (Get-FileHash -LiteralPath $installer.FullName -Algorithm SHA256).Hash -ne $expected[0].sha256) {
    throw 'Installer does not match the release manifest.'
}
$registration = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{C1E337A2-08A4-4E21-A79B-7823C25E21EC}_is1'
function Test-Registration([Microsoft.Win32.RegistryHive]$Hive) {
    foreach ($view in @([Microsoft.Win32.RegistryView]::Registry32, [Microsoft.Win32.RegistryView]::Registry64)) {
        $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey($Hive, $view)
        try {
            $key = $base.OpenSubKey($registration)
            try { if ($key) { return $true } }
            finally { if ($key) { $key.Dispose() } }
        } finally { $base.Dispose() }
    }
    return $false
}
if ((Test-Registration CurrentUser) -or (Test-Registration LocalMachine)) { throw 'An existing installation must not be changed by the test.' }
$settingsDirectory = Join-Path $env:LOCALAPPDATA 'MultiseatPrototype'
$settingsPath = Join-Path $settingsDirectory 'settings.json'
$createdSettings = $false
$createdSettingsDirectory = $false
$settingsHash = $null
$installStarted = $false
$uninstalled = $false
$testComplete = $false
New-Item -ItemType Directory -Path $root | Out-Null
$installRoot = Join-Path $root 'application'
try {
    if (-not (Test-Path -LiteralPath $settingsDirectory)) {
        New-Item -ItemType Directory -Path $settingsDirectory | Out-Null
        $createdSettingsDirectory = $true
    }
    if ((Get-Item -LiteralPath $settingsDirectory -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Redirected settings directory refused.' }
    if (-not (Test-Path -LiteralPath $settingsPath)) {
        '{"installer_test_fixture":true}' | Set-Content -LiteralPath $settingsPath -Encoding UTF8
        $createdSettings = $true
    }
    if ((Get-Item -LiteralPath $settingsPath -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Redirected settings file refused.' }
    $settingsHash = (Get-FileHash -LiteralPath $settingsPath -Algorithm SHA256).Hash
    $arguments = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /NOICONS /DIR="' + $installRoot + '"'
    $installStarted = $true
    $process = Start-Process -FilePath $installer.FullName -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    try {
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) { throw 'Silent per-user installation failed.' }
    } finally { $process.Dispose() }
    if (-not (Test-Registration CurrentUser) -or (Test-Registration LocalMachine)) { throw 'Installer registration must be scoped to the current user.' }
    $manifest = Get-Content -LiteralPath (Join-Path $installRoot 'manifest.json') -Raw | ConvertFrom-Json
    foreach ($file in $manifest.files) {
        if ($file.name -notmatch '^[A-Za-z0-9._-]+$' -or (Get-FileHash -LiteralPath (Join-Path $installRoot $file.name) -Algorithm SHA256).Hash -ne $file.sha256) {
            throw 'Installed payload differs from the package manifest.'
        }
    }
    $smoke = Start-Process -FilePath (Join-Path $installRoot 'MultiseatPrototype.exe') -ArgumentList '--smoke' -WindowStyle Hidden -PassThru
    try {
        if (-not $smoke.WaitForExit(30000)) {
            $smoke.Kill()
            throw 'Installed control panel smoke check timed out.'
        }
        if ($smoke.ExitCode -ne 0) { throw 'Installed control panel smoke check failed.' }
    } finally { $smoke.Dispose() }
    if ((Get-FileHash -LiteralPath $settingsPath -Algorithm SHA256).Hash -ne $settingsHash) { throw 'Installation changed existing settings.' }
    $testComplete = $true
} finally {
    if ($installStarted -and (Test-Path -LiteralPath (Join-Path $installRoot 'unins000.exe') -PathType Leaf)) {
        # Inno's uninstaller may continue in a temporary child process; include descendants.
        $process = Start-Process -FilePath (Join-Path $installRoot 'unins000.exe') -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART' -WindowStyle Hidden -Wait -PassThru
        try {
            $process.WaitForExit()
            if ($process.ExitCode -ne 0) { throw 'Test uninstallation failed.' }
            $uninstalled = $true
        } finally { $process.Dispose() }
    }
    if ($settingsHash -and (-not (Test-Path -LiteralPath $settingsPath -PathType Leaf) -or
        (Get-FileHash -LiteralPath $settingsPath -Algorithm SHA256).Hash -ne $settingsHash)) {
        throw 'Installer or uninstaller changed the preserved settings.'
    }
    # Remove only this test's exact settings fixture, never an existing configuration.
    if ($createdSettings -and $settingsHash) { Remove-Item -LiteralPath $settingsPath -Force }
    if ($createdSettingsDirectory -and -not @(Get-ChildItem -LiteralPath $settingsDirectory -Force).Count) {
        Remove-Item -LiteralPath $settingsDirectory -Force
    }
}
if (-not $testComplete -or -not $uninstalled -or (Test-Registration CurrentUser) -or (Test-Registration LocalMachine)) {
    throw 'Installer integration checks did not complete cleanly.'
}
if ((Test-Path -LiteralPath $installRoot) -and @(Get-ChildItem -LiteralPath $installRoot -Force).Count) {
    throw 'The uninstaller left installed files in the test directory.'
}
Write-Output 'PASS: per-user installation, installed control panel startup, payload hashes, preserved settings, uninstallation and registry cleanup.'
exit 0
