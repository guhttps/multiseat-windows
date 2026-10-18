[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][Guid]$RunId,
    [Parameter(Mandatory = $true)][PSCredential[]]$SeatCredentials,
    [Parameter(Mandatory = $true)][string]$HostIPv4,
    [Parameter(Mandatory = $true)][string]$GuestIPv4,
    [int]$PrefixLength = 30
)

# Run INSIDE the disposable Server 2025 Desktop Experience guest, using
# PowerShell Direct. This is an auxiliary RDS experiment, not a client engine.
# The host creates only this offline marker before boot:
# {"schemaVersion":1,"purpose":"MultiseatRdsLab.RdsAuxiliary","runId":"GUID"}
# No passwords, PSCredential objects or command error details are persisted.
# No automatic restart, domain join, licensing changes or host commands occur.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
Set-StrictMode -Version 2.0

function Assert-LabAddressPair {
    param([string]$HostAddress, [string]$GuestAddress, [int]$Prefix)
    if ($Prefix -ne 30) { throw 'Invalid lab prefix.' }
    $parsed = @()
    foreach ($address in @($HostAddress, $GuestAddress)) {
        if ($address -cnotmatch '^(0|[1-9][0-9]{0,2})(\.(0|[1-9][0-9]{0,2})){3}$') {
            throw 'Invalid lab address.'
        }
        $ip = $null
        if (-not [Net.IPAddress]::TryParse($address, [ref]$ip) -or
            $ip.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork -or $ip.ToString() -cne $address) {
            throw 'Invalid lab address.'
        }
        $bytes = $ip.GetAddressBytes()
        $private = $bytes[0] -eq 10 -or ($bytes[0] -eq 172 -and $bytes[1] -ge 16 -and $bytes[1] -le 31) -or
            ($bytes[0] -eq 192 -and $bytes[1] -eq 168)
        # APIPA is deliberately rejected: RFC 3927 requires a /16, not /30.
        if (-not $private -or ($bytes[3] % 4) -notin @(1, 2)) {
            throw 'Invalid lab host address.'
        }
        $parsed += ,$bytes
    }
    if ($HostAddress -ceq $GuestAddress -or $parsed[0][0] -ne $parsed[1][0] -or
        $parsed[0][1] -ne $parsed[1][1] -or $parsed[0][2] -ne $parsed[1][2] -or
        [Math]::Floor($parsed[0][3] / 4) -ne [Math]::Floor($parsed[1][3] / 4)) {
        throw 'Lab addresses must be the two hosts of the same /30.'
    }
}

function Assert-LabGuestIdentity {
    param($OperatingSystem, $ComputerSystem, [string]$InstallationType, [bool]$HasHyperVParameters)
    if ([int]$OperatingSystem.ProductType -ne 3 -or [int]$OperatingSystem.BuildNumber -lt 26100 -or
        $OperatingSystem.Caption -notmatch 'Windows Server 2025' -or $InstallationType -cne 'Server' -or
        $ComputerSystem.PartOfDomain -or $ComputerSystem.Manufacturer -cne 'Microsoft Corporation' -or
        $ComputerSystem.Model -cne 'Virtual Machine' -or -not $HasHyperVParameters) {
        throw 'This is not the isolated Server 2025 Desktop Experience Hyper-V guest.'
    }
}

function Assert-LabCredentials {
    param([PSCredential[]]$Credentials)
    if (@($Credentials).Count -ne 2) { throw 'Exactly two lab credentials are required.' }
    $names = @()
    foreach ($credential in $Credentials) {
        if ($null -eq $credential -or $credential.UserName -cnotin @('LabSeatA', 'LabSeatB') -or
            $null -eq $credential.Password -or $credential.Password.Length -lt 20) {
            throw 'Only the two fixed lab users with strong generated passwords are allowed.'
        }
        $names += $credential.UserName
    }
    if (@($names | Select-Object -Unique).Count -ne 2) { throw 'Duplicate lab credential.' }
}

function Assert-LabMarker {
    param($Marker, [Guid]$ExpectedRunId)
    if ($null -eq $Marker -or $ExpectedRunId -eq [Guid]::Empty) { throw 'Missing lab marker.' }
    $names = @($Marker.PSObject.Properties.Name)
    if ($names.Count -ne 3 -or @($names | Where-Object { $_ -cnotin @('schemaVersion', 'purpose', 'runId') }).Count -ne 0 -or
        $Marker.schemaVersion -isnot [int] -or $Marker.schemaVersion -ne 1 -or
        $Marker.purpose -cne 'MultiseatRdsLab.RdsAuxiliary' -or $Marker.runId -isnot [string]) {
        throw 'Invalid lab marker.'
    }
    $markerRunId = [Guid]::Empty
    if (-not [Guid]::TryParseExact($Marker.runId, 'D', [ref]$markerRunId) -or $markerRunId -ne $ExpectedRunId) {
        throw 'Lab marker does not match the requested run.'
    }
}

function Assert-LabPath {
    param([string]$Path)
    $current = [IO.Path]::GetFullPath($Path)
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Redirected lab path rejected.' }
        }
        $parent = [IO.Path]::GetDirectoryName($current.TrimEnd('\'))
        if (-not $parent -or $parent -eq $current) { break }
        $current = $parent
    }
}

function Save-LabState {
    param($State, [string]$Path)
    Assert-LabPath $Path
    $temporary = $Path + '.' + [Guid]::NewGuid().ToString('N') + '.tmp'
    try {
        $bytes = [Text.UTF8Encoding]::new($false).GetBytes(($State | ConvertTo-Json -Depth 12))
        $stream = [IO.File]::Open($temporary, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        try { $stream.Write($bytes, 0, $bytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
        if (Test-Path -LiteralPath $Path) { [IO.File]::Replace($temporary, $Path, [Management.Automation.Language.NullString]::Value) }
        else { [IO.File]::Move($temporary, $Path) }
    } finally {
        if (Test-Path -LiteralPath $temporary) { [IO.File]::Delete($temporary) }
    }
}

function Get-LabUserRights {
    param([string]$Directory)
    $path = Join-Path $Directory ('rights-' + [Guid]::NewGuid().ToString('N') + '.inf')
    try {
        $secedit = Join-Path ([Environment]::GetFolderPath('System')) 'secedit.exe'
        $nativeOutput = & $secedit /export /cfg $path /areas USER_RIGHTS /quiet 2>&1
        if ($LASTEXITCODE -ne 0) { throw 'Cannot read local logon policy.' }
        $rights = @{ allow = @(); deny = @() }
        foreach ($line in Get-Content -LiteralPath $path) {
            if ($line -match '^\s*Se(RemoteInteractiveLogonRight|DenyRemoteInteractiveLogonRight)\s*=\s*(.*)$') {
                $key = if ($Matches[1] -eq 'RemoteInteractiveLogonRight') { 'allow' } else { 'deny' }
                $rights[$key] = @($Matches[2].Split(',') | ForEach-Object { $_.Trim().TrimStart('*') } | Where-Object { $_ })
            }
        }
        [PSCustomObject]$rights
    } finally {
        if (Test-Path -LiteralPath $path) { [IO.File]::Delete($path) }
    }
}

function Set-LabFirewallIsolation {
    param([string]$OwnedRuleName)
    # The marker owns this disposable VM. Disable all other inbound allows so
    # even an existing broad Any-port rule cannot defeat the host-only boundary.
    Set-NetFirewallProfile -Profile Domain, Private, Public -Enabled True -DefaultInboundAction Block -AllowInboundRules True
    Get-NetFirewallRule -PolicyStore PersistentStore | Where-Object {
        $_.Direction -eq 'Inbound' -and $_.Action -eq 'Allow' -and $_.Enabled -eq 'True' -and $_.Name -cne $OwnedRuleName
    } | Disable-NetFirewallRule | Out-Null
}

$stage = 'preflight'
$lock = $null
$firewallGuardActive = $false
$completed = $false
$lastRdpReturnValue = $null
try {
    Assert-LabAddressPair $HostIPv4 $GuestIPv4 $PrefixLength
    Assert-LabCredentials $SeatCredentials
    if ($RunId -eq [Guid]::Empty -or -not [Environment]::Is64BitProcess -or $PSVersionTable.PSEdition -ne 'Desktop') {
        throw 'Use Windows PowerShell x64 inside the marked guest.'
    }
    $os = Get-CimInstance Win32_OperatingSystem
    $computer = Get-CimInstance Win32_ComputerSystem
    $installation = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
    # A domain controller or a domain-joined machine is outside this experiment.
    Assert-LabGuestIdentity $os $computer $installation.InstallationType `
        (Test-Path 'HKLM:\SOFTWARE\Microsoft\Virtual Machine\Guest\Parameters')
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    try {
        if (-not ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
            throw 'An administrator token is required inside the guest.'
        }
    } finally { $identity.Dispose() }

    $labRoot = 'C:\ProgramData\MultiseatRdsLab'
    $markerPath = Join-Path $labRoot 'lab-marker.json'
    $statePath = Join-Path $labRoot 'guest-state.json'
    Assert-LabPath $markerPath
    if (-not (Test-Path -LiteralPath $markerPath -PathType Leaf) -or (Get-Item -LiteralPath $markerPath -Force).Length -gt 4096) {
        throw 'The host-created offline marker is required.'
    }
    Assert-LabMarker (Get-Content -LiteralPath $markerPath -Raw -Encoding UTF8 | ConvertFrom-Json) $RunId
    foreach ($path in @($statePath, (Join-Path $labRoot 'guest-init.lock'))) { Assert-LabPath $path }
    $lock = [IO.File]::Open((Join-Path $labRoot 'guest-init.lock'), [IO.FileMode]::OpenOrCreate,
        [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)

    Import-Module ServerManager
    Import-Module Microsoft.PowerShell.LocalAccounts
    $adapters = @(Get-NetAdapter)
    if ($adapters.Count -ne 1 -or $adapters[0].Status -ne 'Up' -or
        $adapters[0].InterfaceDescription -notmatch '^Microsoft Hyper-V Network Adapter(?: #\d+)?$') {
        throw 'Exactly one connected Hyper-V network adapter is required.'
    }
    $adapter = $adapters[0]
    $nic = Get-CimInstance Win32_NetworkAdapter -Filter ('InterfaceIndex=' + [int]$adapter.ifIndex)
    if (-not $nic.PNPDeviceID.StartsWith('VMBUS\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected network adapter bus.' }
    $feature = Get-WindowsFeature -Name RDS-RD-Server
    if ($null -eq $feature) { throw 'The RDS Session Host role is unavailable.' }
    $ruleName = 'MultiseatRdsLab-' + $RunId.ToString('D') + '-RdpTcp'
    $users = @(Get-LocalUser)
    $state = $null
    if (Test-Path -LiteralPath $statePath) {
        if ((Get-Item -LiteralPath $statePath -Force).Length -gt 1MB) { throw 'Invalid lab state size.' }
        $state = Get-Content -LiteralPath $statePath -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($state.schemaVersion -ne 1 -or $state.purpose -cne 'MultiseatRdsLab.RdsAuxiliary' -or
            $state.runId -cne $RunId.ToString('D') -or $state.hostIPv4 -cne $HostIPv4 -or
            $state.guestIPv4 -cne $GuestIPv4 -or $state.interfaceGuid -cne $adapter.InterfaceGuid.ToString() -or
            $state.firewallRule -cne $ruleName -or @($state.accounts).Count -gt 2) { throw 'Lab state ownership mismatch.' }
        $seen = @()
        foreach ($owned in @($state.accounts)) {
            if ($owned.name -cnotin @('LabSeatA', 'LabSeatB') -or $owned.name -cin $seen -or
                $owned.sid -notmatch '^S-1-5-21-\d+-\d+-\d+-\d+$') { throw 'Invalid lab account ownership.' }
            $seen += $owned.name
        }
    }
    foreach ($name in @('LabSeatA', 'LabSeatB')) {
        $existing = @($users | Where-Object Name -EQ $name)
        $owned = @(); if ($null -ne $state) { $owned = @($state.accounts | Where-Object name -CEQ $name) }
        if ($existing.Count -gt 1 -or ($existing.Count -eq 1 -and
            ($existing[0].Name -cne $name -or $owned.Count -ne 1 -or $existing[0].SID.Value -cne $owned[0].sid)) -or
            ($existing.Count -eq 0 -and $owned.Count -ne 0)) { throw 'Existing lab account is not owned by this run.' }
    }
    $existingRule = @(Get-NetFirewallRule -PolicyStore PersistentStore | Where-Object Name -CEQ $ruleName)
    if ($null -eq $state -and $existingRule.Count -ne 0) { throw 'An unowned firewall rule already exists.' }

    $stage = 'snapshot'
    if ($null -eq $state) {
        $state = [PSCustomObject][ordered]@{
            schemaVersion = 1; purpose = 'MultiseatRdsLab.RdsAuxiliary'; runId = $RunId.ToString('D')
            hostIPv4 = $HostIPv4; guestIPv4 = $GuestIPv4; interfaceGuid = $adapter.InterfaceGuid.ToString()
            firewallRule = $ruleName; accounts = @(); pendingBoot = $null; ready = $false
            original = [ordered]@{
                roleInstalled = [bool]$feature.Installed
                directorySddl = (Get-Acl -LiteralPath $labRoot).Sddl
                ipv4 = @(Get-NetIPAddress -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 |
                    Select-Object IPAddress, PrefixLength, PrefixOrigin, SuffixOrigin)
                interfaces = @(Get-NetIPInterface -InterfaceIndex $adapter.ifIndex |
                    Select-Object AddressFamily, Dhcp, RouterDiscovery, Forwarding)
                ipv6Binding = @(Get-NetAdapterBinding -Name $adapter.Name -ComponentID ms_tcpip6 |
                    Select-Object ComponentID, Enabled)
                routes = @(Get-NetRoute -InterfaceIndex $adapter.ifIndex |
                    Select-Object DestinationPrefix, NextHop, RouteMetric)
                dns = @(Get-DnsClientServerAddress -InterfaceIndex $adapter.ifIndex |
                    Select-Object AddressFamily, ServerAddresses)
                firewallProfiles = @(Get-NetFirewallProfile -PolicyStore PersistentStore |
                    Select-Object Name, Enabled, DefaultInboundAction, AllowInboundRules)
                enabledInboundAllowRules = @(Get-NetFirewallRule -PolicyStore PersistentStore |
                    Where-Object { $_.Direction -eq 'Inbound' -and $_.Action -eq 'Allow' -and $_.Enabled -eq 'True' } |
                    Select-Object -ExpandProperty Name)
                rdp = Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Terminal Server' |
                    Select-Object fDenyTSConnections
                rdpTcp = Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Terminal Server\WinStations\RDP-Tcp' |
                    Select-Object PortNumber, UserAuthentication
                termService = Get-CimInstance Win32_Service -Filter "Name='TermService'" | Select-Object StartMode, State
                logonRights = Get-LabUserRights $labRoot
            }
        }
        Save-LabState $state $statePath
    }
    # Restrict the owned marker/state directory before creating any lab users.
    $acl = [Security.AccessControl.DirectorySecurity]::new()
    $acl.SetAccessRuleProtection($true, $false)
    foreach ($sid in @('S-1-5-18', 'S-1-5-32-544')) {
        $rule = [Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.SecurityIdentifier]::new($sid),
            [Security.AccessControl.FileSystemRights]::FullControl, [Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit',
            [Security.AccessControl.PropagationFlags]::None, [Security.AccessControl.AccessControlType]::Allow)
        $acl.AddAccessRule($rule)
    }
    Set-Acl -LiteralPath $labRoot -AclObject $acl

    $state.ready = $false
    Save-LabState $state $statePath
    $stage = 'firewall'
    $firewallGuardActive = $true
    Set-LabFirewallIsolation $ruleName
    $stage = 'role'
    $boot = $os.LastBootUpTime.ToUniversalTime().ToString('o')
    if ($state.pendingBoot -and $state.pendingBoot -cne $boot) { $state.pendingBoot = $null; Save-LabState $state $statePath }
    if (-not $state.pendingBoot -and -not $feature.Installed) {
        $result = Install-WindowsFeature -Name RDS-RD-Server -IncludeManagementTools -Restart:$false -WarningAction SilentlyContinue
        # Installation may add/enable built-in rules. Close them even when the
        # next required action is a host-controlled guest restart.
        Set-LabFirewallIsolation $ruleName
        if (-not $result.Success) { throw 'Session Host installation failed.' }
        if ($result.RestartNeeded.ToString() -ne 'No') { $state.pendingBoot = $boot }
        Save-LabState $state $statePath
    }
    if ($state.pendingBoot) {
        [PSCustomObject][ordered]@{ schema_version = 1; run_id = $RunId.ToString('D'); success = $true
            needs_restart = $true; role_installed = $true; accounts_ready = $false; network_ready = $false
            nla_enabled = $false; firewall_restricted = $false; ready = $false }
        return
    }
    if (-not (Get-WindowsFeature -Name RDS-RD-Server).Installed) { throw 'Session Host role is not installed.' }

    $stage = 'firewall'
    # This disposable, marker-owned VM permits no other inbound traffic. Keep a
    # snapshot rather than opening the built-in broad Remote Desktop group.
    Set-LabFirewallIsolation $ruleName

    $stage = 'network'
    Set-NetIPInterface -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 -Dhcp Disabled -Forwarding Disabled
    $ipv6Binding = @(Get-NetAdapterBinding -Name $adapter.Name -ComponentID ms_tcpip6)
    if ($ipv6Binding.Count -ne 1) { throw 'The guest IPv6 binding is ambiguous.' }
    if ($ipv6Binding[0].Enabled) {
        Set-NetIPInterface -InterfaceIndex $adapter.ifIndex -AddressFamily IPv6 -RouterDiscovery Disabled -Forwarding Disabled
        # Windows can retain fec0:: DNS placeholders after resetting addresses.
        # Disable only this disposable guest NIC's binding; no global registry,
        # host adapters, other interfaces or IPv6 system settings are changed.
        Disable-NetAdapterBinding -Name $adapter.Name -ComponentID ms_tcpip6 -Confirm:$false | Out-Null
    }
    Get-NetRoute -InterfaceIndex $adapter.ifIndex | Where-Object DestinationPrefix -In @('0.0.0.0/0', '::/0') |
        Remove-NetRoute -Confirm:$false | Out-Null
    $addresses = @(Get-NetIPAddress -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4)
    $addresses | Where-Object { $_.IPAddress -cne $GuestIPv4 -or $_.PrefixLength -ne 30 } |
        Remove-NetIPAddress -Confirm:$false | Out-Null
    if (@($addresses | Where-Object { $_.IPAddress -ceq $GuestIPv4 -and $_.PrefixLength -eq 30 }).Count -eq 0) {
        New-NetIPAddress -InterfaceIndex $adapter.ifIndex -IPAddress $GuestIPv4 -PrefixLength 30 -AddressFamily IPv4 | Out-Null
    }
    Set-DnsClientServerAddress -InterfaceIndex $adapter.ifIndex -ResetServerAddresses
    Set-DnsClient -InterfaceIndex $adapter.ifIndex -RegisterThisConnectionsAddress $false -UseSuffixWhenRegistering $false

    $stage = 'accounts'
    $rdpGroup = Get-LocalGroup -SID 'S-1-5-32-555'
    $usersGroup = Get-LocalGroup -SID 'S-1-5-32-545'
    $adminMembers = @(Get-LocalGroupMember -SID 'S-1-5-32-544' | ForEach-Object { $_.SID.Value })
    foreach ($credential in $SeatCredentials) {
        $owned = @($state.accounts | Where-Object name -CEQ $credential.UserName)
        if ($owned.Count -eq 0) {
            # Disabled until its returned SID is safely recorded. A crash before
            # recording ownership leaves an orphan we deliberately refuse to reset.
            $user = New-LocalUser -Name $credential.UserName -Password $credential.Password -Disabled -AccountNeverExpires
            $state.accounts = @($state.accounts) + @([PSCustomObject]@{ name = $credential.UserName; sid = $user.SID.Value })
            Save-LabState $state $statePath
        } else { $user = Get-LocalUser -SID $owned[0].sid }
        if ($user.SID.Value -in $adminMembers) { throw 'A lab account is an administrator.' }
        foreach ($group in @($rdpGroup, $usersGroup)) {
            $memberSids = @(Get-LocalGroupMember -Group $group | ForEach-Object { $_.SID.Value })
            if ($user.SID.Value -notin $memberSids) { Add-LocalGroupMember -Group $group -Member $user }
        }
    }
    $rights = Get-LabUserRights $labRoot
    $deniedSids = @('S-1-1-0', 'S-1-5-11', 'S-1-5-113', 'S-1-5-32-545', 'S-1-5-32-555') + @($state.accounts | ForEach-Object sid)
    if ('S-1-5-32-555' -notin $rights.allow -or @($rights.deny | Where-Object { $_ -in $deniedSids }).Count -ne 0) {
        throw 'The default Remote Desktop Users logon right is unavailable; no security policy was replaced.'
    }
    foreach ($owned in @($state.accounts)) { Enable-LocalUser -SID $owned.sid }

    $stage = 'rdp-nla'
    $namespace = 'root/cimv2/TerminalServices'
    $general = Get-CimInstance -Namespace $namespace -ClassName Win32_TSGeneralSetting -Filter "TerminalName='RDP-tcp'"
    $nla = Invoke-CimMethod -InputObject $general -MethodName SetUserAuthenticationRequired -Arguments @{ UserAuthenticationRequired = [uint32]1 }
    # Some Server/CIM providers return a null ReturnValue even after applying
    # the setting. Never coerce null to numeric success; verify live state.
    if ($null -ne $nla -and $null -ne $nla.ReturnValue) {
        $lastRdpReturnValue = [long]$nla.ReturnValue
        if ($lastRdpReturnValue -ne 0) { throw 'NLA could not be enabled.' }
    }
    $nlaAfter = Get-CimInstance -Namespace $namespace -ClassName Win32_TSGeneralSetting -Filter "TerminalName='RDP-tcp'"
    if ($nlaAfter.UserAuthenticationRequired -ne 1) { throw 'NLA postcondition failed.' }
    $stage = 'rdp-enable'
    $lastRdpReturnValue = $null
    $setting = Get-CimInstance -Namespace $namespace -ClassName Win32_TerminalServiceSetting
    # ModifyFirewallException=0 preserves our narrow firewall policy.
    $enabled = Invoke-CimMethod -InputObject $setting -MethodName SetAllowTSConnections -Arguments @{
        AllowTSConnections = [uint32]1; ModifyFirewallException = [uint32]0
    }
    if ($null -ne $enabled -and $null -ne $enabled.ReturnValue) {
        $lastRdpReturnValue = [long]$enabled.ReturnValue
        if ($lastRdpReturnValue -ne 0) { throw 'RDP could not be enabled.' }
    }
    $allowAfter = Get-CimInstance -Namespace $namespace -ClassName Win32_TerminalServiceSetting
    if ($allowAfter.AllowTSConnections -ne 1) { throw 'RDP postcondition failed.' }
    $stage = 'rdp-port'
    $lastRdpReturnValue = $null
    if ((Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Terminal Server\WinStations\RDP-Tcp').PortNumber -ne 3389) {
        throw 'The guest RDP port is not the default lab port.'
    }
    $stage = 'rdp-firewall'
    if ($existingRule.Count -ne 0) { $existingRule | Remove-NetFirewallRule }
    New-NetFirewallRule -Name $ruleName -DisplayName 'Multiseat RDS lab: host only' -Direction Inbound -Action Allow -Enabled True `
        -Profile Any -Protocol TCP -LocalPort 3389 -LocalAddress $GuestIPv4 -RemoteAddress $HostIPv4 `
        -InterfaceAlias $adapter.Name -Service TermService -EdgeTraversalPolicy Block | Out-Null
    $stage = 'rdp-service'
    Set-Service -Name TermService -StartupType Automatic
    Start-Service -Name TermService

    $stage = 'verification'
    $verifiedAddresses = @(Get-NetIPAddress -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4)
    $dnsAddresses = @(Get-DnsClientServerAddress -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 | ForEach-Object ServerAddresses)
    $verifiedIpv6Binding = @(Get-NetAdapterBinding -Name $adapter.Name -ComponentID ms_tcpip6)
    $defaultRoutes = @(Get-NetRoute -InterfaceIndex $adapter.ifIndex | Where-Object DestinationPrefix -In @('0.0.0.0/0', '::/0'))
    $activeAllows = @(Get-NetFirewallRule -PolicyStore ActiveStore | Where-Object {
        $_.Direction -eq 'Inbound' -and $_.Action -eq 'Allow' -and $_.Enabled -eq 'True'
    })
    $profiles = @(Get-NetFirewallProfile -PolicyStore ActiveStore)
    $verifiedGeneral = Get-CimInstance -Namespace $namespace -ClassName Win32_TSGeneralSetting -Filter "TerminalName='RDP-tcp'"
    $verifiedSetting = Get-CimInstance -Namespace $namespace -ClassName Win32_TerminalServiceSetting
    if ($verifiedAddresses.Count -ne 1 -or $verifiedAddresses[0].IPAddress -cne $GuestIPv4 -or
        $verifiedAddresses[0].PrefixLength -ne 30 -or $dnsAddresses.Count -ne 0 -or $defaultRoutes.Count -ne 0 -or
        $verifiedIpv6Binding.Count -ne 1 -or $verifiedIpv6Binding[0].Enabled -ne $false -or
        $activeAllows.Count -ne 1 -or $activeAllows[0].Name -cne $ruleName -or
        @($profiles | Where-Object { $_.Enabled -ne 'True' -or $_.DefaultInboundAction -ne 'Block' }).Count -ne 0 -or
        $verifiedGeneral.UserAuthenticationRequired -ne 1 -or $verifiedSetting.AllowTSConnections -ne 1 -or
        (Get-Service TermService).Status -ne 'Running') {
        throw 'The isolated guest configuration did not pass verification.'
    }
    $state.ready = $true
    Save-LabState $state $statePath
    $completed = $true
    [PSCustomObject][ordered]@{ schema_version = 1; run_id = $RunId.ToString('D'); success = $true
        needs_restart = $false; role_installed = $true; accounts_ready = $true; network_ready = $true
        nla_enabled = $true; firewall_restricted = $true; ready = $true }
} catch {
    $failureRecord = $_
    # The owned directory is admin/SYSTEM-only. Keep numeric diagnostics there,
    # never exception messages, invocation details, arguments, paths or secrets.
    if ($null -ne $lock) {
        try {
            $nativeCode = $null
            $nativeProperty = $failureRecord.Exception.PSObject.Properties['NativeErrorCode']
            if ($null -ne $nativeProperty -and ($nativeProperty.Value -is [ValueType])) {
                try { $nativeCode = [long]$nativeProperty.Value } catch { }
            }
            $idCategory = 'other'
            foreach ($knownId in @('NamedParameterNotFound', 'PositionalParameterNotFound', 'ParameterArgumentValidationError',
                'ParameterArgumentTransformationError', 'MethodNotFound', 'CimJob_BrokenCimSession', 'HRESULT',
                'UnauthorizedAccessException', 'CimException', 'Win32Exception')) {
                if ($failureRecord.FullyQualifiedErrorId -match ('^' + [regex]::Escape($knownId) + '(?:[,\s:]|$)')) {
                    $idCategory = $knownId; break
                }
            }
            $diagnostic = [PSCustomObject][ordered]@{
                schema_version = 1; run_id = $RunId.ToString('D'); stage = $stage
                exception_type = $failureRecord.Exception.GetType().FullName
                hresult = ('0x' + $failureRecord.Exception.HResult.ToString('X8'))
                native_error_code = $nativeCode; numeric_return_value = $lastRdpReturnValue
                ps_error_id_category = $idCategory; ps_error_category = $failureRecord.CategoryInfo.Category.ToString()
            }
            Save-LabState $diagnostic (Join-Path $labRoot 'guest-failure.json')
        } catch { }
    }
    if ($firewallGuardActive -and -not $completed) {
        # PowerShell Direct remains available without network rules. A partial
        # failure closes even this run's RDP rule; rerunning can reopen it safely.
        try { Set-LabFirewallIsolation '' } catch { }
    }
    # Do not serialize exception chains, credentials, native stderr or machine paths.
    throw [InvalidOperationException]::new(('RDSLAB_INIT_FAILED:' + $stage))
} finally {
    if ($null -ne $lock) { $lock.Dispose() }
}
