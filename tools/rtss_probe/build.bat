@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
set "OUT=%~1"
if not defined OUT exit /b 2
if not exist "%OUT%\host\Plugins\Client" mkdir "%OUT%\host\Plugins\Client"
if not exist "%OUT%\host\Profiles" mkdir "%OUT%\host\Profiles"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /DUNICODE /D_UNICODE /LD mock_editor.cpp /Fo"%OUT%\mock_editor.obj" /Fe"%OUT%\host\Plugins\Client\OverlayEditor.dll" /link /IMPLIB:"%OUT%\mock_editor.lib" user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /DUNICODE /D_UNICODE /LD mock_hooks.cpp /Fo"%OUT%\mock_hooks32.obj" /Fe"%OUT%\host\RTSSHooks.dll" /link /IMPLIB:"%OUT%\mock_hooks32.lib"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /DUNICODE /D_UNICODE mock_host.cpp /Fo"%OUT%\mock_host.obj" /Fe"%OUT%\host\RTSS.exe" /link user32.lib
if errorlevel 1 exit /b 1
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /DUNICODE /D_UNICODE /LD mock_hooks.cpp /Fo"%OUT%\mock_hooks64.obj" /Fe"%OUT%\host\RTSSHooks64.dll" /link /IMPLIB:"%OUT%\mock_hooks64.lib"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /DUNICODE /D_UNICODE native_client_test.cpp /I"..\..\deps\json" /Fo"%OUT%\native_client_test.obj" /Fe"%OUT%\client_test.exe" /link user32.lib
exit /b %errorlevel%
