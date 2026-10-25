#include "../native/monitor_rtss_global.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>
using namespace ymcc::monitorrtss;
int main(int argc, char** argv) {
    assert(argc == 2);
    const auto dir = std::filesystem::absolute(argv[1]);
    std::filesystem::create_directories(dir);
    const auto path = (dir / "Global").wstring();
    auto write = [&](std::string text) { std::ofstream f(path, std::ios::binary | std::ios::trunc); assert(f.good()); f << text; f.close(); };
    std::vector<std::string> cases;
    auto check = [&](const char* name, bool ok) { if (!ok) { std::cerr << "FAIL: " << name << '\n'; std::abort(); } cases.emplace_back(name); };
    for (const auto& input : {"[Framerate]\nLimit=120\n", "[framerate]\r\nLimit = 120\r\nLimitDenominator=1\r\n", "\xef\xbb\xbf[Framerate]\nLIMIT=120\n", "[Other]\nLimit=99\n[Framerate]\nLimit=120 ; cap\n"}) {
        const auto x = parseGlobal(input); check("valid integral 120", x.valid && x.fps == 120);
    }
    check("explicit zero is unlimited", parseGlobal("[Framerate]\nLimit=0\n").valid && parseGlobal("[Framerate]\nLimit=0\n").fps == 0);
    check("fractional denominator retained", parseGlobal("[Framerate]\nLimit=59940\nLimitDenominator=1000\n").fps == 59.94);
    for (const auto& input : {"", "Limit=0\n", "[Other]\nLimit=0\n", "[Framerate]\n", "[Framerate]\nLimit=\n", "[Framerate]\nLimit=-60\n", "[Framerate]\nLimit=+60\n", "[Framerate]\nLimit=120garbage\n", "[Framerate]\nLimit=120.5\n", "[Framerate]\nLimit=NaN\n", "[Framerate]\nLimit=1000001\n", "[Framerate]\nLimit=0\nLimit=120\n", "[Framerate]\nLimit=0\n[Framerate]\nLimit=120\n", "[Framerate]\nLimit=120\nLimitDenominator=0\n", "[Framerate]\nLimit=120\nLimitDenominator=-1\n", "[Framerate]\nLimit=120\nLimitDenominator=\n", "[Framerate]\nLimit=120\nLimitDenominator=1\nLimitDenominator=1\n", "[Framerate]\nLimit=120\n[broken", "[Framerate]garbage\nLimit=0\n", "[Framerate]\nLimit=0;bad\n"}) check("malformed config remains unknown", !parseGlobal(input).valid);
    check("embedded NUL rejected", !parseGlobal(std::string("[Framerate]\nLimit=0\n\0x", 22)).valid);
    check("oversized parser input rejected", !parseGlobal(std::string(kMaxGlobalBytes + 1, 'x')).valid);
    for (unsigned i = 0; i < 10000; ++i) check("invalid suffix never becomes cap/zero", !parseGlobal("[Framerate]\nLimit=" + std::to_string(i) + "x\n").valid);
    GlobalReader reader;
    long long now = 1700000000000LL; ULONGLONG tick = 1000;
    check("unresolved existing-owner path is unknown, not another guessed install",globalPath().empty() && !reader.sample(now,tick).valid);
    auto sample = [&](ULONGLONG step = 1000) { now += step; tick += step; return reader.sample(path, now, tick); };
    auto confirmed = [&](double value) { check("first observation pending", !sample().valid); const auto x=sample(); check("second stable observation confirmed", x.valid && x.fps==value && x.ts==now); };
    write("[Framerate]\nLimit=120\nLimitDenominator=1\n"); confirmed(120);
    auto x=sample(); check("unchanged config reobserved with own fresh timestamp", x.valid && x.fps==120 && x.ts==now);
    // Keep mtime and size identical: content observation must still see a change.
    FILETIME sameTime{}; { Handle f(path); FileState s{}; assert(state(f.value,s)); sameTime=s.written; }
    write("[Framerate]\nLimit=060\nLimitDenominator=1\n");
    { HANDLE f=CreateFileW(path.c_str(),FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr); assert(f!=INVALID_HANDLE_VALUE); assert(SetFileTime(f,nullptr,nullptr,&sameTime)); CloseHandle(f); }
    confirmed(60);
    write("[Framerate]\nLimit=0\n"); confirmed(0);
    write("[Framerate]\nLimit=\n"); check("half-written Limit cannot become unlimited",!sample().valid);check("persistent malformed config cannot renew old value",!sample().valid);
    write("[Framerate]\nLimit=60\n"); confirmed(60);
    { Handle exclusive(L""); HANDLE lock=CreateFileW(path.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr); assert(lock!=INVALID_HANDLE_VALUE);check("locked file invalidates observation instead of preserving old cap",!sample().valid);CloseHandle(lock); } confirmed(60);
    check("fixture delete",DeleteFileW(path.c_str())!=0);check("missing file is unknown not unlimited",!sample().valid);
    write("[Framerate]\nLimit=0\n"); confirmed(0);
    const auto replace=(dir/"Replacement").wstring(); {std::ofstream f(replace);f<<"[Framerate]\nLimit=90\n";}
    assert(MoveFileExW(replace.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING));confirmed(90);
    write("[Framerate]\nLimit=120\n");check("modified data needs separated ticks",!sample().valid);check("same-tick duplicate cannot confirm",!sample(0).valid);check("sub-500ms duplicate cannot confirm",!sample(100).valid);check("stable later tick confirms",sample(1000).valid);
    check("pause does not revive cached cap",!sample(6000).valid);check("fresh observation after pause required",sample().valid);
    reader.reset();confirmed(120);
    write(std::string(kMaxGlobalBytes+1,'x'));check("oversized on-disk file rejected before allocation",!sample().valid);
    write("[Framerate]\nLimit=60\n");confirmed(60);
    tick-=2000;check("monotonic rollback invalidates confirmation",!reader.sample(path,now,tick).valid);
    const auto registered=dir / "Registered";std::filesystem::create_directories(registered / "Profiles");
    {std::ofstream f(registered / "Profiles" / "Global");f<<"[Framerate]\nLimit=30\n";}
    registerDirectory(registered.wstring());
    check("existing-owner directory change invalidates old cap",!reader.sample(now+=1000,tick+=1000).valid);
    const auto registeredCap=reader.sample(now+=1000,tick+=1000);check("registered path is sole source",registeredCap.valid && registeredCap.fps==30);
    registerDirectory(L"");check("lost path never falls back to another installation",globalPath().empty() && !reader.sample(now+=1000,tick+=1000).valid);
    std::cout << "Native Global reader PASS: " << cases.size() << " assertions (10000 malformed suffixes included).\n";
}
