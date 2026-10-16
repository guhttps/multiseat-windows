[CmdletBinding()]
param(
    [ValidatePattern('^(0|[1-9][0-9]{0,3})\.(0|[1-9][0-9]{0,3})\.(0|[1-9][0-9]{0,3})-experimental\.[1-9][0-9]{0,3}$')]
    [string]$Version = '0.1.0-experimental.1',
    [Parameter(Mandatory)][string]$IsccPath,
    [string]$OutputDirectory,
    [string]$Generator
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $env:windir -or -not [Environment]::Is64BitOperatingSystem) { throw 'Windows x64 is required to build the distribution.' }
& (Join-Path $PSScriptRoot 'package/Test-InnoCompiler.ps1') -IsccPath $IsccPath
$runId = [Guid]::NewGuid().ToString('N')
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot ('build/package-' + $Version + '-' + $runId) }
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
$allowedBuildRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build')).TrimEnd('\') + '\'
if (-not $outputRoot.StartsWith($allowedBuildRoot, [StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $outputRoot)) {
    throw 'Use a new output directory inside the project build directory.'
}
$cursor = [IO.Path]::GetDirectoryName($outputRoot)
while ($cursor -and $cursor -ne $projectRoot) {
    if ((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Build output ancestors must not redirect outside the project.'
    }
    $cursor = [IO.Path]::GetDirectoryName($cursor)
}
if (-not $cursor) { throw 'Could not establish the project build boundary.' }
$nativeRoot = Join-Path $outputRoot 'native'
$payloadRoot = Join-Path $outputRoot 'payload'
$artifactRoot = Join-Path $outputRoot 'artifacts'
$privateRoot = Join-Path $outputRoot 'private-build-logs'
foreach ($directory in @($outputRoot, $payloadRoot, $artifactRoot, $privateRoot)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}

function Invoke-ReleaseCheck([string]$Name, [string]$Executable, [string[]]$Arguments) {
    Write-Host ('Release check: ' + $Name)
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $global:LASTEXITCODE = $null
        $output = & $Executable @Arguments 2>&1
        $code = $global:LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    $output | Out-File -LiteralPath (Join-Path $privateRoot ($Name + '.log')) -Encoding utf8
    if ($null -eq $code -or $code -ne 0) { throw ('Release check failed: ' + $Name + '. Inspect local private build logs.') }
}

$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) { $cmake = $cmakeCommand.Source }
else {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) { throw 'CMake or Visual Studio C++ Build Tools is required.' }
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $installation) { throw 'Visual Studio C++ Build Tools was not found.' }
    $cmake = Join-Path $installation 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    if (-not (Test-Path -LiteralPath $cmake -PathType Leaf)) { throw 'Install the Visual Studio CMake component or add CMake to PATH.' }
}
$configure = @('-S', $projectRoot, '-B', $nativeRoot, '-A', 'x64',
    '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded', '-DCMAKE_CXX_FLAGS=/EHsc /Brepro',
    '-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/INCREMENTAL:NO /DEBUG:NONE /Brepro',
    '-DMULTISEAT_BUILD_CHILD_SESSION_HOST=OFF', '-DBUILD_TESTING=ON')
if ($Generator) { $configure += @('-G', $Generator) }
Invoke-ReleaseCheck -Name 'configure' -Executable $cmake -Arguments $configure
Invoke-ReleaseCheck -Name 'build' -Executable $cmake -Arguments @('--build', $nativeRoot, '--config', 'Release', '--parallel', '2')
$ctest = Join-Path (Split-Path $cmake -Parent) 'ctest.exe'
if (-not (Test-Path -LiteralPath $ctest -PathType Leaf)) { throw 'CTest must be available beside CMake.' }
Invoke-ReleaseCheck -Name 'ctest' -Executable $ctest -Arguments @('--test-dir', $nativeRoot, '-C', 'Release', '--output-on-failure')
$executables = @('MultiseatPrototype.exe', 'ms-diagnostic.exe', 'ms-session-probe.exe', 'ms-keyboard-probe.exe', 'ms-seat-demo.exe')
foreach ($name in $executables) {
    $source = Join-Path $nativeRoot ('Release/' + $name)
    $item = Get-Item -LiteralPath $source -Force -ErrorAction Stop
    if ($item.PSIsContainer -or $item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Expected a regular release executable.' }
    Copy-Item -LiteralPath $source -Destination (Join-Path $payloadRoot $name)
}
foreach ($name in @('LEIA-ME.txt', 'THIRD-PARTY-NOTICES.txt')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot ('installer/' + $name)) -Destination (Join-Path $payloadRoot $name)
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'installer/third-party/INNO-SETUP-LICENSE.txt') -Destination (Join-Path $payloadRoot 'INNO-SETUP-LICENSE.txt')
$gitArgs = @('-c', ('safe.directory=' + $projectRoot), '-C', $projectRoot)
$revision = & git @gitArgs rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $revision -notmatch '^[a-f0-9]{40,64}$') { throw 'A valid source revision is required.' }
$dirty = @(& git @gitArgs status --porcelain --untracked-files=normal)
if ($LASTEXITCODE -ne 0) { throw 'Could not establish source status.' }
if ($env:GITHUB_ACTIONS -eq 'true' -and $dirty.Count) { throw 'CI releases require a clean source checkout.' }
$payloadFiles = @($executables + @('LEIA-ME.txt', 'THIRD-PARTY-NOTICES.txt', 'INNO-SETUP-LICENSE.txt') | Sort-Object | ForEach-Object {
    [ordered]@{ name = $_; sha256 = (Get-FileHash -LiteralPath (Join-Path $payloadRoot $_) -Algorithm SHA256).Hash }
})
$manifest = [ordered]@{
    schema_version = 1
    product = 'Multiseat Windows Prototype'
    version = $Version
    channel = 'experimental'
    platform = 'windows-x64'
    source_revision = $revision
    source_dirty = [bool]$dirty.Count
    license = 'not_declared'
    code_signed = $false
    native_runtime = 'static-msvc-crt'
    managed_runtime = 'windows-dotnet-framework-4'
    installer_tool = 'Inno Setup 6.7.3'
    archive_timestamp = '2000-01-01T00:00:00 (fixed ZIP date/time fields; ZIP has no timezone)'
    reproducibility = 'Pinned installer compiler and fixed archive timestamps; bit-identical recompilation is not asserted.'
    limitations = @('Own-application device routing only.', 'External apps, independent Windows accounts and League of Legends multiseat are unproven.')
    files = $payloadFiles
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $payloadRoot 'manifest.json') -Encoding UTF8
$payloadNames = @($executables + @('LEIA-ME.txt', 'THIRD-PARTY-NOTICES.txt', 'INNO-SETUP-LICENSE.txt', 'manifest.json') | Sort-Object)
$payloadNames | ForEach-Object { (Get-FileHash -LiteralPath (Join-Path $payloadRoot $_) -Algorithm SHA256).Hash + '  ' + $_ } |
    Set-Content -LiteralPath (Join-Path $payloadRoot 'SHA256SUMS.txt') -Encoding ASCII
& (Join-Path $PSScriptRoot 'package/Test-ReleasePayload.ps1') -Directory $payloadRoot
$fixedTime = [DateTime]::SpecifyKind([DateTime]::new(2000, 1, 1), [DateTimeKind]::Utc)
Get-ChildItem -LiteralPath $payloadRoot -File -Force | ForEach-Object { $_.LastWriteTimeUtc = $fixedTime }

$portableName = 'MultiseatPrototype-' + $Version + '-windows-x64.zip'
$installerName = 'MultiseatPrototype-' + $Version + '-windows-x64-installer.exe'
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::Open((Join-Path $artifactRoot $portableName), [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($name in @($payloadNames + @('SHA256SUMS.txt') | Sort-Object)) {
        $entry = $archive.CreateEntry($name, [IO.Compression.CompressionLevel]::Optimal)
        $entry.LastWriteTime = [DateTimeOffset]::new($fixedTime)
        $inputStream = [IO.File]::OpenRead((Join-Path $payloadRoot $name))
        $outputStream = $entry.Open()
        try { $inputStream.CopyTo($outputStream) } finally { $outputStream.Dispose(); $inputStream.Dispose() }
    }
} finally { $archive.Dispose() }
$numericVersion = ($Version -split '-')[0]
Invoke-ReleaseCheck -Name 'installer' -Executable $IsccPath -Arguments @('/Qp', ('/DProductVersion=' + $numericVersion),
    ('/DReleaseVersion=' + $Version), ('/DPayloadRoot=' + $payloadRoot), ('/DArtifactRoot=' + $artifactRoot),
    (Join-Path $projectRoot 'installer/MultiseatPrototype.iss'))
$artifactFiles = @($installerName, $portableName | Sort-Object | ForEach-Object {
    [ordered]@{ name = $_; sha256 = (Get-FileHash -LiteralPath (Join-Path $artifactRoot $_) -Algorithm SHA256).Hash }
})
[ordered]@{ schema_version = 1; version = $Version; channel = 'experimental'; source_revision = $revision;
    source_dirty = [bool]$dirty.Count; code_signed = $false; payload_manifest = 'manifest.json inside the portable package'; files = $artifactFiles } |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $artifactRoot 'release-manifest.json') -Encoding UTF8
@($installerName, $portableName, 'release-manifest.json' | Sort-Object) | ForEach-Object {
    (Get-FileHash -LiteralPath (Join-Path $artifactRoot $_) -Algorithm SHA256).Hash + '  ' + $_
} | Set-Content -LiteralPath (Join-Path $artifactRoot 'SHA256SUMS.txt') -Encoding ASCII
Write-Host ('Release artifacts ready: ' + $artifactRoot)
Write-Host 'Experimental, unsigned artifacts; publication and installation are separate actions.'
exit 0
