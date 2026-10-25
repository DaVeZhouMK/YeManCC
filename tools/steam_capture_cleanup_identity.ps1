param([Parameter(Mandatory=$true)][uint32]$SteamPid,[Parameter(Mandatory=$true)][uint32]$HelperPid)
$ErrorActionPreference='Stop'
# Fresh query-only identity for releasing our existing JS subscription. This
# never selects a game, creates a listener, calls Slots(), or grants an action.
Add-Type -TypeDefinition ([IO.File]::ReadAllText((Join-Path $PSScriptRoot 'steam_live_consumer_native.cs')))
$taskHandles=@{}; $taskFacts=@{}
try {
 $taskHandles.steam=[YmccLiveConsumer.ReadOnlyNative]::Open($SteamPid)
 $taskHandles.webhelper=[YmccLiveConsumer.ReadOnlyNative]::Open($HelperPid)
 $taskParent=Get-CimInstance Win32_Process -Filter ('ProcessId='+$HelperPid) | Select-Object -ExpandProperty ParentProcessId
 $taskOwners=@(Get-NetTCPConnection -LocalPort 8080 -State Listen -ErrorAction Stop|Where-Object {$_.LocalAddress-eq'127.0.0.1'}|Select-Object -ExpandProperty OwningProcess -Unique)
 $taskOwner=if($taskOwners.Count-eq1){[uint32]$taskOwners[0]}else{0}
 # Recheck retained handles after listener/parent queries, including exit state.
 $taskFacts.steam=[YmccLiveConsumer.ReadOnlyNative]::Read($taskHandles.steam,$SteamPid)
 $taskFacts.webhelper=[YmccLiveConsumer.ReadOnlyNative]::Read($taskHandles.webhelper,$HelperPid)
 $taskAt=(Get-Date).ToUniversalTime().ToString('o')
 [ordered]@{atUtc=$taskAt;originals=$taskFacts;originalMatches=@{steam=$taskFacts.steam.IdentityObserved;webhelper=$taskFacts.webhelper.IdentityObserved};listenerOwnerPid=$taskOwner;listenerObservedUtc=$taskAt;originalListenerSame=($taskOwner-eq$HelperPid-and$taskParent-eq$SteamPid);helperParentProcessId=$taskParent;readOnly=$true;purpose='cleanup-owned-subscriptions-only';deviceReads=0;deviceWrites=0;gameActions=0;sleepActions=0}|ConvertTo-Json -Depth 8 -Compress
} finally {
 foreach($taskHandle in $taskHandles.Values){[YmccLiveConsumer.ReadOnlyNative]::Close($taskHandle)}
}