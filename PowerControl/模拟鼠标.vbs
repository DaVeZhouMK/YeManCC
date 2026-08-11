Set ws = CreateObject("Wscript.Shell")
ws.Run "cmd /c C:\SOFT\YeMan\PowerControl\JoyXoff.bat", 0, True

' Play a cue sound. Any failure (WMP missing, file missing, slow load)
' is non-fatal: wrapped so it can never block or crash the script.
On Error Resume Next
Dim oPlayer
Set oPlayer = CreateObject("WMPlayer.OCX")
If Err.Number = 0 Then
    oPlayer.URL = "C:\SOFT\Ryzenadj\sounds\YeMan-on.wav"
    oPlayer.settings.volume = 100
    oPlayer.Controls.play
    Dim waited
    waited = 0
    Do
        WScript.Sleep 100
        waited = waited + 100
        If waited >= 2000 Then Exit Do
        If Not (oPlayer.currentMedia Is Nothing) Then
            If oPlayer.currentMedia.duration > 0 Then Exit Do
        End If
    Loop
    If Not (oPlayer.currentMedia Is Nothing) Then
        If oPlayer.currentMedia.duration > 0 Then
            WScript.Sleep oPlayer.currentMedia.duration * 1000
        End If
    End If
    Set oPlayer = Nothing
End If
On Error GoTo 0
