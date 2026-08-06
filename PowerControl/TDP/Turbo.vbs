Set ws = CreateObject("Wscript.Shell")
rc = ws.Run("cmd /c ""C:\SOFT\YeMan\PowerControl\TDP\Turbo.bat""", 0, True)
WScript.Quit rc
