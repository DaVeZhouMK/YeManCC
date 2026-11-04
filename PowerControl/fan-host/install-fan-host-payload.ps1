<#
.SYNOPSIS
  Installs the YeMan Fan Host payload manifest closure.

.DESCRIPTION
  Validates every payload manifest entry against its on-disk file
  (path 防逃逸 + 存在性 + SHA-256) and the shared HC runtime manifest.
  方案 A（2026-09-12 用户裁决）：不再施加 ACL 不可变 / quarantine /
  交互确认；部署边界 = 防逃逸 + 存在 + 哈希两层门。

  取得坑史（2026-09-11 R-publisher 同批 + 2026-09-12 方案 A 用户裁决）：
  ① 禁止以 IndexOfAny('\/') 整体拒绝子路径——HC runtime.json 合法含
     XInputPlus/Loader/...、Resources/... 等子目录条目，所有 manifest 段
     （payload 与 runtime）一律"归一化 + IsPathRooted + 逐段 .."判据，
     （911-23 风扇必挂已实证）。
  ② 禁止在字符串字面量里写 '\'（R3 语法炸）：R3-redo 后统一用
     TrimEnd([char]0x5C)，且本文件保持 UTF-8 BOM（PS 5.1 按 ANSI 误读
     无 BOM 非 ASCII 脚本会吞掉下一行语句）。
  ③ 修改本文件后必须重算 ps1 → payload files[] → manifest → main.cpp
     $expectedFanHostV2ManifestSha256 全链哈希，禁止手填哈希值。build
     前门（build-workspace.ps1 Assert-PublishPreGate）会自动对拍，不一致即 halt。
  ④ 方案 A（2026-09-12 用户裁决）：部署阀门=防逃逸 + 存在 + 哈希两层，
     ACL 不可变 / quarantine 扫描 / 交互 ShouldProcess 已删除，禁止加回；
     实例运行中仅警告放行（不再阻断）。
#>
[CmdletBinding(SupportsShouldProcess)]
param(
  [string]$PayloadDirectory = '',
  [string]$StateDirectory = (Join-Path $env:LOCALAPPDATA 'YeManCC\fan-host'),
  [switch]$SkipStateDirectory
)

$ErrorActionPreference = 'Stop'
$PayloadDirectory = if ([string]::IsNullOrWhiteSpace($PayloadDirectory)) { $PSScriptRoot } else { $PayloadDirectory }
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  throw '请以管理员身份运行此脚本；未执行任何部署。'
}

function Get-FullPath([string]$Path) {
  return [IO.Path]::GetFullPath($Path).TrimEnd([char]0x5C)  # R3-redo: 0x5C='\'，UTF-8 BOM 安全
}

function Get-Sha256([string]$Path) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    $stream = [IO.File]::OpenRead($Path)
    try {
      return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
    } finally {
      $stream.Dispose()
    }
  } finally {
    $sha.Dispose()
  }
}

function Initialize-FanDeploymentRecordStore {
  if ('YeManCC.FanDeploymentRecordV1.Store' -as [type]) { return }
  # Small record IO only: strict JSON, exact archive, source snapshot, bounded lock.
  Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Text;
using System.Globalization;
using System.Collections.Generic;
using System.Security.Cryptography;
using System.Text.RegularExpressions;
namespace YeManCC.FanDeploymentRecordV1 {
  internal sealed class Value {
    internal char Kind; internal string Raw; internal string Text;
    internal Dictionary<string, Value> Fields;
    internal Dictionary<string, string> Names;
    internal List<string> Order;
  }
  internal sealed class StrictJson {
    readonly string text; int at; internal bool FutureSeen;
    internal StrictJson(string value) { text = value; }
    static FormatException Bad() { return new FormatException("FAN_DEPLOYMENT_RECORD_INVALID"); }
    void Space() { while (at < text.Length && (text[at]==' ' || text[at]=='\r' || text[at]=='\n' || text[at]=='\t')) at++; }
    bool Take(char c) { if (at < text.Length && text[at]==c) { at++; return true; } return false; }
    bool Literal(string value) { if(at+value.Length>text.Length||string.CompareOrdinal(text,at,value,0,value.Length)!=0)return false;at+=value.Length;return true; }
    string String() {
      if (!Take('"')) throw Bad(); var value = new StringBuilder();
      while (at < text.Length) {
        char c=text[at++]; if (c=='"') return value.ToString(); if (c<32) throw Bad();
        if (c=='\\') {
          if (at==text.Length) throw Bad(); c=text[at++];
          switch(c) {
            case '"': case '\\': case '/': break;
            case 'b': c='\b'; break; case 'f': c='\f'; break; case 'n': c='\n'; break;
            case 'r': c='\r'; break; case 't': c='\t'; break;
            case 'u': c=Hex(); break; default: throw Bad();
          }
        }
        if (char.IsHighSurrogate(c)) {
          value.Append(c); if (at>=text.Length) throw Bad(); char low=text[at++];
          if (low=='\\') { if (!Take('u')) throw Bad(); low=Hex(); }
          if (!char.IsLowSurrogate(low)) throw Bad(); value.Append(low);
        } else { if (char.IsLowSurrogate(c)) throw Bad(); value.Append(c); }
      } throw Bad();
    }
    char Hex() {
      if (at+4>text.Length) throw Bad(); int n=0;
      for(int i=0;i<4;i++) { char c=text[at++]; int v=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; if(v<0)throw Bad(); n=n*16+v; }
      return (char)n;
    }
    Value Parse(int depth) {
      Space(); int start=at; if(at==text.Length)throw Bad(); char c=text[at]; var value=new Value();
      if(c=='{' || c=='[') {
        if(depth>=16)throw Bad(); at++; value.Kind=c;
        if(c=='{') {
          value.Fields=new Dictionary<string,Value>(StringComparer.Ordinal);value.Names=new Dictionary<string,string>(StringComparer.Ordinal);value.Order=new List<string>();
          Space(); if(!Take('}')) { while(true) {
            Space(); int nameAt=at; string name=String(),rawName=text.Substring(nameAt,at-nameAt); Space(); if(!Take(':'))throw Bad(); Value child=Parse(depth+1);
            if(depth==0 && (name=="schemaVersion" || name=="validationRulesVersion") && Store.Integer(child)>1)FutureSeen=true;
            if(value.Fields.ContainsKey(name))throw Bad(); value.Fields.Add(name,child);value.Names.Add(name,rawName);value.Order.Add(name);
            Space(); if(Take('}'))break; if(!Take(','))throw Bad();
          } }
        } else { Space(); if(!Take(']')) { while(true) { Parse(depth+1);Space();if(Take(']'))break;if(!Take(','))throw Bad(); } } }
      } else if(c=='"') { value.Kind='s';value.Text=String(); }
      else {
        if(Literal("true")){value.Kind='b';} else if(Literal("false")){value.Kind='b';} else if(Literal("null")){value.Kind='0';}
        else {
          value.Kind='n';Take('-'); if(!Take('0')){if(at>=text.Length||text[at]<'1'||text[at]>'9')throw Bad();while(at<text.Length&&text[at]>='0'&&text[at]<='9')at++;}
          if(Take('.')){int first=at;while(at<text.Length&&text[at]>='0'&&text[at]<='9')at++;if(first==at)throw Bad();}
          if(Take('e')||Take('E')){if(!Take('+'))Take('-');int first=at;while(at<text.Length&&text[at]>='0'&&text[at]<='9')at++;if(first==at)throw Bad();}
          double number;if(!double.TryParse(text.Substring(start,at-start),NumberStyles.Float,CultureInfo.InvariantCulture,out number)||double.IsInfinity(number)||double.IsNaN(number))throw Bad();
        }
      }
      value.Raw=text.Substring(start,at-start);return value;
    }
    internal Value Read() { Value value=Parse(0);Space();if(at!=text.Length)throw Bad();return value; }
  }
  public sealed class Snapshot { internal string Path; internal byte[] Bytes; internal Value Document; internal bool Corrupt; }
  public static class Store {
    const int Limit=65536;
    static readonly UTF8Encoding Utf8=new UTF8Encoding(false,true);
    static readonly string[] Managed={"schemaVersion","validationRulesVersion","payloadRoot","runtimeRoot","payloadManifestSha256","runtimeManifestSha256","runtimeId","result","payloadFileCount","runtimeFileCount","deployedAtUtc"};
    static Exception Fault(string code){return new IOException(code);}
    internal static double Integer(Value value){double n;if(value==null||value.Kind!='n'||!double.TryParse(value.Raw,NumberStyles.Float,CultureInfo.InvariantCulture,out n)||n<0||n>9007199254740991d||Math.Truncate(n)!=n)return -1;return n;}
    static Value Field(Value doc,string name){Value v;return doc!=null&&doc.Kind=='{'&&doc.Fields.TryGetValue(name,out v)?v:null;}
    static string Text(Value doc,string name){Value v=Field(doc,name);return v!=null&&v.Kind=='s'?v.Text:null;}
    static bool Digest(string s){return s!=null&&Regex.IsMatch(s,"\\A[0-9a-fA-F]{64}\\z");}
    static bool DirectoryName(string s){return s!=null&&Regex.IsMatch(s,@"\A(?:[a-zA-Z]:[\\/]|\\\\[^\\/]+[\\/][^\\/]+)")&&!Regex.IsMatch(s,@"[\x00-\x1f]")&&!Regex.IsMatch(s,@"(?:\A|[\\/])\.{1,2}(?:[\\/]|\z)");}
    static bool Valid(Value doc) {
      if(Integer(Field(doc,"schemaVersion"))!=1 || Integer(Field(doc,"validationRulesVersion"))<0 || Integer(Field(doc,"validationRulesVersion"))>1)return false;
      string result=Text(doc,"result"),id=Text(doc,"runtimeId");
      if(!DirectoryName(Text(doc,"payloadRoot"))||!DirectoryName(Text(doc,"runtimeRoot"))||!Digest(Text(doc,"payloadManifestSha256"))||!Digest(Text(doc,"runtimeManifestSha256"))||id==null||!Regex.IsMatch(id,"\\A[a-zA-Z0-9][a-zA-Z0-9._-]{0,159}\\z")||(result!="ok"&&result!="failed"))return false;
      foreach(string key in new[]{"payloadFileCount","runtimeFileCount"}) { Value v=Field(doc,key);if(v!=null&&(Integer(v)<=0||Integer(v)>100000))return false; }
      Value stamp=Field(doc,"deployedAtUtc");if(stamp!=null){DateTime parsed;if(stamp.Kind!='s'||!Regex.IsMatch(stamp.Text,@"\A\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(?:\.\d{1,7})?Z\z")||!DateTime.TryParse(stamp.Text,CultureInfo.InvariantCulture,DateTimeStyles.RoundtripKind,out parsed))return false;}
      return true;
    }
    static void Plain(string path) {
      FileAttributes attrs;try{attrs=File.GetAttributes(path);}catch(FileNotFoundException){return;}catch(DirectoryNotFoundException){return;}
      if((attrs&FileAttributes.ReparsePoint)!=0)throw Fault("FAN_DEPLOYMENT_REPARSE_REFUSED");
    }
    static byte[] Observe(string path) {
      Plain(path);
      try {
        using(var file=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read)) {
          if(file.Length>Limit)throw Fault("FAN_DEPLOYMENT_RECORD_LIMIT");
          byte[] bytes=new byte[(int)file.Length];int offset=0,n;
          while(offset<bytes.Length){n=file.Read(bytes,offset,bytes.Length-offset);if(n==0)throw Fault("FAN_DEPLOYMENT_READ_INCOMPLETE");offset+=n;}
          if(file.ReadByte()!=-1)throw Fault("FAN_DEPLOYMENT_SOURCE_CHANGED");return bytes;
        }
      } catch(FileNotFoundException){return null;}catch(DirectoryNotFoundException){return null;}
    }
    static bool Same(byte[] a,byte[] b){if(a==null||b==null)return a==b;if(a.Length!=b.Length)return false;for(int i=0;i<a.Length;i++)if(a[i]!=b[i])return false;return true;}
    static void RequireSame(string path,Snapshot expected){if(!Same(Observe(path),expected.Bytes))throw Fault("FAN_DEPLOYMENT_SOURCE_CHANGED");}
    public static Snapshot Capture(string path) {
      path=System.IO.Path.GetFullPath(path);byte[] bytes=Observe(path);var snapshot=new Snapshot{Path=path,Bytes=bytes,Corrupt=bytes!=null};if(bytes==null)return snapshot;
      StrictJson reader=null;
      try {
        string text=Utf8.GetString(bytes);if(text.StartsWith("\uFEFF",StringComparison.Ordinal))text=text.Substring(1);
        reader=new StrictJson(text);Value doc=reader.Read();if(reader.FutureSeen)throw Fault("FAN_DEPLOYMENT_FUTURE_RECORD");if(doc.Kind=='{')snapshot.Document=doc;snapshot.Corrupt=!Valid(doc);
      } catch(FormatException) {if(reader!=null&&reader.FutureSeen)throw Fault("FAN_DEPLOYMENT_FUTURE_RECORD");}
        catch(DecoderFallbackException) { /* Archive invalid UTF-8 bytes exactly, never decode with replacement. */ }
      return snapshot;
    }
    static string Merge(Value prior,Value next) {
      var text=new StringBuilder("{");bool first=true;var managed=new HashSet<string>(Managed,StringComparer.Ordinal);
      foreach(string name in next.Order){if(!first)text.Append(',');first=false;text.Append(next.Names[name]).Append(':').Append(next.Fields[name].Raw);}
      if(prior!=null)foreach(string name in prior.Order)if(!managed.Contains(name)&&!next.Fields.ContainsKey(name)){if(!first)text.Append(',');first=false;text.Append(prior.Names[name]).Append(':').Append(prior.Fields[name].Raw);}
      return text.Append('}').ToString();
    }
    static void SaveNew(string path,byte[] bytes) {
      using(var file=new FileStream(path,FileMode.CreateNew,FileAccess.Write,FileShare.None,4096,FileOptions.WriteThrough)){file.Write(bytes,0,bytes.Length);file.Flush(true);}
    }
    static void Archive(string path,byte[] original) {
      string digest;using(var hash=SHA256.Create())digest=BitConverter.ToString(hash.ComputeHash(original)).Replace("-","").ToLowerInvariant();
      string archive=path+".corrupt."+digest+".json";byte[] existing=Observe(archive);
      if(existing==null)SaveNew(archive,original);
      if(!Same(Observe(archive),original))throw Fault("FAN_DEPLOYMENT_ARCHIVE_UNCONFIRMED");
    }
    public static void Write(string path,string record,Snapshot expected) {
      path=System.IO.Path.GetFullPath(path);if(expected==null||!string.Equals(path,expected.Path,StringComparison.OrdinalIgnoreCase))throw Fault("FAN_DEPLOYMENT_EXPECTED_SOURCE_REQUIRED");
      var parser=new StrictJson(record);Value next=parser.Read();if(parser.FutureSeen||!Valid(next)||Integer(Field(next,"validationRulesVersion"))!=1||Text(next,"result")!="ok")throw Fault("FAN_DEPLOYMENT_NEW_RECORD_INVALID");
      byte[] content=Utf8.GetBytes(Merge(expected.Document,next));if(content.Length>Limit)throw Fault("FAN_DEPLOYMENT_RECORD_LIMIT");
      string directory=System.IO.Path.GetDirectoryName(path);Plain(directory);string lockPath=path+".lock";Plain(lockPath);
      // Immediate refusal: no lock polling and no retries in this process.
      using(var gate=new FileStream(lockPath,FileMode.OpenOrCreate,FileAccess.ReadWrite,FileShare.None)) {
        RequireSame(path,expected);
        if(expected.Bytes!=null&&expected.Corrupt)Archive(path,expected.Bytes);
        string stagePath=path+"."+Guid.NewGuid().ToString("N")+".tmp";
        try {
          SaveNew(stagePath,content);
          RequireSame(path,expected);
          if(expected.Bytes==null)File.Move(stagePath,path);else File.Replace(stagePath,path,null,true);
          // Confirm the committed bytes before a producer can acknowledge success.
          if(!Same(Observe(path),content))throw Fault("FAN_DEPLOYMENT_RECORD_WRITE_UNCONFIRMED");
        } finally {if(File.Exists(stagePath))File.Delete(stagePath);}
      }
    }
  }
}
'@
}

function Get-FanDeploymentRecordSnapshot([string]$Path) {
  Initialize-FanDeploymentRecordStore
  return [YeManCC.FanDeploymentRecordV1.Store]::Capture($Path)
}

function Write-FanDeploymentRecord([string]$Path, [string]$Record, $Expected) {
  Initialize-FanDeploymentRecordStore
  [YeManCC.FanDeploymentRecordV1.Store]::Write($Path, $Record, $Expected)
}

# Capture before full validation: a later file generation cannot be overwritten.
$deploymentSnapshot = $null
if ($SkipStateDirectory -eq $false) {
  $deploymentRecordPath = Join-Path (Get-FullPath $StateDirectory) 'YeManFanHost.deployment.json'
  $deploymentSnapshot = Get-FanDeploymentRecordSnapshot $deploymentRecordPath
}

$payloadFull = Get-FullPath $PayloadDirectory
if (-not (Test-Path -LiteralPath $payloadFull -PathType Container)) {
  throw "Fan Host 目录不存在: $payloadFull"
}

# 方案 A（2026-09-12 用户裁决）：原"实例运行中即拒改"改为警告放行——升级
# 期间运行的 Host 属已知暂态，部署边界交给防逃逸+存在+哈希两层门；不再用
# 进程占用作为阻断（launcher/updater 已各自有预检护栏）。
$payloadHostPath = Get-FullPath (Join-Path $payloadFull 'YeManFanHost.exe')
try {
  $resident = @(Get-CimInstance Win32_Process -Filter "Name='YeManFanHost.exe'" -ErrorAction Stop)
  foreach ($process in $resident) {
    $image = [string]$process.ExecutablePath
    if (-not [string]::IsNullOrWhiteSpace($image) -and
        (Get-FullPath $image).Equals($payloadHostPath, [StringComparison]::OrdinalIgnoreCase)) {
      Write-Warning "检测到 Fan Host 实例仍在运行 (pid=$($process.ProcessId))，按方案 A 放行；建议先退出主程序再升级。"
    }
  }
} catch {
  Write-Warning "无法确认 Fan Host 是否仍在运行；按方案 A 放行（诊断：$($_.Exception.Message)）"
}
$manifestPath = Join-Path $payloadFull 'YeManFanHost.payload.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
  throw '缺少 YeManFanHost.payload.json；无法校验载荷。'
}
if (((Get-Item -LiteralPath $manifestPath -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
  throw 'Fan Host manifest 不能是重解析点。'
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($manifest.schemaVersion -ne 2 -or $null -eq $manifest.files -or $manifest.files.Count -eq 0) {
  throw 'Fan Host payload manifest 无效或为空。'
}
$runtimeManifestRelative = [string]$manifest.runtimeManifest
$manifestRuntimeId = [string]$manifest.runtimeId
if ([string]::IsNullOrWhiteSpace($runtimeManifestRelative) -or
    $manifestRuntimeId -notmatch '^(?:HC-CANDIDATE-0\.32\.4\.0-[0-9a-f]{8}-[0-9]{8}|YMCC-DEVICECORE-20261018-v1)$' -or
    [IO.Path]::IsPathRooted($runtimeManifestRelative) -or
    $runtimeManifestRelative -notmatch '(?i)^\.\.[\\/][^\\/]+(?:[\\/][^\\/]+)*[\\/]HandheldCompanion\.runtime\.json$' -or
    $runtimeManifestRelative.Substring(3) -match '(^|[\\/])\.\.([\\/]|$)') {
  throw 'Fan Host payload runtimeManifest 路径无效。'
}
$runtimeManifestPath = Get-FullPath (Join-Path (Split-Path -Parent $payloadFull) $runtimeManifestRelative.Substring(3))
$runtimeFull = Get-FullPath (Split-Path -Parent $runtimeManifestPath)

$expectedFiles = @{}
foreach ($entry in $manifest.files) {
  $relative = [string]$entry.path
  $expectedHash = [string]$entry.sha256
  # 方案 A（2026-09-12 用户裁决）：与 runtime 段同构的防逃逸——归一化
  # （/ → \）后只防 IsPathRooted 与逐段 .. 逃逸，允许合法子路径。hex64 校验、
  # 去重、存在性、sha256 对拍全部保留（不再 IndexOfAny('\/') 整体拒绝）。
  $normalized = $relative.Replace('/', '\')
  if ([IO.Path]::IsPathRooted($normalized) -or
      $normalized -match '(^|[\\/])\.\.([\\/]|$)' -or
      $relative -eq '') {
    throw "Fan Host manifest 包含不安全的条目: $relative"
  }
  if ($expectedHash -notmatch '^[0-9a-fA-F]{64}$') {
    throw "Fan Host manifest 包含不安全的哈希: $relative"
  }
  if ($expectedFiles.ContainsKey($relative.ToLowerInvariant())) {
    throw "Fan Host manifest 有重复条目: $relative"
  }
  $path = Join-Path $payloadFull $normalized
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "Fan Host manifest 文件缺失: $relative"
  }
  if (((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw "Fan Host manifest 文件不能是重解析点: $relative"
  }
  if ((Get-Sha256 $path) -ne $expectedHash.ToLowerInvariant()) {
    throw "Fan Host manifest 哈希不匹配: $relative"
  }
  $expectedFiles[$relative.ToLowerInvariant()] = $relative
}

$fanHostConfigEntry = @($manifest.files | Where-Object {
  ([string]$_.path).Equals('YeManFanHost.json', [StringComparison]::OrdinalIgnoreCase)
})
if ($fanHostConfigEntry.Count -ne 1) {
  throw 'Fan Host manifest 必须包含唯一 YeManFanHost.json。'
}
$fanHostConfigPath = Join-Path $payloadFull 'YeManFanHost.json'
$fanHostConfig = Get-Content -LiteralPath $fanHostConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json
$expectedFanLogPath = '%LOCALAPPDATA%\YeManCC\fan-host\logs\yeman-fan-host.log'
$configuredFanLogPaths = @($fanHostConfig.Serilog.WriteTo | ForEach-Object {
  if ($null -ne $_.Args) { [string]$_.Args.path }
})
if ($configuredFanLogPaths -notcontains $expectedFanLogPath) {
  throw "YeManFanHost.json 必须把 Serilog 日志写入: $expectedFanLogPath"
}

foreach ($required in @(
  'YeManFanHost.exe', 'YeManFanHost.dll', 'YeManFanBackend.dll', 'YeManFanHost.deps.json',
  'YeManFanHost.runtimeconfig.json', 'YeManFanHost.authorization.md',
  'install-fan-host-payload.ps1'
)) {
  if (-not $expectedFiles.ContainsKey($required.ToLowerInvariant())) {
    throw "Fan Host manifest 缺少必要文件: $required"
  }
}

if (-not (Test-Path -LiteralPath $runtimeManifestPath -PathType Leaf)) { throw "缺少 HC 公共 runtime manifest: $runtimeManifestPath" }
$runtimeManifest = Get-Content -LiteralPath $runtimeManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($runtimeManifest.schemaVersion -ne 1 -or $runtimeManifest.runtimeId -ne $manifestRuntimeId -or $null -eq $runtimeManifest.files -or $runtimeManifest.files.Count -eq 0) { throw 'HC 公共 runtime manifest 无效或与 FanHost manifest 不一致。' }
foreach ($entry in $runtimeManifest.files) {
  $relative = [string]$entry.path
  # 2026-09-11：去掉 IndexOfAny('\/') 整体拒绝——manifest 允许子路径
  # （XInputPlus/Loader/...），整体拒绝会让任何含子路径条目必失败。改为
  # 归一化（/ → \）后只防根路径与 .. 逃逸；存在性与 sha256 校验逻辑不变。
  $normalizedRelative = $relative.Replace('/', '\')
  $path = Join-Path $runtimeFull $normalizedRelative
  if ([IO.Path]::IsPathRooted($normalizedRelative) -or
      $normalizedRelative -match '(^|[\\/])\.\.([\\/]|$)' -or
      -not (Test-Path -LiteralPath $path -PathType Leaf) -or
      (Get-Sha256 $path) -ne ([string]$entry.sha256).ToLowerInvariant()) { throw "HC 公共 runtime 文件缺失或哈希不匹配: $relative" }
  }

function Assert-DeviceSharedFiles([string]$DeviceRuntimeRoot, $RuntimeManifest) {
  if (-not $RuntimeManifest.PSObject.Properties['sharedDependencies']) { return }
  if ([string]$RuntimeManifest.sharedDependencies.directory -cne 'PowerControl/ymcc-shared-runtime' -or @($RuntimeManifest.sharedDependencies.files).Count -ne 7) { throw 'Device shared dependency contract is invalid.' }
  $sharedRoot = Join-Path (Split-Path -Parent (Split-Path -Parent $DeviceRuntimeRoot)) 'ymcc-shared-runtime'
  $seen = @{}
  foreach ($entry in @($RuntimeManifest.sharedDependencies.files)) {
    $name = [string]$entry.path
    if ($name -notmatch '^[^/\\:]+\.dll$' -or $seen.ContainsKey($name) -or [string]$entry.sha256 -notmatch '^[0-9a-fA-F]{64}$') { throw 'Device shared dependency path/hash is invalid.' }
    $path = Join-Path $sharedRoot $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -ne [int64]$entry.bytes -or
        (Get-Sha256 $path) -cne ([string]$entry.sha256).ToLowerInvariant()) { throw ('Device shared dependency missing or changed: ' + $name) }
    $seen[$name] = $true
  }
}
Assert-DeviceSharedFiles $runtimeFull $runtimeManifest

# 方案 A（2026-09-12 用户裁决）：阀门=防逃逸 + 存在 + 哈希两层。ACL 不可变、
# quarantine 扫描、交互 ShouldProcess 已整段删除（run-emergency-fan-restore.ps1
# 不再被当异物处理），禁止加回。

# FAN-927 §2.1（2026-09-27 用户批准）：**完整文件校验归部署边界**。上面这套逐条
# 校验成功后，向**可变状态目录**原子写出一份小"部署记录"，把这次验证结果绑定到
# 规范安装根 + 两个小清单摘要 + 包代 + 校验规则版本。运行期（前端 launcher 与 Host）
# 只读这份记录与两个小清单摘要，不再遍历依赖目录、不再逐文件 certutil、不再重复全扫。
# 记录**不**放进 payload 自包含清单，也不新增任何人工填写的 pin。
if (-not $SkipStateDirectory) {
  $recordDirectory = Get-FullPath $StateDirectory
  New-Item -ItemType Directory -Force -Path $recordDirectory | Out-Null
  $recordPath = Join-Path $recordDirectory 'YeManFanHost.deployment.json'
  $record = [ordered]@{
    schemaVersion = 1
    validationRulesVersion = 1
    payloadRoot = $payloadFull.ToLowerInvariant()
    runtimeRoot = $runtimeFull.ToLowerInvariant()
    payloadManifestSha256 = (Get-Sha256 $manifestPath)
    runtimeManifestSha256 = (Get-Sha256 $runtimeManifestPath)
    runtimeId = $manifestRuntimeId
    payloadFileCount = $manifest.files.Count
    runtimeFileCount = $runtimeManifest.files.Count
    result = 'ok'
    deployedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
  }
  Write-FanDeploymentRecord $recordPath ($record | ConvertTo-Json -Depth 4) $deploymentSnapshot
  # 注意：不得新增第二个 FAN_HOST_*_OK 成功常量——前门（build-workspace.ps1
  # Assert-PublishPreGate §3）要求 ps1 的每个 FAN_HOST_*_OK 都被 fanHost.ts 的
  # 成功判定正则接受；部署记录的"已写出"不是 launcher 的成功判据（它另行读取记录核对），
  # 因此这里用不带 _OK 的标记，保持单一成功常量契约。
  Write-Output "FAN_HOST_DEPLOYMENT_RECORD_WRITTEN: $recordPath"
}

Write-Output "FAN_HOST_PAYLOAD_OK: $payloadFull"
