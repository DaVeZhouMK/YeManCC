@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /I"C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/um" /I"C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/shared" /I"C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/ucrt" /I"G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC/native" /I"G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC/deps/json" "G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\--help\shortcut-full-runtime.cpp" /Fo"G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\--help/shortcut-full-runtime.obj" /Fe"G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\--help\shortcut-full-runtime.exe" /link /LIBPATH:"C:/Program Files (x86)/Windows Kits/10/Lib/10.0.26100.0/um/x64" /LIBPATH:"C:/Program Files (x86)/Windows Kits/10/Lib/10.0.26100.0/ucrt/x64"
exit /b %errorlevel%
