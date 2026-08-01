start "" "C:\SOFT\YeMan\PowerControl\MG-AUTO\memreduct.exe" /clear /hide
timeout /t 6 /nobreak > nul


taskkill /IM "MemReduct.exe" /F

timeout /t 3 /nobreak > nul

taskkill /IM "MemReduct.exe" /F

timeout /t 3 /nobreak > nul

exit
