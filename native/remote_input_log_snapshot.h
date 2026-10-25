#pragma once
// Explicit export only. Preserve native SLOG ciphertext; never configure UU,
// decode its logs, inspect account files or follow a reparse-point candidate.
static std::wstring remoteInputLogPathValue(const json& value) {
    if(value.is_string())return U2W(value.get<std::string>());
    if(value.is_object()&&value.value("known",false)&&value.contains("value")&&value["value"].is_string())return U2W(value["value"].get<std::string>());
    return {};
}
static void exportRemoteInputLogSnapshots(json& evidence,std::vector<std::wstring>& copied) {
    using namespace ymcc::remoteInputEnvironmentEvidence;
    json receipts=json::array();std::uint64_t total=0;unsigned ordinal=0;
    if(!evidence.contains("logCandidates")||!evidence["logCandidates"].is_array())return;
    const ULONGLONG begin=GetTickCount64();
    for(const auto& candidate:evidence["logCandidates"]){
        if(++ordinal>4)break;
        json receipt={{"ordinal",ordinal},{"copied",false},{"plaintextDecoded",false},{"snapshotAtomic",false}};
        const auto path=candidate.contains("path")?remoteInputLogPathValue(candidate["path"]):std::wstring{};
        const auto root=fspath::path(path).parent_path().wstring();bool listedRoot=false;
        if(evidence.contains("logDiscovery")&&evidence["logDiscovery"].contains("roots")){
            for(const auto& item:evidence["logDiscovery"]["roots"]){
                if(item.contains("root")&&_wcsicmp(remoteInputLogPathValue(item["root"]).c_str(),root.c_str())==0&&item.contains("rootCheck")&&item["rootCheck"].value("known",false)){listedRoot=true;break;}
            }
        }
        const bool local=path.size()>3&&path[1]==L':'&&path[2]==L'\\';
        const bool slog=fspath::path(path).extension()==L".slog";
        if(!local||!listedRoot||!slog||path.find(L'\0')!=std::wstring::npos){receipt["reason"]="candidate-root-or-extension-not-verified";receipts.push_back(std::move(receipt));continue;}
        receipt["sourcePath"]=W2U(path);receipt["sourceRoot"]=W2U(root);
        if(GetTickCount64()-begin>1000){receipt["reason"]="export-copy-checkpoint-budget";receipts.push_back(std::move(receipt));continue;}
        const auto rootCheck=inspectLogRoot(root,GetTickCount64());
        if(!rootCheck.value("known",false)){receipt["reason"]="root-revalidation-failed";receipt["rootCheck"]=rootCheck;receipts.push_back(std::move(receipt));continue;}
        Handle source(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
        if(source.value==INVALID_HANDLE_VALUE){receipt["reason"]="source-open-failed";receipt["win32Error"]=GetLastError();receipts.push_back(std::move(receipt));continue;}
        BY_HANDLE_FILE_INFORMATION before{};LARGE_INTEGER size{};std::array<wchar_t,32768> finalName{};
        const DWORD finalChars=GetFinalPathNameByHandleW(source.value,finalName.data(),static_cast<DWORD>(finalName.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
        std::wstring finalPath=finalChars&&finalChars<finalName.size()?std::wstring(finalName.data(),finalChars):std::wstring{};
        if(finalPath.rfind(L"\\\\?\\",0)==0)finalPath.erase(0,4);
        if(GetFileType(source.value)!=FILE_TYPE_DISK||!GetFileInformationByHandle(source.value,&before)||
            (before.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||_wcsicmp(finalPath.c_str(),path.c_str())!=0||!GetFileSizeEx(source.value,&size)||size.QuadPart<0){
            receipt["reason"]="opened-file-identity-or-type-rejected";receipts.push_back(std::move(receipt));continue;
        }
        const auto initialBytes=static_cast<std::uint64_t>(size.QuadPart);
        const auto available=maxCandidateLogBytes-total;
        const auto wanted=(std::min)(initialBytes,available);
        receipt["sourceBytesAtOpen"]=initialBytes;receipt["snapshotByteLimit"]=wanted;
        if(wanted<8){receipt["reason"]="empty-or-byte-budget-exhausted";receipts.push_back(std::move(receipt));continue;}
        std::string bytes(static_cast<std::size_t>(wanted),'\0');std::size_t offset=0;DWORD readError=ERROR_SUCCESS;
        while(offset<bytes.size()){
            DWORD got=0;const DWORD request=static_cast<DWORD>((std::min)(bytes.size()-offset,std::size_t(65536)));
            if(!ReadFile(source.value,bytes.data()+offset,request,&got,nullptr)){readError=GetLastError();break;}
            if(!got)break;offset+=got;
            if(GetTickCount64()-begin>1000)break;
        }
        bytes.resize(offset);total+=offset;
        receipt["readBytes"]=offset;receipt["readWin32Error"]=readError;
        receipt["initialLengthReadCompletely"]=offset==initialBytes;
        BY_HANDLE_FILE_INFORMATION after{};const bool afterKnown=GetFileInformationByHandle(source.value,&after)!=FALSE;
        receipt["sourceMetadataAfterKnown"]=afterKnown;
        receipt["sourceChangedDuringRead"]=afterKnown?json(before.nFileSizeHigh!=after.nFileSizeHigh||before.nFileSizeLow!=after.nFileSizeLow||CompareFileTime(&before.ftLastWriteTime,&after.ftLastWriteTime)!=0):json(nullptr);
        if(bytes.size()<8||std::memcmp(bytes.data(),"SLOG\r\n\x1a\n",8)!=0){receipt["reason"]="native-SLOG-header-not-observed";receipts.push_back(std::move(receipt));continue;}
        const auto name=L"uu-native-"+std::to_wstring(ordinal)+L".slog";
        const auto destination=SG_DIR+L"\\steam-input-log-tails\\"+name;
        if(sgWriteFileAtomic(destination,bytes)){copied.push_back(destination);receipt["copied"]=true;receipt["exportName"]=W2U(name);receipt["reason"]="native-ciphertext-snapshot";}
        else receipt["reason"]="snapshot-write-failed";
        receipts.push_back(std::move(receipt));
    }
    evidence["logSnapshots"]={{"files",std::move(receipts)},{"readBytes",total},{"totalByteLimit",maxCandidateLogBytes},{"maxFiles",4},{"elapsedMs",GetTickCount64()-begin},{"plaintextDecoded",false},{"sourceFilesModified",false},{"captureAtExportOnly",true},
        {"scope","Native SLOG ciphertext snapshot;may end inside an active frame, truncate at budget, or span source changes;header match is not authentication,content validation,version or event ownership proof. Checkpoints are not hard per-API timeouts."}};
}