#ifdef YMCC_IMPORT_TARGET_BEFORE_FIX
#include "../native/steam_compatibility_win32.h"
#else
#include "../native/steam_import_target_win32.h"
#endif
#include <cstdio>
#include <string>
#include <vector>
namespace c = ymcc::steamcompat;
extern "C" {
__declspec(dllimport) volatile LONG YmccDataImportFixture;
__declspec(dllimport) int YmccCodeImportFixture();
}
struct Case { std::string name; bool passed; };
static std::vector<Case> cases;
static void check(const char* name, bool passed) { cases.push_back({name,passed}); }
static std::uint64_t address(const void* p) { return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(p)); }
static bool resolved(HANDLE process, const c::ImportSlot& slot, std::uint64_t target) {
#ifdef YMCC_IMPORT_TARGET_BEFORE_FIX
    (void)slot; return c::executableImportTarget(process,target);
#else
    return c::resolvedImportTarget(process,slot,target);
#endif
}
struct Protection {
    void* pointer; SIZE_T size; DWORD original = 0; bool changed = false;
    Protection(void* p, SIZE_T n, DWORD next):pointer(p),size(n) { changed=VirtualProtect(pointer,size,next,&original)!=FALSE; }
    ~Protection() { if(changed){DWORD ignored=0;VirtualProtect(pointer,size,original,&ignored);} }
};
int main() {
    HMODULE module=GetModuleHandleW(L"ymcc_import_fixture.dll");
    if(!module)return 2;
    const auto data=address(const_cast<const LONG*>(&YmccDataImportFixture));
    const auto code=static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(GetProcAddress(module,"YmccCodeImportFixture")));
    c::ImportSlot dataSlot{0,0,"ymcc_import_fixture.dll","YmccDataImportFixture"};
    c::ImportSlot codeSlot{0,0,"ymcc_import_fixture.dll","YmccCodeImportFixture"};
    HANDLE process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,GetCurrentProcessId());
    if(!process)return 3;
    check("loader-real-data-and-code-imports",YmccDataImportFixture==701&&YmccCodeImportFixture()==702);
    MEMORY_BASIC_INFORMATION info{};
    check("real-data-is-committed-image-nonexec",VirtualQueryEx(process,reinterpret_cast<void*>(data),&info,sizeof(info))==sizeof(info)&&info.Type==MEM_IMAGE&&info.State==MEM_COMMIT&&!(info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)));
    check("historical-function-validator-rejects-real-data",!c::executableImportTarget(process,data));
    check("historical-function-validator-accepts-real-code",c::executableImportTarget(process,code));
    check("resolved-real-function-accepted",resolved(process,codeSlot,code));
    check("resolved-real-data-accepted",resolved(process,dataSlot,data));
    auto changed=dataSlot;changed.library="YMCC_IMPORT_FIXTURE.DLL";
    check("data-library-name-case-insensitive",resolved(process,changed,data));
    changed=dataSlot;changed.symbol="ymccDataImportFixture";
    check("wrong-symbol-case-rejected",!resolved(process,changed,data));
    changed=dataSlot;changed.symbol="YmccMissingDataFixture";
    check("missing-data-symbol-rejected",!resolved(process,changed,data));
    changed=dataSlot;changed.symbol="YmccCodeImportFixture";
    check("function-symbol-pointing-at-data-rejected",!resolved(process,changed,data));
    changed=dataSlot;changed.library="another_library.dll";
    check("wrong-library-rejected",!resolved(process,changed,data));
    check("interior-data-address-rejected",!resolved(process,dataSlot,data+1));
    check("null-target-rejected",!resolved(process,dataSlot,0));
    check("low-target-rejected",!resolved(process,dataSlot,0xffff));
    check("noncanonical-target-rejected",!resolved(process,dataSlot,0xffffffffffffffffULL));
    void* privatePage=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    check("private-memory-target-rejected",privatePage&&!resolved(process,dataSlot,address(privatePage)));
    if(privatePage)VirtualFree(privatePage,0,MEM_RELEASE);
    auto* base=reinterpret_cast<unsigned char*>(module);
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    auto* exported=reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(base+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    auto* names=reinterpret_cast<DWORD*>(base+exported->AddressOfNames);
    auto* ordinals=reinterpret_cast<WORD*>(base+exported->AddressOfNameOrdinals);
    unsigned ordinal=0;
    for(DWORD i=0;i<exported->NumberOfNames;++i)if(std::string(reinterpret_cast<const char*>(base+names[i]))==dataSlot.symbol)ordinal=exported->Base+ordinals[i];
    changed=dataSlot;changed.symbol="#"+std::to_string(ordinal);
    check("data-ordinal-binding-accepted",ordinal&&resolved(process,changed,data));
    changed.symbol="#65535";
    check("wrong-data-ordinal-rejected",!resolved(process,changed,data));
    {
        Protection protection(reinterpret_cast<void*>(data),sizeof(LONG),PAGE_READONLY);
        check("readonly-image-data-accepted",protection.changed&&resolved(process,dataSlot,data));
    }
    {
        Protection protection(reinterpret_cast<void*>(data),sizeof(LONG),PAGE_NOACCESS);
        check("noaccess-image-data-rejected",protection.changed&&!resolved(process,dataSlot,data));
    }
    {
        Protection protection(reinterpret_cast<void*>(data),sizeof(LONG),PAGE_READWRITE|PAGE_GUARD);
        check("guarded-image-data-rejected",protection.changed&&!resolved(process,dataSlot,data));
    }
    {
        Protection protection(dos,sizeof(*dos),PAGE_READWRITE);
        const auto saved=dos->e_magic;
        if(protection.changed)dos->e_magic=0;
        check("corrupt-target-image-header-rejected",protection.changed&&!resolved(process,dataSlot,data));
        if(protection.changed)dos->e_magic=saved;
    }
    {
        Protection protection(exported,sizeof(*exported),PAGE_READWRITE);
        const auto saved=exported->NumberOfNames;
        if(protection.changed)exported->NumberOfNames=0xffffffffu;
        check("oversized-export-table-rejected",protection.changed&&!resolved(process,dataSlot,data));
        if(protection.changed)exported->NumberOfNames=saved;
    }
#ifndef YMCC_IMPORT_TARGET_BEFORE_FIX
    {
        auto* functions=reinterpret_cast<DWORD*>(base+exported->AddressOfFunctions);
        const DWORD index=ordinal-exported->Base;
        Protection protection(functions+index,sizeof(DWORD),PAGE_READWRITE);
        const auto saved=functions[index];
        const auto forwarderRva=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
        if(protection.changed)functions[index]=forwarderRva;
        check("export-directory-forwarder-is-not-data",protection.changed&&!resolved(process,dataSlot,address(base)+forwarderRva));
        if(protection.changed)functions[index]=saved;
    }
    {
        auto* sections=IMAGE_FIRST_SECTION(nt);
        IMAGE_SECTION_HEADER* containing=nullptr;
        for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)if(data-address(base)>=sections[i].VirtualAddress&&data-address(base)-sections[i].VirtualAddress<(std::max)(sections[i].Misc.VirtualSize,sections[i].SizeOfRawData))containing=sections+i;
        Protection protection(containing?static_cast<void*>(containing):static_cast<void*>(dos),sizeof(IMAGE_SECTION_HEADER),PAGE_READWRITE);
        const auto saved=containing?containing->Characteristics:0;
        if(containing&&protection.changed)containing->Characteristics|=IMAGE_SCN_MEM_EXECUTE;
        check("executable-section-cannot-masquerade-as-data",containing&&protection.changed&&!resolved(process,dataSlot,data));
        if(containing&&protection.changed)containing->Characteristics=saved;
    }
    {
        c::LoadedDiagnostic diagnostic;
        changed=dataSlot;changed.symbol="YmccMissingDataFixture";
        const bool accepted=c::resolvedImportTarget(process,changed,data,&diagnostic);
        check("structured-rejection-keeps-import-identity",!accepted&&diagnostic.library==changed.library&&diagnostic.symbol==changed.symbol&&diagnostic.target==data&&std::string(diagnostic.reason)!="contract-read-unavailable");
    }
    {
        c::LoadedDiagnostic diagnostic;
        const bool accepted=c::resolvedImportTarget(nullptr,dataSlot,data,&diagnostic);
        check("invalid-process-query-keeps-win32-error",!accepted&&diagnostic.win32Error!=ERROR_SUCCESS&&diagnostic.failedAddress==data);
    }
#endif
    check("real-data-remains-unmodified",YmccDataImportFixture==701);
    CloseHandle(process);
    unsigned passed=0;for(const auto& x:cases)passed+=x.passed?1u:0u;
    std::printf("{\n  \"schema\":\"ymcc.steam-import-target-selftest.v1\",\n  \"scope\":\"real-owned-DLL-function-and-data-imports; self-process-only; no-Steam\",\n  \"passed\":%u,\n  \"total\":%zu,\n  \"cases\":[\n",passed,cases.size());
    for(std::size_t i=0;i<cases.size();++i)std::printf("    {\"name\":\"%s\",\"passed\":%s}%s\n",cases[i].name.c_str(),cases[i].passed?"true":"false",i+1<cases.size()?",":"");
    std::printf("  ]\n}\n");
    return passed==cases.size()?0:1;
}
