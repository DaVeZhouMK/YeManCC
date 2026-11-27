# Shared execution of the original production-TU selections. No fixture model.
function Get-GpdPreHostWriterSelection {
    param([Parameter(Mandatory)][string]$HeaderPath,[string[]]$Selection=@('all'))
    $text=[IO.File]::ReadAllText($HeaderPath)
    $start=$text.IndexOf('    if(preHostWriterCase){',[StringComparison]::Ordinal)
    if($start -lt 0){throw 'GPD pre-Host fixture is not integrated in this source'}
    $end=$text.IndexOf("`n        g_gpdPreHostFixtureTick=0;",$start,[StringComparison]::Ordinal)
    if($end -lt 0){throw 'GPD pre-Host fixture boundary is missing'}
    $body=$text.Substring($start,$end-$start)
    $allowedMatch=[regex]::Match($body,'allowed=\{(?<items>.*?)\};','Singleline')
    if(!$allowedMatch.Success){throw 'GPD pre-Host original selections are missing'}
    $allowed=@([regex]::Matches($allowedMatch.Groups['items'].Value,'"([^"]+)"')|ForEach-Object{$_.Groups[1].Value})
    if(!$Selection -or ($Selection.Count -eq 1 -and [string]::IsNullOrWhiteSpace($Selection[0]))){throw 'Empty GPD pre-Host selection'}
    if($Selection.Count -eq 1 -and $Selection[0] -ceq 'all'){$Selection=$allowed}
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach($item in $Selection){if(($allowed -cnotcontains $item -and $item -cne 'win5-return-exit2') -or !$seen.Add($item)){throw ('Unknown or duplicate GPD pre-Host selection: '+$item)}}
    $positive=@([regex]::Matches($body,'"(GPD (?:prehost|firstqueue) [^"\r\n]+)"')|ForEach-Object{$_.Groups[1].Value}|Where-Object{
        $_ -cnotin @('GPD prehost live writer refuses settle and inverse and new ticket','GPD prehost old inverse still blocks at130s','GPD prehost rejection retains original return for ')
    })
    if(!$positive.Count){throw 'GPD pre-Host original assertion names are missing'}
    $expected=[ordered]@{}
    foreach($item in $Selection){
        if($item -ceq 'win5-return-exit2'){
            $fieldNames=@([regex]::Matches($body,'"(Win5 prehost [^"\r\n]+)"')|ForEach-Object{$_.Groups[1].Value})
            if($fieldNames.Count -ne 17){throw 'Win5 directed assertion inventory changed'}
            $perRequest=@($fieldNames[6..7])+@($fieldNames[11..12])
            $expected[$item]=@($positive[0])+@($fieldNames[0..5])+$perRequest+$perRequest+
                @($fieldNames[6..12])+@($fieldNames[13..16])
            continue
        }
        $names=@($positive|Where-Object{
            ($_ -cnotlike 'GPD prehost live-off *' -or $item -ceq 'off') -and
            ($_ -cnotlike 'GPD prehost live-cancel *' -or $item -ceq 'live') -and
            ($_ -cnotlike 'GPD prehost late-cancel *' -or $item -cin @('off','live'))
        })
        if($item -cne 'terminal-owner'){
            $names=@($names|Where-Object{$_ -cnotin @('GPD prehost expired Disable23 settled owner clears without inverse','GPD prehost old ticket is still expired and no old Start is made current','GPD prehost original runtime queues only after actual native owner clears','GPD prehost actual runtime mints one distinct bounded ticket without Host IO')})
            if($item -ceq 'live'){
                $names=@($names|Where-Object{$_ -cnotin @('GPD prehost exact child exits6 and owned Job reaches zero before retirement','GPD prehost production Poll actually retires exact Scope and retains durable attempt evidence')})
                $branch='GPD prehost live writer refuses settle and inverse and new ticket'
            }else{$branch='GPD prehost rejection retains original return for '+$item}
            $boundary=[Array]::IndexOf($names,'GPD prehost selected case returns real child primary handle and exact Job')
            if($boundary -lt 1 -or @($names|Where-Object{$_ -ceq 'GPD prehost selected case returns real child primary handle and exact Job'}).Count -ne 1){throw 'Original prehost return assertion boundary must be unique'}
            $names=@($names[0..($boundary-1)])+@($branch)+@($names[$boundary..($names.Count-1)])
        }
        $expected[$item]=@($names)
    }
    return [pscustomobject]@{selections=@($Selection);expected=$expected;allowed=$allowed}
}

function Write-GpdPreHostNewJson {
    param([string]$Path,$Value)
    $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value|ConvertTo-Json -Depth 30)+"`n")
    $stream=[IO.FileStream]::new($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    try{$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
}

function Invoke-GpdPreHostWriterCases {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$HeaderPath,
        [Parameter(Mandatory)][string]$GuardPath,[Parameter(Mandatory)][string]$OutputRoot,
        [Parameter(Mandatory)][long]$DeadlineEpochMs,[string[]]$Selection=@('all'))
    $selected=Get-GpdPreHostWriterSelection -HeaderPath $HeaderPath -Selection $Selection
    if(Test-Path -LiteralPath $OutputRoot){throw 'GPD pre-Host output must be new'}
    $Executable=[IO.Path]::GetFullPath($Executable)
    $manifest=Join-Path (Split-Path -Parent $Executable) 'module-package.json'
    if(![IO.File]::Exists($manifest) -or [IO.File]::ReadAllText($manifest) -cne '{}'){throw 'GPD pre-Host requires the private {} module gate beside the regression EXE'}
    # Reuse the existing atomic Job implementation verbatim, once per batch.
    # No Start->Assign race and no per-case PowerShell/compiler/source rescan.
    $tokens=$null;$parseErrors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile($GuardPath,[ref]$tokens,[ref]$parseErrors)
    if($parseErrors.Count){throw 'Original raw Job helper has parse errors'}
    $definitions=@($ast.FindAll({param($node)
        $node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -eq 'Add-Type' -and
        @($node.CommandElements|Where-Object{$_ -is [Management.Automation.Language.StringConstantExpressionAst] -and $_.Value.Contains('public sealed class YmccRawProgramJobV1 : IDisposable')}).Count -eq 1
    },$true))
    if($definitions.Count -ne 1){throw 'Original atomic Job class is not unique'}
    $classSource=@($definitions[0].CommandElements|Where-Object{$_ -is [Management.Automation.Language.StringConstantExpressionAst] -and $_.Value.Contains('public sealed class YmccRawProgramJobV1 : IDisposable')})[0].Value
    if('YmccRawProgramJobV1' -as [type]){throw 'Unexpected preloaded raw Job type; original source cannot be proven'}
    Add-Type -TypeDefinition $classSource
    [void][IO.Directory]::CreateDirectory($OutputRoot)
    $imageLock=[IO.FileStream]::new($Executable,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    $manifestLock=[IO.FileStream]::new($manifest,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    $imageSha=(Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash.ToLowerInvariant()
    $started=[Diagnostics.Stopwatch]::StartNew();$rows=@();$failure=$null;$startedCases=@()
    try{
        foreach($case in $selected.selections){
            $caseRoot=Join-Path $OutputRoot $case;[void][IO.Directory]::CreateDirectory($caseRoot)
            $stdout=Join-Path $caseRoot 'stdout.raw';$stderr=Join-Path $caseRoot 'stderr.raw'
            $caseStarted=[Diagnostics.Stopwatch]::StartNew();$job=$null;$zero=$false;$exitCode=$null;$reason='PREPARATION';$jobReceipt=$null;$caseError=$null
            $cutoff=[Math]::Min($DeadlineEpochMs-10000,[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()+30000)
            if($cutoff -le [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()){throw 'No remaining GPD pre-Host case budget'}
            try{
                $job=[YmccRawProgramJobV1]::new();$job.HoldDirectoryChain($caseRoot);$job.OpenOutputs($stdout,$stderr)
                $environment=[Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
                foreach($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()){$environment[$entry.Key]=[string]$entry.Value}
                $environment['YMCC_GPD_PREHOST_WRITER_CASE']=$case
                $envBlock=[string[]]@($environment.GetEnumerator()|ForEach-Object{$_.Key+'='+$_.Value})
                $job.StartSuspended($Executable,[string[]]@((Join-Path $caseRoot 'data'),'--shield-priority-probes'),$caseRoot,$envBlock)
                if(![string]::Equals($job.ActualImage,$Executable,[StringComparison]::OrdinalIgnoreCase) -or !$job.AtomicJobAdmission){throw 'GPD pre-Host actual image/Job admission mismatch'}
                if((Get-FileHash -LiteralPath $job.ActualImage -Algorithm SHA256).Hash.ToLowerInvariant() -cne $imageSha){throw 'GPD pre-Host EXE changed before resume'}
                $job.Resume();$startedCases+=$case;$reason='TIMEOUT'
                while([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() -lt $cutoff){
                    if($job.OutputBytes -gt 16777216 -or $job.ErrorBytes -gt 16777216){$reason='OUTPUT_LIMIT';break}
                    if($job.RootExited){$exitCode=$job.RootExitCode;$job.ReadExitTime();$reason='EXITED';break}
                    Start-Sleep -Milliseconds 5
                }
                if($reason -ceq 'EXITED'){
                    $cleanupCutoff=[Math]::Min($DeadlineEpochMs,[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()+10000)
                    do{$zero=$job.ActiveProcesses -eq 0;if(!$zero){Start-Sleep -Milliseconds 5}}while(!$zero -and [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() -lt $cleanupCutoff)
                    if(!$zero){$reason='RESIDUAL_DESCENDANTS'}
                }
            }catch{$caseError=$_.Exception.Message;$reason='LAUNCH_OR_EVIDENCE_FAILURE'}finally{
                if($job){
                    try{
                        if(!$zero){
                            $job.Terminate(124);$cleanupCutoff=[Math]::Min($DeadlineEpochMs,[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()+10000)
                            do{$zero=$job.ActiveProcesses -eq 0;if(!$zero){Start-Sleep -Milliseconds 5}}while(!$zero -and [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() -lt $cleanupCutoff)
                        }
                        $job.CloseOutputs()
                        $jobReceipt=[ordered]@{pid=$job.Pid;image=$job.ActualImage;startFileTimeUtc=$job.StartFileTime;exitFileTimeUtc=$job.ExitFileTime;atomicJobAdmission=$job.AtomicJobAdmission;zeroObservedBeforeDispose=$zero;activeProcessesFinal=$job.ActiveProcesses;terminateRequested=$job.TerminationRequested;terminateSucceeded=$job.TerminationSucceeded;queryCount=$job.QueryCount}
                    }finally{$job.Dispose();if($jobReceipt){$jobReceipt['disposed']=$job.JobDisposed}}
                }
            }
            $text=if([IO.File]::Exists($stdout)){[IO.File]::ReadAllText($stdout)}else{''}
            $errorText=if([IO.File]::Exists($stderr)){[IO.File]::ReadAllText($stderr)}else{''}
            $passed=@([regex]::Matches($text,'(?m)^PASS native-only: ([^\r\n]+)')|ForEach-Object{$_.Groups[1].Value})
            $failed=@([regex]::Matches($text,'(?m)^FAIL ([^\r\n]+)')|ForEach-Object{$_.Groups[1].Value})
            $summary=@([regex]::Matches($text,'(?m)^GPD_PREHOST_TERMINAL_OWNER ([^\r\n]+)')|ForEach-Object{$_.Groups[1].Value})
            $namesMatch=($passed.Count -eq $selected.expected[$case].Count)
            if($namesMatch){for($i=0;$i -lt $passed.Count;$i++){if($passed[$i] -cne $selected.expected[$case][$i]){$namesMatch=$false;break}}}
            $ok=$reason -ceq 'EXITED' -and $exitCode -eq 0 -and $zero -and $jobReceipt.activeProcessesFinal -eq 0 -and !$jobReceipt.terminateRequested -and !$failed.Count -and $namesMatch -and $summary.Count -eq 1 -and
                $summary[0].Contains("selection=$case checks=$($passed.Count) failures=0 ") -and $errorText.Length -eq 0
            $row=[pscustomobject]@{selection=$case;passed=$ok;reason=$reason;error=$caseError;exitCode=$exitCode;elapsedMs=$caseStarted.Elapsed.TotalMilliseconds;assertions=$passed;expectedAssertions=@($selected.expected[$case]);failures=$failed;summary=$summary;stdout=$stdout;stderr=$stderr;job=$jobReceipt}
            $rows+=$row;Write-GpdPreHostNewJson -Path (Join-Path $caseRoot 'result.json') -Value $row
            Write-Output $text
            if(!$ok){throw ('GPD pre-Host original selection failed: '+$case)}
        }
    }catch{$failure=$_.Exception.Message;throw}finally{
        $imageLock.Dispose();$manifestLock.Dispose()
        $recordedCases=@($rows|ForEach-Object{$_.selection})
        $result=[ordered]@{passed=(!$failure -and $rows.Count -eq $selected.selections.Count);selectedCases=@($selected.selections);cases=@($rows);startedCases=@($startedCases);incompleteStartedCases=@($startedCases|Where-Object{$_ -cnotin $recordedCases});notRun=@($selected.selections|Where-Object{$_ -cnotin $recordedCases -and $_ -cnotin $startedCases});error=$failure;elapsedMs=$started.Elapsed.TotalMilliseconds;executable=$Executable;executableSha256=$imageSha;guardSource=$GuardPath;guardSourceSha256=(Get-FileHash -LiteralPath $GuardPath -Algorithm SHA256).Hash.ToLowerInvariant();hardwareAccess=$false;wholeBusExecuted=$false;fullPersistentAcceptance=$false}
        Write-GpdPreHostNewJson -Path (Join-Path $OutputRoot 'result.json') -Value $result
    }
}
