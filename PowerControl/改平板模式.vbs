Option Explicit

' 还原成非平板模式：直接写注册表，不再调用 bat
' HKLM\...\PriorityControl\ConvertibleSlateMode = 1  => 桌面/非平板模式

If Not IsAdmin() Then
    Dim oShell
    Set oShell = CreateObject("WScript.Shell")
    oShell.ShellExecute "wscript.exe", Chr(34) & WScript.ScriptFullName & Chr(34), "", "runas", 1
    WScript.Quit
End If

' 已是管理员：直接写注册表
On Error Resume Next
Dim ws
Set ws = CreateObject("WScript.Shell")
ws.RegWrite "HKLM\SYSTEM\CurrentControlSet\Control\PriorityControl\ConvertibleSlateMode", 0, "REG_DWORD"
If Err.Number <> 0 Then
    MsgBox "写入注册表失败：" & Err.Description, 16, "还原成非平板模式"
    WScript.Quit 1
End If
On Error GoTo 0

Function IsAdmin()
    Dim sh, errNum
    Set sh = CreateObject("WScript.Shell")
    On Error Resume Next
    sh.RegWrite "HKLM\SYSTEM\CurrentControlSet\Control\PriorityControl\_yeManAdminTest", 0, "REG_DWORD"
    errNum = Err.Number
    sh.RegDelete "HKLM\SYSTEM\CurrentControlSet\Control\PriorityControl\_yeManAdminTest"
    On Error GoTo 0
    IsAdmin = (errNum = 0)
End Function
