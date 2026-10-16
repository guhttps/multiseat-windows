[CmdletBinding()]
param()

# Offline regression fixtures only. No executable from a kit is ever launched.
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$fixtureRoot = Join-Path $projectRoot ('build/comparison-safety-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixtureRoot | Out-Null
$powershell = Join-Path $env:windir 'System32/WindowsPowerShell/v1.0/powershell.exe'
$lines = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Run-ComparisonGuest.cmd')
$commands = @($lines | Where-Object { $_ -match '^powershell.exe .* -Command "(.*)"$' } | ForEach-Object {
    [regex]::Match($_, ' -Command "(.*)"$').Groups[1].Value
})
if ($commands.Count -ne 5) { throw 'Unexpected guest launcher command layout.' }
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$paths = @('README.md', 'build/child-session/Release/ms-child-session-probe.exe',
    'build/child-session/Release/ms-child-session-test.exe', 'build/child-session/Release/ms-session-probe.exe',
    'scripts/Invoke-ChildSessionTest.ps1')

function New-Fixture {
    param([string]$Name, [string]$ExtraEntry, [switch]$CorruptContent)
    $directory = Join-Path $fixtureRoot $Name
    New-Item -ItemType Directory -Path $directory | Out-Null
    $archivePath = Join-Path $directory 'comparison-kit.zip'
    $archive = [IO.Compression.ZipFile]::Open($archivePath, [IO.Compression.ZipArchiveMode]::Create)
    $files = @()
    try {
        foreach ($path in $paths) {
            $content = [Text.Encoding]::UTF8.GetBytes('inert fixture: ' + $path)
            $hasher = [Security.Cryptography.SHA256]::Create()
            try { $hash = [BitConverter]::ToString($hasher.ComputeHash($content)).Replace('-', '') }
            finally { $hasher.Dispose() }
            $files += @{ path = $path; sha256 = $hash }
            $entry = $archive.CreateEntry($path)
            $stream = $entry.Open()
            try {
                if ($CorruptContent -and $path -eq 'README.md') { $content = [Text.Encoding]::UTF8.GetBytes('changed fixture') }
                $stream.Write($content, 0, $content.Length)
            } finally { $stream.Dispose() }
        }
        $entry = $archive.CreateEntry('manifest.json')
        $writer = [IO.StreamWriter]::new($entry.Open())
        try { $writer.Write((@{ files = $files } | ConvertTo-Json -Depth 4)) } finally { $writer.Dispose() }
        if ($ExtraEntry) {
            $writer = [IO.StreamWriter]::new($archive.CreateEntry($ExtraEntry).Open())
            try { $writer.Write('untrusted fixture entry') } finally { $writer.Dispose() }
        }
    } finally { $archive.Dispose() }
    (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash |
        Set-Content -LiteralPath (Join-Path $directory 'comparison-kit.sha256') -Encoding ASCII
    $directory
}

function Invoke-GuestCommandFixture {
    param([string]$Directory, [int]$CommandIndex, [int]$ExpectedExit)
    $literalDirectory = $Directory.Replace("'", "''")
    $code = $commands[$CommandIndex].Replace('C:\Users\Public', $literalDirectory)
    $commandPath = Join-Path $Directory ('command-' + $CommandIndex + '.ps1')
    $code | Set-Content -LiteralPath $commandPath -Encoding UTF8
    $output = & $powershell -NoProfile -File $commandPath 2>&1
    $codeExit = $LASTEXITCODE
    $output | Out-File -LiteralPath ($commandPath + '.log') -Encoding utf8
    if ($codeExit -ne $ExpectedExit) { throw ('Fixture returned ' + $codeExit + ', expected ' + $ExpectedExit + ': ' + $commandPath) }
    $output
}

function Assert-HostRejects {
    param([string]$Directory)
    $rejected = $false
    try { & (Join-Path $PSScriptRoot 'Get-ComparisonKitManifest.ps1') -Path (Join-Path $Directory 'comparison-kit.zip') | Out-Null }
    catch { $rejected = $true }
    if (-not $rejected) { throw ('Unsafe archive accepted: ' + $Directory) }
}

$valid = New-Fixture -Name 'valid'
& (Join-Path $PSScriptRoot 'Get-ComparisonKitManifest.ps1') -Path (Join-Path $valid 'comparison-kit.zip') | Out-Null
Invoke-GuestCommandFixture -Directory $valid -CommandIndex 1 -ExpectedExit 0 | Out-Null
$marker = Join-Path $valid 'MultiseatComparison/preserve.txt'
'keep existing files' | Set-Content -LiteralPath $marker
Invoke-GuestCommandFixture -Directory $valid -CommandIndex 1 -ExpectedExit 0 | Out-Null
if ((Get-Content -LiteralPath $marker -Raw).Trim() -ne 'keep existing files') { throw 'Existing data changed.' }
$readme = Join-Path $valid 'MultiseatComparison/README.md'
'modified by fixture' | Set-Content -LiteralPath $readme
Invoke-GuestCommandFixture -Directory $valid -CommandIndex 1 -ExpectedExit 2 | Out-Null
if ((Get-Content -LiteralPath $readme -Raw).Trim() -ne 'modified by fixture') { throw 'Mismatched data was overwritten.' }
Write-Output 'PASS: valid extraction, repeat launch and preservation of mismatched existing files.'

foreach ($extra in @('../escape.txt', 'readme.md', 'extra.exe')) {
    $unsafe = New-Fixture -Name ([Guid]::NewGuid().ToString('N')) -ExtraEntry $extra
    Assert-HostRejects -Directory $unsafe
    Invoke-GuestCommandFixture -Directory $unsafe -CommandIndex 1 -ExpectedExit 2 | Out-Null
    if (Test-Path -LiteralPath (Join-Path $unsafe 'MultiseatComparison')) { throw 'Archive was extracted before validation.' }
}
$corrupt = New-Fixture -Name 'corrupt' -CorruptContent
Assert-HostRejects -Directory $corrupt
Invoke-GuestCommandFixture -Directory $corrupt -CommandIndex 1 -ExpectedExit 2 | Out-Null
if (Test-Path -LiteralPath (Join-Path $corrupt 'MultiseatComparison')) { throw 'Corrupt archive was extracted.' }
Write-Output 'PASS: traversal, duplicate names, unexpected executables and corrupt content rejected before extraction.'

$wrongHash = New-Fixture -Name 'wrong-transfer-hash'
('0' * 64) | Set-Content -LiteralPath (Join-Path $wrongHash 'comparison-kit.sha256') -Encoding ASCII
Invoke-GuestCommandFixture -Directory $wrongHash -CommandIndex 1 -ExpectedExit 2 | Out-Null
if (Test-Path -LiteralPath (Join-Path $wrongHash 'MultiseatComparison')) { throw 'Mismatched transfer was extracted.' }

$redirected = New-Fixture -Name 'redirected'
$redirectTarget = Join-Path $fixtureRoot 'redirect-target'
New-Item -ItemType Directory -Path $redirectTarget | Out-Null
New-Item -ItemType Junction -Path (Join-Path $redirected 'MultiseatComparison') -Target $redirectTarget | Out-Null
Invoke-GuestCommandFixture -Directory $redirected -CommandIndex 1 -ExpectedExit 2 | Out-Null
if (@(Get-ChildItem -LiteralPath $redirectTarget -Force).Count) { throw 'Redirect target was modified.' }
Write-Output 'PASS: transferred hash mismatch and junction destination rejected without writes.'

$logRoot = Join-Path $valid 'MultiseatComparison/docs/lab/results/guest-attempt-fixture'
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
'previous attempt fixture' | Set-Content -LiteralPath (Join-Path $logRoot 'guest-child-default.log')
$previousOutput = Invoke-GuestCommandFixture -Directory $valid -CommandIndex 2 -ExpectedExit 20
if (($previousOutput -join "`n") -notmatch 'previous attempt fixture') { throw 'Previous result was not displayed.' }
Write-Output 'PASS: previous result displayed without running a kit executable.'
Write-Output ('Fixture reports remain local: ' + $fixtureRoot)
# Expected nonzero fixture exits must not become the test runner's process exit.
# An assertion throws before reaching this success return.
exit 0
