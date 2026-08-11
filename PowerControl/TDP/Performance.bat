@echo off
setlocal EnableExtensions
set "YM_RC=0"
rem Apply Performance power settings without sound.
call :run powercfg /setacvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 238c9fa8-0aad-41ed-83f4-97be242c8f20 bd3b718a-0680-4d9d-8ab2-e1d2b4ac806d 0
call :run powercfg /setdcvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 238c9fa8-0aad-41ed-83f4-97be242c8f20 bd3b718a-0680-4d9d-8ab2-e1d2b4ac806d 0
call :run powercfg /setacvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 238c9fa8-0aad-41ed-83f4-97be242c8f20 d4c1d4c8-d5cc-43d3-b83e-fc51215cb04d 1
call :run powercfg /setdcvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 238c9fa8-0aad-41ed-83f4-97be242c8f20 d4c1d4c8-d5cc-43d3-b83e-fc51215cb04d 1
call :run powercfg /setacvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 2a737441-1930-4402-8d77-b2bebba308a3 d4e98f31-5ffe-4ce1-be31-1b38b384c009 2
call :run powercfg /setdcvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 2a737441-1930-4402-8d77-b2bebba308a3 d4e98f31-5ffe-4ce1-be31-1b38b384c009 3
call :run powercfg /setacvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 54533251-82be-4824-96c1-47b60b740d00 be337238-0d82-4146-a960-4f3749d470c7 0
call :run powercfg /setdcvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 54533251-82be-4824-96c1-47b60b740d00 be337238-0d82-4146-a960-4f3749d470c7 0
call :run powercfg /setacvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 54533251-82be-4824-96c1-47b60b740d00 36687f9e-e3a5-4dbf-b1dc-15eb381c6863 90
call :run powercfg /setdcvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 54533251-82be-4824-96c1-47b60b740d00 36687f9e-e3a5-4dbf-b1dc-15eb381c6863 90
call :run powercfg /setacvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 54533251-82be-4824-96c1-47b60b740d00 36687f9e-e3a5-4dbf-b1dc-15eb381c6864 90
call :run powercfg /setdcvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 54533251-82be-4824-96c1-47b60b740d00 36687f9e-e3a5-4dbf-b1dc-15eb381c6864 90
call :run powercfg /setacvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 54533251-82be-4824-96c1-47b60b740d00 75b0ae3f-bce0-45a7-8c89-c9611c25e100 2000
call :run powercfg /setdcvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 54533251-82be-4824-96c1-47b60b740d00 75b0ae3f-bce0-45a7-8c89-c9611c25e100 2000
call :run powercfg /setacvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 54533251-82be-4824-96c1-47b60b740d00 75b0ae3f-bce0-45a7-8c89-c9611c25e101 2000
call :run powercfg /setdcvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 54533251-82be-4824-96c1-47b60b740d00 75b0ae3f-bce0-45a7-8c89-c9611c25e101 2000
call :run powercfg /setacvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 238c9fa8-0aad-41ed-83f4-97be242c8f20 9d7815a6-7ee4-497e-8888-515a05f02364 0
call :run powercfg /setdcvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 238c9fa8-0aad-41ed-83f4-97be242c8f20 9d7815a6-7ee4-497e-8888-515a05f02364 345600
call :run powercfg /setacvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 e73a048d-bf27-4f12-9731-8b2076e8891f 637ea02f-bbcb-4015-8e2c-a1c7b9c0b546 0
call :run powercfg /setdcvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 e73a048d-bf27-4f12-9731-8b2076e8891f 637ea02f-bbcb-4015-8e2c-a1c7b9c0b546 2
rem Hybrid sleep is left unchanged because device policy may override it.
call :run powercfg /setactive 1cb8b882-a900-4b9f-9bac-99d151e64441
goto :finish

:run
rem Windows power-setting GUIDs are universal. Skip only optional settings
rem that are absent from this scheme; real write failures remain errors.
if /i "%~5"=="36687f9e-e3a5-4dbf-b1dc-15eb381c6863" goto :check_optional
if /i "%~5"=="36687f9e-e3a5-4dbf-b1dc-15eb381c6864" goto :check_optional
if /i "%~5"=="36687f9e-e3a5-4dbf-b1dc-15eb381c6865" goto :check_optional
if /i "%~5"=="75b0ae3f-bce0-45a7-8c89-c9611c25e101" goto :check_optional
if /i "%~5"=="75b0ae3f-bce0-45a7-8c89-c9611c25e102" goto :check_optional
if /i "%~5"=="893dee8e-2bef-41e0-89c6-b55d0929964c" goto :check_optional
if /i "%~5"=="893dee8e-2bef-41e0-89c6-b55d0929964d" goto :check_optional
if /i "%~5"=="893dee8e-2bef-41e0-89c6-b55d0929964e" goto :check_optional
goto :execute

:check_optional
set "YM_QUERY_FILE=%TEMP%\YeManPowerQuery_%RANDOM%_%RANDOM%.txt"
powercfg /query "%~3" "%~4" > "%YM_QUERY_FILE%" 2>&1
if errorlevel 1 (
  del /q "%YM_QUERY_FILE%" >nul 2>&1
  goto :execute
)
findstr /i /c:"%~5" "%YM_QUERY_FILE%" >nul
if errorlevel 1 (
  del /q "%YM_QUERY_FILE%" >nul 2>&1
  exit /b 0
)
del /q "%YM_QUERY_FILE%" >nul 2>&1

:execute
%*
if errorlevel 1 if "%YM_RC%"=="0" set "YM_RC=%ERRORLEVEL%"
exit /b 0

:finish
endlocal & exit /b %YM_RC%
