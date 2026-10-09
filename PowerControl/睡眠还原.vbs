Option Explicit

Dim oShell, sScript, sPlan
Set oShell = CreateObject("WScript.Shell")
sScript = WScript.ScriptFullName
sPlan = "1cb8b882-a900-4b9f-9bac-99d151e64441"

If Not IsAdmin() Then
    oShell.ShellExecute "wscript.exe", Chr(34) & sScript & Chr(34), "", "runas", 1
    WScript.Quit
End If

RunCmd "powercfg -setacvalueindex " & sPlan & " 2E601130-5351-4d9d-8E04-252966BAD054 3166bc41-7e98-4e03-b34e-ec0f5f2b218e 0xFFFFFFFF"
RunCmd "powercfg -setdcvalueindex " & sPlan & " 2E601130-5351-4d9d-8E04-252966BAD054 3166bc41-7e98-4e03-b34e-ec0f5f2b218e 300"

RunCmd "powercfg -setacvalueindex " & sPlan & " 2E601130-5351-4d9d-8E04-252966BAD054 c36f0eb4-2988-4a70-8eee-0884fc2c2433 0"
RunCmd "powercfg -setdcvalueindex " & sPlan & " 2E601130-5351-4d9d-8E04-252966BAD054 c36f0eb4-2988-4a70-8eee-0884fc2c2433 60000"

RunCmd "powercfg -setacvalueindex " & sPlan & " 238C9FA8-0AAD-41ED-83F4-97BE242C8F20 7bc4a2f9-d8fc-4469-b07b-33eb785aaca0 120"
RunCmd "powercfg -setdcvalueindex " & sPlan & " 238C9FA8-0AAD-41ED-83F4-97BE242C8F20 7bc4a2f9-d8fc-4469-b07b-33eb785aaca0 120"

ApplyIOC 1, 0

RunCmd "powercfg -setactive " & sPlan

Function IsAdmin()
    On Error Resume Next
    Dim iErr
    oShell.RegRead("HKEY_USERS\S-1-5-19\Environment\TEMP")
    iErr = Err.Number
    On Error GoTo 0
    IsAdmin = (iErr = 0)
End Function

Sub ApplyIOC(vAC, vDC)
    Dim aSub, aSet, i
    aSub = Split("2a737441-1930-4402-8d77-b9728092c9e1,2a737441-1930-4402-8d77-b2bebba308a3", ",")
    aSet = Split("498c044a-9e4e-4c0d-8a94-4dac61980a1f,498c044a-201b-4631-a522-5c744ed4e678", ",")
    For i = 0 To UBound(aSub)
        If RunCmd("powercfg -setacvalueindex " & sPlan & " " & aSub(i) & " " & aSet(i) & " " & vAC) = 0 Then
            RunCmd "powercfg -setdcvalueindex " & sPlan & " " & aSub(i) & " " & aSet(i) & " " & vDC
            Exit For
        End If
    Next
End Sub

Function RunCmd(sCmd)
    RunCmd = oShell.Run("cmd /c " & sCmd & " >nul 2>&1", 0, True)
End Function
