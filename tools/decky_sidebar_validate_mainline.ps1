param([string]$EvidenceName='mainline-web-regression')
$ErrorActionPreference='Stop'
$source=Split-Path -Parent $PSScriptRoot
$task=if($env:YMCC_DECKY_ARTIFACT_ROOT){$env:YMCC_DECKY_ARTIFACT_ROOT}else{'G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Sidebar'}
if($EvidenceName -notmatch '^[A-Za-z0-9-]+$'){throw 'Invalid validation evidence name'}
$env:YMCC_DECKY_ARTIFACT_ROOT=$task;$log=Join-Path $task ("validation\$EvidenceName.txt")
"YMCC Decky 侧边栏 — authoritative mainline source; $(Get-Date -Format o)"|Set-Content -LiteralPath $log -Encoding utf8
function Write-ValidationEvidence {
 param([Parameter(ValueFromPipeline=$true)]$Line)
 process { $text=$Line.ToString(); Add-Content -LiteralPath $log -Value $text -Encoding utf8; Write-Output $text }
}
function Checked([string[]]$arguments) {
 "COMMAND: node $($arguments -join ' ')" | Write-ValidationEvidence
 $previous=$ErrorActionPreference
 try { $ErrorActionPreference='Continue'; & node.exe @arguments 2>&1 | Write-ValidationEvidence; $exit=$LASTEXITCODE }
 finally { $ErrorActionPreference=$previous }
 if($exit -ne 0){throw ('Mainline validation failed ('+$exit+'): '+($arguments -join ' '))}
}
Push-Location $source
try{
 Checked @('node_modules/vue-tsc/bin/vue-tsc.js','--noEmit')
 Checked @('tools/lossless_scaling_selftest.mjs')
 Checked @('tools/decky_console_power_source_selftest.mjs')
 Checked @('tools/decky_sidebar_gyro_selftest.mjs')
 if(Test-Path -LiteralPath 'D:\Game\Steam\steamui\library.js'){Checked @('tools/decky_sidebar_native_dropdown_selftest.mjs')}
 foreach($n in @('client','readonly','mutation','steam_observation','bootstrap_expression','switch','temp_source','commit_coalesce','host','source_admission','private_admission','game_model_boundary','game_actions','game_store','plugin_presentation','qam_visibility','qam_placement','owner_metadata','real_config_readonly','packaging','mainline_source')){Checked @("tools/decky_sidebar_${n}_selftest.mjs",'--stage14')}
 Checked @('tools/build-decky-sidebar-fan-test.mjs')
 Checked @((Join-Path $task 'Build\decky-sidebar-fan-lifecycle-test.cjs'))
 Checked @((Join-Path $task 'Build\decky-sidebar-fan-rebase-test.cjs'))
 foreach($mode in @('','--fan-mutations','--game-mutations')){if($mode){Checked @('tools/decky_sidebar_loopback_selftest.mjs',$mode)}else{Checked @('tools/decky_sidebar_loopback_selftest.mjs')}}
 Checked @('tools/decky_sidebar_host_loopback_selftest.mjs','--stage14')
 $env:YEMAN_BUILD_WEB_DIR=Join-Path $task 'Build\App\Web'
 Checked @('node_modules/vite/bin/vite.js','build')
 Checked @('tools/build-decky-sidebar-plugin.mjs','--check')
 'MAINLINE_WEB_VALIDATION_PASS=true; REAL_CONFIG_WRITES=0; HARDWARE_WRITES=0'|Write-ValidationEvidence
}finally{Remove-Item Env:\YEMAN_BUILD_WEB_DIR -ErrorAction SilentlyContinue;Pop-Location}
