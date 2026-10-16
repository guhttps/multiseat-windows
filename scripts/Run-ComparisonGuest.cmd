@echo off
setlocal
title Multiseat - teste na VM

rem Reject the physical host before extraction, elevation or changes.
powershell.exe -NoProfile -Command "$ErrorActionPreference = 'Stop'; try { if ((Get-CimInstance Win32_ComputerSystem).Model -ne 'Virtual Machine') { Write-Host 'Execute este arquivo dentro da VM Hyper-V.'; exit 2 }; $identity = [Security.Principal.WindowsIdentity]::GetCurrent(); try { $admin = ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) } finally { $identity.Dispose() }; if (-not $admin) { exit 10 }; exit 0 } catch { Write-Host $_.Exception.Message; exit 2 }"
set "guardExit=%ERRORLEVEL%"
if "%guardExit%"=="10" goto elevate
if not "%guardExit%"=="0" goto guardfailed
if not "%~1"=="--window" goto openwindow

rem The expected hash is supplied by the host's validated transfer, not embedded in code.
powershell.exe -NoProfile -Command "$ErrorActionPreference = 'Stop'; try { function Assert-LocalPath([string]$path) { $cursor = [IO.Path]::GetFullPath($path); while ($cursor) { if (Test-Path -LiteralPath $cursor) { if ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Redirecionamento de arquivo recusado.' } }; $cursor = [IO.Path]::GetDirectoryName($cursor) } }; $zipPath = 'C:\Users\Public\comparison-kit.zip'; $hashPath = 'C:\Users\Public\comparison-kit.sha256'; $destination = 'C:\Users\Public\MultiseatComparison'; Assert-LocalPath $zipPath; Assert-LocalPath $hashPath; Assert-LocalPath $destination; Assert-LocalPath (Join-Path $destination 'docs\lab\results'); $expected = (Get-Content -LiteralPath $hashPath -Raw).Trim(); if ($expected -notmatch '^[A-Fa-f0-9]{64}$' -or (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash -ne $expected) { throw 'O pacote nao corresponde ao hash transferido.' }; $allowed = @('README.md', 'build/child-session/Release/ms-child-session-probe.exe', 'build/child-session/Release/ms-child-session-test.exe', 'build/child-session/Release/ms-session-probe.exe', 'scripts/Invoke-ChildSessionTest.ps1'); $directories = @('build/', 'build/child-session/', 'build/child-session/Release/', 'scripts/'); Add-Type -AssemblyName System.IO.Compression.FileSystem; $archive = [IO.Compression.ZipFile]::OpenRead($zipPath); try { if ($archive.Entries.Count -gt 32) { throw 'Quantidade inesperada de arquivos.' }; $entries = @{}; foreach ($entry in $archive.Entries) { $name = $entry.FullName.Replace('\', '/'); if ($entries.ContainsKey($name) -or ($name -notin $allowed -and $name -notin $directories -and $name -ne 'manifest.json') -or $entry.Length -gt 32MB) { throw 'Arquivo inesperado, duplicado ou grande demais no pacote.' }; $entries[$name] = $entry }; $manifestEntry = $entries['manifest.json']; if (-not $manifestEntry -or $manifestEntry.Length -gt 64KB) { throw 'Manifesto ausente ou grande demais.' }; $reader = [IO.StreamReader]::new($manifestEntry.Open()); try { $manifest = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }; if (@($manifest.files).Count -ne $allowed.Count) { throw 'Manifesto inesperado.' }; $seen = @{}; foreach ($file in $manifest.files) { $name = ([string]$file.path).Replace('\', '/'); if ($name -notin $allowed -or $seen.ContainsKey($name) -or $file.sha256 -notmatch '^[a-fA-F0-9]{64}$' -or -not $entries[$name]) { throw 'Arquivo inesperado no manifesto.' }; $seen[$name] = $true; $stream = $entries[$name].Open(); $hasher = [Security.Cryptography.SHA256]::Create(); try { $hash = [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-', '') } finally { $stream.Dispose(); $hasher.Dispose() }; if ($hash -ne $file.sha256) { throw 'Conteudo do pacote nao corresponde ao manifesto.' }; Assert-LocalPath (Join-Path $destination $name) }; if (Test-Path -LiteralPath $destination) { foreach ($file in $manifest.files) { $target = Join-Path $destination $file.path; if (-not (Test-Path -LiteralPath $target -PathType Leaf) -or (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $file.sha256) { throw ('Arquivo ausente ou diferente: ' + $file.path + '. Nenhum arquivo existente foi substituido.') } } } else { New-Item -ItemType Directory -Path $destination | Out-Null; foreach ($name in @($allowed) + @('manifest.json')) { $target = Join-Path $destination $name; New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force | Out-Null; [IO.Compression.ZipFileExtensions]::ExtractToFile($entries[$name], $target, $false) } } } finally { $archive.Dispose() }; Write-Host 'Pacote conferido. Arquivos existentes preservados.'; exit 0 } catch { Write-Host $_.Exception.Message; exit 2 }"
if errorlevel 1 goto preparationfailed
cd /d "C:\Users\Public\MultiseatComparison"
if errorlevel 1 goto preparationfailed

rem Display a previous attempt instead of silently connecting again.
powershell.exe -NoProfile -Command "$ErrorActionPreference = 'Stop'; try { function Assert-LogPath([string]$path) { $cursor = [IO.Path]::GetFullPath($path); while ($cursor) { if (Test-Path -LiteralPath $cursor) { if ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Redirecionamento de log recusado.' } }; $cursor = [IO.Path]::GetDirectoryName($cursor) } }; $root = 'C:\Users\Public\MultiseatComparison\docs\lab\results'; Assert-LogPath $root; if (-not (Test-Path -LiteralPath $root)) { exit 0 }; $folders = @((Get-Item -LiteralPath $root)) + @(Get-ChildItem -LiteralPath $root -Directory -Filter 'guest-attempt-*'); $logs = @($folders | ForEach-Object { Assert-LogPath $_.FullName; $path = Join-Path $_.FullName 'guest-child-default.log'; Assert-LogPath $path; if (Test-Path -LiteralPath $path -PathType Leaf) { Get-Item -LiteralPath $path } }); if (-not $logs.Count) { exit 0 }; $previous = $logs | Sort-Object LastWriteTime -Descending | Select-Object -First 1; Write-Host 'RESULTADO ANTERIOR: nenhuma nova conexao foi iniciada.'; foreach ($name in @('guest-version.log', 'guest-sessions.log', 'guest-smoke.log', 'guest-child-default.log', 'guest-child-exit.txt')) { $path = Join-Path $previous.DirectoryName $name; Assert-LogPath $path; if (Test-Path -LiteralPath $path -PathType Leaf) { if ((Get-Item -LiteralPath $path).Length -gt 1MB) { throw 'Log grande demais para exibicao automatica.' }; Write-Host ('--- ' + $name); Get-Content -LiteralPath $path } }; Write-Host ('Logs: ' + $previous.DirectoryName); Write-Host 'O arquivo pode ser de uma tentativa interrompida; envie esta saida para avaliacao.'; exit 20 } catch { Write-Host $_.Exception.Message; exit 2 }"
set "previousExit=%ERRORLEVEL%"
if "%previousExit%"=="20" goto previousresult
if not "%previousExit%"=="0" goto preparationfailed

rem A fresh directory preserves any partial diagnostic logs from an earlier launch.
for /f %%R in ('powershell.exe -NoProfile -Command "[Guid]::NewGuid().ToString('N')"') do set "resultDir=docs\lab\results\guest-attempt-%%R"
if not defined resultDir goto preparationfailed
mkdir "%resultDir%"
if errorlevel 1 goto preparationfailed
powershell.exe -NoProfile -Command "Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -ErrorAction Stop | Select-Object EditionID, DisplayVersion, CurrentBuild, UBR | Format-List" > "%resultDir%\guest-version.log" 2>&1
if errorlevel 1 goto preparationfailed
type "%resultDir%\guest-version.log"

build\child-session\Release\ms-session-probe.exe > "%resultDir%\guest-sessions.log" 2>&1
set "probeExit=%ERRORLEVEL%"
type "%resultDir%\guest-sessions.log"
if not "%probeExit%"=="0" goto experimentfailed

build\child-session\Release\ms-child-session-test.exe --smoke > "%resultDir%\guest-smoke.log" 2>&1
set "smokeExit=%ERRORLEVEL%"
type "%resultDir%\guest-smoke.log"
if not "%smokeExit%"=="0" goto experimentfailed

echo.
echo Teste de sessao filha: aguarde ate 20 segundos.
build\child-session\Release\ms-child-session-test.exe --test > "%resultDir%\guest-child-default.log" 2>&1
set "childExit=%ERRORLEVEL%"
> "%resultDir%\guest-child-exit.txt" echo %childExit%
type "%resultDir%\guest-child-default.log"
echo.
echo Codigo de saida do teste: %childExit%
echo Logs: C:\Users\Public\MultiseatComparison\%resultDir%
echo Envie uma captura desta janela. Nenhuma senha e solicitada pelo teste.
pause
exit /b %childExit%

:elevate
powershell.exe -NoProfile -Command "try { Start-Process -FilePath (Join-Path $env:windir 'System32\cmd.exe') -ArgumentList '/d /k C:\Users\Public\Executar-Teste.cmd --window' -Verb RunAs -WindowStyle Normal | Out-Null; exit 0 } catch { Write-Host 'A elevacao foi cancelada ou falhou. Nenhum teste foi iniciado.'; exit 2 }"
if errorlevel 1 goto guardfailed
exit /b 0

:openwindow
powershell.exe -NoProfile -Command "try { Start-Process -FilePath (Join-Path $env:windir 'System32\cmd.exe') -ArgumentList '/d /k C:\Users\Public\Executar-Teste.cmd --window' -WindowStyle Normal | Out-Null; exit 0 } catch { Write-Host $_.Exception.Message; exit 2 }"
if errorlevel 1 goto guardfailed
exit /b 0

:previousresult
echo Envie uma captura desta janela para avaliarmos o resultado anterior.
pause
exit /b 0

:experimentfailed
echo O diagnostico ou smoke falhou. A conexao de teste nao foi iniciada.
pause
exit /b 2

:preparationfailed
echo Preparacao interrompida. Nenhuma conexao de teste foi iniciada.
pause
exit /b 2

:guardfailed
echo Teste nao iniciado.
pause
exit /b 2
