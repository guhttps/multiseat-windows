[CmdletBinding()]
param([string]$OutputDirectory)

$ErrorActionPreference = 'Stop'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $PSScriptRoot '../../../build/rds-guest-observer' }
$compiler = Join-Path ([Environment]::GetFolderPath('Windows')) 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) { throw 'The Windows x64 .NET Framework compiler is required.' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null
$executable = Join-Path $output 'RdsLabObserver.exe'
$source = Join-Path $PSScriptRoot 'RdsLabObserver.cs'
$oldLib = $env:LIB
try {
    $env:LIB = $null
    & $compiler /nologo /target:winexe /platform:x64 /optimize+ /debug- /warn:4 /warnaserror+ `
        /r:System.Management.dll /r:System.Runtime.Serialization.dll /r:System.Security.dll `
        ('/out:' + $executable) $source
    if ($LASTEXITCODE -ne 0) { throw 'Guest observer compilation failed.' }
} finally { $env:LIB = $oldLib }
$test = Start-Process -FilePath $executable -ArgumentList '--self-test' -WindowStyle Hidden -PassThru
if (-not $test.WaitForExit(15000)) { $test.Kill(); throw 'Guest observer self-test timed out.' }
if ($test.ExitCode -ne 0) { throw 'Guest observer offline self-test failed.' }
Write-Output 'PASS: guest observer compiled; offline fixture comparisons and report serialization passed.'
