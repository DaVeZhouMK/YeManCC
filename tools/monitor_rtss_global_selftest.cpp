#include <windows.h>
#include <cstddef>
static std::size_t readerOpens = 0, readerReads = 0, watchesOpened = 0, watchesClosed = 0;
static HANDLE lastWatch = INVALID_HANDLE_VALUE;
static bool denyWatch = false;
static HANDLE countedCreateFileW(LPCWSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD f, HANDLE t) {
    ++readerOpens; return CreateFileW(p,a,s,sa,c,f,t);
}
static BOOL countedReadFile(HANDLE h, LPVOID b, DWORD n, LPDWORD r, LPOVERLAPPED o) {
    ++readerReads; return ReadFile(h,b,n,r,o);
}
static HANDLE countedFindFirstChangeNotificationW(LPCWSTR p, BOOL subtree, DWORD filter) {
    if (denyWatch) { SetLastError(ERROR_NOT_SUPPORTED); return INVALID_HANDLE_VALUE; }
    lastWatch = FindFirstChangeNotificationW(p,subtree,filter);
    if (lastWatch != INVALID_HANDLE_VALUE) ++watchesOpened;
    return lastWatch;
}
static BOOL countedFindCloseChangeNotification(HANDLE h) {
    const auto result = FindCloseChangeNotification(h); if (result) ++watchesClosed; return result;
}
#define CreateFileW countedCreateFileW
#define ReadFile countedReadFile
#define FindFirstChangeNotificationW countedFindFirstChangeNotificationW
#define FindCloseChangeNotification countedFindCloseChangeNotification
#include "../native/monitor_rtss_global.h"
#undef CreateFileW
#undef ReadFile
#undef FindFirstChangeNotificationW
#undef FindCloseChangeNotification
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
    const auto quietDir=dir / "Triggered";std::filesystem::create_directories(quietDir);
    const auto quietPath=(quietDir / "Global").wstring();
    auto quietWrite=[&](const char* text){std::ofstream f(quietPath,std::ios::binary|std::ios::trunc);assert(f.good());f<<text;f.close();};
    auto notified=[&]{check("real directory notification arrived",lastWatch!=INVALID_HANDLE_VALUE && WaitForSingleObject(lastWatch,1000)==WAIT_OBJECT_0);};
    ChangeDrivenReader quiet;ULONGLONG qt=1000;long long qn=1700000000000LL;
    auto qs=[&](ULONGLONG step=1000){qt+=step;qn+=step;return quiet.sample(quietPath,qn,qt);};
    // Windows may deliver more than one notification for write + SetFileTime
    // or create + rename. Each event must revoke the in-flight confirmation.
    auto settleQuiet=[&](double expected){Limit cap{};for(int i=0;i<4;++i){cap=qs();check("pending notifications never publish the previous cap",!cap.valid || cap.fps==expected);if(cap.valid)break;}return cap;};
    quietWrite("[Framerate]\nLimit=120\n");
    check("triggered first observation pending",!qs().valid);
    check("triggered second stable observation confirmed",qs().fps==120);
    auto readsBefore=readerReads,opensBefore=readerOpens;
    for(int i=0;i<29;++i){const auto cap=qs();check("quiet notification renews only confirmed configuration",cap.valid && cap.fps==120 && cap.ts==qn);}
    check("quiet ticks perform zero file reads or opens",readerReads==readsBefore && readerOpens==opensBefore);
    const auto fallback=qs();check("30s fallback rechecks unchanged bytes without a display gap",fallback.valid && fallback.fps==120 && fallback.ts==qn && readerReads==readsBefore+1);
    FILETIME quietTime{};{Handle f(quietPath);FileState info{};assert(state(f.value,info));quietTime=info.written;}
    quietWrite("[Framerate]\nLimit=060\n");
    {HANDLE h=CreateFileW(quietPath.c_str(),FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);assert(h!=INVALID_HANDLE_VALUE);assert(SetFileTime(h,nullptr,nullptr,&quietTime));CloseHandle(h);}
    notified();check("notification invalidates same-size same-mtime old cap",!qs().valid);
    check("changed cap still needs separated observations",!qs(0).valid && !qs(100).valid);
    const auto changedCap=settleQuiet(60);check("changed cap confirmed after stable read",changedCap.valid && changedCap.fps==60);
    quietWrite("[Framerate]\nLimit=\n");notified();check("triggered half-write invalidates old cap",!qs().valid);
    check("triggered persistent malformed stays unknown",!qs().valid);
    for(int i=0;i<4;++i)check("malformed remains unknown while pending notifications settle",!qs().valid);
    readsBefore=readerReads;for(int i=0;i<8;++i)check("quiet malformed does not revive old cap",!qs().valid);
    check("quiet malformed does not repeatedly read",readerReads==readsBefore);
    quietWrite("[Framerate]\nLimit=0\n");notified();check("repair notification starts fresh confirmation",!qs().valid);
    const auto unlimited=settleQuiet(0);check("triggered explicit zero remains unlimited",unlimited.valid && unlimited.fps==0);
    const auto quietReplace=(quietDir / "Replacement").wstring();{std::ofstream f(quietReplace);f<<"[Framerate]\nLimit=90\n";}
    assert(MoveFileExW(quietReplace.c_str(),quietPath.c_str(),MOVEFILE_REPLACE_EXISTING));notified();
    check("triggered atomic replacement invalidates old cap",!qs().valid);check("triggered replacement confirms actual new file",settleQuiet(90).fps==90);
    check("triggered sleep-size gap invalidates cached cap",!qs(6000).valid);check("triggered gap needs fresh second observation",qs().fps==90);
    qt-=2000;check("triggered clock rollback invalidates cached cap",!quiet.sample(quietPath,qn,qt).valid);check("triggered rollback recovers after confirmation",qs().fps==90);
    quiet.reset();check("demand OFF releases notification handle",watchesOpened==watchesClosed);
    check("demand ON does not reuse previous cap",!qs().valid);check("demand ON confirms current cap",qs().fps==90);
    assert(DeleteFileW(quietPath.c_str()));notified();check("deleted file becomes unknown",!qs().valid);check("missing file second bounded attempt stays unknown",!qs().valid);
    readsBefore=readerReads;opensBefore=readerOpens;for(int i=0;i<8;++i)check("quiet missing remains unknown",!qs().valid);
    check("missing file retries are suppressed",readerReads==readsBefore && readerOpens==opensBefore);
    quietWrite("[Framerate]\nLimit=30\n");notified();check("file recreation event bypasses backoff",!qs().valid);check("recreated config confirmed",settleQuiet(30).fps==30);
    quiet.reset();denyWatch=true;
    check("unsupported notifications first observation pending",!qs().valid);const auto noWatch=qs();check("unsupported notifications still read actual config",noWatch.valid && noWatch.fps==30);
    const auto retained=qs();check("missing notification cannot renew old observation timestamp",retained.valid && retained.ts==noWatch.ts && retained.ts<qn);
    quiet.reset();denyWatch=false;check("all notification resources returned",watchesOpened==watchesClosed);
    check("unregistered path remains unknown without creating a watch",!quiet.sample(qn,qt).valid && watchesOpened==watchesClosed);
    std::cout << "Native event configuration reader: opens=" << readerOpens << ", reads=" << readerReads << ", watches=" << watchesOpened << "/" << watchesClosed << ".\n";
    std::cout << "Native Global reader PASS: " << cases.size() << " assertions (10000 malformed suffixes included).\n";
}
