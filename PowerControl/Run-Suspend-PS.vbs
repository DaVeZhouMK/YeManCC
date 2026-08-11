' Run-Suspend-PS.vbs
' Launch Suspend-LargestGame.ps1 via PowerShell (hidden window)
Option Explicit

Dim fso, shell, here, ps1, ps1Quoted, rc
Set fso = CreateObject("Scripting.FileSystemObject")
Set shell = CreateObject("WScript.Shell")

here = fso.GetParentFolderName(WScript.ScriptFullName)
ps1 = here & "\Suspend-LargestGame.ps1"
ps1Quoted = Chr(34) & ps1 & Chr(34)

If Not fso.FileExists(ps1) Then
    WScript.Quit 1
End If

rc = shell.Run("powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File " & ps1Quoted, 0, True)
WScript.Quit rc
