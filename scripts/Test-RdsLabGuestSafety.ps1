[CmdletBinding()]
param()

# Inert fixtures only. Never dot-source or execute the real initializer.
# All feature/network/accounts/service/ACL commands in its in-memory copy are
# mocked, and its filesystem destination is replaced with a new build directory.
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$sourcePath = Join-Path $project 'scripts/Initialize-RdsLabGuest.ps1'
$fixtureRoot = Join-Path $project ('build/rds-init-inert-' + [Guid]::NewGuid().ToString('N'))
$cursor = [IO.Path]::GetFullPath($fixtureRoot)
while ($cursor) {
    if ((Test-Path -LiteralPath $cursor) -and
        ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Redirected fixture directory rejected.'
    }
    $cursor = [IO.Path]::GetDirectoryName($cursor)
}
New-Item -ItemType Directory -Path $fixtureRoot | Out-Null
$source = [IO.File]::ReadAllText($sourcePath)
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseInput($source, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Source syntax failed.' }
$pureNames = @('Assert-LabAddressPair', 'Assert-LabCredentials', 'Assert-LabMarker', 'Assert-LabGuestIdentity')
foreach ($functionAst in $ast.FindAll({param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -in $pureNames
}, $true)) { . ([scriptblock]::Create($functionAst.Extent.Text)) }
$script:passed = 0
function Assert-Rejected {
    param([scriptblock]$Action)
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw 'Unsafe fixture was accepted.' }
    $script:passed++
}
foreach ($pair in @(@('10.24.0.1','10.24.0.2'), @('172.16.4.13','172.16.4.14'),
    @('172.31.255.253','172.31.255.254'), @('192.168.50.1','192.168.50.2'))) {
    Assert-LabAddressPair $pair[0] $pair[1] 30; $script:passed++
}
foreach ($pair in @(@('169.254.50.1','169.254.50.2'), @('8.8.8.1','8.8.8.2'),
    @('172.15.0.1','172.15.0.2'), @('172.32.0.1','172.32.0.2'), @('192.169.0.1','192.169.0.2'),
    @('10.0.0.0','10.0.0.1'), @('10.0.0.1','10.0.0.3'), @('10.0.0.1','10.0.0.1'),
    @('10.0.0.1','10.0.0.6'), @('10.0.0.1','10.0.1.2'), @('10.0.0.1','10.1.0.2'),
    @('10.0.0.1','192.168.0.2'), @('010.0.0.1','10.0.0.2'), @('10.0.0.1','::1'), @('10.0.0.1','10.0.0.256'))) {
    Assert-Rejected { Assert-LabAddressPair $pair[0] $pair[1] 30 }
}
Assert-Rejected { Assert-LabAddressPair '10.0.0.1' '10.0.0.2' 16 }
$testRun = [Guid]::NewGuid()
$validMarker = [pscustomobject]@{schemaVersion=1;purpose='MultiseatRdsLab.RdsAuxiliary';runId=$testRun.ToString('D')}
Assert-LabMarker $validMarker $testRun; $script:passed++
Assert-Rejected { Assert-LabMarker $validMarker ([Guid]::NewGuid()) }
Assert-Rejected { Assert-LabMarker ([pscustomobject]@{schemaVersion='1';purpose='MultiseatRdsLab.RdsAuxiliary';runId=$testRun.ToString('D')}) $testRun }
Assert-Rejected { Assert-LabMarker ([pscustomobject]@{schemaVersion=1;purpose='other';runId=$testRun.ToString('D')}) $testRun }
Assert-Rejected { Assert-LabMarker ([pscustomobject]@{schemaVersion=1;purpose='MultiseatRdsLab.RdsAuxiliary';runId=$testRun.ToString('D');extra=$true}) $testRun }
$fixturePassword = ConvertTo-SecureString ('Fixture-' + ('x' * 24) + '!7') -AsPlainText -Force
$fixtureA = [PSCredential]::new('LabSeatA',$fixturePassword)
$fixtureB = [PSCredential]::new('LabSeatB',$fixturePassword)
Assert-LabCredentials @($fixtureA,$fixtureB); $script:passed++
Assert-Rejected { Assert-LabCredentials @($fixtureA) }
Assert-Rejected { Assert-LabCredentials @($fixtureA,$fixtureA) }
Assert-Rejected { Assert-LabCredentials @($fixtureA,[PSCredential]::new('Administrator',$fixturePassword)) }
Assert-Rejected { Assert-LabCredentials @($fixtureA,[PSCredential]::new('.\LabSeatB',$fixturePassword)) }
Assert-Rejected { Assert-LabCredentials @($fixtureA,[PSCredential]::new('LabSeatB',(ConvertTo-SecureString 'short' -AsPlainText -Force))) }
$fixtureOs = [pscustomobject]@{ProductType=3;BuildNumber=26100;Caption='Microsoft Windows Server 2025 Standard Evaluation'}
$fixtureComputer = [pscustomobject]@{PartOfDomain=$false;Manufacturer='Microsoft Corporation';Model='Virtual Machine'}
Assert-LabGuestIdentity $fixtureOs $fixtureComputer 'Server' $true; $script:passed++
Assert-Rejected { Assert-LabGuestIdentity ([pscustomobject]@{ProductType=1;BuildNumber=26100;Caption='Microsoft Windows 11'}) $fixtureComputer 'Client' $true }
Assert-Rejected { Assert-LabGuestIdentity ([pscustomobject]@{ProductType=2;BuildNumber=26100;Caption='Microsoft Windows Server 2025'}) $fixtureComputer 'Server' $true }
Assert-Rejected { Assert-LabGuestIdentity $fixtureOs $fixtureComputer 'Server Core' $true }
Assert-Rejected { Assert-LabGuestIdentity $fixtureOs $fixtureComputer 'Server' $false }
Assert-Rejected { Assert-LabGuestIdentity $fixtureOs ([pscustomobject]@{PartOfDomain=$true;Manufacturer='Microsoft Corporation';Model='Virtual Machine'}) 'Server' $true }
Assert-Rejected { Assert-LabGuestIdentity $fixtureOs ([pscustomobject]@{PartOfDomain=$false;Manufacturer='Other';Model='Physical PC'}) 'Server' $true }
Write-Output ('PASS: ' + $script:passed + ' inert address, credential, marker and guest-identity fixtures.')
$rightsAst = $ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Get-LabUserRights'}, $true)
$source = $source.Remove($rightsAst.Extent.StartOffset, $rightsAst.Extent.EndOffset - $rightsAst.Extent.StartOffset).Insert($rightsAst.Extent.StartOffset,
    'function Get-LabUserRights { param([string]$Directory) [PSCustomObject]@{allow=@(''S-1-5-32-555'');deny=@()} }')
$adminCheck = '-not ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)'
if (-not $source.Contains($adminCheck)) { throw 'Expected administrator guard not located.' }
# This in-memory fixture bypasses elevation ONLY because every mutation cmdlet
# is replaced below. The real guest source is never executed on the host.
$source = $source.Replace($adminCheck, '$false').Replace("'C:\ProgramData\MultiseatRdsLab'", "'" + $fixtureRoot.Replace("'", "''") + "'")
# Reject new/unknown commands before evaluating the in-memory fixture. A new
# administrative cmdlet cannot silently bypass this test's explicit mocks.
$safeCommands = @('Add-LocalGroupMember','Assert-LabAddressPair','Assert-LabCredentials','Assert-LabGuestIdentity',
    'Assert-LabMarker','Assert-LabPath','ConvertFrom-Json','ConvertTo-Json','Disable-NetAdapterBinding',
    'Disable-NetFirewallRule','Enable-LocalUser','ForEach-Object','Get-Acl','Get-CimInstance','Get-Content',
    'Get-DnsClientServerAddress','Get-Item','Get-ItemProperty','Get-LabUserRights','Get-LocalGroup',
    'Get-LocalGroupMember','Get-LocalUser','Get-NetAdapter','Get-NetAdapterBinding','Get-NetFirewallProfile',
    'Get-NetFirewallRule','Get-NetIPAddress','Get-NetIPInterface','Get-NetRoute','Get-Service','Get-WindowsFeature',
    'Import-Module','Install-WindowsFeature','Invoke-CimMethod','Join-Path','New-LocalUser','New-NetFirewallRule',
    'New-NetIPAddress','Out-Null','Remove-NetFirewallRule','Remove-NetIPAddress','Remove-NetRoute','Save-LabState',
    'Select-Object','Set-Acl','Set-DnsClient','Set-DnsClientServerAddress','Set-LabFirewallIsolation',
    'Set-NetFirewallProfile','Set-NetIPInterface','Set-Service','Set-StrictMode','Start-Service','Test-Path','Where-Object')
$transformedAst = [Management.Automation.Language.Parser]::ParseInput($source, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Inert fixture syntax failed.' }
foreach ($commandAst in $transformedAst.FindAll({param($node) $node -is [Management.Automation.Language.CommandAst]}, $true)) {
    if ($null -eq $commandAst.GetCommandName() -or $commandAst.GetCommandName() -notin $safeCommands) {
        throw 'Unmocked initializer command rejected before fixture execution.'
    }
}
$body = [scriptblock]::Create($source)
$run = [Guid]::NewGuid()
@{schemaVersion=1;purpose='MultiseatRdsLab.RdsAuxiliary';runId=$run.ToString('D')} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixtureRoot 'lab-marker.json') -Encoding UTF8
$script:fixtureSidPrefix = @('S','1','5','21','100000001','100000002','100000003') -join '-'
$script:events = [Collections.Generic.List[string]]::new()
$script:fixture = @{
    installed = $false; restart = $true; boot = [DateTime]::Parse('2026-01-01T00:00:00Z').ToUniversalTime()
    rules = @([PSCustomObject]@{Name='BroadOriginalFixture';Direction='Inbound';Action='Allow';Enabled='True'})
    users = @(); groupMembers = @{}; nextRid = 1000
    methodReturn = 0; nlaState = 1; allowState = 1
    ipv6Enabled = $true; ipv6DisableBlocked = $false
    addresses = @([PSCustomObject]@{IPAddress='10.42.0.100';PrefixLength=24;PrefixOrigin='Dhcp';SuffixOrigin='Dhcp'})
}
$script:fixtureAdapter = [PSCustomObject]@{
    Status='Up';InterfaceDescription='Microsoft Hyper-V Network Adapter';ifIndex=77;Name='Fixture NIC'
    InterfaceGuid=[Guid]::Parse('11111111-2222-3333-4444-555555555555')
}
function Import-Module { param($Name) }
function Test-Path {
    param([string]$Path,[string]$LiteralPath,[string]$PathType)
    if ($Path -like 'HKLM:*') { return $true }
    Microsoft.PowerShell.Management\Test-Path @PSBoundParameters
}
function Get-ItemProperty {
    param([string]$Path)
    if ($Path -like '*CurrentVersion') { return [PSCustomObject]@{InstallationType='Server'} }
    if ($Path -like '*RDP-Tcp') { return [PSCustomObject]@{PortNumber=3389;UserAuthentication=1} }
    [PSCustomObject]@{fDenyTSConnections=1}
}
function Get-CimInstance {
    param([string]$ClassName,[string]$Namespace,[string]$Filter)
    switch ($ClassName) {
        'Win32_OperatingSystem' { [PSCustomObject]@{ProductType=3;BuildNumber=26100;Caption='Microsoft Windows Server 2025 Standard Evaluation';LastBootUpTime=$script:fixture.boot} }
        'Win32_ComputerSystem' { [PSCustomObject]@{PartOfDomain=$false;Manufacturer='Microsoft Corporation';Model='Virtual Machine'} }
        'Win32_NetworkAdapter' { [PSCustomObject]@{PNPDeviceID='VMBUS\Fixture'} }
        'Win32_Service' { [PSCustomObject]@{StartMode='Manual';State='Stopped'} }
        'Win32_TSGeneralSetting' { [PSCustomObject]@{UserAuthenticationRequired=$script:fixture.nlaState} }
        'Win32_TerminalServiceSetting' { [PSCustomObject]@{AllowTSConnections=$script:fixture.allowState} }
        default { throw 'Unexpected CIM class in fixture.' }
    }
}
function Get-NetAdapter { $script:fixtureAdapter }
function Get-WindowsFeature { param($Name) [PSCustomObject]@{Installed=$script:fixture.installed} }
function Install-WindowsFeature {
    param($Name,[switch]$IncludeManagementTools,[switch]$Restart,$WarningAction)
    if ($Restart) { throw 'Unexpected automatic restart.' }
    if (@($script:fixture.rules | Where-Object Enabled -EQ 'True').Count) { throw 'Role install ran with broad firewall rules enabled.' }
    $script:events.Add('install-role');$script:fixture.installed=$true
    # Simulate role installation enabling its broad default firewall rule.
    $script:fixture.rules[0].Enabled='True'
    [PSCustomObject]@{Success=$true;RestartNeeded=$(if($script:fixture.restart){'Yes'}else{'No'})}
}
function Set-Acl { param($LiteralPath,$AclObject) $script:events.Add('set-owned-acl') }
function Get-NetIPAddress { param($InterfaceIndex,$AddressFamily) $script:fixture.addresses }
function Get-NetIPInterface { param($InterfaceIndex) [PSCustomObject]@{AddressFamily='IPv4';Dhcp='Enabled';RouterDiscovery='Disabled';Forwarding='Disabled'} }
function Get-NetRoute { param($InterfaceIndex) }
function Get-DnsClientServerAddress { param($InterfaceIndex,$AddressFamily) [PSCustomObject]@{AddressFamily='IPv4';ServerAddresses=@()} }
function Get-NetAdapterBinding { param($Name,$ComponentID) if($Name-ne'Fixture NIC'-or$ComponentID-ne'ms_tcpip6'){throw 'Unexpected binding scope.'};[PSCustomObject]@{ComponentID='ms_tcpip6';Enabled=$script:fixture.ipv6Enabled} }
function Disable-NetAdapterBinding { param($Name,$ComponentID,[switch]$Confirm) if($Name-ne'Fixture NIC'-or$ComponentID-ne'ms_tcpip6'){throw 'Unexpected binding mutation scope.'};if(-not$script:fixture.ipv6DisableBlocked){$script:fixture.ipv6Enabled=$false} }
function Get-NetFirewallProfile { param($PolicyStore) foreach($name in @('Domain','Private','Public')){[PSCustomObject]@{Name=$name;Enabled='True';DefaultInboundAction='Block';AllowInboundRules='True'}} }
function Get-NetFirewallRule { param($PolicyStore) $script:fixture.rules }
function Disable-NetFirewallRule {
    param([Parameter(ValueFromPipeline)]$InputObject)
    process {$InputObject.Enabled='False';$script:events.Add('disable-allow')}
}
function Set-NetFirewallProfile { param($Profile,$Enabled,$DefaultInboundAction,$AllowInboundRules) $script:events.Add('restrict-profiles') }
function Set-NetIPInterface { param($InterfaceIndex,$AddressFamily,$Dhcp,$RouterDiscovery,$Forwarding) $script:events.Add('set-guest-ip-interface') }
function Remove-NetRoute { param([Parameter(ValueFromPipeline)]$InputObject,[switch]$Confirm) process {} }
function Remove-NetIPAddress {
    param([Parameter(ValueFromPipeline)]$InputObject,[switch]$Confirm)
    process {$script:fixture.addresses=@($script:fixture.addresses|Where-Object IPAddress -NE $InputObject.IPAddress)}
}
function New-NetIPAddress {
    param($InterfaceIndex,$IPAddress,$PrefixLength,$AddressFamily)
    $script:fixture.addresses+=@([PSCustomObject]@{IPAddress=$IPAddress;PrefixLength=$PrefixLength;PrefixOrigin='Manual';SuffixOrigin='Manual'})
}
function Set-DnsClientServerAddress { param($InterfaceIndex,[switch]$ResetServerAddresses) }
function Set-DnsClient { param($InterfaceIndex,$RegisterThisConnectionsAddress,$UseSuffixWhenRegistering) }
function Get-LocalUser { param($SID) if($SID){$script:fixture.users|Where-Object {$_.SID.Value-eq$SID}}else{$script:fixture.users} }
function New-LocalUser {
    param($Name,$Password,[switch]$Disabled,[switch]$AccountNeverExpires)
    if(-not$Disabled -or $Password -isnot [Security.SecureString]){throw 'Unexpected account creation contract.'}
    $script:events.Add('create-user');$script:fixture.nextRid++
    $user=[PSCustomObject]@{Name=$Name;SID=[PSCustomObject]@{Value=(($script:fixtureSidPrefix + '-')+$script:fixture.nextRid)};Enabled=$false}
    $script:fixture.users+=@($user);$user
}
function Get-LocalGroup { param($SID) [PSCustomObject]@{SID=$SID} }
function Get-LocalGroupMember { param($SID,$Group) if($Group){$SID=$Group.SID};foreach($member in @($script:fixture.groupMembers[$SID])){if($null-ne$member){[PSCustomObject]@{SID=[PSCustomObject]@{Value=$member}}}} }
function Add-LocalGroupMember { param($Group,$Member) $script:fixture.groupMembers[$Group.SID]=@($script:fixture.groupMembers[$Group.SID])+@($Member.SID.Value) }
function Enable-LocalUser { param($SID) ($script:fixture.users|Where-Object {$_.SID.Value-eq$SID}).Enabled=$true }
function Invoke-CimMethod { param($InputObject,$MethodName,$Arguments) if($MethodName-eq'SetAllowTSConnections' -and $Arguments.ModifyFirewallException-ne0){throw 'Broad firewall requested.'};[PSCustomObject]@{ReturnValue=$script:fixture.methodReturn} }
function Remove-NetFirewallRule { param([Parameter(ValueFromPipeline)]$InputObject) process {$script:fixture.rules=@($script:fixture.rules|Where-Object Name -NE $InputObject.Name)} }
function New-NetFirewallRule {
    param($Name,$DisplayName,$Direction,$Action,$Enabled,$Profile,$Protocol,$LocalPort,$LocalAddress,$RemoteAddress,$InterfaceAlias,$Service,$EdgeTraversalPolicy)
    if($LocalAddress-ne'10.42.0.2'-or$RemoteAddress-ne'10.42.0.1'-or$InterfaceAlias-ne'Fixture NIC'-or$Protocol-ne'TCP'-or$LocalPort-ne3389-or$Service-ne'TermService'){throw 'Unexpected firewall boundary.'}
    $script:fixture.rules+=@([PSCustomObject]@{Name=$Name;Direction=$Direction;Action=$Action;Enabled=$Enabled})
}
function Set-Service { param($Name,$StartupType) }
function Start-Service { param($Name) }
function Get-Service { param($Name) [PSCustomObject]@{Status='Running'} }
function Set-LocalUser { throw 'Existing account passwords must never be reset.' }
function Restart-Computer { throw 'Automatic restart is forbidden.' }
function Enable-NetFirewallRule { throw 'Broad firewall enable is forbidden.' }
$password=ConvertTo-SecureString ('Fixture-' + ('x' * 24) + '!7') -AsPlainText -Force
$credentials=@([PSCredential]::new('LabSeatA',$password),[PSCredential]::new('LabSeatB',$password))
$arguments=@{RunId=$run;SeatCredentials=$credentials;HostIPv4='10.42.0.1';GuestIPv4='10.42.0.2';PrefixLength=30}
$first=&$body @arguments
if(-not$first.needs_restart-or$first.ready-or$script:fixture.users.Count-ne0-or@($script:fixture.rules|Where-Object Enabled -EQ 'True').Count){throw 'Restart boundary failed.'}
'PASS: role restart returns before accounts/network/RDP and closes newly enabled firewall rules.'
$second=&$body @arguments
if(-not$second.needs_restart-or$script:fixture.users.Count-ne0-or@($script:events|Where-Object {$_-eq'install-role'}).Count-ne1){throw 'Unrestarted retry failed.'}
'PASS: retry without reboot does not reinstall or create accounts.'
$script:fixture.boot=$script:fixture.boot.AddMinutes(2)
$third=&$body @arguments
if(-not$third.ready-or$third.needs_restart-or$script:fixture.users.Count-ne2-or@($script:fixture.users|Where-Object Enabled -EQ $false).Count){throw 'Post-reboot initialization failed.'}
$fourth=&$body @arguments
if(-not$fourth.ready-or@($script:events|Where-Object {$_-eq'create-user'}).Count-ne2){throw 'Idempotence failed.'}
$stateText=[IO.File]::ReadAllText((Join-Path $fixtureRoot 'guest-state.json'))
if($stateText.Contains('Fixture-' + ('x' * 24))-or$stateText.Contains('SecureString')-or$stateText.Contains('PSCredential')){throw 'Credential material was persisted.'}
'PASS: post-reboot account setup, private firewall scope, owned-SID idempotence and credential-free state.'
$script:fixture.methodReturn=$null
$nullable=&$body @arguments
if(-not$nullable.ready){throw 'Null CIM return with verified enabled state failed.'}
foreach($field in @('nlaState','allowState')){
 $script:fixture[$field]=0
 $rejected=$false
 try{&$body @arguments|Out-Null}catch{if($_.Exception.Message-notmatch'^RDSLAB_INIT_FAILED:rdp-(nla|enable)$'){throw};$rejected=$true}
 if(-not$rejected){throw 'Null CIM return incorrectly bypassed disabled state.'}
 $script:fixture[$field]=1
}
'PASS: null CIM return accepted only with enabled NLA/RDP postconditions; disabled state rejected.'
$script:fixture.ipv6Enabled=$true;$script:fixture.ipv6DisableBlocked=$true
$rejected=$false
try{&$body @arguments|Out-Null}catch{if($_.Exception.Message-cne'RDSLAB_INIT_FAILED:verification'){throw};$rejected=$true}
if(-not$rejected){throw 'Active IPv6 guest binding was accepted.'}
$script:fixture.ipv6Enabled=$false;$script:fixture.ipv6DisableBlocked=$false
'PASS: inactive guest IPv6 binding required; no host/global IPv6 setting changed.'
$statePath=Join-Path $fixtureRoot 'guest-state.json'
$state=Get-Content -LiteralPath $statePath -Raw|ConvertFrom-Json
$state.accounts[0].sid=($script:fixtureSidPrefix + '-9999')
$state|ConvertTo-Json -Depth 12|Set-Content -LiteralPath $statePath -Encoding UTF8
$eventCount=$script:events.Count
$rejected=$false
try{&$body @arguments|Out-Null}catch{if($_.Exception.Message-cne'RDSLAB_INIT_FAILED:preflight'){throw};$rejected=$true}
if(-not$rejected-or$script:events.Count-ne$eventCount){throw 'Unowned account was modified.'}
'PASS: account SID mismatch rejects before any configuration mutation.'
exit 0
