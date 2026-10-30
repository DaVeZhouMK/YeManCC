import assert from 'node:assert/strict';
export const OBSERVE_ONLY_FAILURE='Automatic appearance not confirmed within the finite loading window; configuration and resident CSS retained';
export function observeOnlyHarness(){return String.raw`param([Parameter(Mandatory=$true)][string]$taskTestSource,[Parameter(Mandatory=$true)][string]$taskResultRoot,[Parameter(Mandatory=$true)][string]$taskComponentRoot)
$ErrorActionPreference='Stop'
try{. $taskTestSource -Out $taskResultRoot -ComponentRoot $taskComponentRoot}
catch{
 $taskCaught=$_
 $taskFailure=[ordered]@{schemaVersion=1;status='FAIL';phase='original-automatic-appearance-initial-call';message=$taskCaught.Exception.Message;automaticFunctionInStack=($taskCaught.ScriptStackTrace -match 'Invoke-AutomaticAppearanceRefresh');applied=[int]$script:Applied;starts=[int]$script:Starts;resets=[int]$script:Resets;rendered=[bool]$script:Rendered;live=[bool]$script:Live;backendRows=@($script:Rows).Count;themeCount=@($script:ThemeNames).Count;renderSamples=[int]$script:RenderSamples;completedOriginalAssertions=$cases.Count;observedReadDelays=@($script:Delay);realSteamOperations=0;realCssLaunches=0}
 $taskOwnedResult=[IO.Path]::GetFullPath($taskResultRoot)
 [IO.File]::WriteAllText((Join-Path $taskOwnedResult 'observe-only-business-failure.json'),($taskFailure|ConvertTo-Json -Depth 8),(New-Object Text.UTF8Encoding($false)))
 throw $taskCaught
}
`;}
export function validateObserveOnlyFailure(process,receipt){
 assert.equal(process.status,1,'OBSERVE_ONLY_MUST_FAIL_NATURALLY');assert.equal(process.signal??null,null,'OBSERVE_ONLY_SIGNAL_REJECTED');assert(!process.error,'OBSERVE_ONLY_PROCESS_ERROR');
 assert.equal(receipt.schemaVersion,1,'OBSERVE_ONLY_RECEIPT_SCHEMA');assert.equal(receipt.status,'FAIL','OBSERVE_ONLY_RAW_FAIL_REQUIRED');assert.equal(receipt.phase,'original-automatic-appearance-initial-call','OBSERVE_ONLY_ACTUAL_PHASE_REQUIRED');assert.equal(receipt.message,OBSERVE_ONLY_FAILURE,'OBSERVE_ONLY_ACTUAL_BUSINESS_GUARD_REQUIRED');assert.equal(receipt.automaticFunctionInStack,true,'OBSERVE_ONLY_PRODUCTION_FUNCTION_REQUIRED');
 for(const field of ['applied','starts','resets','completedOriginalAssertions','realSteamOperations','realCssLaunches'])assert.equal(receipt[field],0,'OBSERVE_ONLY_UNEXPECTED_SIDE_EFFECT:'+field);
 assert.equal(receipt.rendered,false,'OBSERVE_ONLY_FALSE_RENDER_HEALTH');assert.equal(receipt.live,true,'OBSERVE_ONLY_MISSING_LIVE_FIXTURE');assert.equal(receipt.backendRows,7,'OBSERVE_ONLY_BACKEND_SEVEN_REQUIRED');assert.equal(receipt.themeCount,7,'OBSERVE_ONLY_THEMES_SEVEN_REQUIRED');assert(Number.isSafeInteger(receipt.renderSamples)&&receipt.renderSamples>=5,'OBSERVE_ONLY_REAL_READ_OBSERVATIONS_REQUIRED');assert(Array.isArray(receipt.observedReadDelays)&&receipt.observedReadDelays.length>0&&receipt.observedReadDelays.length<=5,'OBSERVE_ONLY_FINITE_READ_WINDOW_REQUIRED');
 return {status:'EXACT_OBSERVE_ONLY_MISSING_RECONCILE_GUARD_DETECTED',originalRawStatus:'FAIL',sameOriginalAssertions:true,correctFunctionObserved:true,themeWrites:0,processStarts:0,renderSamples:receipt.renderSamples};
}
