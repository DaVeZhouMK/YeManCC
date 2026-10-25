// Only the file-stat and bounded-read boundaries below are inert. Production
// msiCrashLoadLocked/Initialize definitions are inserted verbatim by extend-fixture.mjs.
constexpr DWORD INVALID_FILE_ATTRIBUTES=0xfffffffful;
constexpr DWORD ERROR_SUCCESS=0,ERROR_FILE_NOT_FOUND=2,ERROR_PATH_NOT_FOUND=3,ERROR_ACCESS_DENIED=5;
constexpr DWORD ERROR_NOT_READY=21,ERROR_SHARING_VIOLATION=32,ERROR_LOCK_VIOLATION=33,ERROR_BUSY=170;
constexpr DWORD ERROR_IO_DEVICE=1117,ERROR_RETRY=1237;
static DWORD startupFileAttributes=0,startupFileError=0,startupReadError=ERROR_ACCESS_DENIED;
static bool startupReadOk=true;
static std::string startupFileBytes;
static unsigned startupStatCalls=0,startupReadCalls=0,startupLegacyCalls=0;
static DWORD msiCrashReadOwnedRecord(const std::wstring&,std::string& text){
 ++startupStatCalls;text.clear();if(startupFileAttributes==INVALID_FILE_ATTRIBUTES)return startupFileError;
 ++startupReadCalls;if(!startupReadOk)return startupReadError;
 if(startupFileBytes.size()>65536)return 13;text=startupFileBytes;return ERROR_SUCCESS;
}
static bool msiCrashLegacyRecord(msiRecovery::Record&){++startupLegacyCalls;return false;}
static bool msiCrashHasDuty(const msiRecovery::Record&);
static bool msiCrashStoreLocked(const msiRecovery::Record&,const char*);
static void msiCrashNote(const char*,const char*,bool=false);
@@ACTUAL_RETRY_ERROR@@
@@ACTUAL_LOAD_FAILURE@@
@@ACTUAL_LOAD@@
@@ACTUAL_INITIALIZE@@
