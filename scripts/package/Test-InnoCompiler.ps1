[CmdletBinding()]
param([Parameter(Mandatory)][string]$IsccPath)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$lock = Get-Content -LiteralPath (Join-Path $projectRoot 'installer/toolchain.json') -Raw | ConvertFrom-Json
$compiler = Get-Item -LiteralPath $IsccPath -Force -ErrorAction Stop
if ($compiler.Name -ne 'ISCC.exe' -or $compiler.PSIsContainer -or
    $compiler.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Expected a regular Inno Setup compiler executable.' }
if ((Get-FileHash -LiteralPath $compiler.FullName -Algorithm SHA256).Hash -ne $lock.compiler_sha256) {
    throw 'Inno Setup compiler does not match the pinned toolchain.'
}
$compilerDll = Join-Path $compiler.DirectoryName 'ISCmplr.dll'
if ((Get-FileHash -LiteralPath $compilerDll -Algorithm SHA256).Hash -ne $lock.compiler_dll_sha256) {
    throw 'Inno Setup compiler DLL does not match the pinned toolchain.'
}
$signature = Get-AuthenticodeSignature -LiteralPath $compiler.FullName
$organization = [regex]::Escape($lock.authenticode_organization)
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch (', O=' + $organization + ',')) {
    throw 'Inno Setup compiler must have a valid signature from the pinned publisher.'
}
Write-Output ('PASS: pinned Inno Setup ' + $lock.inno_setup_version + ' compiler and Authenticode publisher.')
