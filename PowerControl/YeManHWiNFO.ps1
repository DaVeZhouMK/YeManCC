param(
    [ValidateSet('start', 'restart')]
    [string]$Mode = 'start',
    [switch]$Elevated,
    # 0 retains a valid existing period, otherwise the managed template's period.
    [ValidateSet(0, 1000, 1500, 2000, 3000)]
    [int]$SensorIntervalMs = 0,
    # Optional path for an unregistered portable HWiNFO64 installation.
    [string]$ExecutablePath = '',
    # Explicit user repair only; never infer usable originals from damaged bytes.
    [switch]$RepairInvalidConfig
)

$ErrorActionPreference = 'Stop'

function Fail([int]$code, [string]$message, [switch]$NeedsElevation) {
    # Throw instead of exiting here so the transaction always releases its mutex
    # before an elevated child is started (and before tests observe the failure).
    $failure = New-Object System.InvalidOperationException($message)
    $failure.Data['HWiNFOExitCode'] = $code
    $failure.Data['HWiNFONeedsElevation'] = [bool]$NeedsElevation
    throw $failure
}

function Test-HWiNFOAccessDenied($errorRecord) {
    $exception = $errorRecord.Exception
    while ($exception) {
        if ($exception -is [System.UnauthorizedAccessException] -or
            $exception.HResult -eq -2147024891 -or
            ($exception -is [System.ComponentModel.Win32Exception] -and $exception.NativeErrorCode -eq 5)) {
            return $true
        }
        $exception = $exception.InnerException
    }
    return $false
}

function Resolve-HWiNFO {
    if ($ExecutablePath) {
        # Never reinterpret an explicit path as a command line or fall back to a
        # different installation. The same checked value is forwarded on UAC.
        if ($ExecutablePath.IndexOfAny([char[]]@([char]0, [char]10, [char]13, [char]34)) -ge 0 -or
            -not [IO.Path]::IsPathRooted($ExecutablePath) -or
            [IO.Path]::GetFileName($ExecutablePath) -ine 'HWiNFO64.exe' -or
            -not (Test-Path -LiteralPath $ExecutablePath -PathType Leaf)) {
            Fail 2 'The explicit HWiNFO64.exe path is invalid or missing'
        }
        return (Resolve-Path -LiteralPath $ExecutablePath).Path
    }
    $candidates = @()
    try {
        $p = @(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue | Select-Object -First 1)
        if ($p) { $candidates += $p.Path }
    } catch { }
    $candidates += @(
        (Join-Path ${env:ProgramFiles} 'HWiNFO64\HWiNFO64.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'HWiNFO64\HWiNFO64.exe'),
        (Join-Path ${env:LOCALAPPDATA} 'HWiNFO64\HWiNFO64.exe')
    )
    try {
        $keys = @(
            'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
            'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
            'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*'
        )
        foreach ($key in $keys) {
            Get-ItemProperty $key -ErrorAction SilentlyContinue |
                Where-Object { $_.DisplayName -match 'HWiNFO' } |
                ForEach-Object {
                    if ($_.InstallLocation) { $candidates += (Join-Path $_.InstallLocation 'HWiNFO64.exe') }
                    if ($_.DisplayIcon) { $candidates += ($_.DisplayIcon -replace ',.*$','').Trim('"') }
                }
        }
    } catch { }
    foreach ($candidate in ($candidates | Where-Object { $_ } | Select-Object -Unique)) {
        try {
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                return (Resolve-Path -LiteralPath $candidate).Path
            }
        } catch { }
    }
    return $null
}

function Stop-HWiNFO {
    $running = @(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue)
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    foreach ($process in $running) {
        try {
            Stop-Process -InputObject $process -Force -ErrorAction Stop
        } catch {
            # A process that exited on its own is already stopped. Other errors
            # (notably an elevated HWiNFO) must not be silently ignored.
            if (@(Get-Process -Id $process.Id -ErrorAction SilentlyContinue).Count -gt 0) {
                Fail 4 ("HWiNFO64.exe could not be stopped: {0}" -f $_.Exception.Message) -NeedsElevation:(Test-HWiNFOAccessDenied $_)
            }
        }
    }
    foreach ($process in $running) {
        try {
            $remaining = [Math]::Max(0, 10000 - [int]$watch.ElapsedMilliseconds)
            if (-not $process.WaitForExit($remaining)) { Fail 4 'HWiNFO64.exe did not exit within 10 seconds; INI was not copied' }
        } catch {
            if ($_.Exception.Data.Contains('HWiNFOExitCode')) { throw }
            Fail 4 ("HWiNFO64.exe exit could not be confirmed; INI was not copied: {0}" -f $_.Exception.Message) -NeedsElevation:(Test-HWiNFOAccessDenied $_)
        }
    }
    # Re-enumerate as well: never copy if another instance appeared meanwhile.
    while (@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count -gt 0) {
        if ($watch.ElapsedMilliseconds -ge 10000) { Fail 4 'HWiNFO64.exe is still running; INI was not copied' }
        Start-Sleep -Milliseconds 100
    }
}

function Get-HWiNFOConfigHash([string]$path) {
    $stream = [System.IO.File]::Open($path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($stream)) }
    finally { $sha.Dispose(); $stream.Dispose() }
}

function Initialize-HWiNFOReceiptJson {
  if('Ymcc.HwinfoJournalJson' -as [type]){return}
  Add-Type -TypeDefinition @"
using System;
using System.IO;
using System.Text;
using System.Collections.Generic;
using System.Globalization;
namespace Ymcc {
 public static class HwinfoJournalJson {
  sealed class Frame { internal bool Object; internal bool Key=true; internal HashSet<string> Keys=new HashSet<string>(StringComparer.OrdinalIgnoreCase); }
  static string String(string text, ref int i) {
   var value=new StringBuilder();
   while(++i<text.Length) {
    char c=text[i]; if(c=='"')return value.ToString();
    if(c<32)throw new IOException("HWINFO_JSON_CONTROL_CHARACTER");
    if(c!='\\'){value.Append(c);continue;}
    if(++i>=text.Length)break;c=text[i];
    switch(c) {
     case '"':case '\\':case '/':value.Append(c);break;
     case 'b':value.Append('\b');break;case 'f':value.Append('\f');break;
     case 'n':value.Append('\n');break;case 'r':value.Append('\r');break;case 't':value.Append('\t');break;
     case 'u':
      if(i+4>=text.Length)throw new IOException("HWINFO_JSON_UNICODE_TRUNCATED");
      value.Append((char)int.Parse(text.Substring(i+1,4),NumberStyles.HexNumber,CultureInfo.InvariantCulture));i+=4;break;
     default:throw new IOException("HWINFO_JSON_ESCAPE_INVALID");
    }
   }
   throw new IOException("HWINFO_JSON_STRING_TRUNCATED");
  }
  public static void AssertObject(string text) {
   var stack=new Stack<Frame>();bool started=false;
   for(int i=0;i<text.Length;i++) {
    char c=text[i];if(char.IsWhiteSpace(c))continue;
    if(started&&stack.Count==0)throw new IOException("HWINFO_JSON_TRAILING_DATA");
    if(!started){if(c!='{')throw new IOException("HWINFO_JSON_ROOT_INVALID");started=true;}
    if(c=='"') {
     string key=String(text,ref i);
     if(stack.Count>0&&stack.Peek().Object&&stack.Peek().Key) {
      if(!stack.Peek().Keys.Add(key))throw new IOException("HWINFO_JSON_DUPLICATE_FIELD");
      stack.Peek().Key=false;
     }
    } else if(c=='{'||c=='[') {
     if(stack.Count>=64)throw new IOException("HWINFO_JSON_DEPTH_LIMIT");
     stack.Push(new Frame{Object=c=='{'});
    } else if(c=='}'||c==']') {
     if(stack.Count==0||stack.Pop().Object!=(c=='}'))throw new IOException("HWINFO_JSON_STRUCTURE_INVALID");
    } else if(c==','&&stack.Count>0&&stack.Peek().Object)stack.Peek().Key=true;
   }
   if(!started||stack.Count!=0)throw new IOException("HWINFO_JSON_INCOMPLETE");
  }
 }
}
"@
}

function Test-HWiNFOConfigFile([string]$Path) {
    try {$attributes=[IO.File]::GetAttributes($Path)}
    catch [IO.FileNotFoundException] {return $false}
    catch [IO.DirectoryNotFoundException] {
        $parent=[IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Path))
        try {if(([IO.File]::GetAttributes($parent) -band [IO.FileAttributes]::Directory) -eq 0){Fail 8 'HWiNFO configuration parent is not a directory'}}
        catch [IO.FileNotFoundException] {return $false}
        catch [IO.DirectoryNotFoundException] {return $false}
        return $false
    }
    if(($attributes -band ([IO.FileAttributes]::Directory -bor [IO.FileAttributes]::ReparsePoint)) -ne 0){Fail 8 'HWiNFO configuration is not a regular file'}
    $parent=[IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Path))
    while($parent){if(([IO.File]::GetAttributes($parent) -band [IO.FileAttributes]::ReparsePoint) -ne 0){Fail 8 'HWiNFO configuration path is redirected'};$parent=[IO.Path]::GetDirectoryName($parent)}
    return $true
}
function Read-HWiNFOConfigBytes([string]$Path,[int]$Maximum=262144) {
    if(!(Test-HWiNFOConfigFile $Path)){throw [IO.FileNotFoundException]::new('HWiNFO configuration missing',$Path)}
    $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    try {
        if($stream.Length -gt $Maximum){Fail 8 'HWiNFO configuration exceeds its fixed read budget'}
        $bytes=New-Object byte[] ([int]$stream.Length);$offset=0
        while($offset -lt $bytes.Length){$read=$stream.Read($bytes,$offset,$bytes.Length-$offset);if(!$read){Fail 8 'HWiNFO configuration changed while reading'};$offset+=$read}
        if($stream.ReadByte() -ne -1){Fail 8 'HWiNFO configuration grew while reading'}
        return ,$bytes
    }finally{$stream.Dispose()}
}
function Get-HWiNFOBytesHash([byte[]]$Bytes){$sha=[Security.Cryptography.SHA256]::Create();try{return [BitConverter]::ToString($sha.ComputeHash($Bytes))}finally{$sha.Dispose()}}
function ConvertFrom-HWiNFOIniBytes([byte[]]$Bytes) {
    if($Bytes.Length -gt 262144){Fail 8 'HWiNFO INI exceeds the 256 KiB configuration limit'}
    $skip=0
    # Windows PowerShell uses ANSI here; pwsh's Encoding.Default is UTF8.
    $encoding=[Text.Encoding]::GetEncoding([Globalization.CultureInfo]::CurrentCulture.TextInfo.ANSICodePage,[Text.EncoderFallback]::ExceptionFallback,[Text.DecoderFallback]::ExceptionFallback)
    if($Bytes.Length -ge 3 -and $Bytes[0] -eq 239 -and $Bytes[1] -eq 187 -and $Bytes[2] -eq 191){$skip=3;$encoding=New-Object Text.UTF8Encoding($false,$true)}
    elseif($Bytes.Length -ge 2 -and $Bytes[0] -eq 255 -and $Bytes[1] -eq 254){$skip=2;$encoding=New-Object Text.UnicodeEncoding($false,$false,$true)}
    elseif($Bytes.Length -ge 2 -and $Bytes[0] -eq 254 -and $Bytes[1] -eq 255){$skip=2;$encoding=New-Object Text.UnicodeEncoding($true,$false,$true)}
    try{$text=$encoding.GetString($Bytes,$skip,$Bytes.Length-$skip)}catch{Fail 8 'HWiNFO INI encoding is invalid'}
    if($text.IndexOf([char]0) -ge 0){Fail 8 'HWiNFO INI contains an embedded zero'}
    $lines=@([regex]::Split($text,'\r\n|\n|\r'));if($lines.Count -gt 8192){Fail 8 'HWiNFO INI exceeds the line limit'}
    return [pscustomobject]@{Bytes=$Bytes;Text=$text;Lines=$lines;Encoding=$encoding;PreambleLength=$skip}
}
function Test-HWiNFOInteger($Value){return $Value -is [int] -or $Value -is [long]}
function Assert-HWiNFOSensorState($State) {
    if($null -eq $State -or $State -is [array] -or !($State -is [pscustomobject] -or $State -is [Collections.IDictionary]) -or $State.Exists -isnot [bool] -or !(Test-HWiNFOInteger $State.Value) -or [long]$State.Value -lt -2147483648 -or [long]$State.Value -gt 4294967295 -or (!$State.Exists -and $State.Value -ne 0)){throw 'Sensor state type or DWORD is unconfirmed'}
}
function Get-HWiNFOSensorNames {
    return @('F0000101','F0000302'|ForEach-Object{$family=$_;0..31|ForEach-Object{$family+'_'+$_}})+@('F00F5000_0')
}
function ConvertFrom-HWiNFOSensorReceipt([byte[]]$Bytes,[string]$Path) {
    if(!$Bytes.Length -or $Bytes.Length -gt 16384){throw 'HWiNFO sensor receipt size is invalid'}
    $encoding=New-Object Text.UTF8Encoding($false,$true);$text=$encoding.GetString($Bytes).TrimStart([char]0xfeff)
    Initialize-HWiNFOReceiptJson;[Ymcc.HwinfoJournalJson]::AssertObject($text)
    $record=ConvertFrom-Json -InputObject $text -ErrorAction Stop
    if($record -isnot [pscustomobject] -or !(Test-HWiNFOInteger $record.schema) -or $record.schema -ne 1 -or $record.executableSha256 -isnot [string] -or $record.executableSha256 -ine 'e4ce0c85c4288fa7e8eb12ebeece99efff4d81e0b13de53f091daabc81a3c9d3' -or $record.values -isnot [pscustomobject]){throw 'HWiNFO sensor receipt identity/schema is unconfirmed'}
    $match=[regex]::Match($Path,'(?i)^(.*)\.ymcc-sensor-groups-(original|last|cstate-original|rtss-original)\.json$');if(!$match.Success){throw 'HWiNFO sensor receipt path is unconfirmed'}
    $target=[IO.Path]::GetFullPath($match.Groups[1].Value);$kind=$match.Groups[2].Value.ToLowerInvariant()
    if($record.PSObject.Properties['owner'] -and ($record.owner -isnot [string] -or $record.owner -cne 'YMCC.HWiNFO.SensorBudget')){throw 'HWiNFO receipt owner changed'}
    if($record.PSObject.Properties['iniPath'] -and ($record.iniPath -isnot [string] -or ![IO.Path]::IsPathRooted($record.iniPath) -or [IO.Path]::GetFullPath($record.iniPath) -ine $target)){throw 'HWiNFO receipt INI identity changed'}
    if($record.PSObject.Properties['phase'] -and ($kind -ne 'last' -or $record.phase -isnot [string] -or $record.phase -cnotin @('prepared','committed','rolled-back','rollback-pending'))){throw 'HWiNFO receipt phase is unconfirmed'}
    $names=@($record.values.PSObject.Properties.Name)
    foreach($name in $names){Assert-HWiNFOSensorBudgetGroup $name;Assert-HWiNFOSensorState $record.values.$name}
    $expected=@(Get-HWiNFOSensorNames)
    if($kind -eq 'original'){$expected=@($expected|Where-Object{$_ -clike 'F0000101_*'})}
    elseif($kind -eq 'cstate-original'){$expected=@($expected|Where-Object{$_ -clike 'F0000302_*'})}
    elseif($kind -eq 'rtss-original'){$expected=@('F00F5000_0')}
    else {
        # Legacy last snapshots can precede either optional-family introduction.
        $cpu=@($names|Where-Object{$_ -clike 'F0000302_*'}).Count
        if($cpu -notin @(0,32)){throw 'HWiNFO legacy sensor family is incomplete'}
        $expected=@($expected|Where-Object{$_ -clike 'F0000101_*' -or ($cpu -eq 32 -and $_ -clike 'F0000302_*') -or ($_ -ceq 'F00F5000_0' -and $names -ccontains $_)})
    }
    if($names.Count -ne $expected.Count -or @($expected|Where-Object{$names -cnotcontains $_}).Count){throw 'HWiNFO sensor receipt inventory is incomplete'}
    return $record
}
function Read-HWiNFOSensorReceipt([string]$Path){if(!(Test-HWiNFOConfigFile $Path)){return $null};return ConvertFrom-HWiNFOSensorReceipt (Read-HWiNFOConfigBytes $Path 16384) $Path}
function Assert-HWiNFOBackupBytes([string]$Path,[byte[]]$Bytes){
    if($Path -match '(?i)\.ymcc-sensor-groups-(original|last|cstate-original|rtss-original)\.json$'){[void](ConvertFrom-HWiNFOSensorReceipt $Bytes $Path)}
    elseif($Path -match '(?i)\.ymcc-(original|last)\.bak$'){[void](Get-HWiNFOIniEntries (ConvertFrom-HWiNFOIniBytes $Bytes))}
    elseif($Bytes.Length -gt 262144){throw 'HWiNFO backup exceeds the fixed budget'}
}
function Assert-HWiNFOBackupSet([string]$Destination){
    foreach($suffix in @('.ymcc-original.bak','.ymcc-last.bak','.ymcc-sensor-groups-original.json','.ymcc-sensor-groups-last.json','.ymcc-sensor-groups-cstate-original.json','.ymcc-sensor-groups-rtss-original.json')){
        $path=$Destination+$suffix;if(Test-HWiNFOConfigFile $path){Assert-HWiNFOBackupBytes $path (Read-HWiNFOConfigBytes $path $(if($suffix.EndsWith('.json')){16384}else{262144}))}
    }
}

# A live process cannot establish the missing half of persistent ownership.
# The healthy path is inspection only; explicit stopped recovery may reconstruct
# a missing first-family record from a validated saved-last inventory.
function Test-HWiNFOSensorReceiptPair([string]$Destination) {
    $receipts=@{}
    foreach($family in @('original','cstate-original','rtss-original','last')) {
        $receipt=Read-HWiNFOSensorReceipt ($Destination+'.ymcc-sensor-groups-'+$family+'.json')
        if($receipt){$receipts[$family]=$receipt}
    }
    if(!$receipts.ContainsKey('last')){return $receipts.Count -eq 0}
    $last=$receipts['last']
    if(!$last.PSObject.Properties['phase'] -or $last.phase -cin @('prepared','rollback-pending')){return $false}
    foreach($pair in @(@('original','F0000101_0'),@('cstate-original','F0000302_0'),@('rtss-original','F00F5000_0'))) {
        if($last.values.PSObject.Properties[$pair[1]] -and !$receipts.ContainsKey($pair[0])){return $false}
    }
    return $true
}

function Read-HWiNFOIni([string]$Path) { return ConvertFrom-HWiNFOIniBytes (Read-HWiNFOConfigBytes $Path) }

function Get-HWiNFOIniEntries($Ini) {
    $entries = New-Object Collections.ArrayList
    $seen = @{}; $sections = @{}; $section = ''
    for ($i = 0; $i -lt $Ini.Lines.Count; $i++) {
        $line = $Ini.Lines[$i]
        if([string]::IsNullOrWhiteSpace($line) -or $line -match '^\s*[;#]'){continue}
        if ($line -match '^\s*\[([^\]]+)\]\s*(?:[;#].*)?$') {
            $section = $Matches[1].Trim()
            if(!$section){Fail 8 'HWiNFO INI section is empty'}
            if ($sections.ContainsKey($section)) { Fail 8 'HWiNFO INI contains a repeated section' }
            $sections[$section] = $true
            continue
        }
        if ($line -match '^\s*([^;#=][^=]*)=(.*)$') {
            if (-not $section) { Fail 8 'HWiNFO INI contains a value outside a section' }
            $key = $Matches[1].Trim(); $value = $Matches[2]
            if(!$key){Fail 8 'HWiNFO INI key is empty'}
            $identity = $section + [char]31 + $key
            if ($seen.ContainsKey($identity)) { Fail 8 'HWiNFO INI contains a duplicate section/key pair' }
            $seen[$identity] = $true
            [void]$entries.Add([pscustomobject]@{ Section = $section; Key = $key; Value = $value; Index = $i; Identity = $identity })
            continue
        }
        Fail 8 'HWiNFO INI contains malformed syntax'
    }
    if(!$entries.Count){Fail 8 'HWiNFO INI has no confirmed values'}
    return ,$entries
}

function Initialize-HWiNFORootAcquisitionJson {
    if ('Ymcc.HwinfoRootAcquisitionJson' -as [type]) { return }
    # Dedicated read-only parser: no ConvertFrom-Json coercion, duplicate loss,
    # schema downgrade, write authorization or reuse of sensor-journal contracts.
    Add-Type -TypeDefinition @"
using System;
using System.IO;
using System.Text;
using System.Globalization;
using System.Collections;
using System.Collections.Generic;
namespace Ymcc {
 public sealed class HwinfoRootAcquisitionRead {
  public Hashtable Choices = new Hashtable { {"PresentMon",1}, {"NvClockMeasuring",0}, {"USBC_PD",0} };
  public string State = "default:absent";
 }
 public static class HwinfoRootAcquisitionJson {
  sealed class Fault : Exception { internal string Kind; internal Fault(string kind) { Kind=kind; } }
  sealed class Parser {
   readonly string text; int at;
   internal Parser(string value) { text=value; }
   void Space() { while(at<text.Length && (text[at]==' '||text[at]=='\t'||text[at]=='\r'||text[at]=='\n')) ++at; }
   bool Take(char c) { Space(); if(at<text.Length && text[at]==c) { ++at; return true; } return false; }
   void Need(char c) { if(!Take(c)) throw new FormatException("JSON token missing"); }
   char Unit() { if(at+4>text.Length) throw new FormatException("JSON escape truncated"); ushort n; if(!ushort.TryParse(text.Substring(at,4),NumberStyles.AllowHexSpecifier,CultureInfo.InvariantCulture,out n)) throw new FormatException("JSON escape invalid"); at+=4; return (char)n; }
   string String() {
    Space(); if(at>=text.Length||text[at++]!='"') throw new FormatException("JSON string missing"); var result=new StringBuilder();
    while(at<text.Length) {
     char c=text[at++]; if(c=='"') return result.ToString(); if(c<32) throw new FormatException("JSON control character");
     bool escaped=false;
     if(c=='\\') {
      if(at>=text.Length) throw new FormatException("JSON escape truncated"); c=text[at++];
      switch(c) {
       case '"': case '\\': case '/': break;
       case 'b': c='\b'; break; case 'f': c='\f'; break; case 'n': c='\n'; break; case 'r': c='\r'; break; case 't': c='\t'; break;
       case 'u': c=Unit(); escaped=true; break;
       default: throw new FormatException("JSON escape invalid");
      }
     }
     if(char.IsHighSurrogate(c)) {
      char low;
      if(escaped) { if(at+2>text.Length||text[at++]!='\\'||text[at++]!='u') throw new FormatException("JSON surrogate missing"); low=Unit(); }
      else { if(at>=text.Length) throw new FormatException("JSON surrogate missing"); low=text[at++]; }
      if(!char.IsLowSurrogate(low)) throw new FormatException("JSON surrogate invalid"); result.Append(c); result.Append(low);
     } else { if(char.IsLowSurrogate(c)) throw new FormatException("JSON surrogate invalid"); result.Append(c); }
    }
    throw new FormatException("JSON string truncated");
   }
   object Number() {
    int begin=at; if(text[at]=='-') ++at;
    if(at>=text.Length) throw new FormatException("JSON number truncated");
    if(text[at]=='0') ++at; else { if(text[at]<'1'||text[at]>'9') throw new FormatException("JSON number invalid"); while(at<text.Length&&text[at]>='0'&&text[at]<='9') ++at; }
    bool integral=true;
    if(at<text.Length&&text[at]=='.') { integral=false; ++at; int digits=at; while(at<text.Length&&text[at]>='0'&&text[at]<='9') ++at; if(digits==at) throw new FormatException("JSON fraction invalid"); }
    if(at<text.Length&&(text[at]=='e'||text[at]=='E')) { integral=false; ++at; if(at<text.Length&&(text[at]=='+'||text[at]=='-')) ++at; int digits=at; while(at<text.Length&&text[at]>='0'&&text[at]<='9') ++at; if(digits==at) throw new FormatException("JSON exponent invalid"); }
    string token=text.Substring(begin,at-begin); long integer; if(integral&&long.TryParse(token,NumberStyles.Integer,CultureInfo.InvariantCulture,out integer)) return integer;
    double number; if(!double.TryParse(token,NumberStyles.Float,CultureInfo.InvariantCulture,out number)||double.IsNaN(number)||double.IsInfinity(number)) throw new FormatException("JSON number not finite"); return number;
   }
   object Value(int depth) {
    Space(); if(at>=text.Length) throw new FormatException("JSON value missing"); char c=text[at];
    if(c=='{'||c=='[') { if(depth>64) throw new FormatException("JSON depth exceeded"); }
    if(c=='{') {
     ++at; var value=new Dictionary<string,object>(StringComparer.Ordinal); if(Take('}')) return value;
     do { string key=String(); Need(':'); if (value.ContainsKey(key)) throw new FormatException("Duplicate JSON field"); value[key]=Value(depth+1); if(Take('}')) return value; Need(','); } while(true);
    }
    if(c=='[') { ++at; var values=new List<object>(); if(Take(']')) return values; do { values.Add(Value(depth+1)); if(Take(']')) return values; Need(','); } while(true); }
    if(c=='"') return String();
    foreach(string token in new [] {"true","false","null"}) { if(at+token.Length<=text.Length && string.CompareOrdinal(text,at,token,0,token.Length)==0) { at+=token.Length; if(token=="null") return null; return token=="true"; } }
    if(c=='-'||(c>='0'&&c<='9')) return Number(); throw new FormatException("JSON value invalid");
   }
   internal Dictionary<string,object> Root() { var value=Value(0) as Dictionary<string,object>; Space(); if(value==null||at!=text.Length) throw new FormatException("JSON root or tail invalid"); return value; }
  }
  static Dictionary<string,object> Object(object value) { var result=value as Dictionary<string,object>; if(result==null) throw new Fault("corrupt"); return result; }
  static bool Integer(object value) { if(!(value is long||value is double)) return false; double n=Convert.ToDouble(value,CultureInfo.InvariantCulture); return !double.IsNaN(n)&&!double.IsInfinity(n)&&Math.Floor(n)==n&&Math.Abs(n)<=9007199254740991d; }
  static void Version(object value,int minimum,int maximum) { if(!Integer(value)) throw new Fault("corrupt"); double n=Convert.ToDouble(value,CultureInfo.InvariantCulture); if(n>maximum) throw new Fault("future"); if(n<minimum) throw new Fault("corrupt"); }
  public static HwinfoRootAcquisitionRead Load(string path) {
   var result=new HwinfoRootAcquisitionRead(); byte[] bytes;
   try {
    for(string parent=Path.GetDirectoryName(Path.GetFullPath(path));!string.IsNullOrEmpty(parent);parent=Path.GetDirectoryName(parent)) { FileAttributes attributes=File.GetAttributes(parent); if((attributes&FileAttributes.Directory)==0||(attributes&FileAttributes.ReparsePoint)!=0) throw new IOException("Root parent is not an ordinary directory"); }
    if((File.GetAttributes(path)&(FileAttributes.Directory|FileAttributes.ReparsePoint))!=0) throw new IOException("Root document not regular");
    using(var file=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read)) {
     if(file.Length>4194304) { result.State="default:oversize"; return result; }
     bytes=new byte[(int)file.Length]; int offset=0; while(offset<bytes.Length) { int count=file.Read(bytes,offset,bytes.Length-offset); if(count<=0) throw new IOException("Root document changed"); offset+=count; }
     if(file.ReadByte()!=-1) throw new IOException("Root document grew");
    }
   } catch(FileNotFoundException) { result.State="default:missing"; return result; }
     catch(DirectoryNotFoundException) { result.State="default:missing"; return result; }
     catch { result.State="default:read-fault"; return result; }
   try {
    int skip=bytes.Length>=3&&bytes[0]==239&&bytes[1]==187&&bytes[2]==191?3:0;
    string text=new UTF8Encoding(false,true).GetString(bytes,skip,bytes.Length-skip); var document=new Parser(text).Root(); object value;
    if(document.TryGetValue("schemaVersion",out value)) Version(value,0,1);
    if(document.TryGetValue("configurationBaseline",out value)) {
     var stamp=Object(value); object id,revision,imported;
     if(!stamp.TryGetValue("id",out id)||!(id is string)||!stamp.TryGetValue("revision",out revision)||!stamp.TryGetValue("legacyImported",out imported)||!(imported is bool)||!(bool)imported) throw new Fault("corrupt");
     if((string)id!="ymcc-settings") throw new Fault("future"); Version(revision,1,1);
    }
    if(!document.TryGetValue("rtss",out value)||value==null) return result;
    var rtss=Object(value); if(!rtss.TryGetValue("monitorAcquisition",out value)||value==null) return result;
    var policy=Object(value); object version,enabled,polling;
    if(!policy.TryGetValue("version",out version)) throw new Fault("corrupt"); Version(version,1,1);
    if(!(version is long)||!policy.TryGetValue("enabled",out enabled)||!(enabled is bool)) throw new Fault("corrupt");
    bool hasPolling=policy.TryGetValue("snapshotPolling",out polling); if(hasPolling&&!(polling is bool)) throw new Fault("corrupt");
    if(!(bool)enabled) { result.State="default:disabled"; return result; }
    if(!hasPolling) throw new Fault("corrupt");
    result.Choices["SnapshotPolling"]=(bool)polling?1:0; result.State="configured"; return result;
   } catch(DecoderFallbackException) { result.State="default:encoding"; }
     catch(Fault error) { result.State="default:"+error.Kind; }
     catch { result.State="default:corrupt"; }
   return result;
  }
 }
}
"@
}

function Get-HWiNFOAcquisitionOverrides([ref]$ReadState) {
    # The script is deployed inside the authoritative PowerControl root. This
    # read never grants permission to repair/overwrite settings or sensor debt.
    $defaults = @{ PresentMon = 1; NvClockMeasuring = 0; USBC_PD = 0 }
    try {
        Initialize-HWiNFORootAcquisitionJson
        $result = [Ymcc.HwinfoRootAcquisitionJson]::Load((Join-Path $PSScriptRoot 'yeman-settings.json'))
        if ($null -ne $ReadState) { $ReadState.Value = $result.State }
        return $result.Choices
    } catch {
        # Even an unavailable parser must leave the preparation chain usable.
        if ($null -ne $ReadState) { $ReadState.Value = 'default:read-fault' }
        return $defaults
    }
}

function Get-HWiNFOManagedIni {
    # Baseline supplied by the user: YeMan/HWiNFO64.INI (2026-10-16).
    # Historical Average is unused by our consumers. Use the default incremental
    # average, preserving current values and Presented FPS/Low/frame-time metrics.
    # Keep background/no-dialog settings in code so recovery does not depend
    # on a mutable external template. This enables sharing; it does not alter
    # HWiNFO licensing or guarantee an unlimited shared-memory lifetime.
    $text = @'
[Settings]
SensorsOnly=1
NegativeTempsOK=1
MinimalizeSensors=1
MinimalizeSensorsClose=1
AutoUpdate=0
ShowFans0RPM=1
MinimalizeMainWnd=1
ShowWelcomeAndProgress=0
PreferAMDADL=1
Theme=1
SensorsFontFace=Segoe UI
SensorsFontHeight=17
SensorsSM=1
WakeGPUs=0
AC97CodecID=0
iMEsupport=0
SVID_VR=0
HighestIdeAddress=-1
CSMI_SAS_Support=0
SWSMI=0
UseHPET=0
BusClkPolling=0
LPC=0
EC=0
SMART=0
ATAStats=0
SupportCorsairAsetek=0
DebugDirect=0
SMBus=0
GPUI2C=0
EcScanRate=5000
SmartScanRate=5000
SensorInterval=3000
SensorAveragingInterval=0
'@
    $text = ([regex]::Replace($text, '\r\n|\n|\r', "`r`n")) + "`r`n"
    $choices = Get-HWiNFOAcquisitionOverrides
    # Extra NVIDIA measured clocks are not consumed by YMCC or its RTSS
    # templates. Disable that producer work, retain the ordinary GPU clock path.
    # USB-C port telemetry is unused by all current consumers. The battery
    # and Windows power-source paths remain independent of this port observer.
    foreach ($key in @('PresentMon', 'NvClockMeasuring', 'USBC_PD', 'SnapshotPolling')) {
        if ($choices.ContainsKey($key)) { $text += $key + '=' + $choices[$key] + "`r`n" }
    }
    $encoding = New-Object Text.UTF8Encoding($false, $true)
    $bytes = $encoding.GetBytes($text)
    return [pscustomobject]@{ Bytes = $bytes; Text = $text; Lines = @([regex]::Split($text, '\r\n|\n|\r')); Encoding = $encoding; PreambleLength = 0 }
}

function Get-HWiNFOConfigPlan([string]$Source, [string]$Destination, [int]$RequestedIntervalMs = 0, [switch]$RepairInvalid) {
    if ($RequestedIntervalMs -notin @(0, 1000, 1500, 2000, 3000)) { Fail 8 'Unsupported HWiNFO polling interval' }
    $sourceIni = if ($Source) { Read-HWiNFOIni $Source } else { Get-HWiNFOManagedIni }
    $sourceEntries = Get-HWiNFOIniEntries $sourceIni
    $destinationIni = $sourceIni
    $currentBytes=$null;$corrupt=$false;$exists=Test-HWiNFOConfigFile $Destination
    if($exists){
        # IO and over-budget reads stay failures, even during explicit repair.
        $currentBytes=Read-HWiNFOConfigBytes $Destination
        try{$destinationIni=ConvertFrom-HWiNFOIniBytes $currentBytes;[void](Get-HWiNFOIniEntries $destinationIni)}
        catch{
            if(!$RepairInvalid -or $_.Exception.Data['HWiNFOExitCode'] -ne 8){throw}
            $corrupt=$true;$destinationIni=$sourceIni
        }
    }
    Assert-HWiNFOBackupSet $Destination
    $destinationEntries = Get-HWiNFOIniEntries $destinationIni
    $lines = New-Object 'Collections.Generic.List[string]'
    foreach ($line in $destinationIni.Lines) { $lines.Add($line) }
    $positions = @{}; $existingValues = @{}
    foreach ($entry in $destinationEntries) { $positions[$entry.Identity] = $entry.Index; $existingValues[$entry.Identity] = $entry.Value }
    # The managed template keeps authority over its keys, except an existing
    # valid period. Other user sections/values and the encoding are retained.
    $periodSeen = $false
    $missing = New-Object Collections.ArrayList
    foreach ($entry in $sourceEntries) {
        $value = $entry.Value
        if ($entry.Section -ieq 'Settings' -and $entry.Key -ieq 'SensorInterval') {
            $periodSeen = $true
            if ($RequestedIntervalMs -gt 0) { $value = [string]$RequestedIntervalMs }
            elseif ($positions.ContainsKey($entry.Identity)) {
                $period = 0
                if ([int]::TryParse($existingValues[$entry.Identity].Trim(), [ref]$period) -and $period -ge 1000 -and $period -le 3000) { $value = $existingValues[$entry.Identity] }
            }
        }
        if ($entry.Section -ieq 'Settings' -and $entry.Key -ieq 'SensorInterval') {
            $effectivePeriod = 0
            if (-not [int]::TryParse($value.Trim(), [ref]$effectivePeriod) -or $effectivePeriod -lt 1000 -or $effectivePeriod -gt 3000) { Fail 8 'HWiNFO period is outside the 1-3 second monitor freshness budget' }
        }
        $replacement = $entry.Key + '=' + $value
        if ($positions.ContainsKey($entry.Identity)) { $lines[$positions[$entry.Identity]] = $replacement }
        else { [void]$missing.Add([pscustomobject]@{ Section = $entry.Section; Line = $replacement }) }
    }
    if (-not $periodSeen -and $RequestedIntervalMs -eq 0) {
        $identity = 'Settings' + [char]31 + 'SensorInterval'
        if ($existingValues.ContainsKey($identity)) {
            $effectivePeriod = 0
            if (-not [int]::TryParse($existingValues[$identity].Trim(), [ref]$effectivePeriod) -or $effectivePeriod -lt 1000 -or $effectivePeriod -gt 3000) { Fail 8 'Unmanaged HWiNFO period exceeds the monitor freshness budget' }
        }
    }
    if ($RequestedIntervalMs -gt 0 -and -not $periodSeen) {
        $identity = 'Settings' + [char]31 + 'SensorInterval'
        if ($positions.ContainsKey($identity)) { $lines[$positions[$identity]] = 'SensorInterval=' + $RequestedIntervalMs }
        else { [void]$missing.Add([pscustomobject]@{ Section = 'Settings'; Line = 'SensorInterval=' + $RequestedIntervalMs }) }
    }
    # Index once so merge work is linear in the bounded lines/entries.
    $missingSections = @{}; $missingOrder = New-Object Collections.ArrayList
    foreach ($entry in $missing) {
        if (-not $missingSections.ContainsKey($entry.Section)) {
            $missingSections[$entry.Section] = New-Object 'Collections.Generic.List[string]'
            [void]$missingOrder.Add($entry.Section)
        }
        $missingSections[$entry.Section].Add($entry.Line)
    }
    $merged = New-Object 'Collections.Generic.List[string]'
    $finished = @{}; $currentSection = ''
    foreach ($line in $lines) {
        if ($line -match '^\s*\[([^\]]+)\]\s*(?:[;#].*)?$') {
            if ($currentSection) {
                if ($missingSections.ContainsKey($currentSection)) { foreach ($pending in $missingSections[$currentSection]) { $merged.Add($pending) } }
                $finished[$currentSection] = $true
            }
            $currentSection = $Matches[1].Trim()
        }
        $merged.Add($line)
    }
    if ($currentSection) {
        if ($missingSections.ContainsKey($currentSection)) { foreach ($pending in $missingSections[$currentSection]) { $merged.Add($pending) } }
        $finished[$currentSection] = $true
    }
    foreach ($section in $missingOrder) {
        if (-not $finished.ContainsKey($section)) {
            $merged.Add('[' + $section + ']')
            foreach ($pending in $missingSections[$section]) { $merged.Add($pending) }
        }
    }
    if ($merged.Count -gt 8192) { Fail 8 'Merged HWiNFO INI exceeds the line limit' }
    $cr = [string][char]13; $lf = [string][char]10
    $newline = if ($destinationIni.Text.Contains($cr + $lf)) { $cr + $lf } elseif ($destinationIni.Text.Contains($cr)) { $cr } else { $lf }
    $text = [string]::Join($newline, $merged)
    $body = $destinationIni.Encoding.GetBytes($text)
    $bytes = New-Object byte[] ($destinationIni.PreambleLength + $body.Length)
    if ($destinationIni.PreambleLength) { [Array]::Copy($destinationIni.Bytes, 0, $bytes, 0, $destinationIni.PreambleLength) }
    [Array]::Copy($body, 0, $bytes, $destinationIni.PreambleLength, $body.Length)
    if ($bytes.Length -gt 262144) { Fail 8 'Merged HWiNFO INI exceeds the configuration limit' }
    return [pscustomobject]@{ Bytes = $bytes; RequestedIntervalMs = $RequestedIntervalMs; CurrentBytes=$currentBytes; Corrupt=$corrupt; DestinationExists=$exists }
}

function Write-HWiNFOBackup([string]$Path, [byte[]]$Bytes, [switch]$KeepExisting) {
    Assert-HWiNFOBackupBytes $Path $Bytes
    $existed=Test-HWiNFOConfigFile $Path;$oldHash=$null
    if($existed){
        $old=Read-HWiNFOConfigBytes $Path $(if($Path.EndsWith('.json')){16384}else{262144})
        Assert-HWiNFOBackupBytes $Path $old;$oldHash=Get-HWiNFOBytesHash $old
        if($KeepExisting){
            if($Path -match '(?i)\.ymcc-corrupt-[a-f0-9]{64}\.bak$' -and $oldHash -cne (Get-HWiNFOBytesHash $Bytes)){throw 'HWiNFO corrupt-original archive collision'}
            return
        }
        if($Path -match '(?i)\.ymcc-sensor-groups-last\.json$'){
            $previous=ConvertFrom-HWiNFOSensorReceipt $old $Path;$next=ConvertFrom-HWiNFOSensorReceipt $Bytes $Path
            if(!$previous.PSObject.Properties['phase'] -or $previous.phase -cin @('prepared','rollback-pending')){
                $names=@($previous.values.PSObject.Properties.Name)
                if($names.Count -ne @($next.values.PSObject.Properties.Name).Count){throw 'HWiNFO pending sensor debt inventory cannot be reset'}
                foreach($name in $names){if(!$next.values.PSObject.Properties[$name] -or !(Test-HWiNFOSensorBudgetState $previous.values.$name $next.values.$name)){throw 'HWiNFO pending before-state cannot be reset'}}
            }
        }
    }
    $temporary=$Path+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
    try{
        $stream=[IO.File]::Open($temporary,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try{$stream.Write($Bytes,0,$Bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
        $expected=Get-HWiNFOBytesHash $Bytes
        if((Get-HWiNFOConfigHash $temporary) -cne $expected){throw 'HWiNFO staged backup verification failed'}
        $stillExists=Test-HWiNFOConfigFile $Path
        if($stillExists -ne $existed -or ($existed -and (Get-HWiNFOConfigHash $Path) -cne $oldHash)){throw 'HWiNFO backup changed before commit'}
        if($existed){[IO.File]::Replace($temporary,$Path,[NullString]::Value)}else{[IO.File]::Move($temporary,$Path)}
        if((Get-HWiNFOConfigHash $Path) -cne $expected){throw 'HWiNFO backup commit is unconfirmed'}
    }finally{if([IO.File]::Exists($temporary)){[IO.File]::Delete($temporary)}}
}

function Copy-HWiNFOConfig([string]$Source, [string]$Destination, [switch]$RepairInvalid) {
    if($Source -and !(Test-HWiNFOConfigFile $Source)){Fail 3 'The explicit HWiNFO template was not found'}
    if(@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count){Fail 4 'HWiNFO64.exe is running; refusing to overwrite its INI'}
    $plan=Get-HWiNFOConfigPlan -Source $Source -Destination $Destination -RequestedIntervalMs $SensorIntervalMs -RepairInvalid:$RepairInvalid
    $stage=$Destination+'.ymcc-stage-'+[guid]::NewGuid().ToString('N');$candidate=$stage+'.commit'
    $expectedPrevious=if($plan.DestinationExists){Get-HWiNFOBytesHash $plan.CurrentBytes}else{$null}
    try{
        $stream=[IO.File]::Open($stage,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try{$stream.Write($plan.Bytes,0,$plan.Bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
        $expectedHash=Get-HWiNFOBytesHash $plan.Bytes
        if((Get-HWiNFOConfigHash $stage) -cne $expectedHash){Fail 5 'HWiNFO INI staging failed'}
        if($plan.Corrupt){
            $archive=$Destination+'.ymcc-corrupt-'+$expectedPrevious.Replace('-','').ToLowerInvariant()+'.bak'
            Write-HWiNFOBackup $archive $plan.CurrentBytes -KeepExisting
        }elseif($plan.DestinationExists){
            Write-HWiNFOBackup ($Destination+'.ymcc-original.bak') $plan.CurrentBytes -KeepExisting
            Write-HWiNFOBackup ($Destination+'.ymcc-last.bak') $plan.CurrentBytes
        }
        for($attempt=0;$attempt -lt 5;$attempt++){
            if(@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count){Fail 4 'HWiNFO64.exe appeared before commit'}
            try{
                $currentExists=Test-HWiNFOConfigFile $Destination
                if($currentExists -ne $plan.DestinationExists -or ($currentExists -and (Get-HWiNFOConfigHash $Destination) -cne $expectedPrevious)){Fail 5 'HWiNFO INI changed externally; preserved instead of rollback'}
                Copy-Item -LiteralPath $stage -Destination $candidate -Force -ErrorAction Stop
                if((Get-HWiNFOConfigHash $candidate) -cne $expectedHash){throw 'HWiNFO INI copy verification failed'}
                if(@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count){Fail 4 'HWiNFO64.exe appeared before publish'}
                $currentExists=Test-HWiNFOConfigFile $Destination
                if($currentExists -ne $plan.DestinationExists -or ($currentExists -and (Get-HWiNFOConfigHash $Destination) -cne $expectedPrevious)){Fail 5 'HWiNFO INI changed before publish; foreign bytes retained'}
                if($currentExists){[IO.File]::Replace($candidate,$Destination,[NullString]::Value)}else{[IO.File]::Move($candidate,$Destination)}
                if((Get-HWiNFOConfigHash $Destination) -cne $expectedHash){Fail 5 'HWiNFO INI publish unconfirmed; backups retained'}
                return
            }catch{
                if($_.Exception.Data.Contains('HWiNFOExitCode')){throw}
                if(Test-HWiNFOAccessDenied $_){Fail 5 'HWiNFO INI preparation needs administrator permission' -NeedsElevation}
                if($attempt -eq 4){Fail 5 ('HWiNFO INI copy/verification failed: '+$_.Exception.Message)}
                Start-Sleep -Milliseconds 200
            }
        }
    }catch{
        if($_.Exception.Data.Contains('HWiNFOExitCode')){throw}
        if(Test-HWiNFOAccessDenied $_){Fail 5 'HWiNFO configuration preparation needs administrator permission' -NeedsElevation}
        throw
    }finally{foreach($file in @($candidate,$stage)){if([IO.File]::Exists($file)){[IO.File]::Delete($file)}}}
}

function Test-HWiNFOSensorBudgetImage([string]$Executable) {
    # Registry layout and disk/C-state/duplicate-RTSS group gates are verified
    # for this upstream image. Unknown builds keep the ordinary acquisition.
    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($Executable)
    if ($info.FileMajorPart -ne 8 -or $info.FileMinorPart -ne 54) { return $false }
    $file = Get-Item -LiteralPath $Executable -ErrorAction Stop
    if ($file.Length -le 0 -or $file.Length -gt 67108864) { return $false }
    $hash = (Get-HWiNFOConfigHash $Executable).Replace('-', '').ToLowerInvariant()
    return $hash -ceq 'e4ce0c85c4288fa7e8eb12ebeece99efff4d81e0b13de53f091daabc81a3c9d3'
}

function Assert-HWiNFOSensorBudgetGroup([string]$Group) {
    # Never accept a registry path, sensor-table index or a display label here.
    if ($Group -cnotmatch '^(?:F000(?:0101|0302)_(?:[0-9]|[12][0-9]|3[01])|F00F5000_0)$') { throw 'Unsupported unused-sensor group identity' }
}

function Read-HWiNFOSensorBudgetState([string]$Group) {
    Assert-HWiNFOSensorBudgetGroup $Group
    $baseKey = $null; $key = $null
    try {
        $baseKey = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser, [Microsoft.Win32.RegistryView]::Registry64)
        $key = $baseKey.OpenSubKey(('Software\HWiNFO64\Sensors\' + $Group), $false)
        if ($null -eq $key) { return [pscustomobject]@{ Exists = $false; Value = 0 } }
        $missing = [object]::new()
        $value = $key.GetValue('Enabled', $missing, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        if ([object]::ReferenceEquals($value, $missing)) { return [pscustomobject]@{ Exists = $false; Value = 0 } }
        if ($key.GetValueKind('Enabled') -ne [Microsoft.Win32.RegistryValueKind]::DWord) { throw 'Unexpected sensor Enabled registry type' }
        return [pscustomobject]@{ Exists = $true; Value = [int]$value }
    } finally { if ($key) { $key.Dispose() }; if ($baseKey) { $baseKey.Dispose() } }
}

function Write-HWiNFOSensorBudgetState([string]$Group, $State) {
    Assert-HWiNFOSensorBudgetGroup $Group
    Assert-HWiNFOSensorState $State
    $baseKey = $null; $key = $null
    try {
        $baseKey = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser, [Microsoft.Win32.RegistryView]::Registry64)
        $path = 'Software\HWiNFO64\Sensors\' + $Group
        if ($State.Exists) {
            $key = $baseKey.CreateSubKey($path)
            $raw=if([long]$State.Value -gt 2147483647){[BitConverter]::ToInt32([BitConverter]::GetBytes([uint32]$State.Value),0)}else{[int]$State.Value}
            $key.SetValue('Enabled', $raw, [Microsoft.Win32.RegistryValueKind]::DWord)
        } else {
            $key = $baseKey.OpenSubKey($path, $true)
            if ($key) { $key.DeleteValue('Enabled', $false) }
            # Do not remove keys: an empty new key is harmless, and another
            # application may concurrently add labels/layout/child keys.
        }
    } finally { if ($key) { $key.Dispose() }; if ($baseKey) { $baseKey.Dispose() } }
}

function Test-HWiNFOSensorBudgetState($Left, $Right) {
    Assert-HWiNFOSensorState $Left;Assert-HWiNFOSensorState $Right
    return $Left.Exists -eq $Right.Exists -and (!$Left.Exists -or (([long]$Left.Value -band [long]4294967295) -eq ([long]$Right.Value -band [long]4294967295)))
}
function Write-HWiNFOSensorPhase([string]$Path,$Receipt,[string]$Destination,[string]$Phase){
    $Receipt|Add-Member -NotePropertyName phase -NotePropertyValue $Phase -Force
    $Receipt|Add-Member -NotePropertyName owner -NotePropertyValue 'YMCC.HWiNFO.SensorBudget' -Force
    $Receipt|Add-Member -NotePropertyName iniPath -NotePropertyValue ([IO.Path]::GetFullPath($Destination)) -Force
    $encoding=New-Object Text.UTF8Encoding($false,$true)
    Write-HWiNFOBackup $Path ($encoding.GetBytes(($Receipt|ConvertTo-Json -Depth 64 -Compress)))
}
function Complete-HWiNFOPendingSensorReceipt([string]$Path,$Receipt,[string]$Destination){
    $disabled=[pscustomobject]@{Exists=$true;Value=0}
    $names=@($Receipt.values.PSObject.Properties.Name)
    # Preflight the entire inventory before restoring any group. A foreign choice
    # cannot be overwritten merely because an earlier group was our disabled state.
    foreach($name in $names){
        if(@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count){throw 'Producer running; prior sensor debt remains pending'}
        $current=Read-HWiNFOSensorBudgetState $name
        if(!(Test-HWiNFOSensorBudgetState $current $Receipt.values.$name) -and !(Test-HWiNFOSensorBudgetState $current $disabled)){throw 'Foreign sensor choice; prior debt kept unchanged'}
    }
    foreach($name in $names){
        if(@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count){throw 'Producer appeared during pending sensor recovery'}
        $current=Read-HWiNFOSensorBudgetState $name
        if(Test-HWiNFOSensorBudgetState $current $Receipt.values.$name){continue}
        if(!(Test-HWiNFOSensorBudgetState $current $disabled)){throw 'Concurrent sensor choice remains untouched'}
        Write-HWiNFOSensorBudgetState $name $Receipt.values.$name
        if(!(Test-HWiNFOSensorBudgetState (Read-HWiNFOSensorBudgetState $name) $Receipt.values.$name)){throw 'Prior sensor restoration unconfirmed'}
    }
    Write-HWiNFOSensorPhase $Path $Receipt $Destination 'rolled-back'
}
function New-HWiNFOSensorReceipt($Before,$Previous,[string]$Destination){
    $record=if($Previous){$Previous|ConvertTo-Json -Depth 64 -Compress|ConvertFrom-Json}else{[pscustomobject]@{schema=1;executableSha256='e4ce0c85c4288fa7e8eb12ebeece99efff4d81e0b13de53f091daabc81a3c9d3'}}
    $values=[ordered]@{}
    foreach($name in $Before.Keys){
        $node=[pscustomobject]@{Exists=$Before[$name].Exists;Value=$Before[$name].Value}
        if($Previous -and $Previous.values.PSObject.Properties[$name]){foreach($property in $Previous.values.$name.PSObject.Properties){if($property.Name -notin @('Exists','Value')){$node|Add-Member -NotePropertyName $property.Name -NotePropertyValue $property.Value}}}
        $values[$name]=$node
    }
    $record|Add-Member -NotePropertyName values -NotePropertyValue ([pscustomobject]$values) -Force
    return $record
}
function Invoke-HWiNFOSensorBudget([string]$Executable, [string]$Destination) {
    $changed=New-Object Collections.ArrayList;$before=@{};$record=$null;$transactionStarted=$false
    $disabled=[pscustomobject]@{Exists=$true;Value=0};$lastPath=$Destination+'.ymcc-sensor-groups-last.json'
    try {
        if(!(Test-HWiNFOSensorBudgetImage $Executable)){return 'unsupported-image'}
        Assert-HWiNFOBackupSet $Destination
        if(@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count){throw 'Producer running; sensor journal not changed'}
        foreach($entry in (Get-HWiNFOIniEntries (Read-HWiNFOIni $Destination))){if($entry.Section -ieq 'Settings' -and $entry.Key -ieq 'RememberPreferences'){$remember=0;if(![int]::TryParse($entry.Value.Trim(),[ref]$remember) -or $remember -eq 0){return 'preferences-disabled'}}}
        $last=Read-HWiNFOSensorReceipt $lastPath
        if(!$last){
            foreach($family in @('original','cstate-original','rtss-original')) {
                if(Read-HWiNFOSensorReceipt ($Destination+'.ymcc-sensor-groups-'+$family+'.json')){throw 'First sensor receipt exists without saved last; original ownership remains unconfirmed'}
            }
        }
        if($last -and (!$last.PSObject.Properties['phase'] -or $last.phase -cin @('prepared','rollback-pending'))){
            Complete-HWiNFOPendingSensorReceipt $lastPath $last $Destination
            $last=Read-HWiNFOSensorReceipt $lastPath
        }
        $groups=@(Get-HWiNFOSensorNames)
        foreach($group in $groups){$value=Read-HWiNFOSensorBudgetState $group;Assert-HWiNFOSensorState $value;$before[$group]=$value}
        $pending=@($groups|Where-Object{!(Test-HWiNFOSensorBudgetState $before[$_] $disabled)})
        if(!$pending.Count -and !$last){return 'already-configured'}
        # A missing first-family file may not reset an older last snapshot's
        # recorded values to our current disabled zeros.
        $first=@{};foreach($group in $groups){$first[$group]=if($last -and $last.values.PSObject.Properties[$group]){$last.values.$group}else{$before[$group]}}
        foreach($family in @('original','cstate-original','rtss-original')){
            $values=@{};foreach($group in $groups){if(($family -eq 'original' -and $group -clike 'F0000101_*') -or ($family -eq 'cstate-original' -and $group -clike 'F0000302_*') -or ($family -eq 'rtss-original' -and $group -ceq 'F00F5000_0')){$values[$group]=$first[$group]}}
            $saved=[pscustomobject]@{schema=1;executableSha256='e4ce0c85c4288fa7e8eb12ebeece99efff4d81e0b13de53f091daabc81a3c9d3';values=$values}
            $encoding=New-Object Text.UTF8Encoding($false,$true)
            Write-HWiNFOBackup ($Destination+'.ymcc-sensor-groups-'+$family+'.json') ($encoding.GetBytes(($saved|ConvertTo-Json -Depth 64 -Compress))) -KeepExisting
        }
        # Missing first-family files were repaired from validated saved-last
        # values above. Current disabled zeros cannot replace missing evidence.
        if(!$pending.Count){return 'already-configured'}
        $record=New-HWiNFOSensorReceipt $before $last $Destination
        Write-HWiNFOSensorPhase $lastPath $record $Destination 'prepared';$transactionStarted=$true
        foreach($group in $pending){
            if(@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count){throw 'Producer appeared during sensor configuration'}
            if(!(Test-HWiNFOSensorBudgetState (Read-HWiNFOSensorBudgetState $group) $before[$group])){throw 'Sensor preference changed during configuration'}
            [void]$changed.Add($group);Write-HWiNFOSensorBudgetState $group $disabled
            if(!(Test-HWiNFOSensorBudgetState (Read-HWiNFOSensorBudgetState $group) $disabled)){throw 'Sensor Enabled verification failed'}
        }
        Write-HWiNFOSensorPhase $lastPath $record $Destination 'committed'
        return 'configured'
    }catch{
        $reason=$_.Exception.Message;$rollbackOk=$false
        if($transactionStarted){
            $rollbackOk=$true
            for($i=$changed.Count-1;$i -ge 0;$i--){$group=$changed[$i];try{
                if(@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count){throw 'Producer running; sensor rollback deferred'}
                $current=Read-HWiNFOSensorBudgetState $group
                if(Test-HWiNFOSensorBudgetState $current $before[$group]){continue}
                if(!(Test-HWiNFOSensorBudgetState $current $disabled)){throw 'Concurrent sensor preference preserved'}
                Write-HWiNFOSensorBudgetState $group $before[$group]
                if(!(Test-HWiNFOSensorBudgetState (Read-HWiNFOSensorBudgetState $group) $before[$group])){throw 'Sensor rollback verification failed'}
            }catch{$rollbackOk=$false}}
            try{Write-HWiNFOSensorPhase $lastPath $record $Destination $(if($rollbackOk){'rolled-back'}else{'rollback-pending'})}catch{$rollbackOk=$false}
        }
        [Console]::Error.WriteLine(('HWiNFO sensor preferences unconfirmed; rollback={0}: {1}' -f $rollbackOk,$reason))
        return $(if($rollbackOk){'unconfirmed'}else{'unconfirmed-rollback-deferred'})
    }
}

function Get-HWiNFOClockMs {
    return [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
}

function Test-HWiNFOSharedMemory {
    param([int]$ExpectedIntervalMs = 0, [ref]$ProducerProof)
    $strictProof = $null -ne $ProducerProof
    if ($strictProof) { $ProducerProof.Value = $null }
    $mmf = $null
    $accessor = $null
    try {
        foreach ($name in @('Global\HWiNFO_SENS_SM2', 'HWiNFO_SENS_SM2', 'Global\HWiNFO_SENS_SM', 'HWiNFO_SENS_SM')) {
            try {
                # Health reads never require write access to the producer.
                $mmf = [System.IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting(
                    $name, [System.IO.MemoryMappedFiles.MemoryMappedFileRights]::Read)
                if ($mmf) { break }
            } catch { $mmf = $null }
        }
        if (-not $mmf) { return $false }

        $accessor = $mmf.CreateViewAccessor(0, 0, [System.IO.MemoryMappedFiles.MemoryMappedFileAccess]::Read)
        if ($accessor.Capacity -lt 44) { return $false }
        if ($accessor.ReadUInt32(0) -ne 0x53695748) { return $false }
        $version = $accessor.ReadUInt32(4)
        $revision = $accessor.ReadUInt32(8)
        $sensorOffset = [uint64]$accessor.ReadUInt32(20)
        $sensorSize = $accessor.ReadUInt32(24)
        $sensorCount = $accessor.ReadUInt32(28)
        $offset = [uint64]$accessor.ReadUInt32(32)
        $size = [uint64]$accessor.ReadUInt32(36)
        $count = [uint64]$accessor.ReadUInt32(40)
        # Match the existing sole native parser's bounded layout admission.
        # Readability alone cannot confirm a source the consumer must reject.
        if ($size -lt 316 -or $size -gt 16384 -or $count -eq 0 -or $count -gt 100000 -or
            $sensorOffset -lt 44 -or $sensorOffset -gt [uint64]$accessor.Capacity) { return $false }
        $readingBytes = $size * $count
        if ($readingBytes -gt 33554432L) { return $false }
        $end = $offset + $readingBytes
        if ($offset -ge [uint64]$accessor.Capacity -or $end -gt [uint64]$accessor.Capacity) { return $false }

        $periodKnown = $version -eq 2 -and $sensorOffset -ge 48 -and $accessor.Capacity -ge 48
        $period = 0
        if ($periodKnown) { $period = $accessor.ReadUInt32(44) }
        if ($ExpectedIntervalMs -ne 0) {
            # Same verified v2 extension gate as the existing native reader.
            # An unknown layout cannot prove an explicitly requested interval.
            if ($ExpectedIntervalMs -notin @(1000, 1500, 2000, 3000)) { return $false }
            if (-not $periodKnown -or $sensorOffset -gt $offset -or $period -ne $ExpectedIntervalMs) { return $false }
        }
        if ($ExpectedIntervalMs -ne 0 -or $strictProof) {
            $pollTime = $accessor.ReadInt64(12)
            if ($pollTime -le 0 -or $pollTime -gt 9223372036854775) { return $false }
        }

        # Read the existing region; do not infer health from non-zero sensor values.
        [void]$accessor.ReadByte([long]$offset)
        if ($ExpectedIntervalMs -ne 0 -or $strictProof) {
            # A receipt owns one coherent header and one producer identity key.
            if ($accessor.ReadUInt32(0) -ne 0x53695748 -or $accessor.ReadUInt32(4) -ne $version -or
                $accessor.ReadUInt32(8) -ne $revision -or $accessor.ReadUInt32(20) -ne $sensorOffset -or
                $accessor.ReadUInt32(24) -ne $sensorSize -or $accessor.ReadUInt32(28) -ne $sensorCount -or
                $accessor.ReadUInt32(32) -ne $offset -or $accessor.ReadUInt32(36) -ne $size -or
                $accessor.ReadUInt32(40) -ne $count -or $accessor.ReadInt64(12) -ne $pollTime -or
                ($periodKnown -and $accessor.ReadUInt32(44) -ne $period)) { return $false }
        }
        if ($strictProof) {
            # Match the native producer-age budget, using the same existing header.
            $budget = 5000L
            if ($periodKnown -and $period -gt 0) { $budget = [Math]::Max(5000L, [Math]::Min(60000L, [Math]::Max(500L, [long]$period)) + 3000L) }
            $age = (Get-HWiNFOClockMs) - $pollTime * 1000L
            if ($age -lt -1000L -or $age -ge $budget) { return $false }
            $key = @($name, $version, $revision, $sensorOffset, $sensorSize, $sensorCount, $offset, $size, $count, $periodKnown, $period) -join '|'
            $ProducerProof.Value = [pscustomobject]@{ PollTime = $pollTime; Layout = $key }
        }
        return $true
    } catch {
        return $false
    } finally {
        if ($accessor) { try { $accessor.Dispose() } catch { } }
        if ($mmf) { try { $mmf.Dispose() } catch { } }
    }
}

function Test-HWiNFOHealth {
    param([int]$ExpectedIntervalMs = 0)
    $proof = $null
    return (@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count -gt 0) -and (Test-HWiNFOSharedMemory -ExpectedIntervalMs $ExpectedIntervalMs -ProducerProof ([ref]$proof))
}

function Invoke-HWiNFORecovery {
    $mutex = $null
    $owned = $false
    try {
        if($RepairInvalidConfig -and $Mode -ne 'restart'){Fail 8 'Explicit damaged-INI repair requires restart mode'}
        $created = $false
        $mutex = New-Object System.Threading.Mutex($false, 'Global\YeManCC_HWiNFO_Recovery', [ref]$created)
        # The elevated handoff may race with another monitor's recovery, but the
        # parent is no longer holding this lock while it waits for its child.
        $lockWaitMs = 0
        if ($Elevated) { $lockWaitMs = 10000 }
        try { $owned = $mutex.WaitOne($lockWaitMs) } catch [System.Threading.AbandonedMutexException] { $owned = $true }
        if (-not $owned) { return 11 }

        if ($Mode -eq 'start' -and (Test-HWiNFOHealth)) {
            # A live healthy producer is still not proof that persistent
            # configuration/rollback debt has settled. Inspect only; never stop
            # it or restore registry values from this healthy no-op path.
            $healthyExe=Resolve-HWiNFO
            if(!$healthyExe){return 16}
            try{
                $healthyDestination=Join-Path ([IO.Path]::GetDirectoryName($healthyExe)) 'HWiNFO64.INI'
                Assert-HWiNFOBackupSet $healthyDestination
                if(!(Test-HWiNFOSensorReceiptPair $healthyDestination)){return 16}
            }catch{return 16}
            if ($SensorIntervalMs -gt 0) { return 12 } # Deferred: live settings were not changed.
            # Healthy start is a no-op: HWiNFO may save its in-memory settings on
            # exit, so writing its INI while it is running is never safe.
            foreach ($process in @(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue)) {
                try { $process.ProcessorAffinity = [IntPtr]0xA0 } catch { }
            }
            return 0
        }

        $exe = Resolve-HWiNFO
        if (-not $exe) { Fail 2 'HWiNFO64.exe was not found' }
        $root = Split-Path -Parent $exe
        $dst = Join-Path $root 'HWiNFO64.INI'
        # Preflight before stopping, then re-merge after exit because HWiNFO may
        # save settings while it closes. The source is YMCC's embedded baseline.
        [void](Get-HWiNFOConfigPlan -Destination $dst -RequestedIntervalMs $SensorIntervalMs -RepairInvalid:($RepairInvalidConfig -and $Mode -eq 'restart'))

        Stop-HWiNFO
        Copy-HWiNFOConfig -Destination $dst -RepairInvalid:($RepairInvalidConfig -and $Mode -eq 'restart')
        $sensorBudget=Invoke-HWiNFOSensorBudget -Executable $exe -Destination $dst
        if (@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count -gt 0) {
            Fail 4 'HWiNFO64.exe appeared during recovery; refusing to launch another instance'
        }
        # The prepared configuration and its byte-for-byte SHA-256 verification are complete before
        # launch. Preserve the original no-switch HWiNFO invocation.
        $process = Start-Process -FilePath $exe -WorkingDirectory $root -WindowStyle Hidden -PassThru
        try { $process.ProcessorAffinity = [IntPtr]0xA0 } catch { }
        $previousProof = $null
        $sawProof = $false
        # Reuse the process handle returned by Start-Process. Polling the global
        # process list at 4 Hz is neither necessary nor an exact lifetime proof.
        for ($i = 0; $i -lt 30; $i++) {
            try { $exited = $process.HasExited }
            catch { Fail 6 'HWiNFO64.exe process lifetime could not be confirmed after launch' }
            if ($exited) { Fail 6 'HWiNFO64.exe exited before shared memory became readable' }
            $proof = $null
            if (Test-HWiNFOSharedMemory -ExpectedIntervalMs $SensorIntervalMs -ProducerProof ([ref]$proof)) {
                $sawProof = $true
                if ($null -ne $previousProof -and $proof.Layout -ceq $previousProof.Layout -and $proof.PollTime -gt $previousProof.PollTime) { if($sensorBudget -clike 'unconfirmed*'){return 16};return 0 }
                $previousProof = $proof
            } else { $previousProof = $null }
            Start-Sleep -Milliseconds 1000
        }
        if ($sawProof) { Fail 14 'HWiNFO64.exe started but continuing fresh producer updates were not confirmed within 30 seconds' }
        if ($SensorIntervalMs -gt 0) { Fail 13 'HWiNFO64.exe started but shared memory and the requested polling period were not both confirmed within 30 seconds' }
        Fail 7 'HWiNFO64.exe started but fresh shared memory was not confirmed within 30 seconds'
    } finally {
        if ($owned -and $mutex) { try { $mutex.ReleaseMutex() } catch { } }
        if ($mutex) { $mutex.Dispose() }
    }
}

function Wait-HWiNFORecoveryChild($Child) {
    if ($null -eq $Child) { Fail 15 'The elevated HWiNFO recovery process was not returned' }
    try {
        # Bind the exact returned process handle before waiting. Do not wait on
        # a process tree: HWiNFO is a deliberately long-lived descendant.
        $handle = $Child.Handle
        if ($handle -eq [IntPtr]::Zero) { Fail 15 'The elevated HWiNFO recovery process handle is unavailable' }
        if (-not $Child.WaitForExit(60000)) { Fail 15 'The elevated HWiNFO recovery process did not finish within 60 seconds; it was not terminated' }
        $code = $Child.ExitCode
        if ($null -eq $code -or ($code -isnot [int] -and $code -isnot [long])) {
            Fail 15 'The elevated HWiNFO recovery process exit code could not be confirmed'
        }
        return [int]$code
    } catch {
        if ($_.Exception.Data['HWiNFOExitCode'] -eq 15) { throw }
        Fail 15 ("The elevated HWiNFO recovery process result could not be read: {0}" -f $_.Exception.Message)
    } finally { $Child.Dispose() }
}

function Invoke-HWiNFORecoveryEntry {
    try { return (Invoke-HWiNFORecovery) }
    catch {
        $failure = $_.Exception
        if (-not $Elevated -and $failure.Data['HWiNFONeedsElevation']) {
            # Invoke-HWiNFORecovery has already run finally and released its lock.
            # Restart the full transaction, including exit confirmation. Quote the
            # script path because release/install directories can contain spaces.
            try {
                $arguments = '-NoProfile -ExecutionPolicy Bypass -File "{0}" -Mode restart -Elevated' -f $PSCommandPath
                if ($SensorIntervalMs -gt 0) { $arguments += ' -SensorIntervalMs ' + $SensorIntervalMs }
                if ($ExecutablePath) { $arguments += ' -ExecutablePath "{0}"' -f $ExecutablePath }
                if ($RepairInvalidConfig) { $arguments += ' -RepairInvalidConfig' }
                $admin = Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList $arguments -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -PassThru
                return (Wait-HWiNFORecoveryChild $admin)
            } catch {
                if ($_.Exception.Data['HWiNFOExitCode'] -eq 15) {
                    [Console]::Error.WriteLine(("HWiNFO elevated recovery was not confirmed: {0}" -f $_.Exception.Message))
                    return 15
                }
                [Console]::Error.WriteLine(("HWiNFO recovery needs administrator permission: {0}" -f $_.Exception.Message))
                return [int]$failure.Data['HWiNFOExitCode']
            }
        }
        [Console]::Error.WriteLine($failure.Message)
        if ($failure.Data.Contains('HWiNFOExitCode')) { return [int]$failure.Data['HWiNFOExitCode'] }
        return 1
    }
}

exit (Invoke-HWiNFORecoveryEntry)
