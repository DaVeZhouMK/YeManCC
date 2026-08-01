$Processes = Get-Process | Where-Object { 
    $_.PM -gt 500MB -and 
    $_.SessionId -eq (Get-Process -Id $PID).SessionId -and 
    $_.ProcessName -notin @("steam", "explorer", "Taskmgr") 
}
$MaxMemoryProcess = $Processes | Sort-Object PM -Descending | Select-Object -First 1
$MaxMemoryProcess.ProcessName
