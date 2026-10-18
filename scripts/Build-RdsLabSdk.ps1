[CmdletBinding()]
param(
    [string]$BuildRoot,
    [ValidateRange(1, 16)][int]$Parallel = 4
)

# Build-only SDK. No elevation, global install, VM, accounts or network settings.
# Raw compiler logs and generated source/binaries stay under ignored build/.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
Set-StrictMode -Version 2.0

function Assert-SdkPath {
    param([string]$Path, [string]$Boundary)
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    $root = [IO.Path]::GetFullPath($Boundary).TrimEnd('\')
    if (-not $full.Equals($root, [StringComparison]::OrdinalIgnoreCase) -and
        -not $full.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'SDK path leaves its approved build directory.'
    }
    $cursor = $full
    while ($cursor) {
        if (Test-Path -LiteralPath $cursor) {
            if ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw 'Redirected SDK path rejected.'
            }
        }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
    $full
}

function Assert-SdkTree {
    param([string]$Path, [string]$Boundary)
    Assert-SdkPath $Path $Boundary | Out-Null
    if (Test-Path -LiteralPath $Path) {
        # Enumerate one directory at a time; never recurse through a reparse point.
        $pending = [Collections.Generic.Queue[string]]::new()
        $pending.Enqueue($Path)
        while ($pending.Count) {
            foreach ($item in Get-ChildItem -LiteralPath $pending.Dequeue() -Force) {
                if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Redirected SDK tree entry rejected.' }
                if ($item.PSIsContainer) { $pending.Enqueue($item.FullName) }
            }
        }
    }
}

function Assert-SdkTagRefs {
    param([string[]]$Lines, [string]$Tag, [string]$TagObject, [string]$Commit)
    $refs = @{}
    foreach ($line in $Lines) {
        if ($line -notmatch '^([a-f0-9]{40})\s+(refs/tags/[^\s]+)$' -or $refs.ContainsKey($Matches[2])) {
            throw 'Invalid source tag response.'
        }
        $refs[$Matches[2]] = $Matches[1]
    }
    $tagRef = 'refs/tags/' + $Tag
    if ($TagObject -notmatch '^[a-f0-9]{40}$' -or $Commit -notmatch '^[a-f0-9]{40}$' -or
        -not $refs.ContainsKey($tagRef) -or $refs[$tagRef] -cne $TagObject) { throw 'Source tag object changed.' }
    if ($TagObject -ceq $Commit) {
        if ($refs.Count -ne 1 -or $refs.ContainsKey($tagRef + '^{}')) { throw 'Unexpected lightweight tag response.' }
    } elseif ($refs.Count -ne 2 -or -not $refs.ContainsKey($tagRef + '^{}') -or $refs[$tagRef + '^{}'] -cne $Commit) {
        throw 'Source tag no longer peels to its pinned commit.'
    }
}

function Get-SdkCommandFailureCategory {
    param([string]$Text)
    if ($Text -match '(?i)unsupported SSL backend') { return 'tls_backend_unavailable' }
    if ($Text -match '(?i)error setting certificate (?:file|verify locations)|CAfile:|CApath:') { return 'tls_ca_file' }
    if ($Text -match '(?i)certificate verify failed|unable to get local issuer|SSL certificate problem|CERT_E_|SEC_E_UNTRUSTED_ROOT') {
        return 'tls_certificate_validation'
    }
    if ($Text -match '(?i)could not resolve host|couldn.t resolve host') { return 'dns_resolution' }
    if ($Text -match '(?i)HTTP[/ ]?2|HTTP_1_1_REQUIRED') { return 'http2_failure' }
    if ($Text -match '(?i)(?:HTTP|error|returned)\D{0,24}429\b|rate limit|too many requests') { return 'http_rate_limit' }
    if ($Text -match '(?i)authentication failed|terminal prompts disabled|could not read Username|returned error:\s*(?:401|403)') {
        return 'unexpected_authentication'
    }
    if ($Text -match '(?i)failed to connect|connection timed out|could not connect|connection reset') { return 'network_connection' }
    return 'native_command_failed'
}

function Invoke-SdkCommand {
    param([string]$Executable, [string[]]$Arguments, [string]$Directory, [string]$LogName, [switch]$ReadOutput)
    $script:lastCommandStage = $LogName
    $script:lastCommandExitCode = $null
    $script:lastCommandCategory = 'command_not_completed'
    $script:lastCommandOutputLines = $null
    $script:lastCommandRefLinesValid = $null
    Assert-SdkPath $Directory $script:sdkRoot | Out-Null
    $log = Assert-SdkPath (Join-Path $script:logRoot ($LogName + '.private.log')) $script:sdkRoot
    Push-Location -LiteralPath $Directory
    $previousPreference = $ErrorActionPreference
    try {
        # In Windows PowerShell, stderr warnings can otherwise become terminating
        # NativeCommandError even when the native process exits successfully.
        $ErrorActionPreference = 'Continue'
        # Windows PowerShell updates the global automatic variable. A local
        # shadow would keep a stale/null value even after a native process exits.
        $global:LASTEXITCODE = $null
        & $Executable @Arguments *> $log
        $nativeExit = $global:LASTEXITCODE
        $script:lastCommandExitCode = $nativeExit
    } finally {
        $ErrorActionPreference = $previousPreference
        Pop-Location
    }
    if ($null -eq $nativeExit -or $nativeExit -ne 0) {
        $script:lastCommandCategory = 'native_command_failed'
        if (Test-Path -LiteralPath $log -PathType Leaf) {
            if ((Get-Item -LiteralPath $log -Force).Length -le 1MB) {
                $script:lastCommandCategory = Get-SdkCommandFailureCategory (Get-Content -LiteralPath $log -Raw)
            }
        }
        throw ('SDK command failed at stage ' + $LogName + '; inspect ignored build logs locally.')
    }
    $script:lastCommandCategory = 'none'
    # Detect the redirection BOM: Windows PowerShell writes UTF-16, PS7 UTF-8.
    if ($ReadOutput) {
        $lines = @(Get-Content -LiteralPath $log)
        $script:lastCommandOutputLines = $lines.Count
        if ($LogName.EndsWith('-remote-tag', [StringComparison]::Ordinal)) {
            $script:lastCommandRefLinesValid = $lines.Count -gt 0 -and
                @($lines | Where-Object { $_ -notmatch '^[a-f0-9]{40}\s+refs/tags/[^\s]+$' }).Count -eq 0
        }
        $lines
    }
}

function Invoke-SdkGit {
    param([string[]]$Arguments, [string]$Directory, [string]$LogName, [switch]$ReadOutput)
    $fixed = @('--no-replace-objects', '-c', 'core.hooksPath=NUL', '-c', 'core.fsmonitor=false',
        '-c', 'core.symlinks=false', '-c', 'core.autocrlf=false', '-c', 'protocol.file.allow=never',
        '-c', 'transfer.fsckObjects=true', '-c', 'http.sslBackend=schannel', '-c', 'http.sslVerify=true')
    Invoke-SdkCommand $script:git ($fixed + $Arguments) $Directory $LogName -ReadOutput:$ReadOutput
}

function Assert-SdkSource {
    param($Pin, [string]$Directory)
    Assert-SdkTree $Directory $script:sdkRoot
    if (-not (Test-Path -LiteralPath (Join-Path $Directory '.git') -PathType Container) -or
        (Test-Path -LiteralPath (Join-Path $Directory '.git/commondir'))) { throw 'Source is not a contained independent checkout.' }
    $head = @(Invoke-SdkGit @('rev-parse', '--verify', 'HEAD') $Directory ($Pin.name + '-head') -ReadOutput)
    $tag = @(Invoke-SdkGit @('rev-parse', '--verify', ('refs/tags/' + $Pin.tag)) $Directory ($Pin.name + '-tag') -ReadOutput)
    $peeled = @(Invoke-SdkGit @('rev-parse', '--verify', ('refs/tags/' + $Pin.tag + '^{commit}')) $Directory ($Pin.name + '-peeled') -ReadOutput)
    $dirty = @(Invoke-SdkGit @('status', '--porcelain=v1', '--untracked-files=all') $Directory ($Pin.name + '-status') -ReadOutput)
    if ($head.Count -ne 1 -or $head[0] -cne $Pin.commit -or $tag.Count -ne 1 -or $tag[0] -cne $Pin.tag_object -or
        $peeled.Count -ne 1 -or $peeled[0] -cne $Pin.commit -or $dirty.Count -ne 0) {
        throw 'Source HEAD, tag or clean-tree verification failed.'
    }
}

function Get-SdkPayloadFiles {
    param([string]$Directory)
    Assert-SdkTree $Directory $script:sdkRoot
    $root = [IO.Path]::GetFullPath($Directory).TrimEnd('\') + '\'
    @(Get-ChildItem -LiteralPath $Directory -File -Recurse -Force | Sort-Object FullName | ForEach-Object {
        [PSCustomObject][ordered]@{ path = $_.FullName.Substring($root.Length).Replace('\', '/')
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
}

function Assert-SdkReceipt {
    param($Receipt, [string]$ExpectedRecipe)
    if ($null -eq $Receipt -or $Receipt.schema_version -ne 1 -or
        $Receipt.purpose -cne 'MultiseatRdsLab.SourceSdk' -or $Receipt.recipe_sha256 -cne $ExpectedRecipe -or
        $Receipt.complete -isnot [bool] -or -not $Receipt.complete -or @($Receipt.files).Count -eq 0) {
        throw 'SDK directory has no matching completed ownership receipt. Preserve it for inspection and use a fresh build tree.'
    }
    $seen = @{}
    foreach ($file in $Receipt.files) {
        if ($file.path -isnot [string] -or $file.path -notmatch '^[A-Za-z0-9_.+-]+(/[A-Za-z0-9_.+-]+)*$' -or
            @($file.path.Split('/') | Where-Object { $_ -in @('.', '..') }).Count -or $seen.ContainsKey($file.path) -or
            $file.sha256 -notmatch '^[a-f0-9]{64}$') { throw 'Invalid SDK ownership file list.' }
        $seen[$file.path] = $true
    }
}

$pins = @(
    [PSCustomObject][ordered]@{ name = 'Perl'; url = 'https://github.com/Perl/perl5.git'; tag = 'v5.42.1'
        tag_object = '66862f2d39ee94d28d3fe822550c5b4030444c10'; commit = '785bc8fe8ed6009ff0d6e657fe0aba7155188873'; directory = 'perl-source' },
    [PSCustomObject][ordered]@{ name = 'OpenSSL'; url = 'https://github.com/openssl/openssl.git'; tag = 'openssl-3.5.9'
        tag_object = 'd0ca66a1abe52545f14eca635c648932fcde5615'; commit = '45e844fa2a14ec92d146bd8f5778ac130b6625fb'; directory = 'openssl-source' },
    [PSCustomObject][ordered]@{ name = 'zlib'; url = 'https://github.com/madler/zlib.git'; tag = 'v1.3.2'
        tag_object = '216c70c020aa53f0c40920d155f808b6b59c9acb'; commit = 'da607da739fa6047df13e66a2af6b8bec7c2a498'; directory = 'zlib-source' },
    [PSCustomObject][ordered]@{ name = 'FreeRDP'; url = 'https://github.com/FreeRDP/FreeRDP.git'; tag = '3.32.2'
        tag_object = 'c7e6848d15e613fda3d3ee7ebca36146088802a2'; commit = '97eff6ddfbbfaa8e42a126d362686bd001ff8bdc'; directory = 'freerdp-source' }
)
$freerdpFlags = @(
    '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL', '-DBUILD_SHARED_LIBS=ON', '-DBUILD_TESTING=OFF',
    '-DWITH_CLIENT_COMMON=OFF', '-DWITH_CLIENT=OFF', '-DWITH_SERVER=OFF', '-DWITH_CHANNELS=OFF',
    '-DWITH_SAMPLE=OFF', '-DWITH_WINPR_TOOLS=OFF', '-DWITH_FFMPEG=OFF', '-DWITH_SWSCALE=OFF',
    '-DWITH_OPENH264=OFF', '-DWITH_JPEG=OFF', '-DWITH_WINMM=OFF', '-DWITH_MEDIA_FOUNDATION=OFF',
    '-DWITH_SMARTCARD_EMULATE=OFF', '-DWITH_SMARTCARD_PCSC=OFF', '-DWITH_AAD=OFF',
    '-DWITH_JSON_DISABLED=ON', '-DWITH_URIPARSER=OFF', '-DWITH_PKCS11=OFF', '-DWITH_KRB5=OFF',
    '-DWITH_NATIVE_SSPI=ON', '-DWITH_OPENSSL=ON', '-DWITH_MBEDTLS=OFF',
    '-DWITH_CLANG_FORMAT=OFF', '-DWITH_CCACHE=OFF', '-DWITH_MANPAGES=OFF', '-DWITH_DEBUG_ALL=OFF',
    '-DWITH_DEBUG_NLA=OFF', '-DWITH_DEBUG_CERTIFICATE=OFF', '-DWITH_DEBUG_KBD=OFF'
)
$recipe = [ordered]@{ version = 1; sources = $pins; freerdp_flags = $freerdpFlags
    perl = 'nmake CCTYPE=MSVC143 INST_TOP=<sdk>/perl-install USE_NO_REGISTRY=define; installbare'
    openssl = 'VC-WIN64A shared no-asm no-tests no-docs; out-of-tree; --prefix=<sdk>/install --openssldir=<sdk>/install/ssl --libdir=lib; build_sw; install_sw'
    zlib = 'Ninja Release; ZLIB_BUILD_TESTING=OFF; ZLIB_BUILD_SHARED=OFF; ZLIB_BUILD_STATIC=ON' }
$recipeBytes = [Text.Encoding]::UTF8.GetBytes(($recipe | ConvertTo-Json -Depth 8 -Compress))
$sha = [Security.Cryptography.SHA256]::Create()
try { $recipeHash = ([BitConverter]::ToString($sha.ComputeHash($recipeBytes))).Replace('-', '').ToLowerInvariant() }
finally { $sha.Dispose() }

$stage = 'preflight'
$script:lastCommandStage = 'none'
$script:lastCommandExitCode = $null
$script:lastCommandCategory = 'none'
$script:lastCommandOutputLines = $null
$script:lastCommandRefLinesValid = $null
$ownedTreeCreated = $false
$environmentBefore = [Environment]::GetEnvironmentVariables('Process')
$lock = $null
try {
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT -or -not [Environment]::Is64BitProcess) {
        throw 'Windows x64 PowerShell is required.'
    }
    $projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    $defaultRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build/rds-sdk'))
    if (-not $BuildRoot) { $BuildRoot = $defaultRoot }
    $requestedRoot = [IO.Path]::GetFullPath($BuildRoot).TrimEnd('\')
    if ($requestedRoot.Equals($defaultRoot, [StringComparison]::OrdinalIgnoreCase) -or
        $requestedRoot.StartsWith($defaultRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        $script:sdkRoot = Assert-SdkPath $requestedRoot $defaultRoot
    } elseif ($env:GITHUB_ACTIONS -ceq 'true' -and $env:RUNNER_ENVIRONMENT -ceq 'github-hosted' -and $env:RUNNER_TEMP) {
        $runnerRoot = [IO.Path]::GetFullPath($env:RUNNER_TEMP).TrimEnd('\')
        if ($requestedRoot.Equals($runnerRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Use a new SDK subdirectory inside runner temporary storage.' }
        $script:sdkRoot = Assert-SdkPath $requestedRoot $runnerRoot
    } else { throw 'Use project build/rds-sdk; only hosted CI may use a contained RUNNER_TEMP subdirectory.' }
    $prefix = Join-Path $script:sdkRoot 'install'
    $receiptPath = Join-Path $script:sdkRoot 'sdk-manifest.json'
    if (Test-Path -LiteralPath $script:sdkRoot) {
        Assert-SdkTree $script:sdkRoot $script:sdkRoot
        if (-not (Test-Path -LiteralPath $receiptPath -PathType Leaf) -or (Get-Item -LiteralPath $receiptPath -Force).Length -gt 4MB) {
            throw 'Existing SDK tree has no valid ownership receipt; it was not modified.'
        }
        $receipt = Get-Content -LiteralPath $receiptPath -Raw -Encoding UTF8 | ConvertFrom-Json
        Assert-SdkReceipt $receipt $recipeHash
        $actual = @(Get-SdkPayloadFiles $prefix)
        if ($actual.Count -ne @($receipt.files).Count) { throw 'Existing SDK payload has unexpected files.' }
        $expected = @{}; foreach ($file in $receipt.files) { $expected[$file.path] = $file.sha256 }
        foreach ($file in $actual) {
            if (-not $expected.ContainsKey($file.path) -or $expected[$file.path] -cne $file.sha256) { throw 'Existing SDK payload changed.' }
        }
        Write-Output 'RDS SDK: verified existing completed build.'
        $global:LASTEXITCODE = 0
        return
    }
    Write-Output 'RDS SDK: preparing a new contained source build.'
    [IO.Directory]::CreateDirectory($script:sdkRoot) | Out-Null
    $script:logRoot = Join-Path $script:sdkRoot 'logs'
    [IO.Directory]::CreateDirectory($script:logRoot) | Out-Null
    $lock = [IO.File]::Open((Join-Path $script:sdkRoot 'build.lock'), [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    $receipt = [ordered]@{ schema_version = 1; purpose = 'MultiseatRdsLab.SourceSdk'; recipe_sha256 = $recipeHash
        complete = $false; sources = $pins; recipe = $recipe; files = @(); tests_disabled = $true; redistribution_approved = $false }
    [IO.File]::WriteAllText($receiptPath, ($receipt | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))
    $ownedTreeCreated = $true

    # Ignore ambient Git credential/config hooks and build-injection variables.
    $emptyConfig = Join-Path $script:sdkRoot 'empty-git-config'
    [IO.File]::WriteAllText($emptyConfig, [String]::Empty)
    foreach ($name in @('CC', 'CXX', 'CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS', 'CL', '_CL_', 'LINK', '_LINK_', 'MAKEFLAGS', 'DESTDIR', 'INSTALLDIR',
        'CMAKE_TOOLCHAIN_FILE', 'CMAKE_GENERATOR', 'CMAKE_PREFIX_PATH', 'PERL5LIB', 'PERLLIB', 'PERL5OPT', 'PERL5DB',
        'PERL5SHELL', 'OPENSSL_CONF', 'OPENSSL_MODULES', 'OPENSSL_LOCAL_CONFIG_DIR', 'GIT_CONFIG_COUNT', 'GIT_CONFIG_PARAMETERS', 'GIT_WORK_TREE', 'GIT_DIR', 'GIT_INDEX_FILE',
        'GIT_OBJECT_DIRECTORY', 'GIT_ALTERNATE_OBJECT_DIRECTORIES', 'GIT_SSL_NO_VERIFY', 'GIT_SSL_CAINFO', 'GIT_SSL_CAPATH',
        'GIT_SSL_VERSION', 'GIT_SSL_CIPHER_LIST', 'GIT_SSL_CERT', 'GIT_SSL_KEY')) { [Environment]::SetEnvironmentVariable($name, $null, 'Process') }
    $env:COMSPEC = Join-Path ([Environment]::GetFolderPath('System')) 'cmd.exe'
    $env:GIT_CONFIG_NOSYSTEM = '1'; $env:GIT_CONFIG_GLOBAL = $emptyConfig; $env:GIT_CONFIG_SYSTEM = $emptyConfig
    # ls-remote must not inherit the project's local .git/config through build/.
    $env:GIT_CEILING_DIRECTORIES = $script:sdkRoot
    $env:GIT_TERMINAL_PROMPT = '0'; $env:GCM_INTERACTIVE = 'Never'; $env:GIT_NO_REPLACE_OBJECTS = '1'
    $env:LANG = 'C'; $env:LC_ALL = 'C'; $env:LC_CTYPE = 'C'
    $script:git = (Get-Command git.exe -CommandType Application -ErrorAction Stop).Source
    $vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) { throw 'Visual Studio Installer vswhere is required.' }
    $vs = @(Invoke-SdkCommand $vswhere @('-latest', '-products', '*', '-version', '[17.0,19.0)',
        '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath') $script:sdkRoot 'vswhere' -ReadOutput)
    if ($vs.Count -ne 1) { throw 'Visual Studio 2022 or 2026 C++ Build Tools are required.' }
    $devShell = Join-Path $vs[0] 'Common7/Tools/Launch-VsDevShell.ps1'
    if (-not (Test-Path -LiteralPath $devShell -PathType Leaf)) { throw 'The installed Visual Studio developer shell is missing.' }
    & $devShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation *> (Join-Path $script:logRoot 'developer-shell.private.log')
    $nmake = (Get-Command nmake.exe -CommandType Application -ErrorAction Stop).Source
    $cmake = (Get-Command cmake.exe -CommandType Application -ErrorAction Stop).Source
    $ninja = (Get-Command ninja.exe -CommandType Application -ErrorAction Stop).Source
    $compiler = Get-Command cl.exe -CommandType Application -ErrorAction Stop
    if ($env:VSCMD_ARG_TGT_ARCH -cne 'x64' -or $env:VSCMD_ARG_HOST_ARCH -cne 'x64') { throw 'The MSVC shell is not x64 host/target.' }

    foreach ($pin in $pins) {
        $stage = 'source-' + $pin.name
        Write-Output ('RDS SDK: verifying and fetching pinned ' + $pin.name + '.')
        $remote = @(Invoke-SdkGit @('ls-remote', '--exit-code', '--tags', $pin.url,
            ('refs/tags/' + $pin.tag), ('refs/tags/' + $pin.tag + '^{}')) $script:sdkRoot ($pin.name + '-remote-tag') -ReadOutput)
        try { Assert-SdkTagRefs $remote $pin.tag $pin.tag_object $pin.commit }
        catch {
            $script:lastCommandCategory = 'source_pin_mismatch'
            throw
        }
        $source = Assert-SdkPath (Join-Path $script:sdkRoot $pin.directory) $script:sdkRoot
        if (Test-Path -LiteralPath $source) { throw 'Refusing to reuse an unknown source directory.' }
        [IO.Directory]::CreateDirectory($source) | Out-Null
        Invoke-SdkGit @('init', '--quiet', '.') $source ($pin.name + '-init')
        Invoke-SdkGit @('remote', 'add', 'origin', $pin.url) $source ($pin.name + '-origin')
        Invoke-SdkGit @('fetch', '--depth=1', '--no-tags', 'origin', ('refs/tags/' + $pin.tag + ':refs/tags/' + $pin.tag)) $source ($pin.name + '-fetch')
        Invoke-SdkGit @('checkout', '--detach', $pin.commit) $source ($pin.name + '-checkout')
        Assert-SdkSource $pin $source
    }

    $stage = 'Perl'
    Write-Output 'RDS SDK: building local Perl (no registry installation).'
    $perlSource = Join-Path $script:sdkRoot 'perl-source'
    $perlPrefix = Join-Path $script:sdkRoot 'perl-install'
    Assert-SdkSource $pins[0] $perlSource
    $perlMake = @('/NOLOGO', 'CCTYPE=MSVC143', ('INST_TOP=' + $perlPrefix), 'USE_NO_REGISTRY=define')
    Invoke-SdkCommand $nmake $perlMake (Join-Path $perlSource 'win32') 'perl-build'
    Invoke-SdkCommand $nmake ($perlMake + @('installbare')) (Join-Path $perlSource 'win32') 'perl-install'
    $perl = Join-Path $perlPrefix 'bin/perl.exe'
    if (-not (Test-Path -LiteralPath $perl -PathType Leaf)) { throw 'Local Perl was not installed.' }
    $env:PATH = (Join-Path $perlPrefix 'bin') + ';' + (Join-Path $prefix 'bin') + ';' + $env:PATH

    $stage = 'OpenSSL'
    Write-Output 'RDS SDK: building pinned shared OpenSSL.'
    $opensslSource = Join-Path $script:sdkRoot 'openssl-source'
    $opensslBuild = Join-Path $script:sdkRoot 'openssl-build'
    [IO.Directory]::CreateDirectory($opensslBuild) | Out-Null
    Assert-SdkSource $pins[1] $opensslSource
    Invoke-SdkCommand $perl @((Join-Path $opensslSource 'Configure'), 'VC-WIN64A', 'shared', 'no-asm', 'no-tests', 'no-docs',
        ('--prefix=' + $prefix), ('--openssldir=' + (Join-Path $prefix 'ssl')), '--libdir=lib') $opensslBuild 'openssl-configure'
    Invoke-SdkCommand $nmake @('/NOLOGO', 'build_sw') $opensslBuild 'openssl-build'
    Invoke-SdkCommand $nmake @('/NOLOGO', 'install_sw') $opensslBuild 'openssl-install'

    $stage = 'zlib'
    Write-Output 'RDS SDK: building pinned zlib.'
    $zlibSource = Join-Path $script:sdkRoot 'zlib-source'
    $zlibBuild = Join-Path $script:sdkRoot 'zlib-build'
    Assert-SdkSource $pins[2] $zlibSource
    Invoke-SdkCommand $cmake @('-S', $zlibSource, '-B', $zlibBuild, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
        ('-DCMAKE_MAKE_PROGRAM=' + $ninja), ('-DCMAKE_INSTALL_PREFIX=' + $prefix), '-DZLIB_BUILD_TESTING=OFF',
        '-DZLIB_BUILD_SHARED=OFF', '-DZLIB_BUILD_STATIC=ON') $script:sdkRoot 'zlib-configure'
    Invoke-SdkCommand $cmake @('--build', $zlibBuild, '--parallel', $Parallel.ToString()) $script:sdkRoot 'zlib-build'
    Invoke-SdkCommand $cmake @('--install', $zlibBuild) $script:sdkRoot 'zlib-install'

    $stage = 'FreeRDP'
    Write-Output 'RDS SDK: building pinned FreeRDP and WinPR shared libraries.'
    $freerdpSource = Join-Path $script:sdkRoot 'freerdp-source'
    $freerdpBuild = Join-Path $script:sdkRoot 'freerdp-build'
    Assert-SdkSource $pins[3] $freerdpSource
    $arguments = @('-S', $freerdpSource, '-B', $freerdpBuild, '-G', 'Ninja',
        ('-DCMAKE_MAKE_PROGRAM=' + $ninja), ('-DCMAKE_INSTALL_PREFIX=' + $prefix), ('-DCMAKE_PREFIX_PATH=' + $prefix),
        ('-DOPENSSL_ROOT_DIR=' + $prefix), ('-DOPENSSL_SSL_LIBRARY=' + (Join-Path $prefix 'lib/libssl.lib')),
        ('-DOPENSSL_CRYPTO_LIBRARY=' + (Join-Path $prefix 'lib/libcrypto.lib'))) + $freerdpFlags
    Invoke-SdkCommand $cmake $arguments $script:sdkRoot 'freerdp-configure'
    Invoke-SdkCommand $cmake @('--build', $freerdpBuild, '--parallel', $Parallel.ToString()) $script:sdkRoot 'freerdp-build'
    Invoke-SdkCommand $cmake @('--install', $freerdpBuild) $script:sdkRoot 'freerdp-install'

    $stage = 'verify-sdk'
    foreach ($makefile in @((Join-Path $opensslBuild 'makefile'), (Join-Path $zlibBuild 'build.ninja'), (Join-Path $freerdpBuild 'build.ninja'))) {
        if (-not (Test-Path -LiteralPath $makefile -PathType Leaf) -or
            (Get-Content -LiteralPath $makefile -Raw) -notmatch '(?m)(?:^|[\s=])[-/]MD(?:\s|$)') {
            throw 'Expected release DLL CRT /MD was not found in the generated build flags.'
        }
    }
    Assert-SdkTree $script:sdkRoot $script:sdkRoot
    foreach ($relative in @('bin/freerdp3.dll', 'bin/winpr3.dll', 'bin/libssl-3-x64.dll', 'bin/libcrypto-3-x64.dll',
        'lib/freerdp3.lib', 'lib/winpr3.lib', 'lib/libssl.lib', 'lib/libcrypto.lib', 'lib/zs.lib',
        'lib/cmake/FreeRDP3/FreeRDPConfig.cmake', 'lib/cmake/WinPR3/WinPRConfig.cmake')) {
        if (-not (Test-Path -LiteralPath (Join-Path $prefix $relative) -PathType Leaf)) { throw 'The installed SDK is incomplete.' }
    }
    foreach ($license in @(@('FreeRDP', 'freerdp-source', 'LICENSE'),
        @('OpenSSL', 'openssl-source', 'LICENSE.txt'), @('zlib', 'zlib-source', 'LICENSE'))) {
        $licenseSource = Join-Path (Join-Path $script:sdkRoot $license[1]) $license[2]
        if (-not (Test-Path -LiteralPath $licenseSource -PathType Leaf)) { throw 'Required upstream license notice is missing.' }
        $licenseDirectory = Join-Path $prefix ('share/licenses/' + $license[0])
        [IO.Directory]::CreateDirectory($licenseDirectory) | Out-Null
        Copy-Item -LiteralPath $licenseSource -Destination (Join-Path $licenseDirectory $license[2])
    }
    $perlLicenseDirectory = Join-Path $perlPrefix 'share/licenses/Perl'
    [IO.Directory]::CreateDirectory($perlLicenseDirectory) | Out-Null
    foreach ($license in @('Copying', 'Artistic')) { Copy-Item -LiteralPath (Join-Path $perlSource $license) -Destination $perlLicenseDirectory }
    $receipt.files = @(Get-SdkPayloadFiles $prefix)
    $receipt.complete = $true
    [IO.File]::WriteAllText($receiptPath, ($receipt | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))
    Write-Output 'RDS SDK: completed; CMake prefix is <BuildRoot>/install. No redistribution is approved.'
    $global:LASTEXITCODE = 0
} catch {
    # Logs can contain machine paths. Keep raw details local and print a category.
    $failureType = $_.Exception.GetType().FullName
    $diagnostic = [ordered]@{ schema_version=1; stage=$stage; command_stage=$script:lastCommandStage
        native_exit_code=$script:lastCommandExitCode; command_category=$script:lastCommandCategory
        output_line_count=$script:lastCommandOutputLines; reference_lines_valid=$script:lastCommandRefLinesValid
        exception_type=$failureType; hresult=$_.Exception.HResult
        ps_error_category=$_.CategoryInfo.Category.ToString() }
    if ($ownedTreeCreated) {
        try {
            $diagnosticPath = Assert-SdkPath (Join-Path $script:sdkRoot 'failure-summary.json') $script:sdkRoot
            [IO.File]::WriteAllText($diagnosticPath, ($diagnostic | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
        } catch { }
    }
    $exitCategory = if ($null -eq $script:lastCommandExitCode) { 'not_started' } else { [string]$script:lastCommandExitCode }
    throw [InvalidOperationException]::new(('RDS_SDK_BUILD_FAILED:' + $stage + ';command=' + $script:lastCommandStage +
        ';exit=' + $exitCategory + ';category=' + $script:lastCommandCategory + ';type=' + $failureType))
} finally {
    if ($null -ne $lock) { $lock.Dispose() }
    $environmentAfter = [Environment]::GetEnvironmentVariables('Process')
    foreach ($name in @($environmentAfter.Keys)) {
        if (-not $environmentBefore.Contains($name)) { [Environment]::SetEnvironmentVariable([string]$name, $null, 'Process') }
    }
    foreach ($name in $environmentBefore.Keys) { [Environment]::SetEnvironmentVariable([string]$name, [string]$environmentBefore[$name], 'Process') }
}
