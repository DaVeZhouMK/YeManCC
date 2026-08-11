# 确保以管理员身份运行
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Start-Process powershell "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`"" -Verb RunAs
    exit
}

# 获取大于500MB的进程
$Processes = Get-Process | Where-Object { 
    $_.PM -gt 500MB -and 
    $_.SessionId -eq (Get-Process -Id $PID).SessionId -and 
    $_.ProcessName -notin @("steam", "explorer", "Taskmgr") 
}

# 找到内存使用量最大的进程
$MaxMemoryProcess = $Processes | Sort-Object PM -Descending | Select-Object -First 1

if ($MaxMemoryProcess) {
    # 输出进程名称
    $ProcessName = $MaxMemoryProcess.ProcessName
    Write-Host "Adjusting CPU affinity for process: $ProcessName"
    
# 设置 CPU 亲和性为12核
# 计算对应的二进制掩码
$AffinityMask = 0xFFF # 0xFFF = 111111111111 (二进制)
(Get-Process $ProcessName).ProcessorAffinity = $AffinityMask
} else {
    Write-Host "No process found that meets the criteria."
}



