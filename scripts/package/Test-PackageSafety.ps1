[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PayloadDirectory,
    [Parameter(Mandatory)][string]$ArtifactDirectory
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$validator = Join-Path $PSScriptRoot 'Test-ReleasePayload.ps1'
& $validator -Directory $PayloadDirectory
$fixtureRoot = Join-Path $projectRoot ('build/package-safety-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixtureRoot | Out-Null
function Copy-PayloadFixture([string]$Name) {
    $directory = Join-Path $fixtureRoot $Name
    New-Item -ItemType Directory -Path $directory | Out-Null
    Get-ChildItem -LiteralPath $PayloadDirectory -File -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $directory $_.Name)
    }
    return $directory
}
function Assert-PayloadRejected([string]$Directory, [string]$ExpectedReason) {
    $rejected = $false
    try { & $validator -Directory $Directory | Out-Null }
    catch {
        if ($_.Exception.Message -notmatch $ExpectedReason) { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw 'An unsafe package fixture was accepted.' }
}
$extra = Copy-PayloadFixture 'extra'
'inert fixture' | Set-Content -LiteralPath (Join-Path $extra 'unexpected.log')
Assert-PayloadRejected $extra 'fixed file list'
$changed = Copy-PayloadFixture 'changed'
'modified inert notice' | Set-Content -LiteralPath (Join-Path $changed 'LEIA-ME.txt')
Assert-PayloadRejected $changed 'manifest hash mismatch'
$redirectTarget = Copy-PayloadFixture 'redirect-target'
$redirect = Join-Path $fixtureRoot 'redirect'
New-Item -ItemType Junction -Path $redirect -Target $redirectTarget | Out-Null
Assert-PayloadRejected $redirect 'must not redirect'
Write-Output 'PASS: extra artifact, changed payload hash and redirected payload rejected.'

$release = Get-Content -LiteralPath (Join-Path $ArtifactDirectory 'release-manifest.json') -Raw | ConvertFrom-Json
if ($release.channel -ne 'experimental' -or $release.code_signed -ne $false) { throw 'Unexpected release classification.' }
$portable = @($release.files | Where-Object name -Like '*-windows-x64.zip')
$installer = @($release.files | Where-Object name -Like '*-windows-x64-installer.exe')
if ($portable.Count -ne 1 -or $installer.Count -ne 1 -or @($release.files).Count -ne 2) { throw 'Unexpected release asset list.' }
foreach ($file in $release.files) {
    if ($file.name -notmatch '^MultiseatPrototype-[0-9.]+-experimental\.[0-9]+-windows-x64(?:-installer\.exe|\.zip)$' -or
        (Get-FileHash -LiteralPath (Join-Path $ArtifactDirectory $file.name) -Algorithm SHA256).Hash -ne $file.sha256) {
        throw 'Release asset hash mismatch.'
    }
}
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead((Join-Path $ArtifactDirectory $portable[0].name))
$expectedNames = @(Get-ChildItem -LiteralPath $PayloadDirectory -File -Force | ForEach-Object Name)
try {
    if ($archive.Entries.Count -ne $expectedNames.Count) { throw 'Unexpected portable archive entries.' }
    $seen = @{}
    foreach ($entry in $archive.Entries) {
        if ($entry.FullName -notin $expectedNames -or $seen.ContainsKey($entry.FullName) -or
            $entry.LastWriteTime.DateTime -ne [DateTime]::new(2000, 1, 1)) {
            throw 'Unexpected archive path, duplicate or timestamp.'
        }
        $seen[$entry.FullName] = $true
        $stream = $entry.Open()
        $hasher = [Security.Cryptography.SHA256]::Create()
        try { $hash = [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-', '') }
        finally { $stream.Dispose(); $hasher.Dispose() }
        if ($hash -ne (Get-FileHash -LiteralPath (Join-Path $PayloadDirectory $entry.FullName) -Algorithm SHA256).Hash) {
            throw 'Portable archive differs from the validated payload.'
        }
    }
} finally { $archive.Dispose() }
$installerBytes = [IO.File]::ReadAllBytes((Join-Path $ArtifactDirectory $installer[0].name))
$manifestText = [Text.Encoding]::UTF8.GetString($installerBytes)
if ($manifestText -notmatch 'requestedExecutionLevel\s+level="asInvoker"') { throw 'Installer must request the current user token.' }
Write-Output 'PASS: release hashes, portable whitelist/content, fixed timestamps and asInvoker installer manifest.'
exit 0
