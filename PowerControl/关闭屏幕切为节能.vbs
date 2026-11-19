Option Explicit

Dim files, shell, helper, powershell, quote
Set files = CreateObject("Scripting.FileSystemObject")
Set shell = CreateObject("WScript.Shell")
helper = files.BuildPath(files.GetParentFolderName(WScript.ScriptFullName), "TurnOffDisplay.ps1")
powershell = shell.ExpandEnvironmentStrings("%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe")
quote = Chr(34)
If Not files.FileExists(helper) Or Not files.FileExists(powershell) Then WScript.Quit 2
WScript.Quit shell.Run(quote & powershell & quote & " -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " & quote & helper & quote, 0, True)
