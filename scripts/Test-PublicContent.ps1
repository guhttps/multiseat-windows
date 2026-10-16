[CmdletBinding()]
param(
    [string]$RepositoryRoot = (Join-Path $PSScriptRoot '..'),
    [switch]$History
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$gitArgs = @('-c', ('safe.directory=' + $root), '-c', 'core.quotepath=false', '-C', $root)
function Invoke-ReviewGit([string[]]$Arguments) {
    # Git emits UTF-8. Windows PowerShell otherwise decodes using the active console code page.
    $previousEncoding = [Console]::OutputEncoding
    try {
        [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
        $output = @(& git @gitArgs @Arguments)
        if ($LASTEXITCODE -ne 0) { throw 'A Git public-content query failed.' }
        return ($output -join "`n")
    } finally { [Console]::OutputEncoding = $previousEncoding }
}
$pathOutput = Invoke-ReviewGit -Arguments @('ls-files', '--cached', '--others', '--exclude-standard')
$paths = @($pathOutput -split "`n" | Where-Object { $_ })
if (-not $paths.Count) { throw 'Could not enumerate repository files.' }

# Print rule names and relative filenames, never the matching sensitive content.
$rules = [ordered]@{
    personal_profile = '(?i)[a-z]:[\\/]Users[\\/](?!Public\b|Default\b|<)[^\\/\s''"<>]+'
    private_key = '-----BEGIN (?:RSA |EC |OPENSSH |ENCRYPTED )?PRIVATE KEY-----'
    github_token = '\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,})\b'
    aws_access_key = '\b(?:AKIA|ASIA)[0-9A-Z]{16}\b'
    personal_sid = '\bS-1-5-21(?:-\d+){3,4}\b'
}
$blockedPath = '(?i)(?:^|/)(?:build|out|artifacts|\.git|\.aws|\.ssh|results)(?:/|$)|(?:^|/)\.env(?:\..+)?$|(?:^|/)(?:id_rsa|id_ed25519)(?:\..*)?$|\.(?:log|etl|dmp|evtx|zip|7z|iso|vhd|vhdx|avhdx|pfx|p12|key|pem|exe|dll|pdb|obj|lib)$|\.local\.'
$findings = [Collections.Generic.List[string]]::new()
function Test-Text([string]$Label, [string]$Content) {
    foreach ($rule in $rules.Keys) {
        if ($Content -match $rules[$rule]) { $findings.Add(($rule + ': ' + $Label)) }
    }
    foreach ($emailMatch in [regex]::Matches($Content, '(?i)\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}\b')) {
        $domain = ($emailMatch.Value -split '@')[-1].ToLowerInvariant()
        if ($domain -notin @('users.noreply.github.com', 'github.com', 'example.com', 'example.org', 'example.net') -and
            -not $domain.EndsWith('.invalid')) {
            $findings.Add(('email_requires_review: ' + $Label))
        }
    }
}
function Assert-ReviewPath([string]$Relative) {
    # Git still quotes filenames containing control characters. Refuse these explicitly.
    if ($Relative.StartsWith('"') -or $Relative -match '[\x00-\x1f]') {
        throw 'A repository filename requires manual review because it contains control characters.'
    }
    $full = [IO.Path]::GetFullPath((Join-Path $root $Relative))
    $rootPrefix = $root.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($rootPrefix, [StringComparison]::Ordinal)) {
        throw 'A repository path leaves the review root.'
    }
    $cursor = $full
    while ($cursor -and $cursor -ne $root) {
        $item = Get-Item -LiteralPath $cursor -Force -ErrorAction Stop
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw ('A repository path redirects to another location: ' + $Relative)
        }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
    if (-not $cursor) { throw 'Could not establish the repository path boundary.' }
    return $full
}
foreach ($relative in $paths) {
    if ($relative -ne '.env.example' -and $relative -match $blockedPath) {
        $findings.Add(('private_artifact: ' + $relative))
        continue
    }
    $full = Assert-ReviewPath $relative
    if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { throw ('Tracked file unavailable: ' + $relative) }
    if ((Get-Item -LiteralPath $full -Force -ErrorAction Stop).Length -gt 2MB) { throw ('Public source file exceeds review limit: ' + $relative) }
    Test-Text $relative ([IO.File]::ReadAllText($full))
}
if ($History) {
    $commits = @((Invoke-ReviewGit -Arguments @('rev-list', '--all')) -split "`n" | Where-Object { $_ })
    $seenEntries = @{}
    $seenBlobs = @{}
    foreach ($commit in $commits) {
        if ($commit -notmatch '^[a-f0-9]{40,64}$') { throw 'Unexpected history commit identifier.' }
        $metadata = Invoke-ReviewGit -Arguments @('show', '-s', '--format=%an%n%ae%n%cn%n%ce%n%B', $commit)
        Test-Text ('commit/' + $commit) $metadata
        # rev-list --objects deduplicates blobs and can omit another filename for identical content.
        # Inspect every commit tree, while avoiding repeated content reads for the same blob.
        $entries = @((Invoke-ReviewGit -Arguments @('ls-tree', '-r', '--full-tree', $commit)) -split "`n" | Where-Object { $_ })
        foreach ($entry in $entries) {
            if ($entry -notmatch '^([0-9]{6}) (blob|commit) ([a-f0-9]{40,64})\t(.+)$') {
                throw 'Unexpected history tree entry.'
            }
            $mode, $kind, $objectId, $relative = $Matches[1], $Matches[2], $Matches[3], $Matches[4]
            if ($relative.StartsWith('"') -or $relative -match '[\x00-\x1f]') { throw 'A historical filename requires manual review.' }
            $entryKey = $mode + '/' + $objectId + '/' + $relative
            if ($seenEntries.ContainsKey($entryKey)) { continue }
            $seenEntries[$entryKey] = $true
            if ($relative -ne '.env.example' -and $relative -match $blockedPath) {
                $findings.Add(('private_artifact_in_history: ' + $relative))
            }
            if ($mode -eq '120000' -or $kind -ne 'blob') {
                $findings.Add(('unreviewed_link_in_history: ' + $relative))
                continue
            }
            if ($seenBlobs.ContainsKey($objectId)) { continue }
            $seenBlobs[$objectId] = $true
            $size = [long](Invoke-ReviewGit -Arguments @('cat-file', '-s', $objectId))
            if ($size -gt 2MB) {
                $findings.Add(('oversized_source_in_history: ' + $relative))
                continue
            }
            $content = Invoke-ReviewGit -Arguments @('cat-file', 'blob', $objectId)
            Test-Text ('history/' + $relative) $content
        }
    }
}
if ($findings.Count) {
    $findings | Sort-Object -Unique | ForEach-Object { Write-Output $_ }
    throw 'Public-content review failed. Remove private data before publishing; see SECURITY.md.'
}
Write-Output ('PASS: public-content policy; ' + $paths.Count + ' source files reviewed. Secret scanning is a separate required check.')
