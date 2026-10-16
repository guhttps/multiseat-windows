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
    param([string]$Directory, [string]$Message, [string]$Email = 'fixture@public-policy.invalid')
    $previousAuthor = [Environment]::GetEnvironmentVariable('GIT_AUTHOR_EMAIL', 'Process')
    $previousCommitter = [Environment]::GetEnvironmentVariable('GIT_COMMITTER_EMAIL', 'Process')
    try {
        [Environment]::SetEnvironmentVariable('GIT_AUTHOR_EMAIL', $Email, 'Process')
        [Environment]::SetEnvironmentVariable('GIT_COMMITTER_EMAIL', $Email, 'Process')
        Invoke-FixtureGit -Directory $Directory -Arguments @('commit', '--quiet', '-m', $Message) | Out-Null
    } finally {
        if ($null -eq $previousAuthor) { Remove-Item -LiteralPath 'Env:GIT_AUTHOR_EMAIL' -ErrorAction SilentlyContinue }
        else { Set-Item -LiteralPath 'Env:GIT_AUTHOR_EMAIL' -Value $previousAuthor }
        if ($null -eq $previousCommitter) { Remove-Item -LiteralPath 'Env:GIT_COMMITTER_EMAIL' -ErrorAction SilentlyContinue }
        else { Set-Item -LiteralPath 'Env:GIT_COMMITTER_EMAIL' -Value $previousCommitter }
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
    param([string]$Directory, [string]$Category, [string]$ExpectedFinding, [switch]$History)
    $arguments = @('-NoLogo', '-NoProfile', '-NonInteractive', '-File', $guardPath, '-RepositoryRoot', $Directory)
    if ($History) { $arguments += '-History' }
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = @(& $shellExecutable @arguments 2>&1)
        $nativeExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    $output | Out-File -LiteralPath (Join-Path $reportRoot ($Category + '.log')) -Encoding utf8
    $combined = $output -join "`n"
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
