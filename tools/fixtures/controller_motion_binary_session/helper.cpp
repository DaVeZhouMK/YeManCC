#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <psapi.h>
#include "gamepad_motion_abi.h"
#include "controller_motion_session.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <type_traits>
#include <vector>

static_assert(sizeof(void*) == 8 && sizeof(int) == 4 && sizeof(float) == 4, "Fixed x64 ABI only");
namespace {
using namespace binary_test;
using Fields = std::vector<std::pair<std::string, std::string>>;
std::string quote(const std::string& s) {
    std::ostringstream out; out << '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 32) out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(c) << std::dec;
            else out << static_cast<char>(c);
        }
    }
    out << '"'; return out.str();
}
std::string number(double x) {
    if (!std::isfinite(x)) return quote(std::isnan(x) ? "NaN" : x > 0 ? "Infinity" : "-Infinity");
    std::ostringstream out; out << std::setprecision(12) << x; return out.str();
}
std::string integer(std::uint64_t x) { return std::to_string(x); }
std::string object(const Fields& fields) {
    std::string out = "{"; bool first = true;
    for (const auto& item : fields) { if (!first) out += ','; first = false; out += quote(item.first) + ':' + item.second; }
    return out + '}';
}
void emit(const std::string& type, Fields fields = {}) {
    fields.insert(fields.begin(), {"type", quote(type)});
    std::cout << object(fields) << '\n';
}
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) throw std::runtime_error("WideCharToMultiByte failed");
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}
std::wstring modulePath(HMODULE module) {
    std::vector<wchar_t> buffer(32768);
    const DWORD n = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!n || n >= buffer.size()) throw std::runtime_error("GetModuleFileNameW failed/truncated");
    return std::wstring(buffer.data(), n);
}
std::wstring normal(const std::wstring& path) { return std::filesystem::path(path).lexically_normal().wstring(); }
bool samePath(const std::wstring& a, const std::wstring& b) { return _wcsicmp(normal(a).c_str(), normal(b).c_str()) == 0; }
struct Module { std::wstring path; std::uintptr_t base; };
std::vector<Module> modules() {
    std::array<HMODULE, 512> handles{}; DWORD needed = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), handles.data(), static_cast<DWORD>(sizeof(handles)), &needed) || needed > sizeof(handles))
        throw std::runtime_error("K32EnumProcessModules failed/truncated");
    std::vector<Module> result;
    for (std::size_t i = 0; i < needed / sizeof(HMODULE); ++i)
        result.push_back({modulePath(handles[i]), reinterpret_cast<std::uintptr_t>(handles[i])});
    std::sort(result.begin(), result.end(), [](const Module& a, const Module& b) { return a.path < b.path; });
    return result;
}
void emitModules(const std::string& stage, const std::vector<Module>& list) {
    std::string values = "[";
    for (std::size_t i = 0; i < list.size(); ++i) { if (i) values += ','; values += object({{"path",quote(utf8(list[i].path))},{"base",quote(integer(list[i].base))}}); }
    emit("modules", {{"stage",quote(stage)},{"modules",values + ']'}});
}
struct PinnedFile {
    HANDLE file = INVALID_HANDLE_VALUE;
    explicit PinnedFile(const std::wstring& path) {
        file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot lock private DLL for read: " + std::to_string(GetLastError()));
    }
    ~PinnedFile() { if (file != INVALID_HANDLE_VALUE) CloseHandle(file); }
    PinnedFile(const PinnedFile&) = delete;
    PinnedFile& operator=(const PinnedFile&) = delete;
};
std::string hashFile(HANDLE file) {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || static_cast<std::uint64_t>(size.QuadPart) != kBytes) throw std::runtime_error("Pinned DLL size mismatch");
    std::vector<UCHAR> state; // Outlives BCrypt hash destruction.
    struct HashResources {
        BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
        ~HashResources() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    } resources;
    auto nt = [](NTSTATUS status) { if (status < 0) throw std::runtime_error("BCrypt SHA256 operation failed"); };
    nt(BCryptOpenAlgorithmProvider(&resources.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0));
    DWORD objectLength = 0, count = 0;
    nt(BCryptGetProperty(resources.algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &count, 0));
    state.resize(objectLength);
    nt(BCryptCreateHash(resources.algorithm, &resources.hash, state.data(), objectLength, nullptr, 0, 0));
    std::array<UCHAR, 4096> bytes{}; DWORD received = 0;
    do {
        if (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &received, nullptr)) throw std::runtime_error("ReadFile(private DLL) failed");
        if (received) nt(BCryptHashData(resources.hash, bytes.data(), received, 0));
    } while (received);
    std::array<UCHAR, 32> digest{};
    nt(BCryptFinishHash(resources.hash, digest.data(), static_cast<ULONG>(digest.size()), 0));
    std::ostringstream result; result << std::hex << std::setfill('0');
    for (UCHAR byte : digest) result << std::setw(2) << static_cast<unsigned>(byte);
    return result.str();
}
template<class Fn> Fn bind(HMODULE module, const char* name, std::uintptr_t expectedRva) {
    const FARPROC address = GetProcAddress(module, name);
    if (!address) throw std::runtime_error(std::string("ABI gap: missing export ") + name);
    Fn function{}; static_assert(sizeof(function) == sizeof(address)); std::memcpy(&function, &address, sizeof(function));
    const std::uintptr_t rva = reinterpret_cast<std::uintptr_t>(address) - reinterpret_cast<std::uintptr_t>(module);
    emit("binding", {{"name",quote(name)},{"rva",integer(rva)},{"expectedRva",integer(expectedRva)}});
    if (rva != expectedRva) throw std::runtime_error(std::string("ABI gap: unexpected RVA ") + name);
    return function;
}
Api bindApi(HMODULE module) {
    Api api;
    api.create = bind<Create>(module, "CreateGamepadMotion", 0x35D0);
    api.destroy = bind<Delete>(module, "DeleteGamepadMotion", 0x3600);
    api.setMode = bind<SetMode>(module, "SetCalibrationMode", 0x3B70);
    api.getMode = bind<GetMode>(module, "GetCalibrationMode", 0x3660);
    api.setOffset = bind<SetOffset>(module, "SetCalibrationOffset", 0x3B80);
    api.getOffset = bind<Get3>(module, "GetCalibrationOffset", 0x3670);
    api.process = bind<Process>(module, "ProcessMotion", 0x3A80);
    api.getConfidence = bind<GetConfidence>(module, "GetAutoCalibrationConfidence", 0x3610);
    api.getCalibrated = bind<Get3>(module, "GetCalibratedGyro", 0x3640);
    api.getGravity = bind<Get3>(module, "GetGravity", 0x36D0);
    api.getPlayer = bind<GetSpace>(module, "GetPlayerSpaceGyro", 0x3720);
    api.getWorld = bind<GetSpace>(module, "GetWorldSpaceGyro", 0x3820);
    return api;
}

using namespace ymcc::controller_motion;
static_assert(std::is_same_v<decltype(Ops{}.process),Process>);
static_assert(std::is_same_v<decltype(ReadOps{}.getPlayer),GetSpace>);
static_assert(std::is_same_v<decltype(ReadOps{}.getWorld),GetSpace>);
using Vec = std::array<float,3>;
struct CaseResult { std::string name; std::uint64_t checks=0, failures=0; };
struct Record { void* pointer=nullptr; unsigned serial=0; bool alive=false; };
struct Tracker {
    Api api; std::array<Record,128> records{}; unsigned created=0,deleted=0,violations=0;
    unsigned modes=0,offsets=0,processes=0,calReads=0,gravityReads=0,players=0,worlds=0;
    unsigned modeBankB=0,gravityBankB=0; // Distinguish observer bodies under linker identical-code folding.
    float lastDt=0,playerFactor=0,worldFactor=0; RawSixAxes lastAxes{};
    explicit Tracker(Api value):api(value) {}
    Record* live(void* p) { for(auto& r:records) if(r.alive&&r.pointer==p) return &r; return nullptr; }
    unsigned serial(void* p) { auto* r=live(p); return r?r->serial:0; }
    bool clean() const { if(created!=deleted||violations) return false; for(const auto& r:records) if(r.alive) return false; return true; }
};
Tracker* trace=nullptr;
void* __cdecl createObserved() {
    void* p=trace->api.create();
    if(p) { if(trace->created>=trace->records.size()){++trace->violations; trace->api.destroy(p); return nullptr;} auto& r=trace->records[trace->created];r={p,++trace->created,true};emit("native-created",{{"serial",integer(r.serial)}}); }
    return p;
}
void __cdecl deleteObserved(void* p) {
    auto* record=trace->live(p); if(!record){++trace->violations;return;}
    record->alive=false; trace->api.destroy(p); ++trace->deleted; emit("native-deleted",{{"serial",integer(record->serial)}});
}
void __cdecl modeObserved(void* p,int mode){++trace->modes;trace->api.setMode(p,mode);}
void __cdecl modeObservedB(void* p,int mode){++trace->modeBankB;++trace->modes;trace->api.setMode(p,mode);}
void __cdecl offsetObserved(void* p,float x,float y,float z,int w){++trace->offsets;trace->api.setOffset(p,x,y,z,w);}
void __cdecl processObserved(void* p,float gx,float gy,float gz,float ax,float ay,float az,float dt){++trace->processes;trace->lastDt=dt;trace->lastAxes={gx,gy,gz,ax,ay,az};trace->api.process(p,gx,gy,gz,ax,ay,az,dt);}
void __cdecl calObserved(void* p,float* x,float* y,float* z){++trace->calReads;trace->api.getCalibrated(p,x,y,z);}
void __cdecl gravityObserved(void* p,float* x,float* y,float* z){++trace->gravityReads;trace->api.getGravity(p,x,y,z);}
void __cdecl gravityObservedB(void* p,float* x,float* y,float* z){++trace->gravityBankB;++trace->gravityReads;trace->api.getGravity(p,x,y,z);}
void __cdecl playerObserved(void* p,float* x,float* y,float f){++trace->players;trace->playerFactor=f;trace->api.getPlayer(p,x,y,f);}
void __cdecl worldObserved(void* p,float* x,float* y,float f){++trace->worlds;trace->worldFactor=f;trace->api.getWorld(p,x,y,f);}
Ops observedOps(){return {createObserved,deleteObserved,modeObserved,offsetObserved,processObserved};}
ReadOps observedReads(){return {calObserved,gravityObserved,playerObserved,worldObserved};}
Key key(){return {"SYNTHETIC-CPU-CONTROLLER-A",1,2,3,0};}
Calibration calibration(){return {0.25f,-0.5f,0.125f,8};}
RawSixAxes sample(){return {4,-8,2,0,0,1};}
Vec vector(const Calibration& v){return {v.x,v.y,v.z};}
Vec vector(const MotionVector3& v){return {v.x,v.y,v.z};}
std::string vecJson(const Vec& v){return '['+number(v[0])+','+number(v[1])+','+number(v[2])+']';}
struct Tests {
    std::uint64_t checks=0,failures=0;std::vector<CaseResult> cases;
    void start(const std::string& name){cases.push_back({name});emit("case-start",{{"name",quote(name)},{"real",quote("current Session/Owner + pinned native DLL")},{"synthetic",quote("keys, double clock, raw samples, in-memory Calibration values; no main/PBT")}});}
    void check(bool value,const std::string& label){++checks;++cases.back().checks;if(!value){++failures;++cases.back().failures;emit("assertion-failed",{{"case",quote(cases.back().name)},{"label",quote(label)}});}}
    void must(bool value,const std::string& label){check(value,label);if(!value)throw std::runtime_error(label);}
    void equal(const Vec& actual,const Vec& expected,const std::string& label){for(std::size_t i=0;i<3;++i)check(std::isfinite(actual[i])&&std::abs(actual[i]-expected[i])<=0.00002f*(1+std::abs(expected[i])),label+" axis "+std::to_string(i));}
    Vec native3(void* p,Get3 getter){const float n=std::numeric_limits<float>::quiet_NaN();Vec out{n,n,n};getter(p,&out[0],&out[1],&out[2]);must(std::all_of(out.begin(),out.end(),[](float v){return std::isfinite(v);}),"native vec3 finite");return out;}
    void offset(void* p,const Calibration& expected){equal(native3(p,trace->api.getOffset),vector(expected),"native offset matches supplied Calibration");}
    MotionResult process(Session& s,const Key& k,double at,double dt,ReadRequest request={},const std::string& label="actual dt forwarded"){
        const auto before=trace->processes;auto result=s.process(k,sample(),at,request);must(result.has_value(),"real Session accepted current key");
        check(trace->processes==before+1,"one real ProcessMotion per accepted sample");check(result->deltaSeconds==dt,label);check(trace->lastDt==static_cast<float>(dt),label);
        check(result->orientationFinite,"Session independent orientation finite");
        const auto& c=result->calibrated;const auto& g=result->gravity;
        check(std::isfinite(c.x)&&std::isfinite(c.y)&&std::isfinite(c.z)&&std::isfinite(g.x)&&std::isfinite(g.y)&&std::isfinite(g.z),"Session native cal/gravity finite");
        check(trace->lastAxes.gx==4&&trace->lastAxes.gy==-8&&trace->lastAxes.gz==2&&trace->lastAxes.ax==0&&trace->lastAxes.ay==0&&trace->lastAxes.az==1,"six axes reach real DLL unchanged");
        check(result->player.has_value()==request.player&&result->world.has_value()==request.world,"only requested planes are returned");
        if(result->player){float x=std::numeric_limits<float>::quiet_NaN(),y=x;trace->api.getPlayer(s.handleFor(k),&x,&y,1.41f);check(trace->playerFactor==1.41f,"actual player factor1.41");check(std::isfinite(result->player->x)&&std::isfinite(result->player->y)&&std::abs(result->player->x-x)<0.0001f&&std::abs(result->player->y-y)<0.0001f,"player result matches direct DLL readback");}
        if(result->world){float x=std::numeric_limits<float>::quiet_NaN(),y=x;trace->api.getWorld(s.handleFor(k),&x,&y,0.125f);check(trace->worldFactor==0.125f,"actual world factor0.125");check(std::isfinite(result->world->x)&&std::isfinite(result->world->y)&&std::abs(result->world->x-x)<0.0001f&&std::abs(result->world->y-y)<0.0001f,"world result matches direct DLL readback");}
        return *result;
    }
};
void initial(Tests& t){
    Session s;const auto k=key();const auto c=calibration();t.must(s.select(k,observedOps(),observedReads(),c,10),"initial real selection");void* h=s.handleFor(k);
    t.check(h&&s.active()&&s.matchesSelection(k,observedOps(),observedReads()),"active exact selection");t.offset(h,c);t.check(trace->api.getMode(h)==0,"new owner actual Manual mode");
    const auto a=t.process(s,k,10.125,0.125);t.equal(vector(a.calibrated),{3.75f,-7.5f,1.875f},"Session Manual calibrated gyro");
    t.check(trace->players==0&&trace->worlds==0,"unrequested planes not called");
    t.process(s,k,10.25,0.125,{true,false});const auto players=trace->players,worlds=trace->worlds;
    t.process(s,k,10.375,0.125,{false,true});t.check(trace->players==players&&trace->worlds==worlds+1,"requested world does not call player");
    t.process(s,k,10.5,0.125,{true,true});const auto deleted=trace->deleted;s.revoke();s.revoke();t.check(trace->deleted==deleted+1&&!s.active()&&!s.handleFor(k),"revoke once, duplicate revoke inert");
}
void stable(Tests& t){
    Session s;auto k=key();const auto c=calibration();t.must(s.select(k,observedOps(),observedReads(),c,20),"stable initial selection");void* h=s.handleFor(k);const auto serial=trace->serial(h);t.process(s,k,20.125,0.125);t.process(s,k,20.375,0.25);
    const auto creates=trace->created,offsets=trace->offsets,modes=trace->modes;t.must(s.select(k,observedOps(),observedReads(),{9,8,7,10},20.5),"same-key selection accepted");
    t.check(s.handleFor(k)==h&&trace->serial(h)==serial&&trace->created==creates,"same-key stable actual handle");t.check(trace->offsets==offsets&&trace->modes==modes,"same-key no repeat offset/mode callbacks");t.offset(h,c);
    t.process(s,k,20.75,0.375,{},"same-key delta continuity");
}
void gates(Tests& t){
    Session s;const auto k=key();t.must(s.select(k,observedOps(),observedReads(),calibration(),30),"gate selection");t.process(s,k,30.125,0.125);
    std::vector<Key> bad;auto add=[&](auto change){auto wrong=k;change(wrong);bad.push_back(wrong);};
    add([](Key& x){x.physicalIdentity="SYNTHETIC-OTHER";});add([](Key& x){++x.sensorIndex;});add([](Key& x){++x.power;});add([](Key& x){++x.rawGeneration;});add([](Key& x){++x.captureEpoch;});add([](Key& x){x.physicalIdentity.clear();});add([](Key& x){x.power=0;});
    for(std::size_t i=0;i<bad.size();++i){const auto n=trace->processes,modes=trace->modes;t.check(!s.handleFor(bad[i]),"wrong key cannot borrow");t.check(!s.process(bad[i],sample(),30.2+static_cast<double>(i)*0.01),"wrong key rejected before ProcessMotion");t.check(!s.reconcileCalibrationMode(bad[i],true),"wrong key cannot set mode");t.check(trace->processes==n&&trace->modes==modes,"wrong key creates no native process/mode effects");}
    t.process(s,k,30.5,0.375,{},"rejected calls do not advance process clock");
}
void modes(Tests& t){
    Session s;const auto k=key();t.must(s.select(k,observedOps(),observedReads(),calibration(),40),"mode selection");void* h=s.handleFor(k);auto count=trace->modes;
    t.must(s.reconcileCalibrationMode(k,false),"cached Manual reconcile");t.check(trace->api.getMode(h)==0&&trace->modes==count,"cached Manual no repeat write and real readback0");
    t.must(s.reconcileCalibrationMode(k,true),"Auto reconcile");t.check(trace->api.getMode(h)==3&&trace->modes==count+1,"Auto transition actual readback3");count=trace->modes;
    t.must(s.reconcileCalibrationMode(k,true),"cached Auto reconcile");t.check(trace->api.getMode(h)==3&&trace->modes==count,"cached Auto no repeat write with real readback3");
    t.process(s,k,40.125,0.125);const auto live=t.native3(h,trace->api.getOffset);t.must(s.reconcileCalibrationMode(k,false),"Auto to Manual reconcile");t.check(trace->api.getMode(h)==0,"Auto to Manual actual readback0");t.equal(t.native3(h,trace->api.getOffset),live,"mode change retains current native offset");t.process(s,k,40.25,0.125);
    const float confidence=trace->api.getConfidence(h);t.check(std::isfinite(confidence),"actual confidence finite only");emit("auto-observation",{{"confidence",number(confidence)},{"offset",vecJson(t.native3(h,trace->api.getOffset))},{"thresholdAsserted","false"}});
}
void borrowed(Tests& t){
    Session s;const auto k=key();t.must(s.select(k,observedOps(),observedReads(),calibration(),50),"borrow selection");void* h=s.handleFor(k);t.must(s.reconcileCalibrationMode(k,true),"initial software Auto preference");t.process(s,k,50.125,0.125);
    s.invalidateCalibrationMode();t.check(!s.calibrationModeFor(k),"borrow invalidates software intent cache");
    const Calibration live{0.5f,-0.25f,0.125f,8};trace->api.setMode(h,0);trace->api.setOffset(h,live.x,live.y,live.z,live.weight);t.check(trace->api.getMode(h)==0,"borrow writes real Manual mode");
    t.must(s.reconcileCalibrationMode(k,true),"restore software Auto preference");t.check(trace->api.getMode(h)==3,"manual borrow restores software Auto preference via native readback");t.offset(h,live);t.process(s,k,50.25,0.125,{},"borrow keeps process time continuity");
    t.must(s.reconcileCalibrationMode(k,false),"current preference switched to Manual");s.invalidateCalibrationMode();trace->api.setMode(h,3);t.must(s.reconcileCalibrationMode(k,false),"restore current Manual preference");t.check(trace->api.getMode(h)==0,"manual borrow restores current Manual preference via native readback");t.process(s,k,50.375,0.125);t.check(s.handleFor(k)==h,"manual borrow retains current actual handle");
}
void epochs(Tests& t){
    Session s;auto current=key();Calibration saved=calibration();double selection=60;
    for(unsigned stage=0;stage<4;++stage){
        t.must(s.select(current,observedOps(),observedReads(),saved,selection),"new generation selection");void* h=s.handleFor(current);t.offset(h,saved);t.check(trace->api.getMode(h)==0,"rebuilt owner native Manual initialization");t.process(s,current,selection+0.125,0.125,{},"new generation first dt from selection");
        const auto value=t.native3(h,trace->api.getOffset);saved={value[0],value[1],value[2],saved.weight}; // Weight retained from caller, no invented weight getter.
        const auto old=current;const auto persistent=calibrationKeyFor(current);const auto deletes=trace->deleted;s.revoke();s.revoke();t.check(trace->deleted==deletes+1&&!s.active(),"sleep-like retirement destroys exactly once");
        const auto creates=trace->created,processes=trace->processes;t.check(!s.select(old,observedOps(),observedReads(),saved,selection+0.25),"retired exact token cannot recreate");t.check(!s.process(old,sample(),selection+0.375),"retired token cannot process");t.check(trace->created==creates&&trace->processes==processes&&!s.handleFor(old),"retired token causes no actual callbacks");
        if(stage==0)++current.power;else if(stage==1)++current.rawGeneration;else ++current.captureEpoch;
        t.check(calibrationKeyFor(current)==persistent,"generation excluded from persistence key");selection+=10;
    }
}
void scope(Tests& t){
    Session left,right;auto l=key(),r=l;r.sensorIndex=1;const auto lc=calibration();const Calibration rc{-0.75f,0.5f,-0.25f,8};
    t.must(left.select(l,observedOps(),observedReads(),lc,110)&&right.select(r,observedOps(),observedReads(),rc,110),"left/right real independent owners");t.check(left.handleFor(l)!=right.handleFor(r),"two current sensor instances distinct");t.check(!(calibrationKeyFor(l)==calibrationKeyFor(r)),"sensor persistence keys separate");
    const auto result=t.process(left,l,110.125,0.125);t.process(right,r,110.125,0.125);t.equal(t.native3(left.handleFor(l),trace->api.getCalibrated),vector(result.calibrated),"right process leaves left state independent");
    const auto old=l;l.sensorIndex=1;++l.captureEpoch;t.must(left.select(l,observedOps(),observedReads(),rc,111),"side change reconstructs owner");t.offset(left.handleFor(l),rc);t.process(left,l,111.125,0.125);t.check(!left.process(old,sample(),111.25),"old side token cannot process");t.offset(right.handleFor(r),rc);
    const auto oldIdentity=l;l.physicalIdentity="SYNTHETIC-CPU-CONTROLLER-B";l.sensorIndex=0;++l.captureEpoch;const Calibration other{1.5f,0.25f,-0.5f,8};
    t.must(left.select(l,observedOps(),observedReads(),other,112),"identity change reconstructs owner");t.offset(left.handleFor(l),other);t.check(!(calibrationKeyFor(l)==calibrationKeyFor(oldIdentity)),"physical identity persistence keys separate");t.process(left,l,112.125,0.125);t.offset(right.handleFor(r),rc);t.check(!left.handleFor(oldIdentity),"old identity cannot borrow new instance");
}
void replacements(Tests& t){
    Session s;const auto k=key();const auto c=calibration();auto ops=observedOps();auto reads=observedReads();t.must(s.select(k,ops,reads,c,120),"replacement initial selection");t.process(s,k,120.125,0.125);t.must(s.reconcileCalibrationMode(k,true),"replacement initial Auto");
    t.must(gravityObservedB!=gravityObserved&&modeObservedB!=modeObserved,"fixture alternate callback addresses distinct");
    auto serial=trace->serial(s.handleFor(k));reads.getGravity=gravityObservedB;t.must(s.select(k,ops,reads,c,121),"read callback replacement");t.check(trace->serial(s.handleFor(k))!=serial,"read callback change rebuilds actual allocation serial");t.check(trace->api.getMode(s.handleFor(k))==0,"read replacement resets mode cache and native mode0");t.offset(s.handleFor(k),c);t.process(s,k,121.125,0.125,{},"read replacement resets first dt");
    serial=trace->serial(s.handleFor(k));ops.setMode=modeObservedB;t.must(s.select(k,ops,reads,c,122),"Ops callback replacement");t.check(trace->serial(s.handleFor(k))!=serial,"Ops change rebuilds actual allocation serial");t.offset(s.handleFor(k),c);t.process(s,k,122.125,0.125);t.check(s.matchesSelection(k,ops,reads),"selection reflects every callback");
}
void ownerGate(Tests& t){
    Owner owner;const auto k=key();t.must(owner.select(k,observedOps(),calibration()),"Owner direct real selection");t.offset(owner.handleFor(k),calibration());t.check(owner.process(k,sample(),0.125f),"Owner current key actual process");auto wrong=k;++wrong.power;const auto count=trace->processes;
    t.check(!owner.process(wrong,sample(),0.125f),"Owner wrong key rejects process");t.check(trace->processes==count,"Owner wrong key has zero native process calls");owner.revoke();const auto creates=trace->created;t.check(!owner.select(k,observedOps(),calibration()),"Owner retired key denied");t.check(trace->created==creates&&!owner.handleFor(k),"Owner denied old token does not create");
    ++wrong.captureEpoch;t.must(owner.select(wrong,observedOps(),calibration()),"Owner fresh token rebuilds");t.offset(owner.handleFor(wrong),calibration());owner.revoke();owner.revoke();
}
using CaseFn=void(*)(Tests&);
const std::array<std::pair<const char*,CaseFn>,9> scenarioFunctions{{
 {"initial-and-requested-planes",initial},{"same-key-stability",stable},{"wrong-key-gates",gates},{"auto-mode-coordination",modes},{"manual-borrow-invalidation",borrowed},{"sleep-like-generations",epochs},{"sensor-and-identity-scope",scope},{"callback-replacement",replacements},{"Owner-key-gate",ownerGate}
}};
}
int wmain(int argc,wchar_t** argv){
    std::cout<<std::unitbuf;SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    HMODULE module=nullptr;std::unique_ptr<PinnedFile> pinned;std::unique_ptr<Tracker> tracking;Tests tests;std::string fatal;bool unloaded=false;std::string filter;
    try{
        if(argc!=3&&argc!=5)throw std::runtime_error("Usage: helper <private DLL> <evidence root> [--case name]");
        if(argc==5){if(std::wstring(argv[3])!=L"--case")throw std::runtime_error("Invalid case flag");filter=utf8(argv[4]);}
        const std::filesystem::path dll(argv[1]),root(argv[2]);if(!dll.is_absolute()||!root.is_absolute()||!samePath(dll.wstring(),(root/L"binary"/L"GamepadMotion.dll").wstring()))throw std::runtime_error("Only absolute private evidence DLL permitted");
        const auto relative=std::filesystem::path(modulePath(nullptr)).lexically_relative(root);if(relative.empty()||relative.is_absolute()||*relative.begin()==L"..")throw std::runtime_error("Only private evidence helper permitted");
        emit("start",{{"pid",integer(GetCurrentProcessId())},{"caseFilter",quote(filter)},{"loadFlags",quote("DLL_LOAD_DIR | SYSTEM32")},{"actualHeaders",quote("current production Session and Owner")}});emitModules("helper-start",modules());
        pinned=std::make_unique<PinnedFile>(dll.wstring());const auto digest=hashFile(pinned->file);if(digest!=kSha256)throw std::runtime_error("Pinned binary SHA mismatch; load refused");emit("hash-verified",{{"sha256",quote(digest)},{"lockedAgainstWriteAndDelete","true"}});emitModules("before-target-load",modules());
        module=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!module)throw std::runtime_error("LoadLibraryExW failed: "+std::to_string(GetLastError()));
        const auto actual=modulePath(module);emit("loaded",{{"path",quote(utf8(actual))}});if(!samePath(actual,dll.wstring()))throw std::runtime_error("Wrong module loaded");emitModules("after-target-load",modules());
        tracking=std::make_unique<Tracker>(bindApi(module));trace=tracking.get();
        for(const auto& scenario:scenarioFunctions)if(filter.empty()||filter==scenario.first){tests.start(scenario.first);scenario.second(tests);tests.check(trace->clean(),"all Session/Owner scopes destroyed before next case");}
        if(tests.cases.empty())throw std::runtime_error("Unknown case filter");
    }catch(const std::exception& e){fatal=e.what();emit("fatal-gap",{{"error",quote(fatal)}});}catch(...){fatal="Unexpected C++ exception at known ABI; stopped";emit("fatal-gap",{{"error",quote(fatal)}});}
    const bool clean=tracking&&tracking->clean();
    if(module&&clean){unloaded=FreeLibrary(module)!=FALSE;try{const auto list=modules();emitModules("after-target-unload",list);for(const auto& m:list)if(samePath(m.path,argv[1])){unloaded=false;if(fatal.empty())fatal="Target remains in module list";}}catch(const std::exception& e){if(fatal.empty())fatal=e.what();}}
    for(const auto& c:tests.cases)emit("case-result",{{"name",quote(c.name)},{"checks",integer(c.checks)},{"failures",integer(c.failures)}});
    const bool pass=fatal.empty()&&!tests.failures&&clean&&unloaded;
    emit("result",{{"status",quote(pass?"pass":"fail")},{"checks",integer(tests.checks)},{"failures",integer(tests.failures)},{"cases",integer(tests.cases.size())},{"created",integer(tracking?tracking->created:0)},{"deleted",integer(tracking?tracking->deleted:0)},{"processCalls",integer(tracking?tracking->processes:0)},{"playerCalls",integer(tracking?tracking->players:0)},{"worldCalls",integer(tracking?tracking->worlds:0)},{"callbackViolations",integer(tracking?tracking->violations:0)},{"cleanupComplete",clean?"true":"false"},{"moduleUnloaded",unloaded?"true":"false"},{"fatal",quote(fatal)},{"actualPbtPerformed","false"},{"fullNativeStateOrAutoConvergenceProven","false"}});
    trace=nullptr;return pass?0:1;
}
