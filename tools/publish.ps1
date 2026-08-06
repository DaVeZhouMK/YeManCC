<#
.SYNOPSIS
  Publish script: sync built dist/ into the FIXED install location.

  Design: dev happens in YeManCC3/, the finished app lives at C:\SOFT\YeMan\
  (program area). The Task Scheduler is just a fixed pointer to this location
  (C:\SOFT\YeMan\YeManCC\YeManCC.exe --minimized).

  Prereqs (run on this machine first):
    1) npm run build          (vite build -> dist/)
    2) compile native shell   (native/YeManCC.exe -> dist/YeManCC.exe)
  This script only publishes dist/ to the install dir; it does NOT compile.

.USAGE
  npm run publish
#>
$ErrorActionPreference = 'Stop'

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$srcDist     = Join-Path $ProjectRoot 'dist'
$instRoot    = 'C:\SOFT\YeMan\YeManCC'
$instExe     = Join-Path $instRoot 'YeManCC.exe'

if (-not (Test-Path $srcDist)) {
    Write-Error "dist/ not found. Run: npm run build"
    exit 1
}
if (-not (Test-Path (Join-Path $srcDist 'YeManCC.exe'))) {
    Write-Error "dist/YeManCC.exe missing. Compile native shell first (native/YeManCC.exe -> dist/YeManCC.exe)"
    exit 1
}

if (-not (Test-Path $instRoot)) { New-Item -ItemType Directory -Path $instRoot -Force | Out-Null }

# Copy the web shell beside the finished executable without creating a nested dist/.
# The merge copy keeps files added by players.
robocopy $srcDist $instRoot /E /COPY:DAT /R:1 /W:1 /NFL /NDL /NJH /NJS /XF YeManCC.exe
if ($LASTEXITCODE -ge 8) { Write-Error "web asset copy failed, exit=$LASTEXITCODE"; exit $LASTEXITCODE }

Copy-Item (Join-Path $srcDist 'YeManCC.exe') $instExe -Force

$powerControlSrc = Join-Path $ProjectRoot 'PowerControl'
$powerControlDst = 'C:\SOFT\YeMan\PowerControl'
if (-not (Test-Path -LiteralPath $powerControlDst)) {
  New-Item -ItemType Directory -Path $powerControlDst -Force | Out-Null
}
robocopy $powerControlSrc $powerControlDst /E /COPY:DAT /R:1 /W:1 /XJ /NFL /NDL /NJH /NJS /NP
if ($LASTEXITCODE -ge 8) { Write-Error "PowerControl copy failed, exit=$LASTEXITCODE"; exit $LASTEXITCODE }

$supportSrc = Join-Path $ProjectRoot 'YeMan-Support.html'
if (-not (Test-Path -LiteralPath $supportSrc)) {
  Write-Error "YeMan-Support.html missing"
  exit 1
}
Copy-Item -LiteralPath $supportSrc -Destination 'C:\SOFT\YeMan\YeMan-Support.html' -Force

Write-Output "== publish done =="
Write-Output ("  exe : " + $instExe + " (" + (Get-Item $instExe).Length + " B)")
Write-Output "  web : embedded in the finished executable"
Write-Output "Task Scheduler points to: $instExe --minimized"
Write-Output "To refresh the registered task copy after editing the XML, toggle the setting in-app once, or re-run schtasks /Create /XML (see docs)."
