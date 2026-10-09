Option Explicit

Dim ws
Set ws = CreateObject("Wscript.Shell")

' 子组：显示 (SUB_VIDEO)
Dim schemeSubG
schemeSubG = "7516b95f-f776-4464-8c53-06167f40cc99"
' 设置：此时间后关闭显示 (VIDEOIDLE)
Dim settingVID
settingVID = "3c0bc021-c8a8-4e07-a973-6b14cbcb2b7e"
' 两个一定存在的“别的方案”，用于强制刷新（确保 != 当前方案）
Dim eliteScheme, balancedScheme
eliteScheme    = "1cb8b882-a900-4b9f-9bac-99d151e64441"
balancedScheme = "381b9d98-0c00-4ef3-bdfd-5a27c5b74555"

' 运行命令并取回标准输出
Function RunOut(cmd)
    Dim e
    Set e = ws.Exec("cmd /c " & cmd)
    Do While e.Status = 0
        WScript.Sleep 50
    Loop
    RunOut = e.Stdout.ReadAll()
End Function

' 1) 读取当前活动电源方案 GUID（不写死，任何方案都适用）
Dim activeRaw, re, m, curScheme
activeRaw = RunOut("powercfg /getactivescheme")
Set re = New RegExp
re.Pattern = "[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}"
Set m = re.Execute(activeRaw)
curScheme = m(0).Value

' 2) 读取该方案当前“关闭显示”的 AC / DC 原值
'    （避开中文标签，按 0x 顺序取：最小/最大/增量/交流/直流 → 第4、第5个）
Dim qRaw, matches, origAC, origDC
qRaw = RunOut("powercfg /q " & curScheme & " " & schemeSubG & " " & settingVID)
Set re = New RegExp
re.Pattern = "0x([0-9A-Fa-f]{8})"
re.Global = True
Set matches = re.Execute(qRaw)
origAC = CDbl("&H" & matches(3).SubMatches(0))
origDC = CDbl("&H" & matches(4).SubMatches(0))

' 3) 选一个“别的”方案用于强制刷新（确保与当前方案不同，否则切走再切回无效）
Dim otherScheme
If LCase(curScheme) = LCase(eliteScheme) Then
    otherScheme = balancedScheme
Else
    otherScheme = eliteScheme
End If

' 4) 把当前方案的关闭显示设为 1 秒
ws.Run "cmd /c powercfg -setacvalueindex " & curScheme & " " & schemeSubG & " " & settingVID & " 1", 0, True
ws.Run "cmd /c powercfg -setdcvalueindex " & curScheme & " " & schemeSubG & " " & settingVID & " 1", 0, True
' 强制刷新：先切到别的方案，再切回当前方案（同方案 setactive 是空操作，必须切走再切回）
ws.Run "cmd /c powercfg -setactive " & otherScheme, 0, True
ws.Run "cmd /c powercfg -setactive " & curScheme, 0, True

' 5) 等待 5 秒（此期间空闲 1 秒即熄屏）
WScript.Sleep 5000

' 计算恢复值（优先级从高到低）：
'   ① 当前方案是 ELITE(1cb8b882-...) → 强制 3600
'   ② 读到的原值是 1（疑似上次异常残留，没正常恢复）→ 用 Windows 默认 300
'   ③ 其他 → 写回真实读取到的原值
Function RestoreVal(scheme, orig)
    If LCase(scheme) = LCase(eliteScheme) Then
        RestoreVal = 3600
    ElseIf orig = 1 Then
        RestoreVal = 300
    Else
        RestoreVal = orig
    End If
End Function

' 6) 恢复原值（带特判）并再次刷新
ws.Run "cmd /c powercfg -setacvalueindex " & curScheme & " " & schemeSubG & " " & settingVID & " " & CStr(RestoreVal(curScheme, origAC)), 0, True
ws.Run "cmd /c powercfg -setdcvalueindex " & curScheme & " " & schemeSubG & " " & settingVID & " " & CStr(RestoreVal(curScheme, origDC)), 0, True
ws.Run "cmd /c powercfg -setactive " & otherScheme, 0, True
ws.Run "cmd /c powercfg -setactive " & curScheme, 0, True
