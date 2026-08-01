# 获取当前系统盘符
$SystemDrive = (Get-WmiObject -Class Win32_OperatingSystem).SystemDrive

# 设置虚拟内存大小为自定义值
$InitialSize = 1024  # 设置初始大小为1024MB（2GB）
$MaximumSize = 32768 # 设置最大大小为32768MB（16GB）

# 禁用自动管理页面文件
Set-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management" -Name "PagingFiles" -Value "$SystemDrive\pagefile.sys $InitialSize $MaximumSize"

# 确认设置
$PagingFiles = Get-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management" -Name "PagingFiles"
Write-Output "虚拟内存已设置为 $PagingFiles.PagingFiles"

