' FPS-Monitor.vbs - 静默启动 FPS 监控守护（无窗口闪烁）
Set ws = CreateObject("Wscript.Shell")
ws.Run "powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File ""C:\SOFT\YeMan\PowerControl\FPS-Monitor.ps1""", 0, False
