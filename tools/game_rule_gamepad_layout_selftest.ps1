$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$quick = Get-Content (Join-Path $root 'src\components\GameQuickActions.vue') -Raw -Encoding UTF8
$rules = Get-Content (Join-Path $root 'src\components\GameRulePanel.vue') -Raw -Encoding UTF8
$custom = Get-Content (Join-Path $root 'src\components\GameCustomProfilePanel.vue') -Raw -Encoding UTF8
$schedule = Get-Content (Join-Path $root 'src\views\PerformanceScheduleView.vue') -Raw -Encoding UTF8
$engine = Get-Content (Join-Path $root 'src\gamepad\engine.ts') -Raw -Encoding UTF8
$recognition = Get-Content (Join-Path $root 'src\components\GameRecognitionControl.vue') -Raw -Encoding UTF8
$mapping = Get-Content (Join-Path $root 'src\views\ButtonMappingView.vue') -Raw -Encoding UTF8
$gyro = Get-Content (Join-Path $root 'src\views\GyroMotionView.vue') -Raw -Encoding UTF8

function Assert-Contains([string]$text, [string]$needle, [string]$message) {
  if (-not $text.Contains($needle)) { throw $message }
}

foreach ($col in 0..3) {
  $needle = 'data-gp-row="0" data-gp-col="' + $col + '"'
  Assert-Contains $quick $needle "top control $col is missing a fixed focus coordinate"
}
Assert-Contains $quick 'data-gp-row="1" data-gp-col="0"' 'FSR row coordinate is missing'
Assert-Contains $quick 'data-gp-row="1" data-gp-col="1"' 'Lossless Scaling row coordinate is missing'
Assert-Contains $quick 'data-gp-row="2" data-gp-col="0"' 'trainer row coordinate is missing'
Assert-Contains $quick 'gp-row="2" gp-col="1"' 'speed dropdown coordinate is missing'
Assert-Contains $quick 'data-gp-game-control="switch-program"' 'switch program semantic target is missing'
Assert-Contains $quick 'data-gp-game-control="fsr-import"' 'FSR semantic source is missing'
# 2026-09-27：视觉顺序=手柄横向顺序。切换程序是第一行第一列，呼出后默认光标也在它上面。
Assert-Contains $quick 'data-gp-row="0" data-gp-col="0" data-gp-game-control="switch-program"' 'switch program must be the first visual and controller cell of the controls row'
Assert-Contains $recognition '[data-gp-game-control="switch-program"]' 'the Y menu must open with the gamepad cursor on switch program'
if ($rules -notmatch 'data-gp-rule-focus="manual-input"[\s\S]*?data-gp-row="4"[\s\S]*?data-gp-col="0"') { throw 'manual input is not aligned with confirmation' }
Assert-Contains $rules 'data-gp-row="4" data-gp-col="1"' 'manual confirmation is not aligned with input'
Assert-Contains $engine "marker === 'manual-input' || marker === 'manual-confirm'" 'manual row down-stop rule is missing'
Assert-Contains $engine 'lastRuleItem' 'manual row must return to the active list tail'
Assert-Contains $engine 'useGameMenuSemanticVertical' 'game menu vertical navigation must use semantic rows'
Assert-Contains $custom 'grid-template-columns: repeat(2, minmax(0, 1fr));' 'core/SMT and pad/gyro selectors must stay side by side'
Assert-Contains $engine "['custom-core-big-picker', 'custom-core-smt-picker']" 'core/SMT selectors must share the first controller row'
Assert-Contains $engine "['custom-input-pad', 'custom-input-gyro']" 'pad/gyro selectors must share the second controller row'
Assert-Contains $engine "customRows.push('custom-input-gyro')" 'semantic controller order must visit core/SMT before pad/gyro selectors'
Assert-Contains $schedule "automaticOptimizationUnavailable.value ? '游戏专属配置生效中' : '自动优化'" 'automatic optimization must expose the dedicated-profile priority state'
Assert-Contains $schedule ':disabled="busy || performanceControlsLocked"' 'automatic AC/DC selectors must be disabled while a dedicated profile is active'
Assert-Contains $custom 'class="custom-top-active">当前启用</span>' 'dedicated profile header must retain its active indicator while collapsed'
# 2026-09-30 用户裁决：顶部陀螺仪下拉直接用陀螺仪页四预设（去掉「陀螺仪开启」），
# 选中任一项 = 启用陀螺仪；「遵循全局 / 陀螺仪关闭」保留。
Assert-Contains $custom ":options=""gyroOverrideOptions""" 'gyro dropdown must render the preset option list'
Assert-Contains $custom "{ value: 'fps', label: 'FPS射击'" 'top menu gyro dropdown must offer the FPS preset'
Assert-Contains $custom "{ value: 'steam', label: 'Steam'" 'top menu gyro dropdown must offer the Steam preset'
Assert-Contains $custom "{ value: 'off', label: '陀螺仪关闭'" 'top menu gyro dropdown must keep the force-off option'
Assert-Contains $custom "{ value: 'on', label: '陀螺仪开启（旧档）'" 'legacy gyro-on value must stay viewable instead of being silently rewritten'
# 2026-09-30 用户裁决：切到 SteamDeck 虚拟手柄时鼠标交给 Steam，JoyXoff 若在跑强行关掉。
Assert-Contains $custom 'closeJoyxoffIfRunning' 'top menu steam deck switch must close JoyXoff when it is running'
Assert-Contains $mapping 'closeJoyxoffIfRunning' 'controller page persona switch must close JoyXoff too'
# 2026-09-30 用户裁决：顶部鼠标行按人格如实显示（SteamDeck → Steam鼠标）。
Assert-Contains $quick "steamDeckPadActive.value ? 'Steam鼠标' : '模拟鼠标'" 'steam deck pad must relabel the top menu mouse row'
# 2026-09-30 用户裁决：控制器页背部按键气泡改为真实能力反馈（陀螺仪 / 背键数量）。
Assert-Contains $mapping '支持识别' 'controller page must report the real back-button count'
Assert-Contains $mapping '陀螺仪-' 'controller page must report gyro support per persona'
if ($mapping.Contains('映射背部按键（常开）')) { throw 'back-button bubble must not keep the hardcoded always-on claim' }
# 2026-09-30 用户裁决：Steam 预设说明小字位于「输入模式」上方。
Assert-Contains $gyro '请在 Steam 内设置，支持 SteamDeck 和 PS5 虚拟手柄' 'steam preset hint is missing above the input-mode card'
# 2026-10-10：顶部顺序改为控制行、专属配置、游戏黑白名单、快捷功能。
$controlsIndex = $quick.IndexOf('<div class="quick-game-controls"')
$customIndex = $quick.IndexOf('<GameCustomProfilePanel')
$rulesIndex = $quick.IndexOf('<GameRulePanel')
$actionsIndex = $quick.IndexOf('<div class="game-quick-actions"')
if (-not ($controlsIndex -ge 0 -and $controlsIndex -lt $customIndex -and $customIndex -lt $rulesIndex -and $rulesIndex -lt $actionsIndex)) {
  throw 'top menu visual order must be controls, dedicated profile, rules, quick actions'
}
Assert-Contains $engine "const customRows = ['custom-ac', '专用frame-ac', 'custom-dc', '专用frame-dc']" 'each dedicated power preset must be followed by its own frame row'
Assert-Contains $engine "rows.indexOf('custom-entry') + 1" 'expanded profile controls must follow the profile entry'
Assert-Contains $engine "rows.indexOf('rules-entry') + 1" 'expanded rule controls must follow the rules entry'
Assert-Contains $engine 'spatialNavigationTarget(explicitCandidates, base' 'local profile coordinates must not match other top-menu regions'
Assert-Contains $engine 'if (rows[i] === pairSibling) continue;' 'vertical navigation must skip horizontal core/input siblings'
Assert-Contains $custom ':side="item"' 'dedicated power groups must render only their own side frame controls'
Assert-Contains $custom 'class="power-mode-content"' 'dedicated power and frame settings must share a vertically centered group'
Assert-Contains $schedule 'class="power-mode-content"' 'automatic power and frame settings must share a vertically centered group'
Write-Output 'game rule gamepad layout selftest: PASS'
