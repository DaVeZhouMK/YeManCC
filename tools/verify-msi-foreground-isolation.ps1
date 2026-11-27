[CmdletBinding()]
param([string]$OutputRoot,[switch]$CompileProduction,[ValidateSet('All','Priority','NativeOff','NativeVisibility','NativeHandoff')][string]$CaseFilter='All',
    [ValidateSet('Optimized','Unoptimized')][string]$BuildProfile='Optimized',[string[]]$PreHostWriterSelection=@('all'),[long]$DeadlineEpochMs=0)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$preHostWriter=$PSBoundParameters.ContainsKey('PreHostWriterSelection')
# Reject the same incompatible native selection before snapshot/compiler work.
$inheritedPreHostCase=[Environment]::GetEnvironmentVariable('YMCC_GPD_PREHOST_WRITER_CASE','Process')
if(!$preHostWriter -and $null -ne $inheritedPreHostCase){
    if($CaseFilter -ne 'Priority'){throw 'Inherited YMCC_GPD_PREHOST_WRITER_CASE requires Priority or an explicit prehost-writer scope'}
    . (Join-Path $PSScriptRoot 'gpd_prehost_writer_cases.ps1')
    $null=Get-GpdPreHostWriterSelection -HeaderPath "$root\tools\msi_native_foreground_regression.h" -Selection @($inheritedPreHostCase)
}
if(!$preHostWriter -and ($BuildProfile -ne 'Optimized' -or $PSBoundParameters.ContainsKey('PreHostWriterSelection') -or $DeadlineEpochMs)){
    throw 'BuildProfile and DeadlineEpochMs require an explicit PreHostWriterSelection'
}
if($preHostWriter){
    if($CaseFilter -ne 'Priority'){throw 'PreHostWriterSelection requires the original Priority CaseFilter'}
    if($CompileProduction){throw 'PreHostWriter is an offline fixture selection, not a product build'}
    . (Join-Path $PSScriptRoot 'gpd_prehost_writer_cases.ps1')
    $null=Get-GpdPreHostWriterSelection -HeaderPath "$root\tools\msi_native_foreground_regression.h" -Selection $PreHostWriterSelection
    if(!$DeadlineEpochMs){$DeadlineEpochMs=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()+240000}
    if($DeadlineEpochMs -le [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()+10000){throw 'No PreHostWriter preparation and cleanup budget'}
    if($OutputRoot -and (Test-Path -LiteralPath $OutputRoot)){throw 'PreHostWriter requires a fresh OutputRoot'}
}
if([string]::IsNullOrWhiteSpace($OutputRoot)){$OutputRoot=Join-Path $root '..\..\Build\Validation\msi-foreground-isolation-20261011\native-entry'}
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Path $OutputRoot -Force|Out-Null
$snapshot=Join-Path $OutputRoot 'source-snapshot'
$nativeSnapshot=Join-Path $snapshot 'native'
New-Item -ItemType Directory -Path $nativeSnapshot,"$snapshot\tools","$snapshot\deps\json" -Force|Out-Null
$vcvars='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$sdkRoot='C:\Program Files (x86)\Windows Kits\10'
$compilerCommand='cl.exe'
if($preHostWriter){
    # The explicit offline scope chooses fixed tools; it never inherits vcvars selection.
    $preHostToolchainFile=Join-Path $PSScriptRoot 'ymcc_sim\global\controller-foreground-latency\prehost-toolchain-inputs.json'
    $preHostToolchain=Get-Content -LiteralPath $preHostToolchainFile -Raw|ConvertFrom-Json
    if($preHostToolchain.schemaVersion -ne 1 -or $preHostToolchain.kind -cne 'ymcc-prehost-fixed-toolchain-v1' -or $preHostToolchain.architecture -cne 'x64'){
        throw 'Invalid fixed pre-Host toolchain declaration'
    }
    foreach($name in @('msvcRoot','sdkRoot','windowsRoot','powershell','node')){
        $value=$preHostToolchain.$name
        if($value -isnot [string] -or ![IO.Path]::IsPathFullyQualified($value) -or $value -match '["%\r\n]'){
            throw "Invalid fixed pre-Host path: $name"
        }
    }
    if($preHostToolchain.sdkVersion -notmatch '^\d+(?:\.\d+){3}$'){throw 'Invalid fixed SDK version'}
    $preHostMsvc=[IO.Path]::GetFullPath($preHostToolchain.msvcRoot)
    $sdkRoot=[IO.Path]::GetFullPath($preHostToolchain.sdkRoot)
    $preHostBin=Join-Path $preHostMsvc 'bin\Hostx64\x64'
    $preHostExpectedTools=@{compiler=(Join-Path $preHostBin 'cl.exe');linker=(Join-Path $preHostBin 'link.exe')
        powershell=([IO.Path]::GetFullPath($preHostToolchain.powershell));node=([IO.Path]::GetFullPath($preHostToolchain.node))}
    $preHostSeenRoles=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach($tool in @($preHostToolchain.toolPins)){
        if(!$preHostExpectedTools.ContainsKey([string]$tool.role) -or !$preHostSeenRoles.Add([string]$tool.role) -or
            $tool.path -isnot [string] -or ![IO.Path]::IsPathFullyQualified($tool.path) -or $tool.sha256 -cnotmatch '^[a-f0-9]{64}$'){
            throw 'Invalid fixed pre-Host tool pin'
        }
        if(![string]::Equals([IO.Path]::GetFullPath($tool.path),$preHostExpectedTools[$tool.role],[StringComparison]::OrdinalIgnoreCase) -or
            (Get-FileHash -LiteralPath $tool.path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $tool.sha256){
            throw "Fixed pre-Host tool identity changed: $($tool.role)"
        }
    }
    if($preHostSeenRoles.Count -ne 4){throw 'All four fixed pre-Host tool roles are required'}
    $preHostActualEngine=[Diagnostics.Process]::GetCurrentProcess().Path
    if(!$preHostActualEngine -or ![string]::Equals([IO.Path]::GetFullPath($preHostActualEngine),$preHostExpectedTools.powershell,[StringComparison]::OrdinalIgnoreCase)){
        throw 'Explicit pre-Host scope requires its declared PowerShell engine'
    }
    $sdkVersion=Get-Item -LiteralPath (Join-Path $sdkRoot ('Include\'+$preHostToolchain.sdkVersion))
    foreach($directory in @('ucrt','shared','um','winrt','cppwinrt')){
        if(!(Test-Path -LiteralPath (Join-Path $sdkVersion.FullName $directory) -PathType Container)){throw "Fixed SDK include missing: $directory"}
    }
    foreach($file in @((Join-Path $sdkVersion.FullName 'um\Windows.h'),(Join-Path $sdkRoot ('Lib\'+$sdkVersion.Name+'\um\x64\kernel32.lib')))){
        if(!(Test-Path -LiteralPath $file -PathType Leaf)){throw "Fixed SDK file missing: $file"}
    }
    $env:VCToolsInstallDir=$preHostMsvc
    $env:VCToolsVersion=Split-Path -Leaf $preHostMsvc
    $env:SystemRoot=[IO.Path]::GetFullPath($preHostToolchain.windowsRoot);$env:WINDIR=$env:SystemRoot
    $env:SystemDrive=[IO.Path]::GetPathRoot($env:SystemRoot).TrimEnd('\')
    $env:ComSpec=Join-Path $env:SystemRoot 'System32\cmd.exe'
    $env:PATH=(@($preHostBin,(Split-Path -Parent $preHostExpectedTools.powershell),(Join-Path $env:SystemRoot 'System32'),$env:SystemRoot) -join ';')
    $env:PSModulePath=Join-Path (Split-Path -Parent $preHostExpectedTools.powershell) 'Modules'
    $env:LIBPATH=(@((Join-Path $sdkRoot ('Lib\'+$sdkVersion.Name+'\ucrt\x64')),(Join-Path $sdkRoot ('Lib\'+$sdkVersion.Name+'\um\x64')),(Join-Path $preHostMsvc 'lib\x64')) -join ';')
    foreach($name in @('CL','_CL_','LINK','_LINK_')){[Environment]::SetEnvironmentVariable($name,$null,'Process')}
    $env:VSLANG='1033'
    $preHostPrivate=Join-Path $OutputRoot 'prehost-environment'
    foreach($entry in @(@('TEMP','tmp'),@('TMP','tmp'),@('USERPROFILE','profile'),@('APPDATA','profile\AppData\Roaming'),@('LOCALAPPDATA','profile\AppData\Local'),@('YEMAN_POWER_CONTROL_DIR','PowerControl'))){
        $directory=Join-Path $preHostPrivate $entry[1]
        $null=New-Item -ItemType Directory -Path $directory -Force
        [Environment]::SetEnvironmentVariable($entry[0],$directory,'Process')
    }
    $compilerCommand=$preHostExpectedTools.compiler
}else{
    $lines=& $env:ComSpec /d /s /c "`"$vcvars`" >nul && set"
    if($LASTEXITCODE -ne 0){throw 'vcvars64 failed'}
    foreach($line in $lines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
    $sdkVersion=Get-ChildItem -LiteralPath "$sdkRoot\Include" -Directory|Where-Object{
        (Test-Path -LiteralPath "$($_.FullName)\um\Windows.h") -and
        (Test-Path -LiteralPath "$sdkRoot\Lib\$($_.Name)\um\x64\kernel32.lib")
    }|Sort-Object { [version]$_.Name } -Descending|Select-Object -First 1
    if(!$sdkVersion){throw 'Complete Windows SDK not found'}
}
$env:WindowsSdkDir="$sdkRoot\";$env:WindowsSDKVersion="$($sdkVersion.Name)\"
$env:INCLUDE=(@('ucrt','shared','um','winrt','cppwinrt')|ForEach-Object{"$sdkRoot\Include\$($sdkVersion.Name)\$_"}) -join ';'
$env:INCLUDE+=";$env:VCToolsInstallDir\include"
$env:LIB="$sdkRoot\Lib\$($sdkVersion.Name)\ucrt\x64;$sdkRoot\Lib\$($sdkVersion.Name)\um\x64;$env:VCToolsInstallDir\lib\x64"
$env:PATH="$env:SystemRoot\System32;$sdkRoot\bin\$($sdkVersion.Name)\x64;$env:PATH"
$winrt=Join-Path $env:WindowsSdkDir "Include\$($env:WindowsSDKVersion.TrimEnd('\'))\cppwinrt"
$winapp=Join-Path $env:WindowsSdkDir "Lib\$($env:WindowsSDKVersion.TrimEnd('\'))\um\x64\WindowsApp.lib"
Copy-Item -LiteralPath "$root\native\main.cpp" -Destination "$nativeSnapshot\main.cpp"
Copy-Item -LiteralPath "$root\native\dsu_server.cpp" -Destination "$nativeSnapshot\dsu_server.cpp"
Copy-Item -LiteralPath "$root\tools\msi_foreground_isolation_native_regression.cpp" -Destination "$snapshot\tools\msi_foreground_isolation_native_regression.cpp"
foreach($header in Get-ChildItem -LiteralPath "$root\native" -File -Recurse){
    $destination=Join-Path $nativeSnapshot ([IO.Path]::GetRelativePath("$root\native",$header.FullName))
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force|Out-Null
    Copy-Item -LiteralPath $header.FullName -Destination $destination
}
Copy-Item -LiteralPath "$root\tools\msi_native_foreground_regression.h" -Destination "$snapshot\tools\msi_native_foreground_regression.h"
Copy-Item -LiteralPath "$root\tools\native_visibility_write_checks.h" -Destination "$snapshot\tools\native_visibility_write_checks.h"
Copy-Item -LiteralPath "$root\tools\gpd_native_handoff_cycle_checks.h" -Destination "$snapshot\tools\gpd_native_handoff_cycle_checks.h"
Copy-Item -LiteralPath "$root\tools\gpd_native_handoff_cycle_legacy.h" -Destination "$snapshot\tools\gpd_native_handoff_cycle_legacy.h"
Copy-Item -LiteralPath "$root\tools\gpd_native_handoff_cycle_partial_before.h" -Destination "$snapshot\tools\gpd_native_handoff_cycle_partial_before.h"
Copy-Item -LiteralPath "$root\deps\json\json.hpp" -Destination "$snapshot\deps\json\json.hpp"
Copy-Item -LiteralPath "$root\capabilities" -Destination "$snapshot\capabilities" -Recurse
Get-ChildItem -LiteralPath $snapshot -File -Recurse|ForEach-Object{Get-FileHash -LiteralPath $_.FullName}|Select-Object Path,Hash|ConvertTo-Json|Set-Content -LiteralPath "$OutputRoot\compiled-snapshot-hashes.json" -Encoding utf8
$optimization=if($BuildProfile -eq 'Unoptimized'){'/Od'}else{'/O2'}
$common=@('/nologo','/std:c++20','/utf-8','/EHsc','/MT',$optimization,'/bigobj','/DNDEBUG','/DUNICODE','/D_UNICODE','/DYMCC_SCREEN_NATIVE_INPUT_CANDIDATE=1')
$include=@("/I$root\deps\webview2\build\native\include","/I$snapshot\deps\json","/I$winrt","/I$nativeSnapshot","/I$snapshot\tools")
@('cl.exe','link.exe')|ForEach-Object{Get-Command $_|Select-Object -ExpandProperty Source}|ForEach-Object{Get-FileHash -LiteralPath $_}|Select-Object Path,Hash|ConvertTo-Json|Set-Content -LiteralPath "$OutputRoot\compiler-tools.json" -Encoding utf8
$regressionCompileArguments=@($common)+@('/c',"$snapshot\tools\msi_foreground_isolation_native_regression.cpp")+@($include)+@("/Fo$OutputRoot\regression.obj",'/sourceDependencies',"$OutputRoot\regression-dependencies.json")
$dsuCompileArguments=@($common)+@('/c',"$nativeSnapshot\dsu_server.cpp","/Fo$OutputRoot\dsu.obj",'/sourceDependencies',"$OutputRoot\dsu-dependencies.json")
$recoveryCompileArguments=@($common)+@('/c',"$nativeSnapshot\steam_original_recovery_engine.cpp")+@($include)+@("/Fo$OutputRoot\steam-recovery.obj",'/sourceDependencies',"$OutputRoot\steam-recovery-dependencies.json")
$libs=@("$root\deps\webview2\build\native\x64\WebView2LoaderStatic.lib",$winapp,'user32.lib','gdi32.lib','shell32.lib','shlwapi.lib','ole32.lib','oleaut32.lib','dwmapi.lib','winhttp.lib','advapi32.lib','shcore.lib','version.lib','psapi.lib','PowrProf.lib','ws2_32.lib')
$exe=Join-Path $OutputRoot 'msi-foreground-native-regression.exe'
$regressionLinkArguments=@('/nologo',"$OutputRoot\regression.obj","$OutputRoot\dsu.obj","$OutputRoot\steam-recovery.obj","/Fe$exe",'/link','/SUBSYSTEM:CONSOLE','/MACHINE:x64')+@($libs)
if($preHostWriter){
    # Observe this original build only; these pins do not authorize a tool cache.
    function Get-PreHostBuildFilePin([string]$Path){
        $file=Get-Item -LiteralPath $Path -ErrorAction Stop
        if($file.PSIsContainer -or ![IO.Path]::IsPathFullyQualified($file.FullName)){throw "Expected an absolute build file: $Path"}
        [ordered]@{path=$file.FullName;bytes=$file.Length;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash}
    }
    function Write-PreHostBuildJson([string]$Path,$Value){
        $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value|ConvertTo-Json -Depth 12))
        $stream=[IO.FileStream]::new($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
        try{$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
    }
    $preHostTools=[ordered]@{}
    foreach($toolName in @('cl.exe','link.exe')){
        $command=Get-Command -Name $toolName -ErrorAction Stop
        if($command.CommandType -ne 'Application'){throw "PreHostWriter requires the original native tool: $toolName"}
        $preHostTools[$toolName]=Get-PreHostBuildFilePin $command.Source
    }
    $preHostEnvironment=[ordered]@{}
    foreach($name in @('PATH','INCLUDE','LIB','LIBPATH','CL','_CL_','LINK','_LINK_','TEMP','TMP','SystemRoot','SystemDrive','ComSpec',
        'PATHEXT','VCToolsInstallDir','VCToolsVersion','VCINSTALLDIR','VSINSTALLDIR','VisualStudioVersion','VSCMD_VER',
        'VSCMD_ARG_HOST_ARCH','VSCMD_ARG_TGT_ARCH','VSCMD_ARG_APP_PLAT','WindowsSdkDir','WindowsSDKVersion','WindowsSDKLibVersion',
        'WindowsSdkBinPath','WindowsSdkVerBinPath','UniversalCRTSdkDir','UCRTVersion','ExtensionSdkDir','Platform','PreferredToolArchitecture',
        'PROCESSOR_ARCHITECTURE','PROCESSOR_ARCHITEW6432','NUMBER_OF_PROCESSORS','VSLANG','PSModulePath','USERPROFILE','APPDATA','LOCALAPPDATA')){
        $preHostEnvironment[$name]=[Environment]::GetEnvironmentVariable($name,'Process')
    }
    $preHostProcess=Get-Process -Id $PID -ErrorAction Stop
    $preHostVersions=[ordered]@{}
    foreach($entry in $PSVersionTable.GetEnumerator()){$preHostVersions[$entry.Key]=[string]$entry.Value}
    $preHostNativePreferences=[ordered]@{}
    foreach($name in @('PSNativeCommandArgumentPassing','PSNativeCommandUseErrorActionPreference')){
        $variable=Get-Variable -Name $name -ErrorAction SilentlyContinue
        $preHostNativePreferences[$name]=if($null -ne $variable){$variable.Value}else{$null}
    }
    $regressionLinkArguments+=@('/VERBOSE:LIB')
    $preHostLinkSearchLog=Join-Path $OutputRoot 'link-search.log'
    Write-PreHostBuildJson (Join-Path $OutputRoot 'build-environment.json') ([ordered]@{
        schemaVersion=1;scope='explicit-prehost-writer';capturedAtUtc=[DateTimeOffset]::UtcNow.ToString('o')
        cacheToolClosureAccepted=$false;completeLinkGraph=$false;toolCacheVerified=$false
        toolSelection=[ordered]@{kind=$preHostToolchain.kind;declaration=(Get-PreHostBuildFilePin $preHostToolchainFile);vcvarsExecuted=$false;automaticSdkSelection=$false}
        environmentScope='Fixed pre-Host tool/SDK variables and private native profile; not a complete process/tool closure'
        environment=$preHostEnvironment;workingDirectory=(Get-Location).Path;processWorkingDirectory=[Environment]::CurrentDirectory
        vcvars64=[ordered]@{executed=$false;requiredByFixedScope=$false};sdkRoot=$sdkRoot;sdkVersion=$sdkVersion.Name
        tools=$preHostTools;toolPinScope='Get-Command resolution; the child linker and dynamically loaded tools are not attested'
        powerShell=[ordered]@{executable=(Get-PreHostBuildFilePin $preHostProcess.Path);pid=$PID;startTimeUtc=$preHostProcess.StartTime.ToUniversalTime().ToString('o')
            psHome=$PSHOME;versions=$preHostVersions;hostName=$Host.Name;hostVersion=$Host.Version.ToString();is64BitProcess=[Environment]::Is64BitProcess
            nativePreferences=$preHostNativePreferences;errorActionPreference=[string]$ErrorActionPreference
            outputEncoding=$OutputEncoding.WebName;consoleOutputEncoding=[Console]::OutputEncoding.WebName}
        buildProfile=$BuildProfile;caseFilter=$CaseFilter;preHostWriterSelection=@($PreHostWriterSelection);deadlineEpochMs=$DeadlineEpochMs
        compileCommands=@(
            [ordered]@{executable=$preHostTools['cl.exe'].path;arguments=$regressionCompileArguments},
            [ordered]@{executable=$preHostTools['cl.exe'].path;arguments=$dsuCompileArguments},
            [ordered]@{executable=$preHostTools['cl.exe'].path;arguments=$recoveryCompileArguments})
        linkCommand=[ordered]@{executable=$preHostTools['cl.exe'].path;arguments=$regressionLinkArguments;stdoutLog=$preHostLinkSearchLog
            stdoutEncoding='utf-8';stdoutScope='PowerShell-decoded stdout text; stderr is not merged'
            exitCodeSource='LASTEXITCODE of the original cl.exe /link command; no separate child link.exe exit observation'}
    })
}
& $compilerCommand @regressionCompileArguments
if($LASTEXITCODE -ne 0){throw 'MSI foreground full production TU regression compile failed'}
& $compilerCommand @dsuCompileArguments
if($LASTEXITCODE -ne 0){throw 'DSU compile failed'}
& $compilerCommand @recoveryCompileArguments
if($LASTEXITCODE -ne 0){throw 'Steam recovery link dependency compile failed'}
if($preHostWriter){
    # CreateNew preserves prior raw logs. Only stdout is copied; stderr stays on its original stream.
    $preHostLogStream=[IO.FileStream]::new($preHostLinkSearchLog,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    $preHostLogWriter=[IO.StreamWriter]::new($preHostLogStream,[Text.UTF8Encoding]::new($false))
    $preHostLinkExitCode=$null;$preHostLinkFailure=$null;$preHostEvidenceFailure=$null
    $global:LASTEXITCODE=$null
    try{
        & $compilerCommand @regressionLinkArguments | ForEach-Object{$preHostLogWriter.WriteLine([string]$_);$_}
        $preHostLinkExitCode=$global:LASTEXITCODE
    }catch{$preHostLinkExitCode=$global:LASTEXITCODE;$preHostLinkFailure=$_}
    finally{try{$preHostLogWriter.Dispose()}catch{$preHostEvidenceFailure=$_}}
    $preHostLogWriteError=if($preHostEvidenceFailure){$preHostEvidenceFailure.Exception.Message}else{$null}
    $preHostLogPresent=$false;$preHostLogPin=$null;$preHostObservationFailure=$null
    $preHostLibraryPins=[Collections.Generic.List[object]]::new()
    $preHostLibraryPaths=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $preHostLibraryPinErrors=[Collections.Generic.List[object]]::new()
    try{
        $preHostLogPresent=Test-Path -LiteralPath $preHostLinkSearchLog -PathType Leaf
        if($preHostLogPresent){
            $preHostLogPin=Get-PreHostBuildFilePin $preHostLinkSearchLog
            # Match absolute drive/UNC paths actually printed, independent of the linker's UI language.
            $libraryPattern='(?i)(?<path>(?:[a-z]:[\\/]|\\\\[^\\/\s]+[\\/][^\\/\s]+[\\/])[^<>:"|?*\r\n]*?\.lib)(?=$|[\s:")\(\]])'
            foreach($logLine in [IO.File]::ReadLines($preHostLinkSearchLog)){
                foreach($match in [regex]::Matches($logLine,$libraryPattern)){
                    $observedPath=$match.Groups['path'].Value
                    $libraryPath=[IO.Path]::GetFullPath($observedPath)
                    if($preHostLibraryPaths.Add($libraryPath)){
                        try{$preHostLibraryPins.Add((Get-PreHostBuildFilePin $libraryPath))}
                        catch{$preHostLibraryPinErrors.Add([ordered]@{path=$libraryPath;observedPath=$observedPath;error=$_.Exception.Message})}
                    }
                }
            }
        }
    }catch{$preHostObservationFailure=$_;if(!$preHostEvidenceFailure){$preHostEvidenceFailure=$_}}
    $preHostObservationStatus=if($preHostObservationFailure){'LINK_OBSERVATION_ERROR'}elseif(!$preHostLogPresent){'MISSING_LINK_LOG'}elseif(!$preHostLibraryPaths.Count){'NO_ABSOLUTE_LIBRARY_PATHS'}elseif($preHostLibraryPinErrors.Count){'OBSERVED_LIBRARY_PIN_INCOMPLETE'}else{'OBSERVED_ABSOLUTE_LIBRARY_PATHS_ONLY'}
    $preHostClosureReasons=@('VERBOSE:LIB lists observed search paths, not a complete link graph or a verified tool cache')
    if(!$preHostLogPresent){$preHostClosureReasons+='Missing link-search.log'}
    if(!$preHostLibraryPaths.Count){$preHostClosureReasons+='No absolute .lib paths observed in the actual link stdout'}
    if($preHostLibraryPinErrors.Count){$preHostClosureReasons+='One or more observed libraries could not be pinned'}
    if($preHostObservationFailure){$preHostClosureReasons+='Link stdout observation failed; the captured link exit code is retained'}
    if($null -eq $preHostLinkExitCode -or $preHostLinkExitCode -ne 0 -or $preHostLinkFailure){$preHostClosureReasons+='Link command failed or did not provide a normal exit code'}
    try{
        Write-PreHostBuildJson (Join-Path $OutputRoot 'link-observed-libraries.json') ([ordered]@{
            schemaVersion=1;scope='explicit-prehost-writer';cacheToolClosureAccepted=$false;completeLinkGraph=$false;toolCacheVerified=$false
            observationStatus=$preHostObservationStatus;closureNotGrantedReasons=$preHostClosureReasons
            linkExitCode=$preHostLinkExitCode;exitCodeSource='Original cl.exe /link command LASTEXITCODE';childLinkerExitCodeObserved=$false
            linkInvocationError=if($preHostLinkFailure){$preHostLinkFailure.Exception.Message}else{$null}
            logWriteError=$preHostLogWriteError
            observationError=if($preHostObservationFailure){$preHostObservationFailure.Exception.Message}else{$null}
            stdoutLogPresent=$preHostLogPresent;stdoutLog=$preHostLogPin;observedAbsolutePathCount=$preHostLibraryPaths.Count
            libraries=@($preHostLibraryPins.ToArray()|Sort-Object path);libraryPinErrors=@($preHostLibraryPinErrors.ToArray())
        })
    }catch{$preHostEvidenceFailure=$_}
    # Evidence errors cannot turn a failed native command into success or replace its exit code.
    $LASTEXITCODE=$preHostLinkExitCode
    if($preHostLinkFailure){throw $preHostLinkFailure}
    if($null -eq $preHostLinkExitCode){throw 'MSI foreground regression link returned no exit code'}
    if($preHostEvidenceFailure){
        if($preHostLinkExitCode -ne 0){throw "MSI foreground regression link failed (exit $preHostLinkExitCode); evidence error: $($preHostEvidenceFailure.Exception.Message)"}
        throw $preHostEvidenceFailure
    }
}else{
    & $compilerCommand @regressionLinkArguments
}
if($LASTEXITCODE -ne 0){throw 'MSI foreground regression link failed'}
$caseArguments=@()
if($CaseFilter -eq 'Priority'){$caseArguments=@('--shield-priority-probes')}
if($CaseFilter -eq 'NativeOff'){$caseArguments=@('--shield-native-off-probes')}
if($CaseFilter -eq 'NativeVisibility'){$caseArguments=@('--native-visibility-write')}
if($CaseFilter -eq 'NativeHandoff'){$caseArguments=@('--native-handoff-cycle')}
if($preHostWriter){
    $moduleGate=[IO.FileStream]::new((Join-Path $OutputRoot 'module-package.json'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    try{$moduleBytes=[Text.UTF8Encoding]::new($false).GetBytes('{}');$moduleGate.Write($moduleBytes,0,$moduleBytes.Length);$moduleGate.Flush($true)}finally{$moduleGate.Dispose()}
    Invoke-GpdPreHostWriterCases -Executable $exe -HeaderPath "$snapshot\tools\msi_native_foreground_regression.h" -GuardPath "$root\tools\ymcc_sim\global\host-process.ps1" -OutputRoot (Join-Path $OutputRoot 'prehost-writer') -DeadlineEpochMs $DeadlineEpochMs -Selection $PreHostWriterSelection
}else{
    & $exe "$OutputRoot\fixture-data" @caseArguments | Tee-Object -FilePath "$OutputRoot\native-regression-results.txt"
    if($LASTEXITCODE -ne 0){throw 'MSI foreground production helper regression failed'}
}
$productionExe=$null
if($CompileProduction){
    & cl.exe @common /c "$nativeSnapshot\main.cpp" @include "/Fo$OutputRoot\main-production.obj"
    if($LASTEXITCODE -ne 0){throw 'Non-test production compile failed'}
    Push-Location "$root\native"
    try{& rc.exe /nologo "/fo$OutputRoot\app-production.res" app.rc;if($LASTEXITCODE -ne 0){throw 'Production resources failed'}}finally{Pop-Location}
    $productionExe=Join-Path $OutputRoot 'YeManCC-MSI-foreground-candidate.exe'
    & cl.exe /nologo "$OutputRoot\main-production.obj" "$OutputRoot\dsu.obj" "$OutputRoot\steam-recovery.obj" "$OutputRoot\app-production.res" "/Fe$productionExe" /link /SUBSYSTEM:WINDOWS /MACHINE:x64 @libs
    if($LASTEXITCODE -ne 0){throw 'Non-test production link failed'}
    & mt.exe -manifest "$root\native\app.manifest" ('-outputresource:'+$productionExe+';#1')
    if($LASTEXITCODE -ne 0){throw 'Production admin manifest failed'}
}
foreach($file in @('msi_foreground_isolation.h','msi_foreground_isolation_runtime.h','msi_native_foreground_isolation.h','msi_native_foreground_isolation_runtime.h','native_foreground_source_policy.h')){
    if((Get-FileHash -LiteralPath "$root\native\$file").Hash -ne (Get-FileHash -LiteralPath "$nativeSnapshot\$file").Hash){throw "Source changed during verification: $file"}
}
# A passing fixed snapshot is not automatically the latest mainline. Account
# for parallel edits instead of silently calling an older candidate current.
$changedSourceFiles=@()
foreach($copied in Get-ChildItem -LiteralPath $snapshot -File -Recurse){
    $sourcePath=Join-Path $root ([IO.Path]::GetRelativePath($snapshot,$copied.FullName))
    if(!(Test-Path -LiteralPath $sourcePath) -or (Get-FileHash -LiteralPath $sourcePath).Hash -ne (Get-FileHash -LiteralPath $copied.FullName).Hash){$changedSourceFiles+=$sourcePath}
}
[ordered]@{passed=$true;actualProductionTu=$true;buildProfile=$BuildProfile;caseFilter=$CaseFilter;preHostWriter=$preHostWriter;testIoOnly=$true;realHardwareAccess=$false;productionCompiled=[bool]$CompileProduction;productionArtifact=$productionExe;productLaunched=$false;installedFilesModified=$false;sourceSnapshot=$snapshot;compiledSourceMatchesCurrent=($changedSourceFiles.Count -eq 0);changedSourceFiles=$changedSourceFiles} | ConvertTo-Json|Set-Content -LiteralPath "$OutputRoot\verification-summary.json" -Encoding utf8
Write-Output "MSI_FOREGROUND_NATIVE_REGRESSION_OK $OutputRoot"
