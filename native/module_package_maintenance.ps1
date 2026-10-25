# Embedded alongside the Decky transaction primitives. The package is authenticated
# by the updater ZIP SHA256 before any manifest here is read.
# Read identity from the process handle without enumerating loaded modules.
# Hooked/elevated tools must not force an upgrade to wait on their loader state.
function Initialize-YmccUpdaterProcessIdentity {
  if('Ymcc.UpdaterProcessIdentity' -as [type]){return}
  Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.ComponentModel;
using System.Runtime.InteropServices;
namespace Ymcc {
 public sealed class UpdaterIdentity { public string Image; public string Creation; }
 public static class UpdaterProcessIdentity {
  [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr OpenProcess(uint access,bool inherit,uint pid);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
  [DllImport("kernel32.dll",SetLastError=true)] static extern uint WaitForSingleObject(IntPtr h,uint timeout);
  [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetProcessTimes(IntPtr h,out long birth,out long exit,out long kernel,out long user);
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool QueryFullProcessImageName(IntPtr h,uint flags,StringBuilder image,ref uint size);
  public static UpdaterIdentity ReadHandle(IntPtr handle) {
   uint wait=WaitForSingleObject(handle,0);if(wait==0)return null;if(wait!=258)throw new Win32Exception(Marshal.GetLastWin32Error());
   long birth,exit,kernel,user;if(!GetProcessTimes(handle,out birth,out exit,out kernel,out user))throw new Win32Exception(Marshal.GetLastWin32Error());
   uint size=32768;var image=new StringBuilder((int)size);if(!QueryFullProcessImageName(handle,0,image,ref size))throw new Win32Exception(Marshal.GetLastWin32Error());
   return new UpdaterIdentity {Image=image.ToString(),Creation=birth.ToString(System.Globalization.CultureInfo.InvariantCulture)};
  }
  public static UpdaterIdentity ReadId(uint pid) {
   IntPtr handle=OpenProcess(0x1000u|0x100000u,false,pid);
   if(handle==IntPtr.Zero){int error=Marshal.GetLastWin32Error();if(error==87)return null;throw new Win32Exception(error);}
   try{return ReadHandle(handle);}finally{CloseHandle(handle);}
  }
 }
}
"@
}
function Get-YmccUpdaterProcessIdentity([Diagnostics.Process]$Process,[int]$ProcessId=0) {
  Initialize-YmccUpdaterProcessIdentity
  if($Process){return [Ymcc.UpdaterProcessIdentity]::ReadHandle($Process.Handle)}
  if($ProcessId -le 0){throw 'UPDATE_PROCESS_IDENTITY_MISSING'}
  return [Ymcc.UpdaterProcessIdentity]::ReadId([uint32]$ProcessId)
}

function Stop-YmccUpdaterParent([int]$ProcessId, [string]$ExpectedPath, [string]$CreationFileTime,
  [ValidateRange(0,60000)][int]$GraceMilliseconds=20000, [ValidateRange(1,60000)][int]$ForceMilliseconds=15000) {
  $process=$null
  try {
    try { $process=[Diagnostics.Process]::GetProcessById($ProcessId) } catch [ArgumentException] { return 'already-exited' }
    # Bind a handle before validating identity; never kill an unrelated reused PID.
    $null=$process.Handle
    if ($process.HasExited) { return 'already-exited' }
    $identity=Get-YmccUpdaterProcessIdentity -Process $process
    if (!$identity) { return 'already-exited' }
    if ([IO.Path]::GetFullPath($identity.Image) -ine [IO.Path]::GetFullPath($ExpectedPath) -or
        $identity.Creation -cne $CreationFileTime) {
      throw 'UPDATE_PARENT_IDENTITY_CHANGED'
    }
    $null=$process.CloseMainWindow()
    if ($process.WaitForExit($GraceMilliseconds)) { return 'graceful' }
    # Only the authenticated outgoing YMCC process is force-stopped, under the
    # module maintenance lease. Driver installers and foreign processes are excluded.
    if (!$process.HasExited) { $process.Kill() }
    if (!$process.WaitForExit($ForceMilliseconds)) { throw 'UPDATE_PARENT_EXIT_TIMEOUT' }
    return 'forced-owned-parent'
  } finally { if ($process) { $process.Dispose() } }
}
function Test-YmccModuleFile([string]$Relative) {
  return $Relative -cmatch '^feature-assets/(virtual-gamepad|gyro-motion)/[A-Za-z0-9_-][A-Za-z0-9_./-]*$' -and
    $Relative -cnotmatch '(^|/)(\.\.?|logs?|cache|settings|user-data)(/|$)|\.(log|tmp|bak)$'
}
function Assert-YmccModuleManifest($Manifest, [string]$Version='') {
  if ($Manifest.schemaVersion -ne 1 -or $Manifest.owner -cne 'YMCC' -or
      ($Version -and $Manifest.packageVersion -cne $Version) -or @($Manifest.files).Count -lt 6 -or @($Manifest.files).Count -gt 10000) { throw 'MODULE_MANIFEST_INVALID' }
  $seen=@{}
  foreach ($file in @($Manifest.files)) {
    if (!(Test-YmccModuleFile $file.path) -or $seen.ContainsKey($file.path) -or
        $file.sha256 -cnotmatch '^[a-f0-9]{64}$' -or $file.bytes -lt 0 -or $file.bytes -gt 2147483648) { throw 'MODULE_MANIFEST_FILE_INVALID' }
    $seen[$file.path]=$true
  }
  foreach ($name in @('YeManInputHost.exe','YeManInputHost.dll','YeManInputHost.deps.json','YeManInputHost.runtimeconfig.json','HIDMaestro.Core.dll')) {
    if (!$seen.ContainsKey('feature-assets/virtual-gamepad/'+$name)) { throw ('MODULE_REQUIRED_FILE_MISSING '+$name) }
  }
  if (!@($Manifest.files | Where-Object { $_.path.StartsWith('feature-assets/gyro-motion/') }).Count) { throw 'MODULE_GYRO_PAYLOAD_MISSING' }
}
function New-YmccModuleManifest([string]$Source, [string]$Version) {
  $moduleFixtureRoot=Resolve-YmccDeckyPath $Source; $files=@()
  foreach ($relative in @('feature-assets/virtual-gamepad','feature-assets/gyro-motion')) {
    $folder=Resolve-YmccDeckyPath $moduleFixtureRoot $relative
    foreach ($file in @(Get-ChildItem -LiteralPath $folder -Recurse -File -Force -ErrorAction Stop | Sort-Object FullName)) {
      $name=$file.FullName.Substring($moduleFixtureRoot.Length+1).Replace('\','/')
      if (!(Test-YmccModuleFile $name)) { throw ('MODULE_UNOWNED_PACKAGE_FILE '+$name) }
      $safe=Resolve-YmccDeckyPath $moduleFixtureRoot $name
      $files += [pscustomobject]@{path=$name;bytes=$file.Length;sha256=(Get-YmccDeckyHash $safe)}
    }
  }
  $manifest=[pscustomobject]@{schemaVersion=1;owner='YMCC';packageVersion=$Version;files=$files}
  Assert-YmccModuleManifest $manifest $Version
  return $manifest
}
function Get-YmccModulePlan([string]$Source, [string]$Target, $Manifest, [string]$Version) {
  Assert-YmccModuleManifest $Manifest $Version
  $sourceHome=Resolve-YmccDeckyPath $Source; $targetHome=Resolve-YmccDeckyPath $Target
  if ($sourceHome -ieq $targetHome -or $sourceHome.StartsWith($targetHome+'\',[StringComparison]::OrdinalIgnoreCase) -or $targetHome.StartsWith($sourceHome+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'MODULE_SOURCE_TARGET_OVERLAP' }
  $entries=@(); $incoming=@{}
  foreach ($file in @($Manifest.files)) {
    $src=Resolve-YmccDeckyPath $sourceHome $file.path; $dst=Resolve-YmccDeckyPath $targetHome $file.path
    if (![IO.File]::Exists($src) -or ([IO.FileInfo]$src).Length -ne $file.bytes -or (Get-YmccDeckyHash $src) -cne $file.sha256) { throw ('MODULE_PACKAGE_HASH_MISMATCH '+$file.path) }
    if ([IO.Directory]::Exists($dst)) { throw 'MODULE_TARGET_IS_DIRECTORY' }
    $before=$null; if ([IO.File]::Exists($dst)) { $before=Get-YmccDeckyHash $dst }
    $incoming[$file.path]=$true
    if ($before -cne $file.sha256) { $entries += [pscustomobject]@{path=$file.path;source=$src;before=$before;after=$file.sha256;remove=$false;bytes=$null} }
  }
  # A separately installed newer SDK must not be silently downgraded. System
  # BUS/driver versions are inspected by setup later, never inferred from files.
  $core='feature-assets/virtual-gamepad/HIDMaestro.Core.dll'
  $oldCore=Resolve-YmccDeckyPath $targetHome $core; $newCore=Resolve-YmccDeckyPath $sourceHome $core
  if ([IO.File]::Exists($oldCore)) {
    $oldVersion=[Diagnostics.FileVersionInfo]::GetVersionInfo($oldCore).FileVersion
    $newVersion=[Diagnostics.FileVersionInfo]::GetVersionInfo($newCore).FileVersion
    if ($oldVersion -and $newVersion -and [version]$oldVersion -gt [version]$newVersion) { throw 'MODULE_NEWER_SDK_PRESERVED: 已保留自主安装的较新 HIDM，请使用兼容的 YMCC 包。' }
  }
  $receipt=Resolve-YmccDeckyPath $targetHome '.ymcc-input-managed.json'
  if ([IO.File]::Exists($receipt)) {
    if (([IO.FileInfo]$receipt).Length -gt 4194304) { throw 'MODULE_RECEIPT_TOO_LARGE' }
    $previous=Get-Content -LiteralPath $receipt -Raw -Encoding UTF8 | ConvertFrom-Json
    Assert-YmccModuleManifest $previous
    foreach ($file in $previous.files) {
      if ($incoming.ContainsKey($file.path)) { continue }
      $dst=Resolve-YmccDeckyPath $targetHome $file.path
      if ([IO.File]::Exists($dst) -and (Get-YmccDeckyHash $dst) -ceq $file.sha256) { $entries += [pscustomobject]@{path=$file.path;source=$null;before=$file.sha256;after=$null;remove=$true;bytes=$null} }
    }
  }
  $receiptBytes=[Text.Encoding]::UTF8.GetBytes(($Manifest | ConvertTo-Json -Depth 8 -Compress))
  $sha=[Security.Cryptography.SHA256]::Create()
  try { $receiptHash=([BitConverter]::ToString($sha.ComputeHash($receiptBytes))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
  $before=$null; if ([IO.File]::Exists($receipt)) { $before=Get-YmccDeckyHash $receipt }
  if ($before -cne $receiptHash) { $entries += [pscustomobject]@{path='.ymcc-input-managed.json';source=$null;before=$before;after=$receiptHash;remove=$false;bytes=$receiptBytes} }
  foreach ($entry in $entries) { if ($entry.before) {
    $stream=[IO.File]::Open((Resolve-YmccDeckyPath $targetHome $entry.path),[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None); $stream.Dispose()
  } }
  return [pscustomobject]@{source=$sourceHome;target=$targetHome;entries=$entries;manifest=$Manifest}
}
function Install-YmccModulePackage($Plan, [string]$BackupRoot) {
  $lease=Enter-YmccModuleMaintenance
  try { return Invoke-YmccDeckyInstall $Plan $BackupRoot }
  finally { Exit-YmccModuleMaintenance $lease }
}
function Restore-YmccModulePackage($Transaction) {
  $lease=Enter-YmccModuleMaintenance
  try { Undo-YmccDeckyTransaction $Transaction }
  finally { Exit-YmccModuleMaintenance $lease }
}
