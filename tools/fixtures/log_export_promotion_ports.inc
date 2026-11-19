static std::function<void(const wchar_t*,BOOL)> promotionAfterOriginalCall;
static std::function<void(const wchar_t*,DWORD)> promotionAfterOriginalOpen;
static HANDLE promotionFixtureOpen(const wchar_t* path,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES sa,
        DWORD disposition,DWORD flags,HANDLE templateFile) {
    const HANDLE result=::CreateFileW(path,access,share,sa,disposition,flags,templateFile);
    const DWORD error=GetLastError();
    if(promotionAfterOriginalOpen&&access==GENERIC_READ)promotionAfterOriginalOpen(path,result==INVALID_HANDLE_VALUE?error:ERROR_SUCCESS);
    SetLastError(error);return result;
}
static BOOL promotionFixtureMove(const wchar_t* from,const wchar_t* to,DWORD flags) {
    const BOOL result=::MoveFileExW(from,to,flags);
    const DWORD error=GetLastError();
    if(promotionAfterOriginalCall)promotionAfterOriginalCall(to,result);
    SetLastError(error);return result;
}
#define MoveFileExW promotionFixtureMove
#define CreateFileW promotionFixtureOpen
