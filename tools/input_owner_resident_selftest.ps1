$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$native = Get-Content -Raw (Join-Path $repo 'native\main.cpp')
$engine = Get-Content -Raw (Join-Path $repo 'src\gamepad\engine.ts')
# The current native Coordinator intentionally documents the separate
# HIDMaestro Host in comments. The resident-owner contract must inspect the
# executable path, not reject a documentation mention of the managed adapter.
$nativeProduction = [regex]::Replace($native, '(?ms)//[^\r\n]*|/\*.*?\*/', '')
$checks = [ordered]@{
  defaultSummonEnabled = $native.Contains('static bool g_summonEnabled = true')
  rawInputSinkRegistered = $native.Contains('RIDEV_INPUTSINK | RIDEV_DEVNOTIFY')
  rawInputRegisteredAtStartup = $native.Contains('gamepadRegisterRawInput();')
  nativeUiDispatchPresent = $native.Contains('gamepadProcessUiInput(w, now);')
  virtualBackendNotInNativePath = -not $nativeProduction.Contains('HIDMaestro') -and -not $nativeProduction.Contains('virtualGamepadFeatureEnabled')
  rendererOwnerStartsWithoutFeatureGate = $engine.Contains('inputOwnerRuntime.start();') -and -not $engine.Contains('if (virtualGamepadFeatureEnabled.value) inputOwnerRuntime.start()')
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
if ($failed.Count) { throw "resident owner default contract failed: $($failed -join ', ')" }
$evidence = [ordered]@{
  evidenceId = 'S-23-RESIDENT-OWNER-NO-VIRTUAL-DEPENDENCY-20260903'
  status = 'PASS'
  systemMutation = $false
  checks = $checks
  conclusion = 'Raw Input/XInput summon and native YMCC semantic dispatch are default-on and do not require the optional virtual-gamepad feature directory or HIDMaestro runtime.'
}
$output = Join-Path $repo '..\..\Build\Validation\HC-Parity\S-23-resident-owner-no-virtual-dependency-20260903.json'
New-Item -ItemType Directory -Force (Split-Path $output) | Out-Null
$evidence | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $output -Encoding UTF8
Write-Output "resident owner no-virtual selftest: PASS ($output)"
