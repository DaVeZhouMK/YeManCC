Option Explicit
' Direct manual launcher. The separate boot BAT and .earlystart stay available.
' No cmd/NirCmd chain, polling, delayed retry or Xbox process termination.
Dim ws, fso, network, here, wanted, active, uncertain, addons, service, processes
Dim item, file, executable, steamExe, currentUser, currentDomain, status, started
Set ws = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
Set network = CreateObject("WScript.Network")
here = fso.GetParentFolderName(WScript.ScriptFullName)
Set wanted = CreateObject("Scripting.Dictionary")
Set active = CreateObject("Scripting.Dictionary")
Set uncertain = CreateObject("Scripting.Dictionary")
Set addons = CreateObject("Scripting.Dictionary")
wanted.CompareMode = 1 : active.CompareMode = 1
uncertain.CompareMode = 1 : addons.CompareMode = 1
currentUser = network.UserName : currentDomain = network.UserDomain
wanted("steam.exe") = True
If fso.FolderExists(fso.BuildPath(here, "YeManSteam")) Then
    For Each file In fso.GetFolder(fso.BuildPath(here, "YeManSteam")).Files
        If LCase(fso.GetExtensionName(file.Name)) = "txt" Then
            executable = ReadLink(file)
            If ValidExe(executable) And LCase(fso.GetFileName(executable)) <> "cssloader-standalone-headless.exe" Then
                addons(executable) = True
                wanted(fso.GetFileName(executable)) = True
            End If
        End If
    Next
End If

' One snapshot per explicit launch, not a resident watcher. Match full paths and
' the current Windows user; an unreadable matching process never authorizes a duplicate.
On Error Resume Next
Set service = GetObject("winmgmts:{impersonationLevel=impersonate}!\\.\root\cimv2")
Set processes = service.ExecQuery("SELECT * FROM Win32_Process")
status = Err.Number
Err.Clear
On Error GoTo 0
If status <> 0 Then Fail "process-snapshot-unavailable", 2
For Each item In processes
    If wanted.Exists(CStr(item.Name)) Then RememberProcess item
Next

steamExe = FindSteam()
If Not active.Exists("@steam") And Not uncertain.Exists("steam.exe") Then
    If Not ValidExe(steamExe) Then Fail "steam-executable-missing", 3
End If
If uncertain.Exists("steam.exe") Then Fail "steam-identity-unconfirmed", 4
For Each executable In addons.Keys
    If Not active.Exists(executable) And Not uncertain.Exists(fso.GetFileName(executable)) Then
        If Launch(Quote(executable), 0) Then active(executable) = True
    End If
Next
If active.Exists("@steam") Then
    ' Existing Steam belongs to the user. A duplicate launch/boot invocation
    ' must not reopen Big Picture or switch a desktop instance's current mode.
    WScript.Quit 0
Else
    ' One launch only; Steam owns startup and Big Picture. Do not force a second
    ' instance/URI while the first startup is still loading.
    started = Launch(Quote(steamExe) & " -bigpicture -offline -noverifyfiles -nobootstrapupdate -skipinitialbootstrap -no-browser -nofriendsui", 1)
End If
If Not started Then Fail "steam-launch-refused", 5
WScript.Quit 0

Function Quote(value)
    Quote = Chr(34) & value & Chr(34)
End Function
Function ValidExe(value)
    ValidExe = False
    If Len(value) < 7 Or InStr(value, Chr(34)) Or InStr(value, vbCr) Or InStr(value, vbLf) Then Exit Function
    If Mid(value, 2, 2) <> ":\" And Left(value, 2) <> "\\" Then Exit Function
    If LCase(fso.GetExtensionName(value)) <> "exe" Then Exit Function
    ValidExe = fso.FileExists(value)
End Function
Function ReadLink(link)
    Dim stream, text, code
    ReadLink = ""
    If link.Size > 8192 Then Exit Function
    On Error Resume Next
    Set stream = CreateObject("ADODB.Stream")
    stream.Type = 2 : stream.Charset = "utf-8" : stream.Open
    stream.LoadFromFile link.Path
    text = stream.ReadText
    code = Err.Number : Err.Clear
    stream.Close
    On Error GoTo 0
    If code = 0 Then ReadLink = Trim(Split(Replace(text, vbCr, ""), vbLf)(0))
End Function
Sub RememberProcess(process)
    Dim user, domain, result, path, name, code
    name = CStr(process.Name) : user = "" : domain = ""
    On Error Resume Next
    result = process.GetOwner(user, domain)
    code = Err.Number : Err.Clear
    On Error GoTo 0
    If code <> 0 Or result <> 0 Then
        uncertain(name) = True
        Exit Sub
    End If
    If StrComp(user, currentUser, vbTextCompare) <> 0 Or StrComp(domain, currentDomain, vbTextCompare) <> 0 Then Exit Sub
    path = ""
    On Error Resume Next
    path = CStr(process.ExecutablePath)
    code = Err.Number : Err.Clear
    On Error GoTo 0
    If code <> 0 Or Len(path) = 0 Then
        uncertain(name) = True
        Exit Sub
    End If
    active(path) = True
    If StrComp(name, "steam.exe", vbTextCompare) = 0 Then active("@steam") = True
End Sub
Function ReadRegistry(path)
    On Error Resume Next
    ReadRegistry = ""
    ReadRegistry = CStr(ws.RegRead(path))
    Err.Clear
    On Error GoTo 0
End Function
Function FindSteam()
    Dim candidate, registryPath
    candidate = ReadRegistry("HKCU\Software\Valve\Steam\SteamExe")
    candidate = Replace(candidate, "/", "\")
    If ValidExe(candidate) Then
        If LCase(fso.GetFileName(candidate)) = "steam.exe" Then FindSteam = candidate : Exit Function
    End If
    For Each registryPath In Array("HKCU\Software\Valve\Steam\SteamPath", "HKLM\SOFTWARE\WOW6432Node\Valve\Steam\InstallPath", "HKLM\SOFTWARE\Valve\Steam\InstallPath")
        candidate = ReadRegistry(registryPath)
        If Len(candidate) > 0 Then
            candidate = fso.BuildPath(candidate, "steam.exe")
            If ValidExe(candidate) Then FindSteam = candidate : Exit Function
        End If
    Next
    FindSteam = ""
End Function
Function Launch(command, style)
    Dim code
    On Error Resume Next
    ws.CurrentDirectory = here
    ws.Run command, style, False
    code = Err.Number : Err.Clear
    On Error GoTo 0
    Launch = (code = 0)
    If code <> 0 Then LogFailure "process-launch-error=" & CStr(code)
End Function
Sub Fail(reason, code)
    LogFailure reason
    WScript.Quit code
End Sub
Sub LogFailure(reason)
    Dim root, folder, output
    On Error Resume Next
    root = ws.ExpandEnvironmentStrings("%LOCALAPPDATA%")
    folder = fso.BuildPath(root, "YeManCC")
    If Not fso.FolderExists(folder) Then fso.CreateFolder folder
    Set output = fso.OpenTextFile(fso.BuildPath(folder, "steam-launch.log"), 8, True, -1)
    output.WriteLine CStr(Now) & " " & reason
    output.Close
    Err.Clear
    On Error GoTo 0
End Sub
