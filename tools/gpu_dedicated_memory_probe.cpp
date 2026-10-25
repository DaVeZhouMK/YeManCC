// Standalone collector: use production read helpers only. No apply, WindowsOps, journal or device writes.
#include "../native/gpu_dedicated_memory_runtime.h"
#include <iostream>
int wmain() {
    using namespace ymcc::dedicatedmemory;
    try {
        std::array<wchar_t,32768> executable{};
        const auto chars=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
        if (!chars || chars>=executable.size()) return 2;
        SYSTEMTIME now{};GetLocalTime(&now);
        std::wostringstream name;name<<L"YMCC-G3E-Memory-ReadOnly-"<<now.wYear<<std::setfill(L'0')
            <<std::setw(2)<<now.wMonth<<std::setw(2)<<now.wDay<<L'-'<<std::setw(2)<<now.wHour
            <<std::setw(2)<<now.wMinute<<std::setw(2)<<now.wSecond<<L'-'<<GetCurrentProcessId()<<L".json";
        const auto output=std::filesystem::path(executable.data()).parent_path()/name.str();
        Json diagnostics;const auto p=probeWindows("intel-read-only-probe",&diagnostics);
        Result result;result.snapshot=p;result.ok=admitted(p);result.reason=p.reason;
        Json report{{"schema",1},{"scope","production-dedicated-memory-reader"},{"registryWrites",false},
            {"sharedMemoryWrites",false},{"memorySettingWrites",false},{"rebootRequested",false},
            {"journalRead",false},{"result",resultJson(result)},{"diagnostics",std::move(diagnostics)}};
        std::ofstream file(output,std::ios::binary|std::ios::trunc);
        if(!file)return 3;file<<report.dump(2)<<"\n";file.close();if(!file)return 3;
        std::cout<<"READ_ONLY_REPORT="<<utf8(output.wstring())<<"\n";
        std::cout<<(result.ok?"DEDICATED_MEMORY_READ_CONFIRMED":"DEDICATED_MEMORY_READ_UNCONFIRMED")<<"\n";
        if(!result.reason.empty())std::cout<<result.reason<<"\n";
        return 0; // A recorded unsupported/failed read is still a successful collection.
    } catch (const std::exception& e) {std::cerr<<e.what()<<"\n";return 4;}
}
