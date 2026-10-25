param([string]$ChildScript,[string]$IdPath)
$p=Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$ChildScript+'"')) -WindowStyle Hidden -PassThru
[IO.File]::WriteAllText($IdPath,([string]$p.Id))
exit 7
