# Portable closed dispatcher. Dot-source imports functions only; no product side effects.
# No mandatory/ValidateSet binding: invalid invocation is logged before validation.
# Arguments are parsed inside the protected dispatcher so missing values also get JSON failure.

function Convert-RogInvocation([object[]]$Arguments,[string]$Kind='runner') {
    $allowed=@('Action','ConfigPath','ExePath','ExpectedSha256','TargetPid','TargetCreationTimeUtc','SessionId','AuthorizedAction','AllowElevation','StartArgument','GraceSeconds','StartWaitSeconds','WarmupSeconds','Seconds','SampleMilliseconds','InventoryMilliseconds','FanState','GamepadState','GyroState','Label','LifecycleJsonl','VirtualHandshakeJsonl','ExpectedSessionId','LogFile')
    if ($Kind -eq 'lifecycle') { $allowed=@('Action','ExePath','ExpectedSha256','TargetPid','TargetCreationTimeUtc','SessionId','AuthorizedAction','AllowElevation','StartArgument','GraceSeconds','StartWaitSeconds') }
    if ($Kind -eq 'fan') { $allowed=@('Action','ExePath','ExpectedSha256','ParentPid','ParentCreationTimeUtc','SessionId','AuthorizedAction','AllowElevation','Preset','TimeoutSeconds') }
    if ($Kind -eq 'worker') { $allowed=@('RequestPath') }
    $o=@{}
    for ($i=0;$i -lt $Arguments.Count;$i++) {
        $arg=$Arguments[$i]
        if ($arg -isnot [string] -or $arg -notmatch '^-[A-Za-z][A-Za-z0-9]*$') { throw 'UNKNOWN_OR_POSITIONAL_PARAMETER' }
        $name=$allowed | Where-Object {$_ -ieq $arg.Substring(1)} | Select-Object -First 1
        if (-not $name) { throw 'UNKNOWN_OR_POSITIONAL_PARAMETER' }
        if ($o.ContainsKey($name)) { throw 'DUPLICATE_PARAMETER' }
        if ($name -eq 'AllowElevation') { $o[$name]=$true; continue }
        if ($i+1 -ge $Arguments.Count -or ($Arguments[$i+1] -is [string] -and $Arguments[$i+1] -match '^-[A-Za-z]')) { throw 'PARAMETER_VALUE_REQUIRED' }
        if ($name -in @('StartArgument','LogFile')) {
            $values=@(); while ($i+1 -lt $Arguments.Count -and -not ($Arguments[$i+1] -is [string] -and $Arguments[$i+1] -match '^-[A-Za-z]')) { $values+=@($Arguments[++$i]) }
            $o[$name]=$values
        } else { $o[$name]=$Arguments[++$i] }
    }
    if ($Kind -eq 'runner' -and $o.ContainsKey('Action') -and $o.Action -notin @('validate','preflight','observe','capture','status','start','stop','restart')) { throw 'INVALID_ACTION' }
    if ($Kind -eq 'lifecycle') { if (-not $o.ContainsKey('Action')) { $o.Action='status' }; if ($o.Action -notin @('status','start','stop','restart')) { throw 'LIFECYCLE_ACTION_NOT_ALLOWED' } }
    if ($Kind -eq 'fan') {
        $a=if($o.ContainsKey('Action')){$o.Action}else{'status'}
        if ($a -notin @('probe','on','off','close','status')) { throw 'FAN_ACTION_NOT_ALLOWED' }; $o.Action='fan-'+$a
        if ($o.ContainsKey('ParentPid')) { $o.TargetPid=$o.ParentPid; $o.Remove('ParentPid') }
        if ($o.ContainsKey('ParentCreationTimeUtc')) { $o.TargetCreationTimeUtc=$o.ParentCreationTimeUtc; $o.Remove('ParentCreationTimeUtc') }
        if ($o.ContainsKey('AuthorizedAction')) { $o.AuthorizedAction='fan-'+$o.AuthorizedAction }
    }
    return $o
}
function Get-RogCode($ErrorRecord) {
    $text=$ErrorRecord.Exception.Message
    if ($text -cmatch '^[A-Z][A-Z0-9_]{2,100}$') { return $text }
    if ($ErrorRecord.Exception -is [UnauthorizedAccessException] -or
        $ErrorRecord.Exception -is [System.Security.SecurityException]) { return 'PERMISSION_DENIED' }
    return 'SAFE_OPERATION_FAILED'
}
function New-RogDiagnostic {
    return [ordered]@{phase='bootstrap';readOnly=$false;
        permission=[ordered]@{queryAttempted=$false;querySucceeded=$false;verifiedHigh=$null;errorCode=$null};
        elevation=[ordered]@{requested=$false;authorized=$false;attempted=$false;decision='NOT_REQUESTED';
            workerPid=$null;workerExited=$null;workerExitCode=$null;bindingVerified=$false;workerHighVerified=$null;workerErrorCode=$null;timeoutMilliseconds=$null};
        identity=[ordered]@{attempted=$false;acquired=$false;discoveryOnly=$false;state='NOT_QUERIED';
            requestedBirthState='NOT_SUPPLIED';inputFieldsPresent=@();availableFields=@();matchingCompleteProcessCount=0;unknownCandidateCount=0}}
}
function Convert-RogBirth($Value,[string]$Prefix='PROCESS_BIRTH') {
    if ($null -eq $Value -or ($Value -is [string] -and [string]::IsNullOrWhiteSpace($Value))) { throw ($Prefix+'_UNAVAILABLE') }
    if ($Value -isnot [string] -and $Value -isnot [DateTime] -and $Value -isnot [DateTimeOffset]) { throw ($Prefix+'_TYPE_INVALID') }
    try { return Format-RogUtc $Value } catch {
        if ((Get-RogCode $_) -eq 'UTC_TIMESTAMP_KIND_REQUIRED') { throw ($Prefix+'_KIND_INVALID') }
        throw ($Prefix+'_FORMAT_INVALID') # No DMTF/local-time guesses, culture casts or approximate birth.
    }
}
function Get-RogReadErrorCode($Record,[string]$Operation) {
    $denied=$false; $e=$Record.Exception
    while ($null -ne $e) {
        if ($e -is [UnauthorizedAccessException] -or $e -is [System.Security.SecurityException] -or
            $e.HResult -eq -2147024891 -or ($null -ne $e.PSObject.Properties['NativeErrorCode'] -and $e.NativeErrorCode -eq 5) -or
            ($null -ne $e.PSObject.Properties['StatusCode'] -and [string]$e.StatusCode -eq 'AccessDenied')) { $denied=$true; break }
        $e=$e.InnerException
    }
    return ($Operation+$(if($denied){'_DENIED'}else{'_FAILED'}))
}
function Get-RogReadCodes {
    return @('PROCESS_QUERY_DENIED','PROCESS_QUERY_FAILED','PROCESS_PATH_UNAVAILABLE','PROCESS_PATH_TYPE_INVALID',
        'PROCESS_PATH_INVALID','PROCESS_BIRTH_UNAVAILABLE','PROCESS_BIRTH_TYPE_INVALID','PROCESS_BIRTH_KIND_INVALID',
        'PROCESS_BIRTH_FORMAT_INVALID','PROCESS_ID_UNAVAILABLE','PROCESS_ID_TYPE_INVALID','PROCESS_OWNER_UNAVAILABLE',
        'PROCESS_OWNER_TYPE_INVALID','PROCESS_OWNER_QUERY_DENIED','PROCESS_OWNER_QUERY_FAILED',
        'TARGET_PROCESS_NOT_RUNNING','TARGET_PROCESS_PATH_CHANGED','TARGET_PROCESS_BIRTH_CHANGED','TARGET_NOT_CURRENT_USER',
        'TARGET_ROOT_AMBIGUOUS','TARGET_IDENTITY_UNAVAILABLE','CURRENT_USER_IDENTITY_DENIED','CURRENT_USER_IDENTITY_FAILED')
}
function Get-RogSafeReadCode($Record,[string]$Operation) {
    $code=Get-RogCode $Record
    if ($code -cin (Get-RogReadCodes)) { return $code }
    return Get-RogReadErrorCode $Record $Operation
}
function Update-RogIdentityDiagnostic($Context,$Status) {
    if ($null -eq $Context -or -not $Context.ContainsKey('Result')) { return }
    $d=$Context.Result.diagnostics.identity
    $d.attempted=$true; $d.acquired=$Status.identityAcquired; $d.state=$Status.identityState
    $d.availableFields=$Status.availableFields; $d.matchingCompleteProcessCount=$Status.rootCount; $d.unknownCandidateCount=$Status.unknownCandidateCount
}
function Get-RogDiagnosticPrivilege($Context) {
    if ($null -ne $Context -and $Context.ContainsKey('Result')) { $d=$Context.Result.diagnostics.permission } else { $d=(New-RogDiagnostic).permission }
    if (-not $d.queryAttempted) {
        $d.queryAttempted=$true
        try {
            $p=Get-RogPrivilege
            if ($p.verifiedHigh -isnot [bool]) { throw 'TOKEN_QUERY_INVALID' }
            $d.querySucceeded=$true; $d.verifiedHigh=$p.verifiedHigh
        } catch { $d.errorCode=Get-RogReadErrorCode $_ 'TOKEN_QUERY'; $d.verifiedHigh=$null }
    }
    return [ordered]@{queryAttempted=$d.queryAttempted;querySucceeded=$d.querySucceeded;verifiedHigh=$d.verifiedHigh;errorCode=$d.errorCode;systemAllowed=$false}
}
function Convert-RogUtc($Value) {
    # PS7 can materialize JSON ISO strings as DateTime; never culture-cast them to string.
    if ($Value -is [DateTimeOffset]) { return $Value.UtcDateTime }
    if ($Value -is [DateTime]) {
        if ($Value.Kind -eq [DateTimeKind]::Unspecified) { throw 'UTC_TIMESTAMP_KIND_REQUIRED' }
        return $Value.ToUniversalTime()
    }
    if ($Value -isnot [string] -or $Value -notmatch '^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(?:\.\d{1,7})?(?:Z|[+-]\d\d:\d\d)$') {
        throw 'UTC_TIMESTAMP_REQUIRED'
    }
    try { return [DateTimeOffset]::Parse($Value,[Globalization.CultureInfo]::InvariantCulture).UtcDateTime }
    catch { throw 'UTC_TIMESTAMP_INVALID' }
}
function Format-RogUtc($Value) {
    return (Convert-RogUtc $Value).ToString('o',[Globalization.CultureInfo]::InvariantCulture)
}
function Read-RogJson([string]$Path, [int]$Limit=65536) {
    Assert-RogNoReparse $Path
    $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
    try {
        if ($stream.Length -gt $Limit) { throw 'JSON_SIZE_LIMIT' }
        $reader=New-Object IO.StreamReader($stream,[Text.Encoding]::UTF8,$true)
        try { $text=$reader.ReadToEnd() } finally { $reader.Dispose() }
    } finally { $stream.Dispose() }
    if ([string]::IsNullOrWhiteSpace($text)) { throw 'JSON_EMPTY' }
    $parameters=@{InputObject=$text;ErrorAction='Stop'}
    if ((Get-Command ConvertFrom-Json).Parameters.ContainsKey('DateKind')) { $parameters.DateKind='String' }
    try { return ConvertFrom-Json @parameters } catch { throw 'JSON_INVALID' }
}
function Write-RogNewJson([string]$Path,$Value) {
    Assert-RogNoReparse ([IO.Path]::GetDirectoryName($Path))
    $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value | ConvertTo-Json -Depth 16))
    $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    try { $stream.Write($bytes,0,$bytes.Length); $stream.Flush() } finally { $stream.Dispose() }
}
function Assert-RogNoReparse([string]$Path) {
    if (-not [IO.Path]::IsPathRooted($Path)) { throw 'ABSOLUTE_PATH_REQUIRED' }
    $current=[IO.Path]::GetFullPath($Path)
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            $item=Get-Item -LiteralPath $current -Force -ErrorAction Stop
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'REPARSE_PATH_REJECTED' }
        }
        $parent=[IO.Path]::GetDirectoryName($current.TrimEnd('\','/'))
        if ($parent -eq $current) { break }; $current=$parent
    }
}
function New-RogContext([string]$Root, [string]$RequestedAction) {
    Assert-RogNoReparse $Root
    $results=Join-Path $Root 'Results'; Assert-RogNoReparse $results
    [void][IO.Directory]::CreateDirectory($results)
    $session='session-'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'-'+[guid]::NewGuid().ToString('N')
    $sessionDir=Join-Path $results $session
    if (Test-Path -LiteralPath $sessionDir) { throw 'OUTPUT_ALREADY_EXISTS' }
    [void][IO.Directory]::CreateDirectory($sessionDir)
    $repeat='repeat-'+[guid]::NewGuid().ToString('N'); $dir=Join-Path $sessionDir $repeat
    if (Test-Path -LiteralPath $dir) { throw 'OUTPUT_ALREADY_EXISTS' }
    [void][IO.Directory]::CreateDirectory($dir)
    return @{ Root=$Root; Directory=$dir; RunId=$session+'/'+$repeat; Action=$RequestedAction; StartedUtc=[DateTime]::UtcNow.ToString('o') }
}
function New-RogResult($Context) {
    return [ordered]@{schemaVersion=1;toolVersion='rog-portable-runner-2';runId=$Context.RunId;
        action='invalid';timestampUtc=$Context.StartedUtc;ok=$false;exitCode=1;errorCode=$null;
        scope='software-tool-operation-only';rogRegressionPassed=$false;abPassed=$false;
        productAcceptancePassed=$false;sideEffectsRequested=$false;details=$null;
        diagnostics=(New-RogDiagnostic);facts=[ordered]@{target=$null;status=$null}}
}
function Write-RogResult($Context,$Result,[string]$Name='result.json') {
    Write-RogNewJson (Join-Path $Context.Directory $Name) $Result
}
function Get-RogInt($Value,[int]$Minimum,[int]$Maximum,[string]$Code) {
    $parsed=0
    if (-not [int]::TryParse([string]$Value,[Globalization.NumberStyles]::Integer,[Globalization.CultureInfo]::InvariantCulture,[ref]$parsed) -or $parsed -lt $Minimum -or $parsed -gt $Maximum) { throw $Code }
    return $parsed
}
function Test-RogTrue($Value) { return ($Value -is [bool] -and $Value) }
function Test-RogFalse($Value) { return ($Value -is [bool] -and -not $Value) }
# Pure lexical gate over the RAW literal. Must run before any Join-Path/GetFullPath normalization:
# on Windows PowerShell 5.1 [IO.Path]::GetFullPath throws MethodInvocationException for wildcard and
# alternate-data-stream literals, which would surface as an opaque error instead of the required code.
function Assert-RogPathDescriptorLiteral($Path) {
    if ($Path -isnot [string] -or [string]::IsNullOrWhiteSpace($Path)) { return }
    $descriptor=$Path.Replace('/','\')
    $afterDrive=if ($descriptor -match '^[A-Za-z]:') { $descriptor.Substring(2) } else { $descriptor }
    if ($descriptor -match '[*?]' -or $descriptor -match '^\\\\[?.]\\' -or $afterDrive.Contains(':')) { throw 'TARGET_PATH_DESCRIPTOR_REJECTED' }
}
# Lazy, mockable normalization boundary. The raw literal gate belongs before this call.
function Resolve-RogTargetPath([string]$Path,[string]$Root) {
    if (-not [IO.Path]::IsPathRooted($Path)) { $Path=Join-Path $Root $Path -ErrorAction Stop }
    return [IO.Path]::GetFullPath($Path)
}
function Get-RogOptions($InputOptions,[string]$Root,[object[]]$Extra=@()) {
    if ($Extra.Count -gt 0) { throw 'UNKNOWN_OR_POSITIONAL_PARAMETER' }
    $o=@{Action='validate';ExePath='';ExpectedSha256='';TargetPid=0;TargetCreationTimeUtc='';SessionId='';
        AuthorizedAction='';AllowElevation=$false;StartArgument=@();GraceSeconds=30;StartWaitSeconds=30;
        WarmupSeconds=30;Seconds=120;SampleMilliseconds=250;InventoryMilliseconds=1000;
        FanState='unknown';GamepadState='unknown';GyroState='unknown';Label='S-observed';
        LifecycleJsonl='';VirtualHandshakeJsonl='';ExpectedSessionId='';LogFile=@();Preset='balanced';TimeoutSeconds=30}
    if ($InputOptions.ContainsKey('ConfigPath') -and $InputOptions.ConfigPath) {
        $configPath=[string]$InputOptions.ConfigPath
        if (-not [IO.Path]::IsPathRooted($configPath)) { $configPath=Join-Path $Root $configPath }
        if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) { throw 'CONFIG_NOT_FOUND' }
        $config=Read-RogJson $configPath
        $allowed=@('schemaVersion','ExePath','ExpectedSha256','GraceSeconds','StartWaitSeconds','WarmupSeconds','Seconds','SampleMilliseconds','InventoryMilliseconds')
        foreach ($p in $config.PSObject.Properties) {
            if ($p.Name -notin $allowed) { throw 'CONFIG_FIELD_NOT_ALLOWED' }
            if ($p.Name -ne 'schemaVersion') { $o[$p.Name]=$p.Value }
        }
        if ($config.schemaVersion -ne 1) { throw 'CONFIG_SCHEMA_UNSUPPORTED' }
    }
    foreach ($key in $InputOptions.Keys) {
        if ($key -eq 'ConfigPath') { continue }
        if (-not $o.ContainsKey($key)) { throw 'OPTION_NOT_ALLOWED' }
        $v=$InputOptions[$key]; if ($v -is [Management.Automation.SwitchParameter]) { $v=$v.IsPresent }; $o[$key]=$v
    }
    if ($o.Action -isnot [string] -or $o.Action -notin @('validate','preflight','observe','capture','status','start','stop','restart','fan-probe','fan-on','fan-off','fan-close','fan-status')) { throw 'INVALID_ACTION' }
    foreach ($field in @('ExePath','ExpectedSha256','SessionId','AuthorizedAction','FanState','GamepadState','GyroState','Label','LifecycleJsonl','VirtualHandshakeJsonl','ExpectedSessionId','Preset')) {
        if ($o[$field] -isnot [string]) { throw 'STRING_PARAMETER_REQUIRED' }
    }
    if ($o.AllowElevation -isnot [bool]) { throw 'BOOLEAN_PARAMETER_REQUIRED' }
    $o.TargetPid=Get-RogInt $o.TargetPid 0 2147483647 'INVALID_TARGET_PID'
    foreach ($name in @('GraceSeconds','StartWaitSeconds','TimeoutSeconds')) { $o[$name]=Get-RogInt $o[$name] 1 300 ('INVALID_'+$name.ToUpperInvariant()) }
    $o.WarmupSeconds=Get-RogInt $o.WarmupSeconds 0 3600 'INVALID_WARMUP_SECONDS'
    $o.Seconds=Get-RogInt $o.Seconds 1 3600 'INVALID_SECONDS'
    $o.SampleMilliseconds=Get-RogInt $o.SampleMilliseconds 100 5000 'INVALID_SAMPLE_MILLISECONDS'
    $o.InventoryMilliseconds=Get-RogInt $o.InventoryMilliseconds 250 10000 'INVALID_INVENTORY_MILLISECONDS'
    foreach ($name in @('FanState','GamepadState','GyroState')) { if ($o[$name] -notin @('on','off','unknown','unavailable')) { throw 'INVALID_SCENARIO_STATE' } }
    if ($o.SessionId -and $o.SessionId -cnotmatch '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$') { throw 'INVALID_SESSION_ID' }
    if ($o.ExpectedSessionId -and $o.ExpectedSessionId -cnotmatch '^[0-9a-f-]{36}$') { throw 'INVALID_EVIDENCE_SESSION' }
    if ($o.ExpectedSha256 -and $o.ExpectedSha256 -notmatch '^[0-9a-fA-F]{64}$') { throw 'INVALID_EXPECTED_SHA256' }
    if ($o.ExePath) {
        Assert-RogPathDescriptorLiteral $o.ExePath
        try { $o.ExePath=Resolve-RogTargetPath $o.ExePath $Root }
        catch { throw 'TARGET_PATH_DESCRIPTOR_REJECTED' }
    }
    if ($null -eq $o.TargetCreationTimeUtc -or ($o.TargetCreationTimeUtc -is [string] -and [string]::IsNullOrWhiteSpace($o.TargetCreationTimeUtc))) {
        $o.TargetCreationTimeUtc=''
    } else {
        $o.TargetCreationTimeUtc=Convert-RogBirth $o.TargetCreationTimeUtc 'TARGET_CREATION_TIME'
    }
    $o.LogFile=@($o.LogFile)
    if ($o.LogFile.Count -gt 32) { throw 'LOG_FILE_COUNT_LIMIT' }
    $resolvedLogs=@()
    foreach ($log in $o.LogFile) {
        if ($log -isnot [string] -or [string]::IsNullOrWhiteSpace($log) -or $log -match '[*?]') { throw 'LOG_FILE_ARGUMENT_INVALID' }
        try {
            $p=$log; if (-not [IO.Path]::IsPathRooted($p)) { $p=Join-Path $Root $p }
            $resolvedLogs+=@([IO.Path]::GetFullPath($p))
        } catch { throw 'LOG_FILE_ARGUMENT_INVALID' }
    }
    $o.LogFile=$resolvedLogs
    $o.StartArgument=@($o.StartArgument)
    # Only exact, public diagnostic mock/session pairs. No private args or arbitrary program CLI.
    if ($o.StartArgument.Count -gt 0) {
        if ($o.StartArgument.Count -notin @(2,4) -or -not $o.SessionId) { throw 'START_ARGUMENT_NOT_ALLOWED' }
        $seen=@{}
        for ($i=0;$i -lt $o.StartArgument.Count;$i+=2) {
            $flag=$o.StartArgument[$i]; $value=$o.StartArgument[$i+1]
            if ($flag -isnot [string] -or $flag -cnotin @('--ai-fan-mock-session','--ai-cpu-isolated-session') -or
                $value -isnot [string] -or $value -cne $o.SessionId -or $seen.ContainsKey($flag)) { throw 'START_ARGUMENT_NOT_ALLOWED' }
            $seen[$flag]=$true
        }
        if (-not $seen.ContainsKey('--ai-fan-mock-session')) { throw 'PAIRED_SESSION_REQUIRED' }
    }
    if ($o.Preset -notin @('soft','balanced','aggressive')) { throw 'INVALID_FAN_PRESET' }
    if ($o.Label -notmatch '^[A-Za-z0-9_-]{1,64}$') { throw 'INVALID_LABEL' }
    return $o
}
# Pure parameter gates: no target file/process/native reads before choosing a permission context.
function Assert-RogTargetDescriptor($Options,[switch]$RequireTrusted) {
    if ($Options.ExePath -isnot [string] -or [string]::IsNullOrWhiteSpace($Options.ExePath)) { throw 'TARGET_PATH_REQUIRED' }
    # Ordinary local/UNC files only. Reject the literal before any potentially throwing Path API.
    Assert-RogPathDescriptorLiteral $Options.ExePath
    try {
        $rooted=[IO.Path]::IsPathRooted($Options.ExePath)
        $fileName=[IO.Path]::GetFileName($Options.ExePath)
    } catch { throw 'TARGET_PATH_DESCRIPTOR_REJECTED' }
    if (-not $rooted) { throw 'TARGET_PATH_REQUIRED' }
    if ($fileName -notmatch '^(?i:YeManCC|YMCC)(?:[-_.A-Za-z0-9]+)?\.exe$') { throw 'TARGET_NOT_YMCC' }
    if ($RequireTrusted -and -not $Options.ExpectedSha256) { throw 'TRUSTED_PRODUCT_HASH_REQUIRED' }
    if ($Options.ExpectedSha256 -and ($Options.ExpectedSha256 -isnot [string] -or $Options.ExpectedSha256 -cnotmatch '^[0-9a-fA-F]{64}$')) { throw 'INVALID_EXPECTED_SHA256' }
    return [ordered]@{exePath=$Options.ExePath;observedSha256=$null;metadataVerified=$false;hashMatchVerified=$false;
        trustedHashSupplied=[bool]$Options.ExpectedSha256;
        authorizationSource=$(if($Options.ExpectedSha256){'operator-supplied-final-product-identity'}else{'untrusted-discovery-only'});discoveredHashIsAuthorization=$false}
}
function Test-RogPidValue($Value) {
    return (($Value -is [int] -or $Value -is [uint32] -or $Value -is [long]) -and $Value -ge 1 -and $Value -le [int]::MaxValue)
}
function Assert-RogProcessSelector($Options) {
    if (-not (Test-RogPidValue $Options.TargetPid) -or -not $Options.TargetCreationTimeUtc) { throw 'EXACT_PROCESS_IDENTITY_REQUIRED' }
    [void](Convert-RogBirth $Options.TargetCreationTimeUtc 'TARGET_CREATION_TIME')
}
function Assert-RogTarget($Options,[switch]$RequireTrusted) {
    $target=Assert-RogTargetDescriptor $Options -RequireTrusted:$RequireTrusted
    Assert-RogNoReparse $Options.ExePath
    if (-not (Test-Path -LiteralPath $Options.ExePath -PathType Leaf)) { throw 'TARGET_EXE_NOT_FOUND' }
    $hash=(Get-FileHash -LiteralPath $Options.ExePath -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant()
    if ($Options.ExpectedSha256 -and $hash -ine $Options.ExpectedSha256) { throw 'TARGET_SHA256_MISMATCH' }
    $target.observedSha256=$hash; $target.metadataVerified=$true; $target.hashMatchVerified=[bool]$Options.ExpectedSha256
    return $target
}
function Initialize-RogNative {
    if ('RogPortable.Native' -as [type]) { return }
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
namespace RogPortable {
 public static class Native {
  [StructLayout(LayoutKind.Sequential)] struct FT { public uint Low,High; }
  [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr OpenProcess(uint a,bool i,uint p);
  [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetProcessTimes(IntPtr h,out FT c,out FT e,out FT k,out FT u);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
  [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
  [DllImport("advapi32.dll",SetLastError=true)] static extern bool OpenProcessToken(IntPtr p,uint a,out IntPtr t);
  [DllImport("advapi32.dll",SetLastError=true)] static extern bool GetTokenInformation(IntPtr t,int k,IntPtr b,int l,out int n);
  [DllImport("advapi32.dll")] static extern IntPtr GetSidSubAuthorityCount(IntPtr s);
  [DllImport("advapi32.dll")] static extern IntPtr GetSidSubAuthority(IntPtr s,uint i);
  [DllImport("shell32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CommandLineToArgvW(string c,out int n);
  [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr m);
  public static string Creation(uint pid) {
   IntPtr h=OpenProcess(0x1000,false,pid);if(h==IntPtr.Zero)throw new InvalidOperationException("PERMISSION_DENIED");
   try{FT c,e,k,u;if(!GetProcessTimes(h,out c,out e,out k,out u))throw new InvalidOperationException("PROCESS_BIRTH_UNVERIFIED");return (((ulong)c.High<<32)|c.Low).ToString(System.Globalization.CultureInfo.InvariantCulture);}finally{CloseHandle(h);}
  }
  public static bool HighToken() { return HighTokenHandle(GetCurrentProcess()); }
  public static bool HighTokenForProcess(uint pid) {
   IntPtr h=OpenProcess(0x1000,false,pid);if(h==IntPtr.Zero)throw new InvalidOperationException("PERMISSION_DENIED");try{return HighTokenHandle(h);}finally{CloseHandle(h);}
  }
  static bool HighTokenHandle(IntPtr process) {
   IntPtr t;if(!OpenProcessToken(process,8,out t))throw new InvalidOperationException("PERMISSION_DENIED");
   try{int n;IntPtr e=Marshal.AllocHGlobal(4);bool elevated;
    try{if(!GetTokenInformation(t,20,e,4,out n))throw new InvalidOperationException("PERMISSION_DENIED");elevated=Marshal.ReadInt32(e)!=0;}finally{Marshal.FreeHGlobal(e);}
    GetTokenInformation(t,25,IntPtr.Zero,0,out n);if(n<=0)throw new InvalidOperationException("PERMISSION_DENIED");
    IntPtr b=Marshal.AllocHGlobal(n);try{if(!GetTokenInformation(t,25,b,n,out n))throw new InvalidOperationException("PERMISSION_DENIED");IntPtr s=Marshal.ReadIntPtr(b);byte count=Marshal.ReadByte(GetSidSubAuthorityCount(s));if(count==0)return false;uint rid=(uint)Marshal.ReadInt32(GetSidSubAuthority(s,(uint)(count-1)));return elevated&&rid>=0x3000&&rid<0x4000;}finally{Marshal.FreeHGlobal(b);}
   }finally{CloseHandle(t);}
  }
  public static string[] Argv(string line) {
   int n;IntPtr p=CommandLineToArgvW(line,out n);if(p==IntPtr.Zero)throw new InvalidOperationException("CLI_IDENTITY_UNVERIFIED");
   try{string[] a=new string[n];for(int i=0;i<n;i++)a[i]=Marshal.PtrToStringUni(Marshal.ReadIntPtr(p,i*IntPtr.Size));return a;}finally{LocalFree(p);}
  }
 }
}
"@ -ErrorAction Stop
}
function Wait-RogTick { Start-Sleep -Milliseconds 100 }
function Get-RogClock { return [DateTime]::UtcNow }
function Get-RogPrincipalSid { return [Security.Principal.WindowsIdentity]::GetCurrent().User.Value }
function Get-RogPrivilege {
    Initialize-RogNative
    return @{currentUserSid=Get-RogPrincipalSid;verifiedHigh=[RogPortable.Native]::HighToken();workerPid=$PID;systemAllowed=$false}
}
function Get-RogTargetPrivilege([int]$ProcessId) {
    Initialize-RogNative
    $high=[RogPortable.Native]::HighTokenForProcess([uint32]$ProcessId)
    return @{ok=$true;isElevated=$high;integrityLevel=$(if($high){'high'}else{'not-high'});canMutateYmcc=$high}
}
function Write-RogLifecycleRecord($Context,$Options,$Fields) {
    $record=[ordered]@{timestampUtc=[DateTime]::UtcNow.ToString('o');sessionId=$Options.SessionId;scriptVersion='rog-portable-lifecycle-1';targetExe=$Options.ExePath;targetSha256=$Options.ExpectedSha256.ToLowerInvariant()}
    foreach ($key in $Fields.Keys) { $record[$key]=$Fields[$key] }
    $path=Join-Path $Context.Directory 'lifecycle-control.jsonl'; Assert-RogNoReparse $path
    $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($record | ConvertTo-Json -Depth 10 -Compress)+[Environment]::NewLine)
    $stream=[IO.File]::Open($path,[IO.FileMode]::Append,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    try { $stream.Write($bytes,0,$bytes.Length) } finally { $stream.Dispose() }
}
function Get-RogProcessRow([int]$ProcessId) {
    try { return Get-CimInstance -ClassName Win32_Process -Filter ('ProcessId='+$ProcessId) -OperationTimeoutSec 30 -ErrorAction Stop }
    catch { throw (Get-RogReadErrorCode $_ 'PROCESS_QUERY') }
}
function Get-RogNamedRows([string]$ExePath) {
    try { return Get-CimInstance -ClassName Win32_Process -Filter ("Name='"+[IO.Path]::GetFileName($ExePath)+"'") -OperationTimeoutSec 30 -ErrorAction Stop }
    catch { throw (Get-RogReadErrorCode $_ 'PROCESS_QUERY') }
}
function Get-RogOwnerSid($Row) {
    try { $owner=Invoke-CimMethod -InputObject $Row -MethodName GetOwnerSid -OperationTimeoutSec 30 -ErrorAction Stop }
    catch { throw (Get-RogReadErrorCode $_ 'PROCESS_OWNER_QUERY') }
    if (($owner.ReturnValue -isnot [int] -and $owner.ReturnValue -isnot [uint32] -and $owner.ReturnValue -isnot [long]) -or $owner.ReturnValue -lt 0 -or $owner.ReturnValue -gt [uint32]::MaxValue) { throw 'PROCESS_OWNER_TYPE_INVALID' }
    if ($owner.ReturnValue -in @(2,3)) { throw 'PROCESS_OWNER_QUERY_DENIED' }
    if ($owner.ReturnValue -ne 0) { throw 'PROCESS_OWNER_QUERY_FAILED' }
    if ($null -eq $owner.Sid -or ($owner.Sid -is [string] -and [string]::IsNullOrWhiteSpace($owner.Sid))) { throw 'PROCESS_OWNER_UNAVAILABLE' }
    if ($owner.Sid -isnot [string] -or $owner.Sid -notmatch '^S-\d+(?:-\d+)+$') { throw 'PROCESS_OWNER_TYPE_INVALID' }
    return $owner.Sid
}
function Assert-RogProcess($Options,[int]$ProcessId=0,$Birth='') {
    if (-not $ProcessId) { $ProcessId=$Options.TargetPid }; if (-not $Birth) { $Birth=$Options.TargetCreationTimeUtc }
    if (-not $ProcessId -or -not $Birth) { throw 'EXACT_PROCESS_IDENTITY_REQUIRED' }
    $row=Get-RogProcessRow $ProcessId
    if ($null -eq $row) { throw 'TARGET_PROCESS_NOT_RUNNING' }
    if (-not $row.ExecutablePath) { throw 'PROCESS_PATH_PERMISSION_FAILED' }
    if ([IO.Path]::GetFullPath($row.ExecutablePath) -ine $Options.ExePath) { throw 'TARGET_PROCESS_PATH_CHANGED' }
    if ((Convert-RogUtc $row.CreationDate).Ticks -ne (Convert-RogUtc $Birth).Ticks) { throw 'TARGET_PROCESS_BIRTH_CHANGED' }
    if ((Get-RogOwnerSid $row) -ne (Get-RogPrincipalSid)) { throw 'TARGET_NOT_CURRENT_USER' }
    return $row # Raw CIM object stays in memory only. Never serialize CommandLine.
}
function Get-RogStatus($Options,$Context=$null) {
    $target=Assert-RogTarget $Options
    $state=[ordered]@{target=$target;processes=@();candidates=@();rootCount=0;identityAcquired=$false;
        identityState='NOT_QUERIED';root=$null;unknownCandidateCount=0;excludedCandidateCount=0;
        availableFields=@('exePath','observedSha256');errorCodes=@();readOnly=$true;commandLineRecorded=$false}
    if ($null -ne $Context -and $Context.ContainsKey('Result')) {
        $Context.Result.facts.target=$target; $Context.Result.facts.status=$state
        if ($Options.Action -eq 'status') { $Context.Result.details=$state }
        $Context.Result.diagnostics.phase='identity-query'; $Context.Result.diagnostics.identity.attempted=$true
    }
    try {
        if ($Options.TargetPid) {
            $rows=@(Get-RogProcessRow $Options.TargetPid)
            if ($rows.Count -eq 0 -or $null -eq $rows[0]) { throw 'TARGET_PROCESS_NOT_RUNNING' }
        } else { $rows=@(Get-RogNamedRows $Options.ExePath) }
    } catch {
        $code=Get-RogSafeReadCode $_ 'PROCESS_QUERY'; $state.errorCodes=@($code); $state.identityState='QUERY_FAILED'
        Update-RogIdentityDiagnostic $Context $state; throw $code
    }
    try { $currentSid=Get-RogPrincipalSid }
    catch {
        $code=Get-RogReadErrorCode $_ 'CURRENT_USER_IDENTITY'; $state.errorCodes=@($code); $state.identityState='USER_QUERY_FAILED'
        Update-RogIdentityDiagnostic $Context $state; throw $code
    }
    foreach ($row in $rows) {
        $candidate=[ordered]@{pid=$null;exePath=$null;creationTimeUtc=$null;currentUserVerified=$null;
            classification='UNKNOWN';availableFields=@();errorCodes=@()}
        $errors=@(); $isForeign=$false; $id=$row.ProcessId
        if ($null -eq $id) { $errors+=@('PROCESS_ID_UNAVAILABLE') }
        elseif (-not (Test-RogPidValue $id)) { $errors+=@('PROCESS_ID_TYPE_INVALID') }
        else { $candidate.pid=[int]$id; $candidate.availableFields+=@('pid') }
        if ($Options.TargetPid -and $candidate.pid -ne $Options.TargetPid) { $errors+=@('TARGET_IDENTITY_UNAVAILABLE') }
        $path=$row.ExecutablePath
        if ($null -eq $path -or ($path -is [string] -and [string]::IsNullOrWhiteSpace($path))) { $errors+=@('PROCESS_PATH_UNAVAILABLE') }
        elseif ($path -isnot [string]) { $errors+=@('PROCESS_PATH_TYPE_INVALID') }
        else {
            try {
                if (-not [IO.Path]::IsPathRooted($path)) { throw 'PROCESS_PATH_INVALID' }
                if ([IO.Path]::GetFullPath($path) -ine $Options.ExePath) { $isForeign=$true; $candidate.classification='FOREIGN_PATH' }
                else { $candidate.exePath=$Options.ExePath; $candidate.availableFields+=@('exePath') }
            } catch { $errors+=@('PROCESS_PATH_INVALID') }
        }
        if (-not $isForeign) {
            try { $candidate.creationTimeUtc=Convert-RogBirth $row.CreationDate; $candidate.availableFields+=@('creationTimeUtc') }
            catch { $errors+=@(Get-RogSafeReadCode $_ 'PROCESS_BIRTH') }
            try {
                $ownerSid=Get-RogOwnerSid $row
                if ($null -eq $ownerSid -or ($ownerSid -is [string] -and [string]::IsNullOrWhiteSpace($ownerSid))) { throw 'PROCESS_OWNER_UNAVAILABLE' }
                if ($ownerSid -isnot [string] -or $ownerSid -notmatch '^S-\d+(?:-\d+)+$') { throw 'PROCESS_OWNER_TYPE_INVALID' }
                $candidate.currentUserVerified=($ownerSid -ceq $currentSid); $candidate.availableFields+=@('currentUserVerified')
                if (-not $candidate.currentUserVerified) { $isForeign=$true; $candidate.classification='FOREIGN_OWNER' }
            } catch { $errors+=@(Get-RogSafeReadCode $_ 'PROCESS_OWNER_QUERY') }
            if ($Options.TargetCreationTimeUtc -and $candidate.creationTimeUtc -and
                (Convert-RogUtc $Options.TargetCreationTimeUtc).Ticks -ne (Convert-RogUtc $candidate.creationTimeUtc).Ticks) { $errors+=@('TARGET_PROCESS_BIRTH_CHANGED') }
        }
        if ($isForeign) {
            $state.excludedCandidateCount++
            if ($Options.TargetPid) { $errors+=@($(if($candidate.classification -eq 'FOREIGN_PATH'){'TARGET_PROCESS_PATH_CHANGED'}else{'TARGET_NOT_CURRENT_USER'})) }
            else { $errors=@() }
        } elseif ($errors.Count -eq 0) {
            $candidate.classification='MATCHED_CURRENT_USER'
            $state.processes+=@([ordered]@{pid=$candidate.pid;creationTimeUtc=$candidate.creationTimeUtc;exePath=$Options.ExePath;currentUserVerified=$true})
        } else { $state.unknownCandidateCount++ }
        $candidate.errorCodes=@($errors|Select-Object -Unique); $state.errorCodes+=@($candidate.errorCodes)
        $state.candidates+=@($candidate)
    }
    $state.rootCount=$state.processes.Count; $state.errorCodes=@($state.errorCodes|Select-Object -Unique)
    foreach ($candidate in $state.candidates) { $state.availableFields+=@($candidate.availableFields) }
    $state.availableFields=@($state.availableFields|Select-Object -Unique)
    if ($state.errorCodes.Count) { $state.identityState='INCOMPLETE' }
    elseif ($state.rootCount -gt 1) { $state.identityState='AMBIGUOUS'; $state.errorCodes=@('TARGET_ROOT_AMBIGUOUS') }
    elseif ($state.rootCount -eq 1) { $state.identityState='EXACT_CURRENT_USER_ROOT'; $state.root=$state.processes[0]; $state.identityAcquired=$true }
    else { $state.identityState='NO_MATCHING_ROOT' }
    Update-RogIdentityDiagnostic $Context $state
    if ($state.errorCodes.Count) { throw $state.errorCodes[0] }
    return $state
}
function Get-RogKitManifest([string]$Root) {
    $path=Join-Path $Root 'kit-manifest.json'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return @{present=$false;verified=$false;status='NOT_PRESENT_SOURCE_CHECK_ONLY';file='kit-manifest.json'} }
    $manifest=Read-RogJson $path 1048576
    foreach ($p in $manifest.PSObject.Properties) { if ($p.Name -notin @('version','sourceRole','localToolValidation','rogRegressionStatus','files')) { throw 'MANIFEST_FIELD_REJECTED' } }
    if ($manifest.version -isnot [string] -or $manifest.version -notmatch '^[A-Za-z0-9.-]{1,64}$' -or
        $manifest.sourceRole -cne 'portable-ROG-collector-toolkit' -or $manifest.rogRegressionStatus -cne 'NOT_RUN' -or
        $manifest.localToolValidation.scope -cne 'local-software-tool-validation-only' -or
        $manifest.localToolValidation.status -notin @('PASSED','NOT_RUN') -or
        -not (Test-RogFalse $manifest.localToolValidation.rogRegressionPassed)) { throw 'MANIFEST_PROVENANCE_INVALID' }
    $seen=@{}; $rootPrefix=[IO.Path]::GetFullPath($Root).TrimEnd('\')+'\'
    $files=@($manifest.files)
    if ($files.Count -lt 6 -or $files.Count -gt 256) { throw 'MANIFEST_FILE_COUNT_INVALID' }
    foreach ($file in $files) {
        foreach ($p in $file.PSObject.Properties) { if ($p.Name -notin @('path','sha256','bytes')) { throw 'MANIFEST_FILE_FIELD_REJECTED' } }
        $relative=$file.path
        if ($relative -isnot [string] -or [string]::IsNullOrWhiteSpace($relative) -or [IO.Path]::IsPathRooted($relative) -or
            $relative -match '(^|[\\/])\.\.?([\\/]|$)|[:*?]' -or $relative -match '(^|[\\/])Results([\\/]|$)' -or
            [IO.Path]::GetExtension($relative) -notin @('.ps1','.json','.md') -or
            $relative -match '(?i)(settings|token|lease|profile|worker-request|^kit-manifest\.json$)' -or $seen.ContainsKey($relative.Replace('/','\'))) { throw 'MANIFEST_PATH_REJECTED' }
        $full=[IO.Path]::GetFullPath((Join-Path $Root $relative))
        if (-not $full.StartsWith($rootPrefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'MANIFEST_PATH_REJECTED' }
        Assert-RogNoReparse $full
        if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { throw 'MANIFEST_FILE_MISSING' }
        if ($file.sha256 -isnot [string] -or $file.sha256 -notmatch '^[0-9a-fA-F]{64}$' -or
            -not (Test-RogSequenceValue $file.bytes)) { throw 'MANIFEST_FILE_IDENTITY_INVALID' }
        if ((Get-Item -LiteralPath $full).Length -ne $file.bytes -or (Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash -ine $file.sha256) { throw 'MANIFEST_FILE_MISMATCH' }
        $seen[$relative.Replace('/','\')]=$true
    }
    foreach ($required in @('Invoke-ROG-CPU.ps1','Invoke-YMCC-AuthorizedWorker.ps1','Control-YMCC-Lifecycle.ps1','Send-YMCC-AI-Fan-Mock.ps1','Collect-YMCC-CPU.ps1','config.example.json')) {
        if (-not $seen.ContainsKey($required)) { throw 'MANIFEST_REQUIRED_FILE_MISSING' }
    }
    $docs=Join-Path $Root 'docs'
    Assert-RogNoReparse $docs
    if (-not (Test-Path -LiteralPath $docs -PathType Container)) { throw 'MANIFEST_DOCS_MISSING' }
    foreach ($requiredDoc in @('ROG-CAPTURE-AI-TASK-v10-20261004.md','KNOWN-CAPTURE-ERRORS-AND-FIXES.md','OPERATOR-COMMANDS.md')) {
        if (-not $seen.ContainsKey('docs\'+$requiredDoc)) { throw 'MANIFEST_REQUIRED_DOC_MISSING' }
    }
    foreach ($item in @(Get-ChildItem -LiteralPath $docs -Force -ErrorAction Stop)) {
        Assert-RogNoReparse $item.FullName
        if ($item.PSIsContainer) { throw 'MANIFEST_DOCS_STRUCTURE_UNSUPPORTED' }
        if (-not $seen.ContainsKey('docs\'+$item.Name)) { throw 'MANIFEST_DOC_NOT_BOUND' }
    }
    return @{present=$true;verified=$true;status='PORTABLE_FILES_VERIFIED_NOT_ROG_REGRESSION';file='kit-manifest.json';fileCount=$files.Count;rogRegressionPassed=$false}
}
function Test-RogSequenceValue($Value) { return (($Value -is [int] -or $Value -is [long]) -and $Value -ge 0 -and $Value -le 9007199254740991L) }
function Get-RogToolValidation([string]$Root) {
    $files=@('Invoke-ROG-CPU.ps1','Invoke-YMCC-AuthorizedWorker.ps1','Control-YMCC-Lifecycle.ps1','Send-YMCC-AI-Fan-Mock.ps1','Collect-YMCC-CPU.ps1')
    $checks=@(); $all=$true
    foreach ($file in $files) {
        $path=Join-Path $Root $file; $exists=Test-Path -LiteralPath $path -PathType Leaf; $count=0
        if ($exists) { Assert-RogNoReparse $path; $tokens=$null; $errors=$null; [void][Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors); $count=@($errors).Count }
        else { $all=$false }
        if ($count) { $all=$false }
        $checks+=@{file=$file;exists=$exists;parserErrorCount=$count}
    }
    $manifest=Get-RogKitManifest $Root
    return @{valid=$all;files=$checks;manifest=$manifest;productTouched=$false;runtimeRequired='Windows PowerShell 5.1 or PowerShell 7; no source/Node/SDK';rogRegressionPassed=$false}
}
function Get-RogPreflight($Options,[string]$Root,$Context=$null) {
    $target=Assert-RogTarget $Options
    $privilege=Get-RogDiagnosticPrivilege $Context
    $reachability=@{checked=$false;namespaceReachable=$null;responseSlotPresent=$null;runtimeVerified=$false;
        identityTrusted=[bool]$Options.ExpectedSha256;errorCode=$null;reason='READ_ONLY_IDENTITY_ONLY_NO_FUNCTION_PROBE'}
    $details=[ordered]@{target=$target;privilege=$privilege;status=$null;
        collectorPresent=(Test-Path -LiteralPath (Join-Path $Root 'Collect-YMCC-CPU.ps1') -PathType Leaf);
        fan=@{state='CAPABILITY_UNVERIFIED';reachability=$reachability;remediation='PRODUCT_UPDATE_REQUIRED_IF_RUNTIME_MISSING';functionalOnAttempted=$false};
        gamepad=@{state='CAPABILITY_UNVERIFIED';functionalOnAttempted=$false};gyro=@{state='CAPABILITY_UNVERIFIED';functionalOnAttempted=$false};
        permissionFailureIsNotCapabilityAbsence=$true;readOnly=$true;runAsAttempted=$false;
        productArgumentEnvironmentAudit=@{environmentInheritedOnStart=$true;rawEnvironmentRecorded=$false;rawCommandLineRecorded=$false;rogVerified=$false}}
    if ($null -ne $Context -and $Context.ContainsKey('Result')) { $Context.Result.details=$details }
    try { $details.status=Get-RogStatus $Options $Context }
    catch {
        if ($null -ne $Context -and $Context.ContainsKey('Result')) { $details.status=$Context.Result.facts.status }
        throw
    }
    return $details
}
function Assert-RogAuthorized($Options) {
    if ($Options.AuthorizedAction -cne $Options.Action) { throw 'USER_ACTION_AUTHORIZATION_REQUIRED' }
}
function Assert-RogCaptureRootEvidence($Options) {
    Assert-RogNoReparse $Options.LifecycleJsonl
    if ((Get-Item -LiteralPath $Options.LifecycleJsonl).Length -gt 524288) { throw 'CONTROL_EVIDENCE_SIZE_LIMIT' }
    $latest=$null
    foreach ($line in [IO.File]::ReadAllLines($Options.LifecycleJsonl,[Text.Encoding]::UTF8)) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        $p=@{InputObject=$line;ErrorAction='Stop'}
        if ((Get-Command ConvertFrom-Json).Parameters.ContainsKey('DateKind')) { $p.DateKind='String' }
        try { $row=ConvertFrom-Json @p } catch { throw 'CONTROL_EVIDENCE_JSON_INVALID' }
        if ($row.sessionId -ceq $Options.ExpectedSessionId -and $row.action -ceq 'ymcc.start') { $latest=$row }
    }
    if ($null -eq $latest -or -not (Test-RogTrue $latest.applied) -or -not (Test-RogSequenceValue $latest.targetPid) -or
        $latest.targetPid -ne $Options.TargetPid -or $latest.targetExe -isnot [string] -or
        [IO.Path]::GetFullPath($latest.targetExe) -ine $Options.ExePath -or
        (Convert-RogUtc $latest.targetCreationTimeUtc).Ticks -ne (Convert-RogUtc $Options.TargetCreationTimeUtc).Ticks -or
        $latest.targetSha256 -isnot [string] -or $latest.targetSha256 -ine $Options.ExpectedSha256 -or
        -not (Test-RogTrue $latest.targetPrivilegeVerified)) { throw 'CONTROL_EVIDENCE_EXACT_ROOT_UNVERIFIED' }
    $at=Convert-RogUtc $latest.timestampUtc
    if ($at -lt [DateTime]::UtcNow.AddSeconds(-600) -or $at -gt [DateTime]::UtcNow.AddSeconds(2)) { throw 'CONTROL_EVIDENCE_NOT_FRESH' }
    # The collector still verifies restart/cleanup/feature/runtime evidence. Never synthesize feature ACKs here.
}
function Invoke-RogCollector($Options,$Context) {
    [void](Assert-RogTarget $Options -RequireTrusted)
    [void](Assert-RogProcess $Options)
    $capture=$Options.Action -eq 'capture'
    if ($capture) {
        if (-not $Options.ExpectedSessionId -or -not $Options.LifecycleJsonl -or -not $Options.VirtualHandshakeJsonl) { throw 'REAL_CONTROL_AND_RUNTIME_EVIDENCE_REQUIRED' }
        foreach ($key in @('LifecycleJsonl','VirtualHandshakeJsonl')) {
            if (-not [IO.Path]::IsPathRooted($Options[$key])) { $Options[$key]=Join-Path $Context.Root $Options[$key] }
            Assert-RogNoReparse $Options[$key]
            if (-not (Test-Path -LiteralPath $Options[$key] -PathType Leaf)) { throw 'CONTROL_EVIDENCE_FILE_NOT_FOUND' }
        }
        Assert-RogCaptureRootEvidence $Options
    }
    $collector=Join-Path $Context.Root 'Collect-YMCC-CPU.ps1'
    if (-not (Test-Path -LiteralPath $collector -PathType Leaf)) { throw 'COLLECTOR_NOT_FOUND' }
    Assert-RogNoReparse $collector
    $cmd=Get-Command -Name $collector -ErrorAction Stop
    $arguments=@{ExePath=$Options.ExePath;OutDir=(Join-Path $Context.Directory 'capture');WarmupSeconds=$Options.WarmupSeconds;
        Seconds=$Options.Seconds;SampleMilliseconds=$Options.SampleMilliseconds;InventoryMilliseconds=$Options.InventoryMilliseconds;
        FanState=$Options.FanState;GamepadState=$Options.GamepadState;GyroState=$Options.GyroState;
        Label=$(if ($capture) {$Options.Label} else {'S-observed'});RequireControlEvidence=$capture;
        LifecycleJsonl=$(if($capture){$Options.LifecycleJsonl}else{''});VirtualHandshakeJsonl=$(if($capture){$Options.VirtualHandshakeJsonl}else{''});ExpectedSessionId=$(if($capture){$Options.ExpectedSessionId}else{''})}
    if ($Options.LogFile.Count -gt 0) {
        if (-not $cmd.Parameters.ContainsKey('LogFile')) { throw 'LOG_METADATA_INTERFACE_UNVERIFIED' }
        $arguments.LogFile=$Options.LogFile
    }
    foreach ($key in $arguments.Keys) { if (-not $cmd.Parameters.ContainsKey($key)) { throw 'COLLECTOR_INTERFACE_UNVERIFIED' } }
    # Identity selectors are forwarded when supported; the runner verifies again afterwards.
    foreach ($key in @('TargetPid','TargetCreationTimeUtc','ExpectedSha256')) { if ($cmd.Parameters.ContainsKey($key)) { $arguments[$key]=$Options[$key] } }
    [void](Assert-RogProcess $Options); [void](Assert-RogTarget $Options -RequireTrusted)
    $global:LASTEXITCODE=0
    & $collector @arguments | Out-Null
    $code=$LASTEXITCODE
    if ($code -ne 0) { throw 'COLLECTOR_FAILED' }
    [void](Assert-RogProcess $Options)
    $summaryPath=Join-Path $arguments.OutDir 'summary.json'
    if (-not (Test-Path -LiteralPath $summaryPath -PathType Leaf)) { throw 'COLLECTOR_SUMMARY_MISSING' }
    $summary=Read-RogJson $summaryPath 67108864
    if (-not (Test-RogTrue $summary.measurementValid)) { throw 'MEASUREMENT_NOT_VALID' }
    if ($capture -and -not (Test-RogTrue $summary.scenarioVerified)) { throw 'SCENARIO_NOT_VERIFIED' }
    return @{measurementValid=$true;scenarioVerified=($capture -and (Test-RogTrue $summary.scenarioVerified));
        scenarioClass=$(if($capture){'collector-evidence-verified-not-acceptance'}else{'unknown-observation-not-S0-not-S4-not-AB'});
        summaryRelativePath='capture/summary.json';sourceEvidenceCopied=$false;abPassed=$false;rogRegressionPassed=$false}
}
function Invoke-RogFiniteOperation($Options,$Context) {
    switch ($Options.Action) {
        'status' { return Get-RogStatus $Options $Context }
        'preflight' { return Get-RogPreflight $Options $Context.Root $Context }
        {$_ -in @('observe','capture')} { return Invoke-RogCollector $Options $Context }
        {$_ -in @('start','stop','restart')} {
            . (Join-Path $Context.Root 'Control-YMCC-Lifecycle.ps1')
            return Invoke-RogLifecycle $Options $Context
        }
        {$_ -like 'fan-*'} {
            . (Join-Path $Context.Root 'Send-YMCC-AI-Fan-Mock.ps1')
            return Invoke-RogFan $Options $Context
        }
        default { throw 'WORKER_ACTION_NOT_ALLOWED' }
    }
}
function Get-RogSafeWorkerError($Value) {
    $allowed=@(Get-RogReadCodes)+@('PERMISSION_DENIED','WORKER_HIGH_TOKEN_UNVERIFIED','USER_ACTION_AUTHORIZATION_REQUIRED',
        'TRUSTED_PRODUCT_HASH_REQUIRED','EXACT_PROCESS_IDENTITY_REQUIRED','TARGET_EXE_NOT_FOUND','TARGET_SHA256_MISMATCH',
        'CAPABILITY_UNVERIFIED','COLLECTOR_FAILED','COLLECTOR_INTERFACE_UNVERIFIED','REAL_CONTROL_AND_RUNTIME_EVIDENCE_REQUIRED',
        'CONTROL_EVIDENCE_EXACT_ROOT_UNVERIFIED','CONTROL_EVIDENCE_NOT_FRESH','CONTROL_EVIDENCE_JSON_INVALID','SCENARIO_NOT_VERIFIED',
        'MEASUREMENT_NOT_VALID','TARGET_ALREADY_RUNNING_OR_AMBIGUOUS','NORMAL_CLOSE_UNAVAILABLE_NO_FORCE',
        'NORMAL_EXIT_TIMEOUT_OWNED_TREE_NOT_CLOSED_NO_FORCE','TARGET_HIGH_TOKEN_UNVERIFIED',
        'WORKER_REQUEST_REQUIRED','WORKER_REQUEST_ABSOLUTE_PATH_REQUIRED','WORKER_REQUEST_NAMESPACE_REJECTED',
        'WORKER_REQUEST_FIELD_REJECTED','WORKER_REQUEST_INVALID','WORKER_REQUEST_EXPIRED','ELEVATED_WORKER_USER_CHANGED',
        'WORKER_RUN_ID_MISMATCH','WORKER_REQUEST_ALREADY_USED','WORKER_ACTION_NOT_ALLOWED',
        'INVALID_ACTION','OPTION_NOT_ALLOWED','STRING_PARAMETER_REQUIRED','BOOLEAN_PARAMETER_REQUIRED','INVALID_TARGET_PID',
        'INVALID_EXPECTED_SHA256','INVALID_SESSION_ID','TARGET_PATH_REQUIRED','TARGET_NOT_YMCC','REPARSE_PATH_REJECTED',
        'TARGET_CREATION_TIME_TYPE_INVALID','TARGET_CREATION_TIME_KIND_INVALID','TARGET_CREATION_TIME_FORMAT_INVALID','TARGET_PATH_DESCRIPTOR_REJECTED')
    if ($Value -is [string] -and $Value -cin $allowed) { return $Value }
    return 'WORKER_ACTION_FAILED'
}
function Invoke-RogElevated($Options,$Context) {
    if (-not $Context.ContainsKey('Result')) { $Context.Result=New-RogResult $Context }
    $d=$Context.Result.diagnostics; $d.elevation.requested=[bool]$Options.AllowElevation
    $d.elevation.authorized=($Options.AuthorizedAction -ceq $Options.Action)
    if (-not $d.elevation.authorized) { $d.elevation.decision='AUTHORIZATION_MISSING'; throw 'USER_ACTION_AUTHORIZATION_REQUIRED' }
    if (-not $Options.AllowElevation) { throw 'PRIVILEGE_REQUIRED_NOT_AUTHORIZED' }
    if ($d.elevation.attempted) { throw 'ELEVATION_ALREADY_ATTEMPTED' }
    $d.phase='worker-request'; $d.elevation.decision='AUTHORIZED_REQUEST'
    $requestPath=Join-Path $Context.Directory 'worker-request.json'
    $request=@{schemaVersion=1;nonce=[guid]::NewGuid().ToString('N');requesterSid=Get-RogPrincipalSid;
        options=$Options;createdUtc=[DateTime]::UtcNow.ToString('o');runId=$Context.RunId}
    # Options are finite/public. No private start CLI, token, lease, settings or ambient env copied.
    Write-RogNewJson $requestPath $request
    $workerPath=Join-Path $Context.Root 'Invoke-YMCC-AuthorizedWorker.ps1'; Assert-RogNoReparse $workerPath
    if (-not (Test-Path -LiteralPath $workerPath -PathType Leaf)) { throw 'AUTHORIZED_WORKER_NOT_FOUND' }
    $engineName=if ($PSVersionTable.PSEdition -eq 'Core') {'pwsh.exe'} else {'powershell.exe'}
    $engine=Join-Path $PSHOME $engineName
    $encodedPath=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($requestPath))
    $encodedWorker=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($workerPath))
    $command="& ([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('$encodedWorker'))) -RequestPath ([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('$encodedPath')))"
    $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
    $d.phase='elevation-launch'; $d.elevation.attempted=$true; $d.elevation.decision='ATTEMPTED'
    try { $worker=Start-Process -FilePath $engine -Verb RunAs -WindowStyle Hidden -WorkingDirectory $Context.Root -ArgumentList ('-NoProfile -NonInteractive -EncodedCommand '+$encoded) -PassThru -ErrorAction Stop }
    catch { $d.elevation.decision='DENIED_OR_TOOL_POLICY_REFUSED'; throw 'ELEVATION_DENIED_OR_TOOL_POLICY_REFUSED' }
    $d.elevation.workerPid=$worker.Id
    try {
        $handle=$worker.Handle # Bind this worker, never descendants or PID/name-based termination.
        $timeout=if($Options.Action -in @('preflight','status')){180000}else{($Options.WarmupSeconds+$Options.Seconds+$Options.GraceSeconds+$Options.StartWaitSeconds+$Options.TimeoutSeconds+90)*1000}
        $d.phase='worker-wait'; $d.elevation.timeoutMilliseconds=$timeout
        if (-not $worker.WaitForExit($timeout)) { $d.elevation.workerExited=$false; $d.elevation.decision='TIMED_OUT_NO_FORCE'; throw 'FINITE_WORKER_TIMEOUT_NO_FORCE' }
        $d.elevation.workerExited=$true; $d.elevation.workerExitCode=$worker.ExitCode
        if ($null -eq $worker.ExitCode) { $d.elevation.decision='EXIT_CODE_UNAVAILABLE'; throw 'WORKER_EXIT_CODE_UNAVAILABLE' }
        $d.phase='worker-proof'
        $proofPath=Join-Path $Context.Directory 'worker-result.json'
        if (-not (Test-Path -LiteralPath $proofPath -PathType Leaf)) { $d.elevation.decision='PROOF_MISSING'; throw 'WORKER_PROOF_MISSING' }
        $proof=Read-RogJson $proofPath
        if ($proof.toolVersion -isnot [string] -or $proof.toolVersion -cne 'rog-portable-runner-2' -or
            -not (Test-RogSequenceValue $proof.workerPid) -or $proof.workerPid -lt 1 -or $proof.workerPid -ne $worker.Id -or
            $proof.nonce -isnot [string] -or $proof.requesterSid -isnot [string] -or $proof.action -isnot [string] -or $proof.runId -isnot [string] -or
            $proof.nonce -cne $request.nonce -or $proof.requesterSid -cne $request.requesterSid -or
            $proof.action -cne $Options.Action -or $proof.runId -cne $Context.RunId -or
            $proof.ok -isnot [bool] -or $proof.verifiedHigh -isnot [bool] -or
            -not (Test-RogSequenceValue $proof.exitCode) -or $proof.exitCode -ne $worker.ExitCode) {
            $d.elevation.decision='PROOF_UNVERIFIED'; throw 'WORKER_PROOF_UNVERIFIED'
        }
        $d.elevation.bindingVerified=$true; $d.elevation.workerHighVerified=$proof.verifiedHigh
        $Context.Result.workerResultRelativePath='worker-result.json'
        $workerPhase=$proof.diagnostics.phase
        if ($workerPhase -is [string] -and $workerPhase -cin @('worker-claim','parameter-validation','action-authorization','target-descriptor-validation','selector-validation','target-validation','identity-gate','permission-verification','finite-operation','identity-query','completed')) { $d.elevation.workerActionPhase=$workerPhase }
        if ($Options.Action -in @('preflight','status')) {
            if (Test-RogTrue $proof.diagnostics.identity.attempted) { $d.identity.attempted=$true }
            $allowedFields=@('exePath','observedSha256','pid','creationTimeUtc','currentUserVerified')
            $d.identity.availableFields=@($proof.diagnostics.identity.availableFields|Where-Object {$_ -is [string] -and $_ -cin $allowedFields}|Select-Object -Unique)
        }
        if ($worker.ExitCode -ne 0 -or -not $proof.ok -or -not $proof.verifiedHigh) {
            $code=Get-RogSafeWorkerError $proof.errorCode
            if (-not $proof.verifiedHigh) { $code='WORKER_HIGH_TOKEN_UNVERIFIED' }
            $d.elevation.workerErrorCode=$code; $d.elevation.decision='BOUND_WORKER_FAILED'
            throw $code # Only a fixed allowlist after PID/SID/nonce/action/run binding. Never opaque details.
        }
        if ($Options.Action -in @('preflight','status')) {
            if ($proof.identityAcquired -isnot [bool]) { $d.elevation.decision='PROOF_UNVERIFIED'; throw 'WORKER_PROOF_UNVERIFIED' }
            $d.identity.attempted=$true; $d.identity.acquired=$proof.identityAcquired
            $d.identity.state=if($proof.identityAcquired){'BOUND_WORKER_IDENTITY_AVAILABLE'}else{'BOUND_WORKER_NO_ROOT'}
        }
        $d.elevation.decision='BOUND_WORKER_SUCCEEDED'; $d.phase='worker-completed'
        return @{workerPid=$worker.Id;verifiedHigh=$true;workerResultRelativePath='worker-result.json';finiteWorkerExited=$true;
            identityAcquired=$d.identity.acquired;readOnly=($Options.Action -in @('preflight','status'));rogRegressionPassed=$false}
    } finally { $worker.Dispose() }
}
function Invoke-RogEntry($InputOptions,[string]$Root,[object[]]$Extra=@(),[string]$Kind='runner') {
    $ErrorActionPreference='Stop'; $context=$null; $result=$null
    $modules=Join-Path $PSHOME 'Modules'
    if (($env:PSModulePath -split ';')[0] -ine $modules) { $env:PSModulePath=$modules+';'+$env:PSModulePath }
    try {
        $context=New-RogContext $Root ''; $result=New-RogResult $context; $context.Result=$result
        $d=$result.diagnostics; $d.phase='parameter-validation'
        $invocation=Convert-RogInvocation $Extra $Kind
        foreach ($key in $invocation.Keys) { if ($InputOptions.ContainsKey($key)) { throw 'DUPLICATE_PARAMETER' }; $InputOptions[$key]=$invocation[$key] }
        if ($InputOptions.Action -is [string] -and $InputOptions.Action -in @('validate','preflight','status','start','stop','restart','observe','capture','fan-probe','fan-on','fan-off','fan-close','fan-status')) { $result.action=$InputOptions.Action }
        $d.elevation.requested=($InputOptions.AllowElevation -eq $true)
        $d.elevation.authorized=($InputOptions.AuthorizedAction -is [string] -and $InputOptions.AuthorizedAction -ceq $result.action)
        $d.identity.inputFieldsPresent=@(@('ExePath','TargetPid','TargetCreationTimeUtc')|Where-Object {$InputOptions.ContainsKey($_)})
        $birthInput=$InputOptions.TargetCreationTimeUtc
        if ($null -ne $birthInput -and -not ($birthInput -is [string] -and [string]::IsNullOrWhiteSpace($birthInput))) {
            $d.identity.requestedBirthState=if($birthInput -is [string] -or $birthInput -is [DateTime] -or $birthInput -is [DateTimeOffset]){'SUPPLIED'}else{'TYPE_INVALID'}
        }
        $o=Get-RogOptions $InputOptions $Root; $result.action=$o.Action
        if (-not $o.SessionId) { $o.SessionId=[guid]::NewGuid().ToString() }; $result.sessionId=$o.SessionId
        $d.readOnly=($o.Action -in @('validate','preflight','status')); $d.identity.discoveryOnly=(-not [bool]$o.ExpectedSha256)
        if ($o.Action -eq 'validate') {
            $d.phase='tool-validation'; $result.details=Get-RogToolValidation $Root
            if (-not $result.details.valid) { throw 'PORTABLE_TOOLSET_INCOMPLETE_OR_INVALID' }
        } else {
            # Explicit elevation always needs exact action authorization, including observe/capture/read-only.
            if ($o.AllowElevation -and $o.AuthorizedAction -cne $o.Action) {
                $d.phase='elevation-authorization'; $d.elevation.decision='AUTHORIZATION_MISSING'; throw 'USER_ACTION_AUTHORIZATION_REQUIRED'
            }
            $readOnly=($o.Action -in @('preflight','status'))
            $needsSelector=($o.Action -in @('observe','capture','stop','restart') -or $o.Action -like 'fan-*')
            if (-not $readOnly -and ($o.Action -in @('start','stop','restart') -or $o.Action -like 'fan-*')) {
                $d.phase='action-authorization'; Assert-RogAuthorized $o; $result.sideEffectsRequested=$true
            }
            $d.phase='target-descriptor-validation'
            $result.facts.target=Assert-RogTargetDescriptor $o -RequireTrusted:(-not $readOnly)
            if ($needsSelector) { $d.phase='selector-validation'; Assert-RogProcessSelector $o }
            $d.phase='permission-verification'; $privilege=Get-RogDiagnosticPrivilege $context
            if ($o.AllowElevation -and -not (Test-RogTrue $privilege.verifiedHigh)) {
                # Only descriptor/selector facts exist here. All real target gates run in the high worker.
                $result.details=Invoke-RogElevated $o $context
            } else {
                if ($o.AllowElevation) { $d.elevation.decision='ALREADY_HIGH_NO_WORKER' }
                $d.phase='target-validation'; $result.facts.target=Assert-RogTarget $o -RequireTrusted:(-not $readOnly)
                if ($needsSelector) { $d.phase='identity-gate'; [void](Assert-RogProcess $o) }
                if (-not $readOnly -and ($o.Action -in @('start','stop','restart') -or $o.Action -like 'fan-*') -and -not (Test-RogTrue $privilege.verifiedHigh)) {
                    throw 'PRIVILEGE_REQUIRED_NOT_AUTHORIZED'
                }
                $d.phase='finite-operation'; $result.details=Invoke-RogFiniteOperation $o $context
            }
        }
        $result.ok=$true; $result.exitCode=0; $d.phase='completed'
    } catch {
        if ($null -eq $result) { $result=[ordered]@{schemaVersion=1;toolVersion='rog-portable-runner-2';ok=$false;exitCode=1;rogRegressionPassed=$false;errorCode='RESULT_BOOTSTRAP_FAILED'} }
        else {
            $result.errorCode=Get-RogCode $_; $result.exitCode=1
            if ($result.errorCode -eq 'CAPABILITY_UNVERIFIED' -and $null -eq $result.details) { $result.details=@{capabilityStatus='CAPABILITY_UNVERIFIED';remediation='PRODUCT_UPDATE_REQUIRED';absenceClaimed=$false} }
        }
    }
    if ($context) {
        try { Write-RogResult $context $result; Write-Output ('ROG_RESULT '+(Join-Path $context.Directory 'result.json')) }
        catch { Write-Output '{"schemaVersion":1,"toolVersion":"rog-portable-runner-2","ok":false,"exitCode":2,"errorCode":"RESULT_WRITE_FAILED","rogRegressionPassed":false}'; return 2 }
    } else { Write-Output ($result | ConvertTo-Json -Compress) }
    return [int]$result.exitCode
}
if ($MyInvocation.InvocationName -ne '.') {
    $outcome=@(Invoke-RogEntry @{} $PSScriptRoot @($args))
    $code=[int]$outcome[-1]; if ($outcome.Count -gt 1) { $outcome[0..($outcome.Count-2)] | Write-Output }; exit $code
}