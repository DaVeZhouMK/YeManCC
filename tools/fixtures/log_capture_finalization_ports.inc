// Explicit phase only. Real same-EXE children and real access-right failures.
static bool captureDenyTermination=false,captureDenyFinalWait=false;
static HANDLE captureRootWitness=nullptr;
static unsigned captureTerminationCalls=0,capturePostWaitCalls=0;
static unsigned captureAccountingCalls=0,captureTerminationsAtAccounting=0;
static BOOL captureAccounting(HANDLE job,JOBOBJECTINFOCLASS kind,void* buffer,DWORD size,DWORD* returned) {
    ++captureAccountingCalls;captureTerminationsAtAccounting=captureTerminationCalls;
    return ::QueryInformationJobObject(job,kind,buffer,size,returned);
}
static BOOL captureTerminate(HANDLE job,UINT code) {
    ++captureTerminationCalls;
    if(!captureDenyTermination)return ::TerminateJobObject(job,code);
    HANDLE restricted=nullptr;
    if(!DuplicateHandle(GetCurrentProcess(),job,GetCurrentProcess(),&restricted,JOB_OBJECT_QUERY,FALSE,0))throw std::runtime_error("owned restricted job duplicate failed");
    const auto accepted=::TerminateJobObject(restricted,code);const DWORD error=accepted?0:GetLastError();
    CloseHandle(restricted);SetLastError(error);return accepted;
}
static DWORD captureWait(HANDLE process,DWORD allowance) {
    if(allowance!=1000)return ::WaitForSingleObject(process,allowance);
    ++capturePostWaitCalls;
    if(captureRootWitness)throw std::runtime_error("root witness was not returned");
    if(!DuplicateHandle(GetCurrentProcess(),process,GetCurrentProcess(),&captureRootWitness,SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,0))throw std::runtime_error("owned root witness duplicate failed");
    if(!captureDenyFinalWait)return ::WaitForSingleObject(process,allowance);
    HANDLE restricted=nullptr;
    if(!DuplicateHandle(GetCurrentProcess(),process,GetCurrentProcess(),&restricted,PROCESS_QUERY_LIMITED_INFORMATION,FALSE,0))throw std::runtime_error("owned restricted process duplicate failed");
    const auto wait=::WaitForSingleObject(restricted,allowance);const DWORD error=wait==WAIT_FAILED?GetLastError():0;
    CloseHandle(restricted);SetLastError(error);return wait;
}
#define TerminateJobObject captureTerminate
#define WaitForSingleObject captureWait
#define QueryInformationJobObject captureAccounting
