[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$outputDirectory = Join-Path $projectRoot 'build/child-session/Release'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$compiler = Join-Path $env:windir 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw '.NET Framework x64 compiler not found.' }
$source = Join-Path $projectRoot 'experiments/006-child-session/ChildSessionTest.cs'
$executable = Join-Path $outputDirectory 'ms-child-session-test.exe'
# This host uses only .NET Framework references. Native LIB directories inherited
# from a Visual Studio shell can be stale and cause CSC warnings under /warnaserror.
$hadLibraryPath = Test-Path Env:LIB
$previousLibraryPath = $env:LIB
try {
    Remove-Item Env:LIB -ErrorAction SilentlyContinue
    & $compiler /nologo /target:exe /platform:x64 /warn:4 /warnaserror+ "/out:$executable" /reference:System.Windows.Forms.dll /reference:System.Drawing.dll $source
    if ($LASTEXITCODE -ne 0) { throw 'Child-session host build failed.' }
} finally {
    if ($hadLibraryPath) { $env:LIB = $previousLibraryPath }
    else { Remove-Item Env:LIB -ErrorAction SilentlyContinue }
}
Write-Host ('Built: ' + $executable)
