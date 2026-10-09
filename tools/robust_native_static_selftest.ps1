$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$native = Get-Content -LiteralPath (Join-Path $root 'native\main.cpp') -Raw
$exe = Join-Path $root 'Build\App\Native\YeManCC.exe'

function Assert-Contains([string]$text, [string]$needle, [string]$message) {
  if (-not $text.Contains($needle)) { throw $message }
}

Assert-Contains $native 'BrowserProcessExited' '缺少 BrowserProcessExited 注册'
Assert-Contains $native 'process-failed-stale-generation' '缺少旧 generation 事件隔离'
Assert-Contains $native 'browserProcessId' '缺少浏览器进程 ID 诊断字段'
Assert-Contains $native 'frontend-host-fallback' '缺少旧 ICoreWebView2_3 资源回退'
Assert-Contains $native 'resolveFrontendFallbackAsset' '缺少资源路径安全解析'
Assert-Contains $native 'webview-gpu-state.json' '缺少外置 GPU 状态文件'
Assert-Contains $native 'diagnostics.frontendError' '缺少前端诊断 IPC'
Assert-Contains $native 'frontend-errors.log' '缺少 native 前端持久日志'
Assert-Contains $native 'routeStatus' '缺少 route 状态记录'

if ($native.Contains('failWebViewRecovery("browser failed during recovery")')) {
  throw "Recovery overlap still exits immediately"
}
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "native 编译产物不存在: $exe" }

[pscustomobject]@{
  ok = $true
  nativeExe = $exe
  browserProcessExited = $true
  generationGuard = $true
  oldRuntimeFallback = $true
  gpuStateOutsideProfile = $true
  frontendNativeLog = $true
} | ConvertTo-Json -Compress
