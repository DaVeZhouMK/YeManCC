$procs = Get-Process | Where-Object { $_.WorkingSet64 -gt 524288000 }
foreach ($pr in $procs) {
  try { $p = $pr.Path } catch { $p = '' }
  if ($p -and $p -like '*.exe') {
    Write-Output (($pr.Id.ToString()) + '|' + ($pr.ProcessName) + '|' + ($pr.MainWindowTitle) + '|' + $p + '|' + ($pr.WorkingSet64))
  }
}