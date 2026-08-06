@echo off
"C:\SOFT\YeMan\PowerControl\pawnio\YeManTdpCtl.exe" set 200

powercfg /setactive 1cb8b882-a900-4b9f-9bac-99d151e64441


REM 切换至 极致性能 Extreme 并写入关键参数
REM ===== 关键参数直接写入野蛮电源 1cb8b882 (即时生效) =====
REM 唤醒定时器 关
powercfg /setacvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 238c9fa8-0aad-41ed-83f4-97be242c8f20 bd3b718a-0680-4d9d-8ab2-e1d2b4ac806d 0
powercfg /setdcvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 238c9fa8-0aad-41ed-83f4-97be242c8f20 bd3b718a-0680-4d9d-8ab2-e1d2b4ac806d 0
REM 远程唤醒 开
powercfg /setacvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 238c9fa8-0aad-41ed-83f4-97be242c8f20 d4c1d4c8-d5cc-43d3-b83e-fc51215cb04d 1
powercfg /setdcvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 238c9fa8-0aad-41ed-83f4-97be242c8f20 d4c1d4c8-d5cc-43d3-b83e-fc51215cb04d 1
REM USB3 LPM 0/0
powercfg /setacvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 2a737441-1930-4402-8d77-b2bebba308a3 d4e98f31-5ffe-4ce1-be31-1b38b384c009 0
powercfg /setdcvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 2a737441-1930-4402-8d77-b2bebba308a3 d4e98f31-5ffe-4ce1-be31-1b38b384c009 0
REM 开关睿频 (2=开 0=关)
powercfg /setacvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 54533251-82be-4824-96c1-47b60b740d00 be337238-0d82-4146-a960-4f3749d470c7 2
powercfg /setdcvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 54533251-82be-4824-96c1-47b60b740d00 be337238-0d82-4146-a960-4f3749d470c7 2
REM CPU调度 0/0 (0=最大性能 100=最大续航)
powercfg /setacvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 54533251-82be-4824-96c1-47b60b740d00 36687f9e-e3a5-4dbf-b1dc-15eb381c6863 0
powercfg /setdcvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 54533251-82be-4824-96c1-47b60b740d00 36687f9e-e3a5-4dbf-b1dc-15eb381c6863 0
powercfg /setacvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 54533251-82be-4824-96c1-47b60b740d00 36687f9e-e3a5-4dbf-b1dc-15eb381c6864 0
powercfg /setdcvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 54533251-82be-4824-96c1-47b60b740d00 36687f9e-e3a5-4dbf-b1dc-15eb381c6864 0
REM CPU最大主频 0/0 (0=最大性能)
powercfg /setacvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 54533251-82be-4824-96c1-47b60b740d00 75b0ae3f-bce0-45a7-8c89-c9611c25e100 0
powercfg /setdcvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 54533251-82be-4824-96c1-47b60b740d00 75b0ae3f-bce0-45a7-8c89-c9611c25e100 0
powercfg /setacvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 54533251-82be-4824-96c1-47b60b740d00 75b0ae3f-bce0-45a7-8c89-c9611c25e101 0
powercfg /setdcvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 54533251-82be-4824-96c1-47b60b740d00 75b0ae3f-bce0-45a7-8c89-c9611c25e101 0
REM 混合休眠 关
powercfg /setacvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 238c9fa8-0aad-41ed-83f4-97be242c8f20 94ac6d29-73ce-41a6-809f-6363ba21b47e 0
powercfg /setdcvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 238c9fa8-0aad-41ed-83f4-97be242c8f20 94ac6d29-73ce-41a6-809f-6363ba21b47e 0
REM 休眠 0/345600(4天)
powercfg /setacvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 238c9fa8-0aad-41ed-83f4-97be242c8f20 9d7815a6-7ee4-497e-8888-515a05f02364 0
powercfg /setdcvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 238c9fa8-0aad-41ed-83f4-97be242c8f20 9d7815a6-7ee4-497e-8888-515a05f02364 345600
REM 关键电池操作 0/2 (0=不操作 2=休眠)
powercfg /setacvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 e73a048d-bf27-4f12-9731-8b2076e8891f 637ea02f-bbcb-4015-8e2c-a1c7b9c0b546 0
powercfg /setdcvalueindex 1cb8b882-a900-4b9f-9bac-99d151e64441 e73a048d-bf27-4f12-9731-8b2076e8891f 637ea02f-bbcb-4015-8e2c-a1c7b9c0b546 2
