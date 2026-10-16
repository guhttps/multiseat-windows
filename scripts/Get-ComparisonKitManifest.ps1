[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$Path
)

# Read-only validation. Hashes establish package consistency, not publisher identity.
$ErrorActionPreference = 'Stop'
$allowedFiles = @(
    'README.md',
    'build/child-session/Release/ms-child-session-probe.exe',
    'build/child-session/Release/ms-child-session-test.exe',
    'build/child-session/Release/ms-session-probe.exe',
    'scripts/Invoke-ChildSessionTest.ps1'
)
$allowedDirectories = @('build/', 'build/child-session/', 'build/child-session/Release/', 'scripts/')
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead((Get-Item -LiteralPath $Path -ErrorAction Stop).FullName)
try {
    if ($archive.Entries.Count -gt 32) { throw 'Unexpected comparison kit entry count.' }
    $entries = @{}
    foreach ($entry in $archive.Entries) {
        $name = $entry.FullName.Replace('\', '/')
        if ($entries.ContainsKey($name) -or ($name -notin $allowedFiles -and
            $name -notin $allowedDirectories -and $name -ne 'manifest.json')) {
            throw 'Unexpected or duplicate comparison kit entry.'
        }
        if ($entry.Length -gt 32MB) { throw 'Comparison kit entry exceeds the size limit.' }
        $entries[$name] = $entry
    }
    $manifestEntry = $entries['manifest.json']
    if (-not $manifestEntry -or $manifestEntry.Length -gt 64KB) { throw 'Missing or oversized comparison kit manifest.' }
    $reader = [IO.StreamReader]::new($manifestEntry.Open())
    try { $manifest = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
    if (@($manifest.files).Count -ne $allowedFiles.Count) { throw 'Unexpected comparison kit manifest.' }
    $seen = @{}
    foreach ($file in $manifest.files) {
        $name = ([string]$file.path).Replace('\', '/')
        if ($name -notin $allowedFiles -or $seen.ContainsKey($name) -or $file.sha256 -notmatch '^[a-fA-F0-9]{64}$') {
            throw 'Unexpected comparison kit manifest file.'
        }
        $seen[$name] = $true
        $entry = $entries[$name]
        if (-not $entry) { throw ('Missing comparison kit entry: ' + $name) }
        $stream = $entry.Open()
        $hasher = [Security.Cryptography.SHA256]::Create()
        try { $hash = [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-', '') }
        finally { $stream.Dispose(); $hasher.Dispose() }
        if ($hash -ne $file.sha256) { throw ('Archive hash mismatch: ' + $name) }
    }
    $manifest
} finally { $archive.Dispose() }
