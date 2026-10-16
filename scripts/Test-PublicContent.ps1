[CmdletBinding()]
param(
    [string]$RepositoryRoot,
    [switch]$History
)

$ErrorActionPreference = 'Stop'
function Read-PublicationPolicy {
    # The inspected repository cannot supply a weaker policy to this trusted tool.
    $toolRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
    $policyPath = Join-Path $toolRoot '.github/publication-policy.json'
    try {
        $cursor = $policyPath
        while ($cursor -ne $toolRoot) {
            $item = Get-Item -LiteralPath $cursor -Force -ErrorAction Stop
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Redirected policy.' }
            $cursor = [IO.Path]::GetDirectoryName($cursor)
            if (-not $cursor) { throw 'Unknown policy boundary.' }
        }
        $item = Get-Item -LiteralPath $policyPath -Force -ErrorAction Stop
        if ($item.PSIsContainer -or $item.Length -gt 32KB) { throw 'Unexpected policy file.' }
        $policyText = [IO.File]::ReadAllText($policyPath)

        # ConvertFrom-Json can discard duplicate keys. Detect them before conversion,
        # using the built-in JSON reader available in both .NET Framework and .NET.
        if ($PSVersionTable.PSEdition -eq 'Core') { Add-Type -AssemblyName System.Runtime.Serialization.Json }
        else { Add-Type -AssemblyName System.Runtime.Serialization }
        $quotas = [Xml.XmlDictionaryReaderQuotas]::new()
        $quotas.MaxDepth = 16
        $quotas.MaxStringContentLength = 1024
        $quotas.MaxArrayLength = 64
        $quotas.MaxNameTableCharCount = 4096
        $quotas.MaxBytesPerRead = 4096
        $reader = [Runtime.Serialization.Json.JsonReaderWriterFactory]::CreateJsonReader([Text.Encoding]::UTF8.GetBytes($policyText), $quotas)
        $frames = [Collections.Generic.Stack[object]]::new()
        try {
            while ($reader.Read()) {
                if ($reader.NodeType -eq [Xml.XmlNodeType]::Element) {
                    if ($frames.Count -and $frames.Peek().kind -eq 'object') {
                        $seen = $frames.Peek().seen
                        if ($seen.ContainsKey($reader.LocalName)) { throw 'Duplicate policy property.' }
                        $seen[$reader.LocalName] = $true
                    }
                    if (-not $reader.IsEmptyElement) { $frames.Push(@{ kind = $reader.GetAttribute('type'); seen = @{} }) }
                } elseif ($reader.NodeType -eq [Xml.XmlNodeType]::EndElement) {
                    if (-not $frames.Count) { throw 'Malformed policy structure.' }
                    $frames.Pop() | Out-Null
                }
            }
        } finally { $reader.Dispose() }
        $policy = $policyText | ConvertFrom-Json -ErrorAction Stop
        function Assert-PolicyProperties($Object, [string[]]$Expected) {
            if ($Object -isnot [pscustomobject]) { throw 'Expected policy object.' }
            $names = @($Object.PSObject.Properties.Name)
            if ($names.Count -ne $Expected.Count -or @($names | Where-Object { $_ -cnotin $Expected }).Count) { throw 'Unexpected policy properties.' }
        }
        function Read-ApprovedNames($Names) {
            if ($Names -isnot [array] -or $Names.Count -lt 1 -or $Names.Count -gt 8) { throw 'Expected approved-name list.' }
            $seenNames = @{}
            foreach ($name in $Names) {
                if ($name -isnot [string] -or $name.Length -lt 1 -or $name.Length -gt 80 -or
                    $name -cne $name.Trim() -or $name -match '[\x00-\x1f\x7f]' -or $seenNames.ContainsKey($name)) { throw 'Invalid approved name.' }
                $seenNames[$name] = $true
            }
            return ,([string[]]$Names)
        }
        Assert-PolicyProperties $policy @('schema_version', 'maintainers', 'automation_committers')
        if (($policy.schema_version -isnot [int] -and $policy.schema_version -isnot [long]) -or $policy.schema_version -ne 1 -or
            $policy.maintainers -isnot [array] -or $policy.maintainers.Count -lt 1 -or $policy.maintainers.Count -gt 32 -or
            $policy.automation_committers -isnot [array] -or $policy.automation_committers.Count -gt 1) { throw 'Unexpected policy schema.' }
        $maintainers = @{}
        foreach ($maintainer in $policy.maintainers) {
            Assert-PolicyProperties $maintainer @('handle', 'approved_names')
            if ($maintainer.handle -isnot [string] -or $maintainer.handle -cnotmatch '^[a-z0-9](?:[a-z0-9-]{0,37}[a-z0-9])?$' -or
                $maintainer.handle -match '--' -or $maintainers.ContainsKey($maintainer.handle)) { throw 'Invalid maintainer handle.' }
            $maintainers[$maintainer.handle] = Read-ApprovedNames $maintainer.approved_names
        }
        $automation = @{}
        foreach ($actor in $policy.automation_committers) {
            Assert-PolicyProperties $actor @('email', 'approved_names')
            if ($actor.email -isnot [string] -or $actor.email -cne 'noreply@github.com' -or $automation.ContainsKey($actor.email)) { throw 'Invalid automation identity.' }
            $automation[$actor.email] = Read-ApprovedNames $actor.approved_names
        }
        return @{ maintainers = $maintainers; automation_committers = $automation }
    } catch {
        # Parser errors may contain policy values. Expose only a fixed category.
        throw 'Invalid trusted publication policy; public-content review cannot continue.'
    }
}
$publicationPolicy = Read-PublicationPolicy
if (-not $RepositoryRoot) { $RepositoryRoot = Join-Path $PSScriptRoot '..' }
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
    personal_profile = '(?i)[a-z]:[\\/]{1,2}Users[\\/]{1,2}(?!Public\b|Default\b|<)[^\\/\s''"<>]+'
    private_key = '-----BEGIN (?:RSA |EC |OPENSSH |ENCRYPTED )?PRIVATE KEY-----'
    github_token = '\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,})\b'
    aws_access_key = '\b(?:AKIA|ASIA)[0-9A-Z]{16}\b'
    personal_sid = '\bS-1-5-21(?:-\d+){3,4}\b'
}
$blockedPath = '(?i)(?:^|/)(?:build|out|artifacts|\.git|\.aws|\.ssh|results)(?:/|$)|(?:^|/)\.env(?:\..+)?$|(?:^|/)(?:id_rsa|id_ed25519)(?:\..*)?$|\.(?:log|etl|dmp|evtx|zip|7z|bundle|tar|gz|tgz|bz2|sqlite|sqlite3|db|iso|vhd|vhdx|avhdx|pfx|p12|key|pem|exe|dll|pdb|obj|lib)$|\.local\.'
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
function Test-CommitIdentity([string]$Commit, [string]$Role, [string]$Name, [string]$Email) {
    $normalizedEmail = $Email.Trim()
    $identity = [regex]::Match($normalizedEmail, '^(?:[0-9]{1,20}\+)?(?<handle>[a-z0-9](?:[a-z0-9-]{0,37}[a-z0-9])?)@users\.noreply\.github\.com$', [Text.RegularExpressions.RegexOptions]::IgnoreCase)
    if ($identity.Success) {
        $handle = $identity.Groups['handle'].Value.ToLowerInvariant()
        if ($publicationPolicy.maintainers.ContainsKey($handle)) {
            if ($Name -cnotin $publicationPolicy.maintainers[$handle]) {
                $findings.Add(('maintainer_' + $Role + '_name_requires_review: commit/' + $Commit))
            }
            return
        }
    }
    if ($Role -eq 'committer' -and $publicationPolicy.automation_committers.ContainsKey($normalizedEmail.ToLowerInvariant()) -and
        $Name -cnotin $publicationPolicy.automation_committers[$normalizedEmail.ToLowerInvariant()]) {
        $findings.Add(('automation_committer_name_requires_review: commit/' + $Commit))
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
        $metadata = Invoke-ReviewGit -Arguments @('show', '-s', '--format=%an%x00%ae%x00%cn%x00%ce%x00%B', $commit)
        Test-Text ('commit/' + $commit) $metadata
        $identityFields = @($metadata -split [char]0, 5)
        if ($identityFields.Count -ne 5) {
            $findings.Add(('commit_metadata_requires_review: commit/' + $commit))
        } else {
            Test-CommitIdentity -Commit $commit -Role 'author' -Name $identityFields[0] -Email $identityFields[1]
            Test-CommitIdentity -Commit $commit -Role 'committer' -Name $identityFields[2] -Email $identityFields[3]
        }
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
