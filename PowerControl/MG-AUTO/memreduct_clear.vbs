' memreduct_clear.vbs
' 以管理员权限运行 memreduct 清理内存，然后结束进程。

' ---- 1. 自提权：如果不是管理员，就用 runas 重新以管理员身份启动自己 ----
If WScript.Arguments.Length = 0 Then
    CreateObject("Shell.Application").ShellExecute _
        "wscript.exe", _
        Chr(34) & WScript.ScriptFullName & Chr(34) & " elev", _
        "", "runas", 0
    WScript.Quit
End If

Set WshShell = CreateObject("WScript.Shell")
exe = Chr(34) & "C:\SOFT\YeMan\PowerControl\MG-AUTO\memreduct.exe" & Chr(34)

' ---- 2. 启动 memreduct 执行清理（/clear /hide），不等待 ----
WshShell.Run exe & " /clear /hide", 0, False

WScript.Sleep 6000

' ---- 3. 结束进程（taskkill 的 /IM 大小写不敏感） ----
WshShell.Run "taskkill /IM MemReduct.exe /F", 0, True
WScript.Sleep 3000

WshShell.Run "taskkill /IM MemReduct.exe /F", 0, True
WScript.Sleep 3000
