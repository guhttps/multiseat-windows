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
} finally { $env:PATH = $previousPath }
Write-Output 'PASS: SDK duplicate-PATH command discovery, native launch and missing-tool rejection.'
