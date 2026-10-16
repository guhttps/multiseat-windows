[CmdletBinding()]
param()

# Offline Git fixtures only. All generated repositories and captured subprocess
# output remain under the ignored build directory. No remote is configured.
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$guardPath = Join-Path $PSScriptRoot 'Test-PublicContent.ps1'
$fixtureRoot = Join-Path $projectRoot ('build/public-content-safety-' + [Guid]::NewGuid().ToString('N'))
$reportRoot = Join-Path $fixtureRoot 'reports'
$emptyHooks = Join-Path $fixtureRoot 'empty-hooks'
New-Item -ItemType Directory -Path $reportRoot, $emptyHooks -Force | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
$emptyGitConfig = Join-Path $fixtureRoot 'empty.gitconfig'
[IO.File]::WriteAllText($emptyGitConfig, '', $utf8)
$gitExecutable = (Get-Command git -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$shellProcess = [Diagnostics.Process]::GetCurrentProcess()
try { $shellExecutable = $shellProcess.MainModule.FileName }
finally { $shellProcess.Dispose() }
if (-not $shellExecutable) { throw 'Could not locate the current PowerShell executable.' }

# Prevent inherited Git environment/configuration, signing and hooks from
# affecting fixtures or exposing the developer's real commit identity.
$gitEnvironmentNames = @('GIT_DIR', 'GIT_COMMON_DIR', 'GIT_WORK_TREE', 'GIT_INDEX_FILE',
    'GIT_OBJECT_DIRECTORY', 'GIT_ALTERNATE_OBJECT_DIRECTORIES', 'GIT_NAMESPACE', 'GIT_SHALLOW_FILE',
    'GIT_CONFIG', 'GIT_CONFIG_PARAMETERS', 'GIT_CONFIG_COUNT', 'GIT_CONFIG_GLOBAL', 'GIT_CONFIG_NOSYSTEM',
    'GIT_AUTHOR_NAME', 'GIT_AUTHOR_EMAIL', 'GIT_AUTHOR_DATE',
    'GIT_COMMITTER_NAME', 'GIT_COMMITTER_EMAIL', 'GIT_COMMITTER_DATE')
$previousGitEnvironment = @{}
foreach ($name in $gitEnvironmentNames) {
    $previousGitEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

function Invoke-FixtureGit {
    param([string]$Directory, [string[]]$Arguments)
    $gitArguments = @('-c', ('safe.directory=' + $Directory), '-c', 'core.quotepath=false',
        '-c', ('core.hooksPath=' + $emptyHooks), '-c', 'core.fsmonitor=false',
        '-c', 'core.autocrlf=false', '-c', 'core.safecrlf=false', '-c', 'commit.gpgsign=false',
        '-c', 'user.name=Public Policy Fixture', '-c', 'user.email=fixture@public-policy.invalid',
        '-C', $Directory) + $Arguments
    $previousPreference = $ErrorActionPreference
    try {
        # Windows PowerShell can turn redirected native stderr into ErrorRecords.
        # Capture it privately and inspect the real native exit before asserting.
        $ErrorActionPreference = 'Continue'
        $output = @(& $gitExecutable @gitArguments 2>&1)
        $nativeExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    if ($nativeExit -ne 0) {
        $output | Out-File -LiteralPath (Join-Path $reportRoot 'git-failure.log') -Encoding utf8
        # Never publish stderr, argument values, filenames or environment data.
        # Fixed diagnostic categories are enough to investigate runner failures.
        $failureClass = 'native-git-failure'
        $captured = $output -join "`n"
        if ($captured -match 'GIT_CONFIG_COUNT|GIT_CONFIG_PARAMETERS|GIT_DIR|GIT_WORK_TREE|bogus count') {
            $failureClass = 'invalid-git-environment'
        } elseif ($captured -match 'dubious ownership') {
            $failureClass = 'repository-ownership'
        } elseif ($captured -match 'identity unknown|auto-detect email|empty ident|invalid date') {
            $failureClass = 'commit-identity'
        } elseif ($captured -match 'permission denied|access is denied') {
            $failureClass = 'filesystem-permission'
        } elseif ($captured -match 'cannot chdir|no such file or directory') {
            $failureClass = 'filesystem-location'
        } elseif ($captured -match 'unknown option|usage: git') {
            $failureClass = 'native-arguments'
        }
        $operation = if ($Arguments[0] -in @('init', 'add', 'commit', 'remote', 'rev-parse', 'rm')) {
            $Arguments[0]
        } else { 'unknown' }
        throw ('Fixture Git operation failed: operation=' + $operation + '; exit=' + $nativeExit + '; class=' + $failureClass + '.')
    }
    return ($output -join "`n")
}

function Save-FixtureCommit {
    param(
        [string]$Directory,
        [string]$Message,
        [string]$Email = 'fixture@public-policy.invalid',
        [string]$AuthorName = 'Public Policy Fixture',
        [string]$CommitterName = 'Public Policy Fixture',
        [string]$AuthorEmail,
        [string]$CommitterEmail
    )
    if (-not $AuthorEmail) { $AuthorEmail = $Email }
    if (-not $CommitterEmail) { $CommitterEmail = $Email }
    $identityValues = @{ GIT_AUTHOR_NAME = $AuthorName; GIT_AUTHOR_EMAIL = $AuthorEmail;
        GIT_COMMITTER_NAME = $CommitterName; GIT_COMMITTER_EMAIL = $CommitterEmail }
    $previousIdentity = @{}
    foreach ($name in $identityValues.Keys) { $previousIdentity[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
    try {
        foreach ($name in $identityValues.Keys) { [Environment]::SetEnvironmentVariable($name, $identityValues[$name], 'Process') }
        Invoke-FixtureGit -Directory $Directory -Arguments @('commit', '--quiet', '-m', $Message) | Out-Null
    } finally {
        foreach ($name in $identityValues.Keys) {
            if ($null -eq $previousIdentity[$name]) { Remove-Item -LiteralPath ('Env:' + $name) -ErrorAction SilentlyContinue }
            else { Set-Item -LiteralPath ('Env:' + $name) -Value $previousIdentity[$name] }
        }
    }
}

function New-PublicFixture {
    param([string]$Name)
    $directory = Join-Path $fixtureRoot $Name
    New-Item -ItemType Directory -Path $directory | Out-Null
    Invoke-FixtureGit -Directory $directory -Arguments @('init', '--quiet', ('--template=' + $emptyHooks)) | Out-Null
    [IO.File]::WriteAllText((Join-Path $directory '.gitignore'), "build/`n.env`n", $utf8)
    [IO.File]::WriteAllText((Join-Path $directory 'README.md'), "Inert public source fixture.`n", $utf8)
    # Use code points so this script has identical behavior in PowerShell 5/7
    # regardless of the source-file encoding used by a checkout.
    $unicodeName = 'a' + [char]0x00e7 + [char]0x00e3 + 'o-' + [char]0x96ea + '.md'
    $unicodeText = 'Public documentation: ' + [char]0x96ea + ' ' + [char]0x00e7
    [IO.File]::WriteAllText((Join-Path $directory $unicodeName), $unicodeText, $utf8)
    Invoke-FixtureGit -Directory $directory -Arguments @('add', '--all') | Out-Null
    Save-FixtureCommit -Directory $directory -Message 'Inert baseline fixture'
    if (Invoke-FixtureGit -Directory $directory -Arguments @('remote')) {
        throw 'Offline fixture unexpectedly has a remote.'
    }
    return $directory
}

function Assert-PublicPolicy {
    param([string]$Directory, [string]$Category, [string]$ExpectedFinding, [switch]$History,
        [string]$GuardScript = $guardPath, [string[]]$ForbiddenOutputValues = @())
    $arguments = @('-NoLogo', '-NoProfile', '-NonInteractive', '-File', $GuardScript, '-RepositoryRoot', $Directory)
    if ($History) { $arguments += '-History' }
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = @(& $shellExecutable @arguments 2>&1)
        $nativeExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    $output | Out-File -LiteralPath (Join-Path $reportRoot ($Category + '.log')) -Encoding utf8
    $combined = $output -join "`n"
    foreach ($value in $ForbiddenOutputValues) {
        if ($value -and $combined.Contains($value)) { throw ('Public policy exposed fixture identity values; category: ' + $Category) }
    }
    if ($ExpectedFinding) {
        if ($nativeExit -eq 0) { throw ('Public policy accepted prohibited fixture category: ' + $Category) }
        if ($combined -notmatch [regex]::Escape($ExpectedFinding)) {
            throw ('Fixture failed for an unexpected reason; category: ' + $Category)
        }
    } elseif ($nativeExit -ne 0 -or $combined -notmatch 'PASS: public-content policy;') {
        throw ('Public policy rejected legitimate fixture category: ' + $Category)
    }
    Write-Output ('PASS: public policy fixture category: ' + $Category)
}

try {
    # Explicit removal is required: modern PowerShell/.NET can coerce a null
    # SetEnvironmentVariable value to an empty variable, which Git still reads.
    foreach ($name in $gitEnvironmentNames) { Remove-Item -LiteralPath ('Env:' + $name) -ErrorAction SilentlyContinue }
    [Environment]::SetEnvironmentVariable('GIT_CONFIG_GLOBAL', $emptyGitConfig, 'Process')
    [Environment]::SetEnvironmentVariable('GIT_CONFIG_NOSYSTEM', '1', 'Process')
    [Environment]::SetEnvironmentVariable('GIT_AUTHOR_NAME', 'Public Policy Fixture', 'Process')
    [Environment]::SetEnvironmentVariable('GIT_COMMITTER_NAME', 'Public Policy Fixture', 'Process')

    $legitimate = New-PublicFixture -Name 'legitimate-source'
    Assert-PublicPolicy -Directory $legitimate -Category 'legitimate-unicode-and-hidden-file' -History

    # Assemble fictitious prohibited values at runtime so this public test source
    # does not itself contain a complete prohibited personal-data pattern.
    $profileValue = 'C:' + '\' + 'Us' + 'ers' + '\' + 'policy-fixture-person' + '\' + 'work'
    $sidValue = 'S-' + '1-5-' + '21-' + '1000000001-' + '1000000002-' + '1000000003-' + '1001'
    $privateEmail = 'policy-fixture' + '@' + 'privacy-policy.' + 'test'
    $contentFixtures = @(
        @{ name = 'profile-path'; value = $profileValue; finding = 'personal_profile: notes.md' },
        @{ name = 'account-identifier'; value = $sidValue; finding = 'personal_sid: notes.md' },
        @{ name = 'private-email'; value = $privateEmail; finding = 'email_requires_review: notes.md' }
    )
    foreach ($fixture in $contentFixtures) {
        $directory = New-PublicFixture -Name $fixture.name
        [IO.File]::WriteAllText((Join-Path $directory 'notes.md'), $fixture.value, $utf8)
        Invoke-FixtureGit -Directory $directory -Arguments @('add', '--', 'notes.md') | Out-Null
        Assert-PublicPolicy -Directory $directory -Category $fixture.name -ExpectedFinding $fixture.finding
    }

    foreach ($artifact in @('.env', 'archive.bundle')) {
        $category = if ($artifact -eq '.env') { 'tracked-environment-file' } else { 'tracked-git-bundle' }
        $directory = New-PublicFixture -Name $category
        [IO.File]::WriteAllText((Join-Path $directory $artifact), 'Inert fixture bytes.', $utf8)
        Invoke-FixtureGit -Directory $directory -Arguments @('add', '--force', '--', $artifact) | Out-Null
        Assert-PublicPolicy -Directory $directory -Category $category -ExpectedFinding ('private_artifact: ' + $artifact)
    }

    $aliases = New-PublicFixture -Name 'historical-blob-alias'
    $sameContent = 'The two filenames intentionally refer to the same inert Git blob.'
    [IO.File]::WriteAllText((Join-Path $aliases 'normal.md'), $sameContent, $utf8)
    Invoke-FixtureGit -Directory $aliases -Arguments @('add', '--', 'normal.md') | Out-Null
    Save-FixtureCommit -Directory $aliases -Message 'Add ordinary blob name'
    [IO.File]::WriteAllText((Join-Path $aliases '.env'), $sameContent, $utf8)
    Invoke-FixtureGit -Directory $aliases -Arguments @('add', '--force', '--', '.env') | Out-Null
    Save-FixtureCommit -Directory $aliases -Message 'Add prohibited alias to same blob'
    $ordinaryBlob = Invoke-FixtureGit -Directory $aliases -Arguments @('rev-parse', 'HEAD:normal.md')
    $prohibitedBlob = Invoke-FixtureGit -Directory $aliases -Arguments @('rev-parse', 'HEAD:.env')
    if ($ordinaryBlob -ne $prohibitedBlob) { throw 'History alias fixture did not reuse the same blob.' }
    Invoke-FixtureGit -Directory $aliases -Arguments @('rm', '--quiet', '--', '.env') | Out-Null
    Save-FixtureCommit -Directory $aliases -Message 'Remove prohibited alias from current tree'
    Assert-PublicPolicy -Directory $aliases -Category 'safe-current-tree-after-alias'
    Assert-PublicPolicy -Directory $aliases -Category 'historical-prohibited-alias' -History -ExpectedFinding 'private_artifact_in_history: .env'

    $metadata = New-PublicFixture -Name 'historical-private-metadata'
    [IO.File]::WriteAllText((Join-Path $metadata 'normal.md'), 'Ordinary public source.', $utf8)
    Invoke-FixtureGit -Directory $metadata -Arguments @('add', '--', 'normal.md') | Out-Null
    Save-FixtureCommit -Directory $metadata -Message 'Fictitious private metadata' -Email $privateEmail
    Assert-PublicPolicy -Directory $metadata -Category 'safe-source-with-private-metadata'
    Assert-PublicPolicy -Directory $metadata -Category 'historical-private-metadata' -History -ExpectedFinding 'email_requires_review: commit/'

    # Only identities associated with the maintained public GitHub handle use
    # this alias list; contributors with unrelated noreply identities are not renamed.
    $maintainerEmail = 'guhttps@users.noreply.github.com'
    $idMaintainerEmail = '123456+guhttps@users.noreply.github.com'
    $unapprovedName = 'Unapproved Fictitious Identity'
    foreach ($identity in @(
        @{ category = 'approved-project-alias'; name = 'Multiseat Windows'; email = $maintainerEmail },
        @{ category = 'approved-handle-alias-with-id'; name = 'guhttps'; email = $idMaintainerEmail },
        @{ category = 'unrelated-noreply-contributor'; name = $unapprovedName; email = 'policy-outsider@users.noreply.github.com' }
    )) {
        $directory = New-PublicFixture -Name $identity.category
        [IO.File]::WriteAllText((Join-Path $directory 'normal.md'), 'Ordinary public source.', $utf8)
        Invoke-FixtureGit -Directory $directory -Arguments @('add', '--', 'normal.md') | Out-Null
        Save-FixtureCommit -Directory $directory -Message 'Public identity fixture' -Email $identity.email -AuthorName $identity.name -CommitterName $identity.name
        Assert-PublicPolicy -Directory $directory -Category $identity.category -History
    }

    $authorIdentity = New-PublicFixture -Name 'maintainer-author-history'
    [IO.File]::WriteAllText((Join-Path $authorIdentity 'removed.md'), 'Inert file removed in the following revision.', $utf8)
    Invoke-FixtureGit -Directory $authorIdentity -Arguments @('add', '--', 'removed.md') | Out-Null
    Save-FixtureCommit -Directory $authorIdentity -Message 'Fictitious author identity' -AuthorEmail $maintainerEmail -AuthorName $unapprovedName -CommitterEmail $maintainerEmail -CommitterName 'Multiseat Windows'
    $unapprovedAuthorCommit = Invoke-FixtureGit -Directory $authorIdentity -Arguments @('rev-parse', 'HEAD')
    Invoke-FixtureGit -Directory $authorIdentity -Arguments @('rm', '--quiet', '--', 'removed.md') | Out-Null
    Save-FixtureCommit -Directory $authorIdentity -Message 'Current revision uses approved identities' -Email $maintainerEmail -AuthorName 'Multiseat Windows' -CommitterName 'guhttps'
    Assert-PublicPolicy -Directory $authorIdentity -Category 'clean-current-tree-after-unapproved-author'
    Assert-PublicPolicy -Directory $authorIdentity -Category 'unapproved-author-in-ancestor' -History -ExpectedFinding ('maintainer_author_name_requires_review: commit/' + $unapprovedAuthorCommit) -ForbiddenOutputValues @($unapprovedName, $maintainerEmail)

    $committerIdentity = New-PublicFixture -Name 'maintainer-committer'
    [IO.File]::WriteAllText((Join-Path $committerIdentity 'normal.md'), 'Ordinary public source.', $utf8)
    Invoke-FixtureGit -Directory $committerIdentity -Arguments @('add', '--', 'normal.md') | Out-Null
    Save-FixtureCommit -Directory $committerIdentity -Message 'Fictitious committer identity' -AuthorEmail $maintainerEmail -AuthorName 'Multiseat Windows' -CommitterEmail $idMaintainerEmail -CommitterName $unapprovedName
    $unapprovedCommitterCommit = Invoke-FixtureGit -Directory $committerIdentity -Arguments @('rev-parse', 'HEAD')
    Assert-PublicPolicy -Directory $committerIdentity -Category 'unapproved-committer-with-noreply-id' -History -ExpectedFinding ('maintainer_committer_name_requires_review: commit/' + $unapprovedCommitterCommit) -ForbiddenOutputValues @($unapprovedName, $idMaintainerEmail)

    foreach ($automationName in @('GitHub', $unapprovedName)) {
        $category = if ($automationName -eq 'GitHub') { 'approved-github-committer' } else { 'unapproved-github-committer' }
        $directory = New-PublicFixture -Name $category
        [IO.File]::WriteAllText((Join-Path $directory 'normal.md'), 'Ordinary public source.', $utf8)
        Invoke-FixtureGit -Directory $directory -Arguments @('add', '--', 'normal.md') | Out-Null
        Save-FixtureCommit -Directory $directory -Message 'Automation identity fixture' -AuthorEmail $maintainerEmail -AuthorName 'Multiseat Windows' -CommitterEmail 'noreply@github.com' -CommitterName $automationName
        $finding = if ($automationName -eq 'GitHub') { $null } else { 'automation_committer_name_requires_review: commit/' }
        Assert-PublicPolicy -Directory $directory -Category $category -History -ExpectedFinding $finding -ForbiddenOutputValues @($unapprovedName)
    }

    $trustedPolicyPath = Join-Path $projectRoot '.github/publication-policy.json'
    $trustedPolicyText = [IO.File]::ReadAllText($trustedPolicyPath)
    $targetPolicy = New-PublicFixture -Name 'target-policy-cannot-weaken-trusted-tool'
    New-Item -ItemType Directory -Path (Join-Path $targetPolicy '.github') | Out-Null
    $weakened = $trustedPolicyText | ConvertFrom-Json
    $weakened.maintainers[0].approved_names += $unapprovedName
    [IO.File]::WriteAllText((Join-Path $targetPolicy '.github/publication-policy.json'), ($weakened | ConvertTo-Json -Depth 8), $utf8)
    Invoke-FixtureGit -Directory $targetPolicy -Arguments @('add', '--all') | Out-Null
    Save-FixtureCommit -Directory $targetPolicy -Message 'Target policy is untrusted input' -Email $maintainerEmail -AuthorName $unapprovedName -CommitterName $unapprovedName
    Assert-PublicPolicy -Directory $targetPolicy -Category 'target-policy-cannot-weaken-trusted-tool' -History -ExpectedFinding 'maintainer_author_name_requires_review: commit/' -ForbiddenOutputValues @($unapprovedName, $maintainerEmail)

    foreach ($invalidKind in @('unknown-property', 'wrong-version-type', 'duplicate-property')) {
        $tool = Join-Path $fixtureRoot ('invalid-policy-tool-' + $invalidKind)
        New-Item -ItemType Directory -Path (Join-Path $tool 'scripts'), (Join-Path $tool '.github') -Force | Out-Null
        $copiedGuard = Join-Path $tool 'scripts/Test-PublicContent.ps1'
        Copy-Item -LiteralPath $guardPath -Destination $copiedGuard
        $invalid = $trustedPolicyText | ConvertFrom-Json
        if ($invalidKind -eq 'unknown-property') {
            $invalid | Add-Member -NotePropertyName unexpected -NotePropertyValue $true
            $invalidText = $invalid | ConvertTo-Json -Depth 8
        } elseif ($invalidKind -eq 'wrong-version-type') {
            $invalid.schema_version = '1'
            $invalidText = $invalid | ConvertTo-Json -Depth 8
        } else {
            $invalidText = $trustedPolicyText -replace '"schema_version"\s*:\s*1', '"schema_version": 1, "schema_version": 1'
        }
        [IO.File]::WriteAllText((Join-Path $tool '.github/publication-policy.json'), $invalidText, $utf8)
        Assert-PublicPolicy -Directory $legitimate -Category ('invalid-policy-' + $invalidKind) -GuardScript $copiedGuard -ExpectedFinding 'Invalid trusted publication policy;'
    }
} finally {
    foreach ($name in $gitEnvironmentNames) {
        if ($null -eq $previousGitEnvironment[$name]) {
            Remove-Item -LiteralPath ('Env:' + $name) -ErrorAction SilentlyContinue
        } else {
            Set-Item -LiteralPath ('Env:' + $name) -Value $previousGitEnvironment[$name]
        }
    }
}

# Expected rejections must not leak their nonzero exit into this suite's exit.
# Any assertion throws before this success exit; reports are deliberately private.
Write-Output 'PASS: offline public-content safety regression suite.'
exit 0
