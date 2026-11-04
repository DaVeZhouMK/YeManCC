#include <initguid.h>
#include "../native/fxsound_recovery.h"
#include <shlobj.h>
#include <knownfolders.h>
static std::wstring fxOwnedRoaming;
static HRESULT WINAPI fxTestKnownFolder(REFKNOWNFOLDERID folder,DWORD,HANDLE,PWSTR* output) {
    if(!output || fxOwnedRoaming.empty() || !IsEqualGUID(folder,FOLDERID_RoamingAppData))return E_ACCESSDENIED;
    const auto bytes=(fxOwnedRoaming.size()+1)*sizeof(wchar_t);*output=static_cast<PWSTR>(CoTaskMemAlloc(bytes));if(!*output)return E_OUTOFMEMORY;
    memcpy(*output,fxOwnedRoaming.c_str(),bytes);return S_OK;
}
#include <map>
#include <fstream>
#include <filesystem>
static std::filesystem::path fxAllowedFileDirectory;
static std::map<HANDLE,std::filesystem::path> fxOwnedFiles;
static bool fxDenyTemp=false,fxFailWrite=false,fxShortWrite=false,fxFailFlush=false,fxDenyArchive=false,fxFailPublish=false,fxCorruptPublished=false;
static HANDLE WINAPI fxTestCreate(LPCWSTR name,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,DWORD creation,DWORD flags,HANDLE templ){
    const auto path=std::filesystem::path(name).lexically_normal();if(fxAllowedFileDirectory.empty()||path.parent_path()!=fxAllowedFileDirectory||!path.filename().wstring().starts_with(L"FxSound.settings")){SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}
    if(fxDenyTemp&&creation==CREATE_NEW){SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}
    const auto result=::CreateFileW(name,access,share,security,creation,flags,templ);if(result!=INVALID_HANDLE_VALUE)fxOwnedFiles[result]=path;return result;
}
static BOOL WINAPI fxTestClose(HANDLE handle){fxOwnedFiles.erase(handle);return ::CloseHandle(handle);}
static BOOL WINAPI fxTestWrite(HANDLE handle,LPCVOID bytes,DWORD count,LPDWORD written,LPOVERLAPPED overlapped){
    if(!fxOwnedFiles.contains(handle)){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}if(fxFailWrite){SetLastError(ERROR_WRITE_FAULT);return FALSE;}
    return ::WriteFile(handle,bytes,fxShortWrite&&count?count-1:count,written,overlapped);
}
static BOOL WINAPI fxTestFlush(HANDLE handle){if(!fxOwnedFiles.contains(handle)||fxFailFlush){SetLastError(ERROR_WRITE_FAULT);return FALSE;}return ::FlushFileBuffers(handle);}
static BOOL WINAPI fxTestMove(LPCWSTR from,LPCWSTR to,DWORD flags){
    const auto source=std::filesystem::path(from).lexically_normal(),target=std::filesystem::path(to).lexically_normal();
    if(source.parent_path()!=fxAllowedFileDirectory||target.parent_path()!=fxAllowedFileDirectory||!source.filename().wstring().starts_with(L"FxSound.settings")||!target.filename().wstring().starts_with(L"FxSound.settings")){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    const bool archive=target.filename().wstring().starts_with(L"FxSound.settings.ymcc-invalid-");const bool publish=source.extension()==L".tmp";
    if((archive&&fxDenyArchive)||(publish&&fxFailPublish)){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    const auto result=::MoveFileExW(from,to,flags);if(result&&publish&&fxCorruptPublished){std::ofstream out(target,std::ios::binary|std::ios::trunc);out<<"{external-corrupt";}return result;
}
#define CreateFileW fxTestCreate
#define CloseHandle fxTestClose
#define WriteFile fxTestWrite
#define FlushFileBuffers fxTestFlush
#define MoveFileExW fxTestMove
#define SHGetKnownFolderPath fxTestKnownFolder
#include "../native/fxsound_control.h"
#undef SHGetKnownFolderPath
#undef CreateFileW
#undef CloseHandle
#undef WriteFile
#undef FlushFileBuffers
#undef MoveFileExW
#include <iostream>
#include <stdexcept>
#include <functional>
#include <iomanip>
#include <map>
#include <set>
using namespace ymcc::fxsound;
static int checks = 0;
static void check(bool ok, const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
static Snapshot sample() {
    Snapshot s; s.valid = true; s.process = ProcessState::stopped;
    s.devices = {{L"fx",L"FxSound Speakers (FxSound Audio Enhancer)",true,false,1},
                 {L"speakers",L"Speakers",false,true,1}, {L"phones",L"Headphones",false,true,3},
                 {L"virtual",L"Virtual Cable",false,false,1}};
    s.defaults = {L"fx",L"fx",L"phones"}; s.preferred = {L"speakers"}; return s;
}
struct MemoryPort {
    Snapshot state = sample();
    std::vector<unsigned> writes;
    std::function<void(MemoryPort&)> beforeRead;
    int reads = 0; bool fail = false;
    Snapshot read() { ++reads; if (beforeRead) beforeRead(*this); return state; }
    SetResult setDefault(unsigned role, const std::wstring&, const std::wstring& target) {
        writes.push_back(role); if (fail) return {false,5}; state.defaults[role] = target; return {true,0};
    }
};
static std::vector<Attempt> confirm(Recovery& r, MemoryPort& p, std::uint64_t start = 0) {
    std::vector<Attempt> result;
    for (std::uint64_t t = start; t <= start + 3000; t += 500) {
        auto batch = r.step(p,t,true); result.insert(result.end(),batch.begin(),batch.end());
    }
    return result;
}
static void tests() {
    {
        Recovery r; MemoryPort p;
        for (unsigned t = 0; t <= 2500; t += 500) check(r.step(p,t,true).empty(),"must wait 3 seconds");
        check(r.step(p,2999,true).empty(),"2999ms is too early");
        const auto result = r.step(p,3000,true);
        check(result.size()==2 && result[0].ok && result[1].ok,"repair console and multimedia once");
        check(p.state.defaults[0]==L"speakers" && p.state.defaults[1]==L"speakers","restore recorded system default");
        check(p.state.defaults[2]==L"phones" && p.writes.size()==2,"preserve real communications default");
        check(r.finished,"successful startup ends detection");
        const auto reads=p.reads;p.state.defaults={L"fx",L"fx",L"fx"};
        for(unsigned t=3500;t<=20000;t+=500)check(r.step(p,t,true).empty(),"later FxSound use must never trigger recovery");
        check(p.reads==reads && p.writes.size()==2,"no reads or writes after startup");
    }
    for (auto state : {ProcessState::running, ProcessState::unknown}) {
        Recovery r; MemoryPort p; p.state.process=state;
        check(confirm(r,p).empty() && p.writes.empty() && r.finished,"running/unknown at launch ends detection");
        const auto reads=p.reads;p.state.process=ProcessState::stopped;
        check(confirm(r,p,6000).empty()&&p.reads==reads,"later FxSound exit never rearms detection");
    }
    {
        Recovery r; MemoryPort p;p.state.defaults={L"speakers",L"speakers",L"phones"};
        check(confirm(r,p).empty()&&r.finished,"normal output at launch ends detection");
        const auto reads=p.reads;p.state.defaults={L"fx",L"fx",L"phones"};
        check(confirm(r,p,5000).empty()&&p.reads==reads,"changing to FxSound later never triggers repair");
    }
    {
        Recovery r;MemoryPort p;r.step(p,0,true);r.step(p,500,true);
        p.state.process=ProcessState::running;check(r.step(p,1000,true).empty()&&r.finished,"launch during countdown cancels");
        const auto reads=p.reads;p.state.process=ProcessState::stopped;
        check(confirm(r,p,1500).empty()&&p.reads==reads&&p.writes.empty(),"cancelled startup never rearms");
    }
    {
        Recovery r;MemoryPort p;r.step(p,0,true);r.step(p,500,true);r.step(p,1000,false);
        const auto reads=p.reads;
        check(confirm(r,p,1500).empty()&&p.reads==reads&&p.writes.empty(),"OFF then ON cannot rearm this launch");
    }
    {
        Recovery r;MemoryPort p;r.step(p,0,false);
        check(confirm(r,p,500).empty()&&p.reads==0,"enabling after disabled startup waits for next launch");
    }
    {
        Recovery r;MemoryPort p;r.step(p,0,true);r.step(p,500,true);
        check(r.step(p,60000,true).empty()&&r.finished,"sleep gap cancels startup instead of repairing on resume");
        const auto reads=p.reads;check(confirm(r,p,60500).empty()&&p.reads==reads,"resume never rearms startup");
    }
    {
        Recovery r;MemoryPort p;p.beforeRead=[](MemoryPort& m){if(m.reads==8)m.state.process=ProcessState::running;};
        check(confirm(r,p).empty()&&p.writes.empty()&&r.finished,"FxSound starts at commit boundary");
    }
    {
        Recovery r;MemoryPort p;p.beforeRead=[](MemoryPort& m){if(m.reads==8)m.state.defaults={L"phones",L"phones",L"phones"};};
        check(confirm(r,p).empty()&&p.writes.empty(),"user changes output at commit boundary");
    }
    {
        Recovery r;MemoryPort p;r.step(p,0,true);p.state.defaults={L"phones",L"phones",L"phones"};
        check(r.step(p,500,true).empty()&&r.finished,"user changes output during countdown");
        const auto reads=p.reads;p.state.defaults={L"fx",L"fx",L"phones"};
        check(confirm(r,p,1000).empty()&&p.reads==reads,"output changes never restart confirmation");
    }
    {
        Recovery r;MemoryPort p;p.state.preferred={L"missing",L"virtual",L"fx"};
        const auto result=confirm(r,p);
        check(result.size()==2&&result[0].target==L"phones","ignore missing/virtual records and prefer available headphones");
    }
    {
        Recovery r;MemoryPort p;p.state.devices.resize(1);
        const auto result=confirm(r,p);
        check(result.size()==2&&!result[0].ok&&result[0].target.empty()&&p.writes.empty(),"no physical output never writes");
        const auto reads=p.reads;check(confirm(r,p,5000).empty()&&p.reads==reads,"no-target failure never retries");
    }
    {
        Recovery r;MemoryPort p;p.state.valid=false;
        check(confirm(r,p).empty()&&p.writes.empty()&&r.finished,"enumeration failure cancels startup");
    }
    {
        Recovery r;MemoryPort p;p.fail=true;const auto result=confirm(r,p);
        check(result.size()==2&&!result[0].ok&&p.writes.size()==2,"failed writes have explicit receipts");
        const auto reads=p.reads;p.fail=false;
        check(confirm(r,p,100000).empty()&&p.reads==reads&&p.writes.size()==2,"write failure never retries later");
    }
    {
        Recovery r;MemoryPort p;p.state.defaults={L"speakers",L"speakers",L"fx"};const auto result=confirm(r,p);
        check(result.size()==1&&result[0].role==2,"only orphaned communications default is repaired");
    }
    {
        Recovery r;MemoryPort p;r.step(p,0,true);p.state.devices.push_back({L"fx2",L"FxSound",true,false,1});
        p.state.defaults={L"fx2",L"fx2",L"phones"};
        check(r.step(p,500,true).empty()&&r.finished,"different FxSound endpoint cancels startup");
    }
    {
        Recovery r;MemoryPort p;r.step(p,1000,true);
        check(r.step(p,500,true).empty()&&r.finished,"clock rollback cancels startup");
    }
    {
        Recovery r;MemoryPort p;p.state.defaults={L"fx",L"fx",L"fx"};
        const auto result=confirm(r,p);check(result.size()==3&&p.writes.size()==3,"all three orphaned roles repair in one startup transaction");
    }
}
static void silentControlTests() {
    const std::string before="<?xml version=\"1.0\"?><PROPERTIES>\n<VALUE name=\"preset\" val=\"General\"/>\n<VALUE name=\"power\" val=\"0\"/>\n<VALUE name=\"run_minimized\" val=\"0\"/>\n<VALUE name=\"version\" val=\"1.0.0.0\"/>\n<VALUE name=\"output_device_id\" val=\"player-speaker\"/>\n<VALUE name=\"balance\" val=\"-7.25\"/>\n<VALUE name=\"future_setting\" val=\"opaque&amp;content\"/>\n</PROPERTIES>";
    std::string after;std::wstring reason;
    check(rewriteConfig(before,"1.2.6.0",after,reason),"silent config rewrites valid XML");
    for(const auto* literal : {"<VALUE name=\"output_device_id\" val=\"player-speaker\"/>","<VALUE name=\"balance\" val=\"-7.25\"/>","<VALUE name=\"future_setting\" val=\"opaque&amp;content\"/>"})check(after.find(literal)!=std::string::npos,"unrelated configuration remains byte-identical");
    check(after.find("name=\"power\" val=\"1\"")!=std::string::npos,"effects enabled only through config");
    check(after.find("name=\"run_minimized\" val=\"1\"")!=std::string::npos,"hidden startup requested");
    check(after.find("name=\"hide_notifications\" val=\"1\"")!=std::string::npos,"FxSound notifications suppressed");
    check(after.find("name=\"version\" val=\"1.2.6.0\"")!=std::string::npos,"version-change foreground path suppressed");
    std::string again;check(rewriteConfig(after,"1.2.6.0",again,reason)&&again==after,"repeated ON config is idempotent");
    check(!rewriteConfig("<PROPERTIES><VALUE", "1.2.6.0",again,reason),"damaged config is not reset");
    check(!rewriteConfig("<!DOCTYPE PROPERTIES [<!ENTITY x SYSTEM 'file:///bad'>]><PROPERTIES>&x;</PROPERTIES>","1.2.6.0",again,reason),"external entities are rejected");
    check(rewriteConfig("","1.2.6.0",again,reason)&&again.find("survey_displayed")!=std::string::npos&&again.find("output_device_id")==std::string::npos,"fresh seed has no source-machine speaker ID");
    auto folder=std::filesystem::temp_directory_path()/(L"ymcc-fxsound-unit-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(folder);
    fxAllowedFileDirectory=folder.lexically_normal();
    const auto backup=folder/L"backup.fac",target=folder/L"user"/presetFile;
    {std::ofstream out(backup,std::ios::binary);out<<"opaque preset bytes";}
    bool copied=false;check(supplementPreset(target,{folder/L"missing.fac",backup},copied,reason)&&copied,"missing preset uses available backup");
    {std::ofstream out(target,std::ios::binary);out<<"player edited opaque bytes";}
    copied=false;check(supplementPreset(target,{backup},copied,reason)&&!copied,"existing edited preset is never replaced");
    {std::ifstream in(target,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(in)),{});check(bytes=="player edited opaque bytes","existing preset bytes remain unchanged");}
    std::filesystem::remove(target);copied=false;check(supplementPreset(target,{backup},copied,reason)&&copied,"deletion is the only reset trigger");
    copied=true;check(supplementPreset(target,{backup},copied,reason)&&!copied,"reused opaque preset never reports a fresh copy");
    const auto config=folder/L"FxSound.settings";
    std::string fileBytes;bool exists=false;
    check(readConfigFile(config,fileBytes,exists,reason)&&!exists,"only missing config is reported absent");
    check(prepareConfigAt(config,"1.2.6.0",[]{return true;},reason),"missing settings are actually seeded atomically");
    check(readConfigFile(config,fileBytes,exists,reason)&&exists&&fileBytes.find("output_device_id")==std::string::npos,"new settings omit source-machine hardware IDs");
    {std::ofstream out(config,std::ios::binary);out<<before;}
    HANDLE locked=CreateFileW(config.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(locked!=INVALID_HANDLE_VALUE,"exclusive config lock acquired");
    check(!readConfigFile(config,fileBytes,exists,reason),"unreadable config is never treated as absent");
    check(!prepareConfigAt(config,"1.2.6.0",[]{return true;},reason),"locked settings are not replaced or seeded");
    CloseHandle(locked);
    check(readConfigFile(config,fileBytes,exists,reason)&&fileBytes==before,"locked settings retain every original byte");
    {std::ofstream out(config,std::ios::binary|std::ios::trunc);}
    fxDenyArchive=true;
    check(!prepareConfigAt(config,"1.2.6.0",[]{return true;},reason),"existing empty settings are not silently reset");
    check(readConfigFile(config,fileBytes,exists,reason)&&exists&&fileBytes.empty(),"empty existing settings remain empty");
    fxDenyArchive=false;
    {std::ofstream out(config,std::ios::binary);out<<before;}
    int checkpoint=0;
    check(!prepareConfigAt(config,"1.2.6.0",[&]{if(++checkpoint==2){std::ofstream out(config,std::ios::binary);out<<"external update";}return true;},reason),"concurrent config replacement cancels commit");
    check(readConfigFile(config,fileBytes,exists,reason)&&fileBytes=="external update","concurrent writer's bytes survive");
    std::filesystem::remove(config);checkpoint=0;
    check(!prepareConfigAt(config,"1.2.6.0",[&]{if(++checkpoint==2){std::ofstream out(config,std::ios::binary);}return true;},reason),"new empty file created by another owner cancels missing-file seed");
    check(readConfigFile(config,fileBytes,exists,reason)&&exists&&fileBytes.empty(),"concurrent empty file is not replaced");
    std::filesystem::remove(config);
    check(!prepareConfigAt(config,"1.2.6.0",[]{return false;},reason),"external process startup cancels settings write");
    check(!std::filesystem::exists(config),"process startup refusal creates no configuration");
    auto temp=config;temp+=L".ymcc-"+std::to_wstring(GetCurrentProcessId())+L".tmp";
    check(!std::filesystem::exists(temp),"all refused commits release their temporary file");
    // Delete only the three explicitly created test files, never a recursive root.
    std::filesystem::remove(target);std::filesystem::remove(backup);std::filesystem::remove(target.parent_path());std::filesystem::remove(folder);
}
static std::string utf8(const std::wstring& value) {
    if(value.empty())return {};
    const int size=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    std::string result(size,'\0');WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),size,nullptr,nullptr);return result;
}

static std::map<std::wstring,std::vector<std::wstring>> inspectConfig(const std::string& bytes) {
    ComPtr<IStream> stream;stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.data()),static_cast<UINT>(bytes.size())));
    ComPtr<IXmlReader> xml;check(stream&&SUCCEEDED(CreateXmlReader(__uuidof(IXmlReader),reinterpret_cast<void**>(xml.GetAddressOf()),nullptr))&&SUCCEEDED(xml->SetInput(stream.Get())),"independent XML reader setup");
    xml->SetProperty(XmlReaderProperty_DtdProcessing,DtdProcessing_Prohibit);xml->SetProperty(XmlReaderProperty_MaxElementDepth,64);
    std::map<std::wstring,std::vector<std::wstring>> fields;XmlNodeType type{};HRESULT hr;bool root=false;
    while((hr=xml->Read(&type))==S_OK){if(type!=XmlNodeType_Element)continue;UINT depth=0;xml->GetDepth(&depth);const wchar_t* tag=nullptr;xml->GetLocalName(&tag,nullptr);
        if(depth==0){check(tag&&wcscmp(tag,L"PROPERTIES")==0,"output root is not PROPERTIES");root=true;}
        if(depth!=1||!tag||wcscmp(tag,L"VALUE"))continue;std::wstring key,value;const wchar_t* text=nullptr;
        if(SUCCEEDED(xml->MoveToAttributeByName(L"name",nullptr))&&SUCCEEDED(xml->GetValue(&text,nullptr))&&text)key=text;xml->MoveToElement();
        if(SUCCEEDED(xml->MoveToAttributeByName(L"val",nullptr))&&SUCCEEDED(xml->GetValue(&text,nullptr))&&text)value=text;xml->MoveToElement();fields[key].push_back(value);
    }
    check(root&&hr==S_FALSE,"output XML is incomplete or invalid");return fields;
}
static int configStorm(const std::filesystem::path& supplied) {
    const auto root=std::filesystem::absolute(supplied).lexically_normal();check(root.is_absolute(),"owned storm root must be absolute");std::filesystem::create_directories(root/L"FxSound");fxOwnedRoaming=root.wstring();
    const auto file=root/L"FxSound"/L"FxSound.settings";unsigned count=0,failed=0;fxAllowedFileDirectory=file.parent_path();
    const auto test=[&](const std::string& name,const std::function<void()>& run){++count;try{run();std::cout<<"FX_CONFIG_CASE\t"<<count<<"\tPASS\t"<<name<<"\n";}catch(const std::exception& error){++failed;std::cout<<"FX_CONFIG_CASE\t"<<count<<"\tFAIL\t"<<name<<"\t"<<error.what()<<"\n";}};
    const auto put=[&](const std::string& data){std::ofstream out(file,std::ios::binary|std::ios::trunc);out.write(data.data(),static_cast<std::streamsize>(data.size()));check(out.good(),"owned fixture setup failed");};
    const auto read=[&](){std::ifstream in(file,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});};
    const auto archives=[&](){std::set<std::filesystem::path> result;for(const auto& item:std::filesystem::directory_iterator(file.parent_path()))if(item.path().filename().wstring().starts_with(L"FxSound.settings.ymcc-invalid-"))result.insert(item.path());return result;};
    const auto exactArchive=[&](const std::set<std::filesystem::path>& before,const std::string& raw){auto after=archives();std::vector<std::filesystem::path> added;for(const auto& path:after)if(!before.contains(path))added.push_back(path);check(added.size()==1,"one exact bad original was not archived");std::ifstream in(added.front(),std::ios::binary);check(std::string(std::istreambuf_iterator<char>(in),{})==raw,"bad original archive differs");};
    const std::string standard="<?xml version=\"1.0\"?><PROPERTIES><VALUE name=\"preset\" val=\"General\"/><VALUE name=\"power\" val=\"0\"/><VALUE name=\"output_device_id\" val=\"speaker-user\"/></PROPERTIES>";
    const auto validate=[&](const std::string& out){const auto values=inspectConfig(out);for(const auto& row:std::vector<std::pair<std::wstring,std::wstring>>{{L"preset",presetName},{L"power",L"1"},{L"run_minimized",L"1"},{L"hide_notifications",L"1"},{L"version",L"1.2.6.0"}}){const auto it=values.find(row.first);check(it!=values.end()&&it->second.size()==1&&it->second.front()==row.second,"managed output missing, duplicated or wrong");}};
    const std::vector<std::pair<std::string,std::string>> formats={
      {"standard",standard},{"single quotes","<PROPERTIES><VALUE name='power' val='0'/><VALUE name='preset' val='General'/></PROPERTIES>"},
      {"attribute order","<PROPERTIES><VALUE val=\"0\" name=\"power\"/><VALUE val=\"General\" name=\"preset\"/></PROPERTIES>"},
      {"unknown attributes","<PROPERTIES custom='keep'><VALUE name='power' unknown='keep' val='0'/><VALUE name='preset' val='General'/></PROPERTIES>"},
      {"selfclosing root","<PROPERTIES/>"},{"whitespace root","<PROPERTIES ></PROPERTIES >"},
      {"duplicate managed field","<PROPERTIES><VALUE name=\"power\" val=\"0\"/><VALUE name=\"power\" val=\"1\"/><VALUE name=\"preset\" val=\"General\"/></PROPERTIES>"},
      {"nested same name","<PROPERTIES><GROUP><VALUE name=\"power\" val=\"77\"/></GROUP><VALUE name=\"power\" val=\"0\"/></PROPERTIES>"},
      {"comment fake close","<PROPERTIES><!-- </PROPERTIES> <VALUE name='power' val='9'/> --></PROPERTIES>"},
      {"CDATA fake close","<PROPERTIES><opaque><![CDATA[</PROPERTIES>]]></opaque></PROPERTIES>"},
      {"entity managed name","<PROPERTIES><VALUE name='p&#111;wer' val='0'/></PROPERTIES>"},
      {"UTF8 BOM",std::string("\xef\xbb\xbf")+standard}
    };
    for(const auto& row:formats)test("rewrite "+row.first,[&]{std::string output="unchanged",again;std::wstring reason;check(rewriteConfig(row.second,"1.2.6.0",output,reason),"legal XML rewrite failed");validate(output);check(rewriteConfig(output,"1.2.6.0",again,reason)&&again==output,"rewrite not idempotent");
      if(row.first=="nested same name")check(output.find("<VALUE name=\"power\" val=\"77\"/>")!=std::string::npos,"nested unowned value modified");
      if(row.first=="standard")check(output.find("<VALUE name=\"output_device_id\" val=\"speaker-user\"/>")!=std::string::npos,"unowned output ID not byte-identical");});
    test("rewrite UTF16 preserves encoding and unknown values",[&]{const std::wstring text=L"<?xml version='1.0' encoding='UTF-16'?><PROPERTIES><VALUE name='output_device_id' val='user-device'/></PROPERTIES>";std::string raw("\xff\xfe",2);raw.append(reinterpret_cast<const char*>(text.data()),text.size()*sizeof(wchar_t));std::string output;std::wstring reason;check(rewriteConfig(raw,"1.2.6.0",output,reason),"UTF16 rewrite failed");validate(output);check(output.starts_with(std::string("\xff\xfe",2))&&inspectConfig(output).at(L"output_device_id").front()==L"user-device","encoding or user value changed");});
    for(const auto& version:std::vector<std::string>{"", "1.2.6.0\" /><oops/>","1.2.6.0&bad","1.2.6.999999999999","x.y.z"})test("invalid version "+version,[&]{std::string output="unchanged";std::wstring reason;check(!rewriteConfig(standard,version,output,reason)&&output=="unchanged","invalid version accepted or output partially changed");});
    for(const auto& raw:std::vector<std::string>{"<NOT_PROPERTIES><VALUE name='preset' val='General'/><VALUE name='power' val='1'/></NOT_PROPERTIES>","<PROPERTIES><VALUE name='preset' val='General'/><VALUE name='power' val='1'/><broken", "<PROPERTIES><VALUE name='preset' val='General'/></PROPERTIES>","<PROPERTIES><VALUE name='preset' val='General'/><VALUE name='power' val='1'/><VALUE name='power' val='bad'/></PROPERTIES>"})test("reader atomic invalid case "+std::to_string(count),[&]{put(raw);std::wstring selected=L"unchanged";bool power=false;check(!settings(selected,power)&&selected==L"unchanged"&&!power,"invalid file mutated reader outputs or returned confirmed state");});
    test("reader duplicate fields never confirms ON",[&]{put("<PROPERTIES><VALUE name='preset' val='General'/><VALUE name='power' val='0'/><VALUE name='power' val='1'/></PROPERTIES>");std::wstring selected=L"unchanged";bool power=false;check(!settings(selected,power)&&selected==L"unchanged"&&!power,"duplicate state accepted");});
    test("reader ignores nested preset and power",[&]{put("<PROPERTIES><GROUP><VALUE name='preset' val='General'/><VALUE name='power' val='1'/></GROUP></PROPERTIES>");std::wstring selected=L"unchanged";bool power=false;check(!settings(selected,power)&&selected==L"unchanged"&&!power,"nested unrelated setting confirmed state");});
    unsigned recovery=0;for(const auto& raw:std::vector<std::string>{"", "{broken", "<PROPERTIES><VALUE", "<?xml version='1.0'", std::string("\xff\xfe\0",3), std::string(1024*1024+1,'x')})test("bad existing config recovery "+std::to_string(++recovery),[&]{put(raw);const auto before=archives();std::wstring reason;check(prepareConfigAt(file,"1.2.6.0",[]{return true;},reason),"bad existing config permanently blocks explicit ON");validate(read());exactArchive(before,raw);});
    test("locked config remains protected and explicit retry heals",[&]{put(standard);HANDLE locked=CreateFileW(file.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(locked!=INVALID_HANDLE_VALUE,"owned lock setup");std::wstring reason;const bool blocked=!prepareConfigAt(file,"1.2.6.0",[]{return true;},reason);CloseHandle(locked);check(blocked&&read()==standard,"locked file changed");check(prepareConfigAt(file,"1.2.6.0",[]{return true;},reason),"unlocked retry failed");validate(read());});
    test("corrupt config never recovers while running",[&]{put("{broken");const auto before=archives();std::wstring reason;check(!prepareConfigAt(file,"1.2.6.0",[]{return false;},reason)&&read()=="{broken"&&archives()==before,"running FxSound config changed");});

    for(const auto& fault:std::vector<std::string>{"temp","write","short","flush","publish"})test("writer "+fault+" failure and retry",[&]{put(standard);const auto before=archives();std::wstring reason;fxDenyTemp=fault=="temp";fxFailWrite=fault=="write";fxShortWrite=fault=="short";fxFailFlush=fault=="flush";fxFailPublish=fault=="publish";
        const bool refused=!prepareConfigAt(file,"1.2.6.0",[]{return true;},reason);fxDenyTemp=fxFailWrite=fxShortWrite=fxFailFlush=fxFailPublish=false;
        check(refused&&read()==standard&&archives()==before,"failed writer changed source or pretended success");check(fxOwnedFiles.empty(),"failed writer leaked owned handle");check(prepareConfigAt(file,"1.2.6.0",[]{return true;},reason),"released writer failure cannot retry");validate(read());});
    test("archive failure preserves bad original and retries",[&]{put("");const auto before=archives();std::wstring reason;fxDenyArchive=true;const bool refused=!prepareConfigAt(file,"1.2.6.0",[]{return true;},reason);fxDenyArchive=false;check(refused&&read().empty()&&archives()==before,"bad original reset without verified archive");check(prepareConfigAt(file,"1.2.6.0",[]{return true;},reason),"archive failure not retryable");exactArchive(before,"");validate(read());});
    test("publication failure after archive restores original without overwrite",[&]{put("{broken");const auto before=archives();std::wstring reason;fxFailPublish=true;const bool refused=!prepareConfigAt(file,"1.2.6.0",[]{return true;},reason);fxFailPublish=false;check(refused&&read()=="{broken"&&archives()==before,"publication failure lost original rollback");check(prepareConfigAt(file,"1.2.6.0",[]{return true;},reason),"rollback cannot retry");exactArchive(before,"{broken");validate(read());});
    test("independent publication readback refuses external corruption",[&]{put(standard);std::wstring reason;fxCorruptPublished=true;const bool refused=!prepareConfigAt(file,"1.2.6.0",[]{return true;},reason);fxCorruptPublished=false;check(refused&&read()=="{external-corrupt","unverified publication returned success");const auto before=archives();check(prepareConfigAt(file,"1.2.6.0",[]{return true;},reason),"corrupt publication cannot recover");exactArchive(before,"{external-corrupt");validate(read());});
    test("unknown prior temporary is never truncated or removed",[&]{put(standard);auto old=file;old+=L".ymcc-"+std::to_wstring(GetCurrentProcessId())+L".tmp";{std::ofstream out(old);out<<"foreign-temp";}std::wstring reason;check(prepareConfigAt(file,"1.2.6.0",[]{return true;},reason),"old temp blocks new unique writer");{std::ifstream in(old);check(std::string(std::istreambuf_iterator<char>(in),{})=="foreign-temp","unknown prior temp was changed");}std::filesystem::remove(old);});
    test("no owned handles or failed unique temporaries remain",[&]{check(fxOwnedFiles.empty(),"owned file handles leaked");for(const auto& item:std::filesystem::directory_iterator(file.parent_path()))check(item.path().extension()!=L".tmp","owned temporary leaked");});

    test("depth boundary rejects unsafe document before output mutation",[&]{std::string raw="<PROPERTIES>";for(int i=0;i<80;++i)raw+="<x>";for(int i=0;i<80;++i)raw+="</x>";raw+="</PROPERTIES>";std::string output="unchanged";std::wstring reason;check(!rewriteConfig(raw,"1.2.6.0",output,reason)&&output=="unchanged","deep document accepted or mutated output");put(raw);const auto before=archives();check(prepareConfigAt(file,"1.2.6.0",[]{return true;},reason),"bounded deep document cannot recover");exactArchive(before,raw);validate(read());});
    for(const auto& raw:std::vector<std::string>{"<?xml version='1.0' encoding='windows-1252'?><PROPERTIES><VALUE name='note' val='keep'/></PROPERTIES>","<FUTURE_PROPERTIES><VALUE name='note' val='keep'/></FUTURE_PROPERTIES>","<PROPERTIES><VALUE xmlns='urn:future' name='power' val='0'/></PROPERTIES>"})test("unknown format is protected "+std::to_string(count),[&]{put(raw);const auto before=archives();std::wstring reason;check(!prepareConfigAt(file,"1.2.6.0",[]{return true;},reason)&&read()==raw&&archives()==before,"unknown external format was downgraded");});
    for(const bool bigEndian:{false,true})test(bigEndian?"UTF16 BE metadata remains exact":"UTF16 LE without BOM remains exact",[&]{const std::wstring text=L"<?xml version='1.0' encoding='UTF-16'?><PROPERTIES><VALUE name='user_note' val='preserved'/></PROPERTIES>";std::string raw;if(bigEndian)raw="\xfe\xff";for(const auto c:text){raw.push_back(static_cast<char>(bigEndian?(c>>8)&255:c&255));raw.push_back(static_cast<char>(bigEndian?c&255:(c>>8)&255));}std::string output;std::wstring reason;check(rewriteConfig(raw,"1.2.6.0",output,reason),"UTF16 rewrite failed");validate(output);check(inspectConfig(output).at(L"user_note").front()==L"preserved","UTF16 unknown field changed");});

    for(const bool damaged:{false,true})test(damaged?"process starts after archive, original is restored":"process starts at publication boundary, source is retained",[&]{const auto raw=damaged?std::string("{broken"):standard;put(raw);const auto before=archives();unsigned calls=0;std::wstring reason;check(!prepareConfigAt(file,"1.2.6.0",[&]{return ++calls<3;},reason)&&calls==3&&read()==raw&&archives()==before,"late process startup still publishes or loses original");});
    std::cout<<"FXSOUND_CONFIG_STORM cases="<<count<<" failed="<<failed<<"\n";return failed?1:0;
}
int main(int argc,char** argv) {
    try {
        if(argc==3 && std::string(argv[1])=="--config-storm") {CoInitializeEx(nullptr,COINIT_MULTITHREADED);const auto result=configStorm(std::filesystem::u8path(argv[2]));CoUninitialize();return result;}
        if(argc>1 && std::string(argv[1])=="--inventory") {
            if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return 2;
            {
                ComPtr<IMMDeviceEnumerator> e;
                if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&e))))return 3;
                std::atomic<bool> disabled{false};WindowsPort port(e.Get(),disabled);const auto s=port.read();
                std::cout<<"READ_ONLY valid="<<s.valid<<" process="<<static_cast<int>(s.process)<<"\n";
                for(unsigned r=0;r<3;++r)std::cout<<"role="<<r<<" endpoint="<<utf8(s.defaults[r])<<"\n";
                for(const auto& d:s.devices)std::cout<<"device="<<utf8(d.id)<<" fx="<<d.fx<<" usable="<<d.usable<<" form="<<d.form<<" name="<<utf8(d.name)<<"\n";
                for(const auto& id:s.preferred)std::cout<<"recorded-default="<<utf8(id)<<"\n";
                check(s.valid,"read-only inventory failed");
            }
            CoUninitialize();return 0;
        }
        CoInitializeEx(nullptr,COINIT_MULTITHREADED);silentControlTests();CoUninitialize();tests();std::cout<<"PASS fxsound recovery + silent control: "<<checks<<" assertions; memory audio/process ports and isolated files only\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
