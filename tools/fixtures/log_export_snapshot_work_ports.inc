static bool snapshotWorkDelay=false;
static unsigned snapshotWorkRootCopies=0,snapshotWorkSteamCopies=0,snapshotWorkManifestCopies=0;
static std::atomic<unsigned> snapshotWorkInspections{0};
namespace ymcc::logexport {
inline bool snapshotWorkCopyPort(const fs::path& source,const fs::path& destination,DWORD& error,unsigned long long maxBytes=0,SnapshotCopyEvidence* evidence=nullptr,const SnapshotSourceSync* writerSync=nullptr,const char* boundaryField="monitorWriterBoundary"){
    const bool result=snapshotSharedLog(source,destination,error,maxBytes,evidence,writerSync,boundaryField);const auto saved=GetLastError();
    const bool steam=source.filename()==L"steam-source.log";
    if(steam)++snapshotWorkSteamCopies;else ++snapshotWorkRootCopies;
    if(snapshotWorkDelay)Sleep(steam?80:20);SetLastError(saved);return result;
}
inline EvidenceJson snapshotWorkInspectionPort(const std::filesystem::path& staged,const std::string& entry) noexcept {
    auto result=stagedFileEvidence(staged,entry);const auto saved=GetLastError();++snapshotWorkInspections;
    if(snapshotWorkDelay)Sleep(40);SetLastError(saved);return result;
}
}
static BOOL WINAPI snapshotWorkManifestPort(LPCWSTR source,LPCWSTR target,BOOL failIfExists){
    const auto result=::CopyFileW(source,target,failIfExists);const auto saved=GetLastError();++snapshotWorkManifestCopies;
    if(snapshotWorkDelay)Sleep(60);SetLastError(saved);return result;
}
#define snapshotSharedLog snapshotWorkCopyPort
#define stagedFileEvidence snapshotWorkInspectionPort
#define CopyFileW snapshotWorkManifestPort
