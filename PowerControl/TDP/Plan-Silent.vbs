Set ws = CreateObject("Wscript.Shell")
ws.Run "cmd /c """ & Replace(WScript.ScriptFullName, ".vbs", ".bat") & """", 0, True
