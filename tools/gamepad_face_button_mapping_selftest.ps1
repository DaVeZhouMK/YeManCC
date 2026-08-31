$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$native = Get-Content (Join-Path $root 'native\main.cpp') -Raw
$engine = Get-Content (Join-Path $root 'src\gamepad\engine.ts') -Raw

if ($native -notmatch 'else if\s*\(pressed\(XINPUT_GAMEPAD_Y\)\)\s*\{\s*gamepadEmitUiAction\("edit-game"\)') {
  throw 'main YeManCC Y button is not mapped to edit-game'
}
if ($native -notmatch 'if\s*\(customSteamLibraryChildForeground\(\)\)\s*\{\s*if\s*\(pressed\(XINPUT_GAMEPAD_X\)\) gamepadEmitUiAction\("edit-game"\)') {
  throw 'CustomSteamLibrary child X edit mapping is missing'
}
if (-not $engine.Contains('Y(3)           → 全局编辑游戏识别名单')) {
  throw 'renderer gamepad contract does not document Y as edit-game'
}
if (-not $engine.Contains('X(2)           → 不参与 YeManCC 统一页面调度')) {
  throw 'renderer gamepad contract does not reserve X'
}
Write-Output 'gamepad face-button mapping selftest: 4/4 passed'
