[CmdletBinding()]
param()

# Exercise command discovery/launch only. Do not run the SDK builder, download
# sources, initialize a developer shell, or connect to a guest.
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot 'Build-RdsLabSdk.ps1'), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'SDK builder syntax failed.' }
$names = @('Resolve-SdkApplication', 'Assert-SdkPath', 'Invoke-SdkCommand')
$functions = @($ast.FindAll({ param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -in $names
}, $true))
if ($functions.Count -ne $names.Count) { throw 'Required command helpers were not found.' }
foreach ($function in $functions) { . ([scriptblock]::Create($function.Extent.Text)) }

$script:sdkRoot = Join-Path $project ('build/rds-sdk-inert-' + [Guid]::NewGuid().ToString('N'))
Assert-SdkPath $script:sdkRoot $script:sdkRoot | Out-Null
$script:logRoot = Join-Path $script:sdkRoot 'logs'
$first = Join-Path $script:sdkRoot 'first'
$second = Join-Path $script:sdkRoot 'second'
foreach ($directory in @($script:logRoot, $first, $second)) {
    [IO.Directory]::CreateDirectory($directory) | Out-Null
}
$fixtureName = 'multiseat-sdk-fixture.exe'
$systemWhere = Join-Path ([Environment]::GetFolderPath('System')) 'where.exe'
if (-not (Test-Path -LiteralPath $systemWhere -PathType Leaf)) { throw 'System command fixture unavailable.' }
Copy-Item -LiteralPath $systemWhere -Destination (Join-Path $first $fixtureName)
Copy-Item -LiteralPath $systemWhere -Destination (Join-Path $second $fixtureName)
$previousPath = $env:PATH
$previousDirectorySearch = [Environment]::GetEnvironmentVariable('NoDefaultCurrentDirectoryInExePath', 'Process')
try {
    $env:PATH = $first + ';' + $second + ';' + $previousPath
    if (@(Get-Command $fixtureName -CommandType Application).Count -ne 2) {
        throw 'Duplicate PATH regression fixture was not established.'
    }
    $resolved = Resolve-SdkApplication $fixtureName
    if ($resolved -isnot [string] -or $resolved -cne (Join-Path $first $fixtureName)) {
        throw 'Command discovery did not select one executable in PATH order.'
    }
    $null = Invoke-SdkCommand $resolved @('/q', $fixtureName) $script:sdkRoot 'duplicate-path'
    if ($script:lastCommandExitCode -ne 0 -or $script:lastCommandCategory -cne 'none') {
        throw 'Resolved native command did not run successfully.'
    }
    $missingRejected = $false
    try { Resolve-SdkApplication ('missing-' + [Guid]::NewGuid().ToString('N') + '.exe') | Out-Null }
    catch { $missingRejected = $true }
    if (-not $missingRejected) { throw 'Missing command was accepted.' }

    # Exercise the child environment, not just a text inspection of the recipe.
    # The private directory must win over an existing same-name executable,
    # without relaxing the current-directory search restriction or leaking PATH.
    $env:NoDefaultCurrentDirectoryInExePath = '1'
    $childScript = Join-Path $script:sdkRoot 'inspect-child.private.ps1'
    $childText = @'
param([switch]$Fail)
$ErrorActionPreference = 'Stop'
[pscustomobject]@{
    directory_search_guard = $env:NoDefaultCurrentDirectoryInExePath
    executable = (Get-Command 'multiseat-sdk-fixture.exe' -CommandType Application | Select-Object -First 1).Source
} | ConvertTo-Json -Compress
if ($Fail) { exit 7 }
exit 0
'@
    [IO.File]::WriteAllText($childScript, $childText, [Text.UTF8Encoding]::new($false))
    $shell = [Diagnostics.Process]::GetCurrentProcess()
    try { $shellPath = $shell.MainModule.FileName } finally { $shell.Dispose() }
    $beforeChildPath = $env:PATH
    $observed = @(Invoke-SdkCommand $shellPath @('-NoProfile', '-File', $childScript) $script:sdkRoot 'private-path' -ReadOutput -PrivateToolDirectory $second)
    $observation = ($observed -join "`n") | ConvertFrom-Json
    if ($observation.directory_search_guard -cne '1' -or $observation.executable -cne (Join-Path $second $fixtureName) -or
        $env:PATH -cne $beforeChildPath -or $env:NoDefaultCurrentDirectoryInExePath -cne '1') {
        throw 'Private child PATH or directory-search guard was not preserved.'
    }
    $failedChildRejected = $false
    try { Invoke-SdkCommand $shellPath @('-NoProfile', '-File', $childScript, '-Fail') $script:sdkRoot 'failed-private-path' -PrivateToolDirectory $second | Out-Null }
    catch { $failedChildRejected = $true }
    if (-not $failedChildRejected -or $script:lastCommandExitCode -ne 7 -or $env:PATH -cne $beforeChildPath -or
        $env:NoDefaultCurrentDirectoryInExePath -cne '1') { throw 'Failed child did not restore the guarded environment.' }
    $foreignDirectoryRejected = $false
    try { Invoke-SdkCommand $shellPath @('-NoProfile', '-File', $childScript) $script:sdkRoot 'foreign-private-path' -PrivateToolDirectory ([Environment]::GetFolderPath('System')) | Out-Null }
    catch { $foreignDirectoryRejected = $true }
    if (-not $foreignDirectoryRejected -or $env:PATH -cne $beforeChildPath) { throw 'Foreign tool directory was accepted.' }
    $redirect = Join-Path $script:sdkRoot 'redirected-tools'
    New-Item -ItemType Junction -Path $redirect -Target $first | Out-Null
    foreach ($invalid in @(
        @{ directory = '.'; tag = 'relative-tool-directory' },
        @{ directory = ($first + ';' + $second); tag = 'injected-path-list' },
        @{ directory = $redirect; tag = 'redirected-tool-directory' }
    )) {
        $rejected = $false
        try { Invoke-SdkCommand $shellPath @('-NoProfile', '-File', $childScript) $script:sdkRoot $invalid.tag -PrivateToolDirectory $invalid.directory | Out-Null }
        catch { $rejected = $true }
        if (-not $rejected -or (Test-Path -LiteralPath (Join-Path $script:logRoot ($invalid.tag + '.private.log'))) -or
            $env:PATH -cne $beforeChildPath -or $env:NoDefaultCurrentDirectoryInExePath -cne '1') {
            throw 'Invalid private tool directory was not refused before launching the child.'
        }
    }
} finally {
    $env:PATH = $previousPath
    [Environment]::SetEnvironmentVariable('NoDefaultCurrentDirectoryInExePath', $previousDirectorySearch, 'Process')
}
Write-Output 'PASS: SDK command discovery, private child PATH, unchanged search guard, environment restoration and foreign-directory rejection.'
# The negative child intentionally exits 7. The Actions PowerShell wrapper
# propagates LASTEXITCODE, so a successful suite must explicitly return zero.
exit 0
