@echo off
setlocal
where cl.exe >nul 2>nul
if errorlevel 1 call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
set "OUTDIR=%~1"
if not defined OUTDIR set "OUTDIR=%~dp0..\..\..\..\Build\CustomSteamLibrary"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"
rc /nologo /fo "%OUTDIR%\steam_artwork_lab.res" steam_artwork_lab.rc
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /bigobj /DNDEBUG /DUNICODE /D_UNICODE ^
  steam_artwork_lab.cpp "%OUTDIR%\steam_artwork_lab.res" /I"..\..\deps\json" ^
  /Fo"%OUTDIR%\steam_artwork_lab.obj" /Fe"%OUTDIR%\SteamArtworkLab.exe" ^
  /link /SUBSYSTEM:CONSOLE /MACHINE:x64 winhttp.lib iphlpapi.lib version.lib bcrypt.lib advapi32.lib shell32.lib user32.lib gdiplus.lib ole32.lib ws2_32.lib
if errorlevel 1 exit /b 1
if /I not "%~2"=="selftest" exit /b 0
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /bigobj /DNDEBUG /DUNICODE /D_UNICODE ^
  /FC "..\..\tools\custom_steam_library_localconfig_selftest.cpp" /I"..\..\deps\json" ^
  /Fo"%OUTDIR%\custom_steam_library_localconfig_selftest.obj" /Fe"%OUTDIR%\custom_steam_library_localconfig_selftest.exe" ^
  /link /SUBSYSTEM:CONSOLE /MACHINE:x64 winhttp.lib iphlpapi.lib version.lib bcrypt.lib advapi32.lib shell32.lib user32.lib gdiplus.lib ole32.lib ws2_32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /bigobj /DNDEBUG /DUNICODE /D_UNICODE ^
  /FC "..\..\tools\custom_steam_library_datachain_selftest.cpp" /I"..\..\deps\json" ^
  /Fo"%OUTDIR%\custom_steam_library_datachain_selftest.obj" /Fe"%OUTDIR%\custom_steam_library_datachain_selftest.exe" ^
  /link /SUBSYSTEM:CONSOLE /MACHINE:x64 winhttp.lib iphlpapi.lib version.lib bcrypt.lib advapi32.lib shell32.lib user32.lib gdiplus.lib ole32.lib ws2_32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /bigobj /DNDEBUG /DUNICODE /D_UNICODE ^
  /FC "..\..\tools\custom_steam_library_artwork_tier_selftest.cpp" /I"..\..\deps\json" ^
  /Fo"%OUTDIR%\custom_steam_library_artwork_tier_selftest.obj" /Fe"%OUTDIR%\custom_steam_library_artwork_tier_selftest.exe" ^
  /link /SUBSYSTEM:CONSOLE /MACHINE:x64 winhttp.lib iphlpapi.lib version.lib bcrypt.lib advapi32.lib shell32.lib user32.lib gdiplus.lib ole32.lib ws2_32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /bigobj /DNDEBUG /DUNICODE /D_UNICODE ^
  /FC "..\..\tools\custom_steam_library_worker_lifecycle_selftest.cpp" /I"..\..\deps\json" ^
  /Fo"%OUTDIR%\custom_steam_library_worker_lifecycle_selftest.obj" /Fe"%OUTDIR%\custom_steam_library_worker_lifecycle_selftest.exe" ^
  /link /SUBSYSTEM:CONSOLE /MACHINE:x64 winhttp.lib iphlpapi.lib version.lib bcrypt.lib advapi32.lib shell32.lib user32.lib gdiplus.lib ole32.lib ws2_32.lib
exit /b %errorlevel%
