#include "../native/shared_game_media_frame_probe.h"
#include <iostream>
#include <iomanip>

int wmain(int argc,wchar_t** argv) {
    if(argc<3 || argc%2!=1)return 2;
    unsigned int failed=0;
    for(int i=1;i<argc;i+=2) {
        const std::filesystem::path path(argv[i]);
        const auto c=ymcc::media::frameProbe::inspectMp4(path);
        const std::wstring expected(argv[i+1]);
        const wchar_t* actual=!c?L"unknown":c.exceeds30()?L"high":L"low";
        std::wcout<<path.filename().wstring()<<L" "<<actual<<L" "
                  <<std::setprecision(9)<<c.averageFps()<<L" "<<c.samples<<L" "<<c.shortSamples<<L"\n";
        if(expected!=actual)++failed;
    }
    return failed?1:0;
}
