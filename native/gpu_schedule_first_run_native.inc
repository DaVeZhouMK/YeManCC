#include "gpu_schedule_first_run.h"
// Configuration bootstrap only: no timer, resume hook or replay after settings exist.
// The normal single-instance owner calls this once after legacy-directory migration,
// before native modules or the renderer can create their first settings document.
static void initializeGpuScheduleFirstRunDefault() {
    if(g_aiFanMockSession.isolated)return;
    try {
        SettingsFileGuard guard;
        std::error_code error;
        const bool mainExists=fspath::exists(YM_SETTINGS_FILE,error);
        if(error) {appendNativeLifecycleLog("gpu-schedule-first-run",{{"ok",false},{"stage","main-configuration-probe"},{"error",error.value()}});return;}
        const bool backupExists=fspath::exists(YM_SETTINGS_BACKUP_FILE,error);
        if(error) {appendNativeLifecycleLog("gpu-schedule-first-run",{{"ok",false},{"stage","backup-configuration-probe"},{"error",error.value()}});return;}
        // Even empty/corrupt/backup-only configurations represent an existing install.
        if(mainExists||backupExists)return;
        ymccResolveMachineIdentityOnce(); // Reuse the process-wide cached CPU identity.
        const auto& processor=g_machineIdentity.processor;
        if(!ymcc::gpuschedule::isAmd395(processor))return;
        constexpr auto key=L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers";
        constexpr auto value=L"HwSchMode";
        DWORD before=0,bytes=sizeof(before);
        const LONG beforeResult=RegGetValueW(HKEY_LOCAL_MACHINE,key,value,RRF_RT_REG_DWORD,nullptr,&before,&bytes);
        if(beforeResult!=ERROR_SUCCESS&&beforeResult!=ERROR_FILE_NOT_FOUND) {
            appendNativeLifecycleLog("gpu-schedule-first-run",{{"ok",false},{"stage","read"},{"error",beforeResult},{"processor",processor}});return;
        }
        bool wrote=false;LONG result=ERROR_SUCCESS;
        if(beforeResult!=ERROR_SUCCESS||before!=1) {
            HKEY handle=nullptr;
            result=RegCreateKeyExW(HKEY_LOCAL_MACHINE,key,0,nullptr,0,KEY_SET_VALUE|KEY_QUERY_VALUE,nullptr,&handle,nullptr);
            if(result==ERROR_SUCCESS) {
                const DWORD disabled=1;
                result=RegSetValueExW(handle,value,0,REG_DWORD,reinterpret_cast<const BYTE*>(&disabled),sizeof(disabled));
                wrote=result==ERROR_SUCCESS;RegCloseKey(handle);
            }
        }
        DWORD after=0;bytes=sizeof(after);
        const LONG readback=result==ERROR_SUCCESS?RegGetValueW(HKEY_LOCAL_MACHINE,key,value,RRF_RT_REG_DWORD,nullptr,&after,&bytes):result;
        const bool ok=result==ERROR_SUCCESS&&readback==ERROR_SUCCESS&&after==1;
        appendNativeLifecycleLog("gpu-schedule-first-run",{{"ok",ok},{"stage",result!=ERROR_SUCCESS?"write":readback!=ERROR_SUCCESS||after!=1?"readback":"complete"},
            {"processor",processor},{"configuration","main-and-backup-absent"},{"requestedEnabled",false},
            {"previousValue",beforeResult==ERROR_SUCCESS?json(before):json(nullptr)},{"readbackValue",readback==ERROR_SUCCESS?json(after):json(nullptr)},
            {"writeSucceeded",wrote},{"error",result!=ERROR_SUCCESS?result:readback!=ERROR_SUCCESS?readback:after!=1?ERROR_INVALID_DATA:ERROR_SUCCESS},{"restartRequired",ok&&wrote},{"retryScheduled",false}});
    } catch(const std::exception& exception) {
        appendNativeLifecycleLog("gpu-schedule-first-run",{{"ok",false},{"stage","exception"},{"reason",exception.what()},{"retryScheduled",false}});
    }
}
