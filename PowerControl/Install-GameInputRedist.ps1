[CmdletBinding()]
param(
  [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
$packageVersion = '3.3.221'
$serviceName = 'GameInputRedistService'
$serviceExe = 'C:\Program Files\Microsoft GameInput\x64\GameInputRedistService.exe'

function Test-GameInputRedist {
  return (Test-Path -LiteralPath $serviceExe) -and
    $null -ne (Get-Service -Name $serviceName -ErrorAction SilentlyContinue)
}

if (Test-GameInputRedist) {
  Write-Output 'Microsoft GameInput Redist is already installed.'
  exit 0
}

$currentIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
$currentPrincipal = [Security.Principal.WindowsPrincipal]::new($currentIdentity)
if (-not $currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"{0}"' -f $PSCommandPath))
  if ($Quiet) { $arguments += '-Quiet' }
  Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList $arguments
  exit 0
}

$workDirectory = Join-Path $env:TEMP ('YeMan-GameInput-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $workDirectory -Force | Out-Null
try {
  $packagePath = Join-Path $workDirectory 'Microsoft.GameInput.nupkg'
  $archivePath = Join-Path $workDirectory 'Microsoft.GameInput.zip'
  $extractPath = Join-Path $workDirectory 'package'
  $packageUrl = "https://api.nuget.org/v3-flatcontainer/microsoft.gameinput/$packageVersion/microsoft.gameinput.$packageVersion.nupkg"

  Invoke-WebRequest -UseBasicParsing -Uri $packageUrl -OutFile $packagePath
  # Windows PowerShell only accepts a .zip extension here although NuGet
  # packages are ZIP archives by format.
  Copy-Item -LiteralPath $packagePath -Destination $archivePath -Force
  Expand-Archive -LiteralPath $archivePath -DestinationPath $extractPath -Force
  $msiPath = Join-Path $extractPath 'redist\GameInputRedist.msi'
  if (-not (Test-Path -LiteralPath $msiPath -PathType Leaf)) {
    throw 'The official Microsoft.GameInput package did not contain GameInputRedist.msi.'
  }

  $signature = Get-AuthenticodeSignature -LiteralPath $msiPath
  if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Microsoft') {
    throw "GameInputRedist.msi signature validation failed: $($signature.Status)"
  }

  $arguments = @('/i', ('"{0}"' -f $msiPath), '/norestart')
  if ($Quiet) { $arguments += '/qn' } else { $arguments += '/passive' }
  $installer = Start-Process -FilePath 'msiexec.exe' -ArgumentList $arguments -Wait -PassThru
  if ($installer.ExitCode -notin 0, 3010) {
    throw "GameInput Redist installer failed with exit code $($installer.ExitCode)."
  }
  if (-not (Test-GameInputRedist)) {
    throw 'GameInput Redist installation completed, but its service was not registered.'
  }
  Write-Output 'Microsoft GameInput Redist installed successfully.'
  exit $installer.ExitCode
} finally {
  Remove-Item -LiteralPath $workDirectory -Recurse -Force -ErrorAction SilentlyContinue
}
