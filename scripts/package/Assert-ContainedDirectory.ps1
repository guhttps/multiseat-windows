[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RootPath,
    [Parameter(Mandatory)][string]$TargetPath,
    [switch]$RequireNew
)

# Pure path validation; performs no filesystem changes.
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($RootPath).TrimEnd('\')
$target = [IO.Path]::GetFullPath($TargetPath)
if (-not (Test-Path -LiteralPath $root -PathType Container) -or
    -not $target.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase) -or
    ($RequireNew -and (Test-Path -LiteralPath $target))) {
    throw 'Use a contained directory within the approved root.'
}
$cursor = $target
while ($cursor) {
    if ((Test-Path -LiteralPath $cursor) -and
        ((Get-Item -LiteralPath $cursor -Force -ErrorAction Stop).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'A directory ancestor redirects outside the approved location.'
    }
    $cursor = [IO.Path]::GetDirectoryName($cursor)
}
Write-Output $target
