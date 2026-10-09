@echo off
setlocal
where cl.exe >nul 2>nul
if errorlevel 1 call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
set "OUTDIR=%~1"
if not defined OUTDIR set "OUTDIR=%~dp0..\..\..\..\Build\CustomSteamLibrary"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"
rc /nologo /fo "%OUTDIR%\workspace_host.res" workspace_host.rc
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /bigobj /DNDEBUG /DUNICODE /D_UNICODE ^
  workspace_host.cpp "%OUTDIR%\workspace_host.res" /I"..\..\deps\json" /I"..\..\deps\webview2\build\native\include" ^
  /Fo"%OUTDIR%\workspace_host.obj" /Fe"%OUTDIR%\CustomSteamLibrary.exe" ^
  /link /SUBSYSTEM:WINDOWS /MACHINE:x64 "..\..\deps\webview2\build\native\x64\WebView2LoaderStatic.lib" ^
  user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib dwmapi.lib winhttp.lib advapi32.lib version.lib bcrypt.lib
if errorlevel 1 exit /b 1
if /I not "%~2"=="selftest" exit /b 0
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /bigobj /DNDEBUG /DUNICODE /D_UNICODE ^
  /FC "..\..\tools\custom_steam_library_host_artwork_selftest.cpp" /I"..\..\deps\json" /I"..\..\deps\webview2\build\native\include" ^
  /Fo"%OUTDIR%\host_artwork_selftest.obj" /Fe"%OUTDIR%\custom_steam_library_host_artwork_selftest.exe" ^
  /link /SUBSYSTEM:CONSOLE /MACHINE:x64 "..\..\deps\webview2\build\native\x64\WebView2LoaderStatic.lib" ^
  user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib dwmapi.lib winhttp.lib advapi32.lib version.lib bcrypt.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /bigobj /DNDEBUG /DUNICODE /D_UNICODE ^
  /FC "..\..\tools\custom_steam_library_host_lifecycle_selftest.cpp" /I"..\..\deps\json" /I"..\..\deps\webview2\build\native\include" ^
  /Fo"%OUTDIR%\host_lifecycle_selftest.obj" /Fe"%OUTDIR%\custom_steam_library_host_lifecycle_selftest.exe" ^
  /link /SUBSYSTEM:CONSOLE /MACHINE:x64 "..\..\deps\webview2\build\native\x64\WebView2LoaderStatic.lib" ^
  user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib dwmapi.lib winhttp.lib advapi32.lib version.lib bcrypt.lib
exit /b %errorlevel%
