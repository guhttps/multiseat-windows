[CmdletBinding()]
param([Parameter(Mandatory)][string]$Directory)

# Bootstrap only on an ephemeral GitHub-hosted build runner, never the personal PC.
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted' -or -not $env:RUNNER_TEMP) {
    throw 'Compiler bootstrap is restricted to ephemeral GitHub-hosted runners.'
}
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$lock = Get-Content -LiteralPath (Join-Path $projectRoot 'installer/toolchain.json') -Raw | ConvertFrom-Json
$directoryPath = & (Join-Path $PSScriptRoot 'Assert-ContainedDirectory.ps1') -RootPath $env:RUNNER_TEMP -TargetPath $Directory -RequireNew
New-Item -ItemType Directory -Path $directoryPath | Out-Null
$downloadPath = Join-Path $directoryPath 'inno-compiler-bootstrap.exe'
Invoke-WebRequest -UseBasicParsing -Uri $lock.download_url -OutFile $downloadPath -TimeoutSec 120
if ((Get-FileHash -LiteralPath $downloadPath -Algorithm SHA256).Hash -ne $lock.download_sha256) {
    throw 'Downloaded Inno Setup installer does not match its pinned SHA-256.'
}
$signature = Get-AuthenticodeSignature -LiteralPath $downloadPath
$organization = [regex]::Escape($lock.authenticode_organization)
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch (', O=' + $organization + ',')) {
    throw 'Downloaded compiler installer must have a valid signature from the pinned publisher.'
}
$toolDirectory = Join-Path $directoryPath 'compiler'
$arguments = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /CURRENTUSER /NOICONS /DIR="' + $toolDirectory + '"'
$process = Start-Process -FilePath $downloadPath -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
try {
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw 'Compiler installation on the build runner failed.' }
} finally { $process.Dispose() }
$compiler = Join-Path $toolDirectory 'ISCC.exe'
& (Join-Path $PSScriptRoot 'Test-InnoCompiler.ps1') -IsccPath $compiler
if ($env:GITHUB_ENV) { ('MULTISEAT_ISCC=' + $compiler) | Out-File -LiteralPath $env:GITHUB_ENV -Encoding utf8 -Append }
