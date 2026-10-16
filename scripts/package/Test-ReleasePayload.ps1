[CmdletBinding()]
param([Parameter(Mandatory)][string]$Directory)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $Directory).Path
$cursor = $root
while ($cursor) {
    if ((Get-Item -LiteralPath $cursor -Force -ErrorAction Stop).Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw 'Release payload directories must not redirect to another location.'
    }
    $cursor = [IO.Path]::GetDirectoryName($cursor)
}
$executables = @('MultiseatPrototype.exe', 'ms-diagnostic.exe', 'ms-session-probe.exe', 'ms-keyboard-probe.exe', 'ms-seat-demo.exe')
$allowed = $executables + @('LEIA-ME.txt', 'THIRD-PARTY-NOTICES.txt', 'INNO-SETUP-LICENSE.txt', 'manifest.json', 'SHA256SUMS.txt')
$items = @(Get-ChildItem -LiteralPath $root -Force)
if ($items.Count -ne $allowed.Count) { throw 'Release payload does not match the fixed file list.' }
$profilePattern = '(?i)[a-z]:[\\/]Users[\\/](?!Public\b|Default\b|<)[^\\/\s''"<>]+'
$sidPattern = '\bS-1-5-21(?:-\d+){3,4}\b'
foreach ($item in $items) {
    if ($item.PSIsContainer -or $item.Name -notin $allowed -or $item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw 'Unexpected file or redirection in release payload.'
    }
    if ($item.Length -gt 32MB) { throw 'Release payload file exceeds the size limit.' }
    $bytes = [IO.File]::ReadAllBytes($item.FullName)
    foreach ($text in @([Text.Encoding]::UTF8.GetString($bytes), [Text.Encoding]::Unicode.GetString($bytes))) {
        if ($text -match $profilePattern -or $text -match $sidPattern -or $text -match '-----BEGIN (?:RSA |EC |OPENSSH |ENCRYPTED )?PRIVATE KEY-----' -or
            $text -match '\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,})\b') {
            throw ('Potential private content in release payload: ' + $item.Name)
        }
    }
    if ($item.Name -in $executables) {
        if ($bytes.Length -lt 256 -or [BitConverter]::ToUInt16($bytes, 0) -ne 0x5a4d) { throw 'Expected a Windows executable.' }
        $pe = [BitConverter]::ToInt32($bytes, 60)
        if ($pe -lt 64 -or $pe + 256 -gt $bytes.Length -or [BitConverter]::ToUInt32($bytes, $pe) -ne 0x4550 -or
            [BitConverter]::ToUInt16($bytes, $pe + 4) -ne 0x8664 -or [BitConverter]::ToUInt16($bytes, $pe + 24) -ne 0x20b) {
            throw 'Expected a valid x64 PE executable.'
        }
        $ascii = [Text.Encoding]::ASCII.GetString($bytes)
        if ($ascii -match '(?i)\b(?:vcruntime\d*(?:_\d+)?|msvcp\d*(?:_\d+)?|concrt\d*|ucrtbase|api-ms-win-crt-[a-z0-9-]+)\.dll\b') {
            throw ('A native runtime dependency was found; build with the static CRT: ' + $item.Name)
        }
        if ($ascii -match '(?i)\.pdb\x00') { throw ('A PDB path was found in a release executable: ' + $item.Name) }
    }
}
$manifest = Get-Content -LiteralPath (Join-Path $root 'manifest.json') -Raw | ConvertFrom-Json
if ($manifest.schema_version -ne 1 -or $manifest.channel -ne 'experimental' -or $manifest.license -ne 'not_declared' -or
    @($manifest.files).Count -ne 8 -or $manifest.source_revision -notmatch '^[a-f0-9]{40,64}$') {
    throw 'Unexpected release manifest.'
}
$seen = @{}
foreach ($file in $manifest.files) {
    if ($file.name -notin ($executables + @('LEIA-ME.txt', 'THIRD-PARTY-NOTICES.txt', 'INNO-SETUP-LICENSE.txt')) -or
        $seen.ContainsKey($file.name) -or $file.sha256 -notmatch '^[A-Fa-f0-9]{64}$') { throw 'Unexpected manifest entry.' }
    $seen[$file.name] = $true
    if ((Get-FileHash -LiteralPath (Join-Path $root $file.name) -Algorithm SHA256).Hash -ne $file.sha256) { throw 'Release manifest hash mismatch.' }
}
$checksums = @(Get-Content -LiteralPath (Join-Path $root 'SHA256SUMS.txt'))
if ($checksums.Count -ne 9) { throw 'Unexpected payload checksum count.' }
$seenChecksums = @{}
foreach ($line in $checksums) {
    if ($line -notmatch '^([a-fA-F0-9]{64})  ([A-Za-z0-9._-]+)$') { throw 'Unexpected payload checksum line.' }
    $expectedHash, $name = $Matches[1], $Matches[2]
    if ($name -notin ($allowed | Where-Object { $_ -ne 'SHA256SUMS.txt' }) -or $seenChecksums.ContainsKey($name)) { throw 'Unexpected payload checksum filename.' }
    $seenChecksums[$name] = $true
    if ((Get-FileHash -LiteralPath (Join-Path $root $name) -Algorithm SHA256).Hash -ne $expectedHash) { throw 'Payload checksum mismatch.' }
}
Write-Output 'PASS: fixed payload list, x64 PE headers, static CRT, privacy patterns, manifest and SHA-256 checksums.'
