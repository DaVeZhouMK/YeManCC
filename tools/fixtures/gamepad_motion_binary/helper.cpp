#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <psapi.h>
#include "gamepad_motion_abi.h"

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
    return api;
}
using Vec = std::array<float, 3>;
using Sample = std::array<float, 7>;
std::string vectorJson(const Vec& v) { return '[' + number(v[0]) + ',' + number(v[1]) + ',' + number(v[2]) + ']'; }
std::string sampleJson(const Sample& v) {
    std::string result = "["; for (std::size_t i = 0; i < v.size(); ++i) { if (i) result += ','; result += number(v[i]); } return result + ']';
}
struct Case { std::string name; std::uint64_t checks = 0, failures = 0; };
struct Instance { std::uint64_t serial; void* handle; };
struct Harness {
    Api api; std::vector<Instance> instances; std::vector<Case> cases;
    std::uint64_t checks = 0, failures = 0, created = 0, deleted = 0, processed = 0;
    explicit Harness(Api value) : api(value) { instances.reserve(64); cases.reserve(16); }
    void start(const std::string& name) { cases.push_back({name}); emit("case-start",{{"name",quote(name)}}); }
    void check(bool condition, const std::string& label) {
        ++checks; ++cases.back().checks;
        if (!condition) {
            ++failures; ++cases.back().failures;
            emit("assertion-failed",{{"case",quote(cases.back().name)},{"label",quote(label)}});
            if (failures >= 16) throw std::runtime_error("Too many numerical assertions failed");
        }
    }
    void must(bool condition, const std::string& label) { check(condition,label); if (!condition) throw std::runtime_error(label); }
    std::size_t create() {
        void* handle = api.create(); if (!handle) throw std::runtime_error("CreateGamepadMotion returned nullptr");
        const auto serial = ++created; instances.push_back({serial,handle});
        emit("created",{{"serial",integer(serial)},{"address",quote(integer(reinterpret_cast<std::uintptr_t>(handle)))}});
        return instances.size()-1;
    }
    void* handle(std::size_t id) { if (id >= instances.size() || !instances[id].handle) throw std::runtime_error("Fixture attempted stale native handle"); return instances[id].handle; }
    void destroy(std::size_t id) {
        void* pointer = handle(id); instances[id].handle = nullptr;
        api.destroy(pointer); ++deleted;
        emit("deleted",{{"serial",integer(instances[id].serial)}});
    }
    bool cleanup() {
        bool complete = true;
        for (std::size_t i = 0; i < instances.size(); ++i) if (instances[i].handle) {
            try { destroy(i); }
            catch (const std::exception& error) { complete = false; emit("cleanup-error",{{"error",quote(error.what())}}); }
            catch (...) { complete = false; emit("cleanup-error",{{"error",quote("Unknown C++ exception from Delete")}}); }
        }
        return complete && created == deleted;
    }
    Vec read(std::size_t id, Get3 getter) {
        const float nan = std::numeric_limits<float>::quiet_NaN(); Vec result{nan,nan,nan};
        getter(handle(id), &result[0], &result[1], &result[2]);
        must(std::isfinite(result[0]) && std::isfinite(result[1]) && std::isfinite(result[2]),"Native vec3 readback finite/fully written");
        return result;
    }
    float confidence(std::size_t id) { const float result = api.getConfidence(handle(id)); must(std::isfinite(result),"Native confidence finite"); return result; }
    void equal(const Vec& actual, const Vec& expected, const std::string& label) {
        for (std::size_t i=0;i<3;++i) check(std::abs(actual[i]-expected[i]) <= 0.00002f * (1.0f + std::abs(expected[i])),label+" axis "+std::to_string(i));
    }
    void mode(std::size_t id, int selected) { api.setMode(handle(id),selected); must(api.getMode(handle(id)) == selected,"GetCalibrationMode equals requested mode"); }
    void offset(std::size_t id, const Vec& value, int weight) { api.setOffset(handle(id),value[0],value[1],value[2],weight); }
    void process(std::size_t id, const Sample& value) {
        for (float x : value) must(std::isfinite(x),"Fixture sample and dt finite");
        api.process(handle(id),value[0],value[1],value[2],value[3],value[4],value[5],value[6]); ++processed;
    }
    void compensated(std::size_t id, const Sample& raw, const Vec& baseline) {
        process(id,raw); equal(read(id,api.getCalibrated),{raw[0]-baseline[0],raw[1]-baseline[1],raw[2]-baseline[2]},"Manual raw minus offset");
        equal(read(id,api.getOffset),baseline,"Manual offset retained"); read(id,api.getGravity);
    }
};
Sample synthetic(unsigned phase, unsigned frame) {
    if (phase != 1) return {0.35f,-0.20f,0.125f,0,0,1,0.004f};
    const float t=static_cast<float>(frame%128)/128.0f;
    return {5.0f+20.0f*t,-7.0f+8.0f*t,3.0f-6.0f*t,0.1f*t,-0.05f*t,1,0.004f};
}
void tests(Harness& h) {
    const Vec initial{1.25f,-2.5f,0.75f};
    h.start("mode-real-readback"); const auto a=h.create(); h.mode(a,0); h.mode(a,3); h.mode(a,0);
    h.start("offset-roundtrip-and-zero");
    for (int weight : {1,3,8,10}) { h.offset(a,initial,weight); h.equal(h.read(a,h.api.getOffset),initial,"Positive-weight offset roundtrip"); }
    h.offset(a,{0,0,0},0); h.equal(h.read(a,h.api.getOffset),{0,0,0},"Zero offset weight0 legal");
    h.start("manual-compensation-and-zero-samples"); h.mode(a,0); h.offset(a,initial,8);
    h.compensated(a,{initial[0],initial[1],initial[2],0,0,1,0.004f},initial);
    // Fixed DLL RVA 0x2943..0x2998 skips processing if all six axes are zero.
    // This valid input preserves prior readbacks; it is not raw-minus-offset work.
    const auto beforeZeroCal=h.read(a,h.api.getCalibrated), beforeZeroGravity=h.read(a,h.api.getGravity);
    for (float dt : {0.0f,0.004f}) {
        h.process(a,{0,0,0,0,0,0,dt});
        const auto cal=h.read(a,h.api.getCalibrated), gravity=h.read(a,h.api.getGravity), offset=h.read(a,h.api.getOffset);
        h.equal(cal,beforeZeroCal,"All-zero axes preserve previous calibrated output");
        h.equal(gravity,beforeZeroGravity,"All-zero axes preserve previous gravity");
        h.equal(offset,initial,"All-zero axes preserve Manual offset");
        emit("all-zero-observation",{{"dt",number(dt)},{"calibrated",vectorJson(cal)},{"gravity",vectorJson(gravity)},{"offset",vectorJson(offset)}});
    }
    h.compensated(a,{0,0,0,0,0,1,0.004f},initial);
    for (unsigned i=0;i<256;++i) {
        const float x=static_cast<float>(i%17)-8.0f;
        h.compensated(a,{x,-2.0f*x+0.5f,0.25f*x-1.0f,0.125f,-0.0625f,1.0f,0.002f+static_cast<float>(i%5)*0.001f},initial);
    }
    h.start("two-handle-isolation"); const auto b=h.create(); h.must(h.handle(a)!=h.handle(b),"Concurrent instances have distinct handles");
    const Vec other{-0.75f,0.5f,-0.25f}; h.mode(b,3); h.offset(b,other,8);
    const auto bOffset=h.read(b,h.api.getOffset), bCal=h.read(b,h.api.getCalibrated), bGravity=h.read(b,h.api.getGravity); const float bConfidence=h.confidence(b);
    for (unsigned i=0;i<64;++i) h.compensated(a,{4,-8,2,0,0,1,0.004f},initial);
    h.equal(h.read(b,h.api.getOffset),bOffset,"Unprocessed other handle offset isolated");
    h.equal(h.read(b,h.api.getCalibrated),bCal,"Unprocessed other handle calibrated axes isolated");
    h.equal(h.read(b,h.api.getGravity),bGravity,"Unprocessed other handle gravity isolated");
    h.check(h.confidence(b)==bConfidence,"Other handle confidence unchanged without process");
    h.must(h.api.getMode(h.handle(b))==3 && h.api.getMode(h.handle(a))==0,"Independent mode readbacks");
    const auto aCal=h.read(a,h.api.getCalibrated), aGravity=h.read(a,h.api.getGravity);
    h.process(b,{0.35f,-0.2f,0.125f,0,0,1,0.004f}); h.read(b,h.api.getCalibrated); h.read(b,h.api.getGravity);
    h.equal(h.read(a,h.api.getOffset),initial,"Processing auto handle leaves manual offset isolated");
    h.equal(h.read(a,h.api.getCalibrated),aCal,"Processing auto handle leaves other calibrated axes isolated");
    h.equal(h.read(a,h.api.getGravity),aGravity,"Processing auto handle leaves other gravity isolated");
    h.start("new-handle-lifecycle"); h.destroy(b);
    for (unsigned i=0;i<16;++i) {
        const auto fresh=h.create(); h.equal(h.read(fresh,h.api.getOffset),{0,0,0},"New instance has no retired offset");
        h.mode(fresh,static_cast<int>(i%2)*3);
        const Vec own{static_cast<float>(i)+0.125f,-0.25f,0.5f}; h.offset(fresh,own,2);
        h.equal(h.read(fresh,h.api.getOffset),own,"Fresh instance own offset");
        h.process(fresh,{1,2,3,0,0,1,0.004f}); h.read(fresh,h.api.getCalibrated); h.read(fresh,h.api.getGravity); h.destroy(fresh);
    }
    const auto automatic=h.create(); h.equal(h.read(automatic,h.api.getOffset),{0,0,0},"New auto instance starts without retired calibration"); h.mode(automatic,3); h.offset(automatic,{0,0,0},0);
    h.start("auto-observed-evolution"); const unsigned phaseCounts[]{4096,2048,4096}; const char* names[]{"biased-still","synthetic-changing-motion","biased-still-again"};
    unsigned total=0; double elapsed=0;
    emit("auto-observation",{{"phase",quote("initial")},{"frame",integer(0)},{"syntheticSeconds",number(0)},{"confidence",number(h.confidence(automatic))},{"offset",vectorJson(h.read(automatic,h.api.getOffset))},{"mode",integer(static_cast<unsigned>(h.api.getMode(h.handle(automatic))))}});
    for (unsigned phase=0;phase<3;++phase) for (unsigned frame=0;frame<phaseCounts[phase];++frame) {
        const Sample input=synthetic(phase,frame); h.process(automatic,input); ++total; elapsed+=input[6];
        const Vec offset=h.read(automatic,h.api.getOffset), cal=h.read(automatic,h.api.getCalibrated), gravity=h.read(automatic,h.api.getGravity); const float confidence=h.confidence(automatic);
        h.compensated(a,input,initial);
        if (frame==0 || (frame+1)%128==0 || frame+1==phaseCounts[phase]) {
            const int actualMode=h.api.getMode(h.handle(automatic)); h.must(actualMode==3,"Auto mode readback remains3");
            emit("auto-observation",{{"phase",quote(names[phase])},{"phaseFrame",integer(frame+1)},{"frame",integer(total)},{"syntheticSeconds",number(elapsed)},{"input",sampleJson(input)},{"mode",integer(static_cast<unsigned>(actualMode))},{"confidence",number(confidence)},{"offset",vectorJson(offset)},{"calibrated",vectorJson(cal)},{"gravity",vectorJson(gravity)}});
        }
    }
    h.start("auto-to-manual-live-baseline"); const Vec live=h.read(automatic,h.api.getOffset); const float liveConfidence=h.confidence(automatic); h.mode(automatic,0);
    for (unsigned i=0;i<128;++i) h.compensated(automatic,{2,-3,4,0,0,1,0.004f},live);
    emit("manual-live-baseline",{{"offset",vectorJson(live)},{"confidenceAtSwitch",number(liveConfidence)},{"modeReadback",integer(static_cast<unsigned>(h.api.getMode(h.handle(automatic))))}});
    h.equal(h.read(a,h.api.getOffset),initial,"Manual control independent through whole auto trajectory");
}
}
int wmain(int argc, wchar_t** argv) {
    std::cout << std::unitbuf; SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    HMODULE module=nullptr; bool cleanupComplete=true, unloaded=false; std::string fatal; std::vector<Module> afterLoad;
    std::unique_ptr<PinnedFile> pinned; std::unique_ptr<Harness> harness;
    try {
        if (argc!=3) throw std::runtime_error("Usage: helper.exe <absolute private DLL> <absolute evidence run root>");
        const std::filesystem::path dll(argv[1]), root(argv[2]);
        if (!dll.is_absolute() || !root.is_absolute() || !samePath(dll.wstring(),(root/L"binary"/L"GamepadMotion.dll").wstring())) throw std::runtime_error("DLL must be the absolute private evidence snapshot");
        const auto executable=modulePath(nullptr);
        const auto relative=std::filesystem::path(executable).lexically_relative(root);
        if (relative.empty() || relative.is_absolute() || *relative.begin()==L"..") throw std::runtime_error("Helper must execute inside private evidence root");
        emit("start",{{"pid",integer(GetCurrentProcessId())},{"helper",quote(utf8(executable))},{"dll",quote(utf8(dll.wstring()))},{"loadFlags",quote("LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32")},{"hardware",quote("none")}});
        emitModules("helper-start",modules());
        pinned=std::make_unique<PinnedFile>(dll.wstring()); const auto digest=hashFile(pinned->file);
        emit("hash-verified",{{"sha256",quote(digest)},{"bytes",integer(kBytes)},{"lockedAgainstWriteAndDelete", "true"}});
        if (digest!=kSha256) throw std::runtime_error("Private DLL SHA256 mismatch; load refused");
        emitModules("before-target-load",modules());
        module=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) throw std::runtime_error("LoadLibraryExW gap/error: "+std::to_string(GetLastError()));
        const auto actualPath=modulePath(module); emit("loaded",{{"path",quote(utf8(actualPath))}});
        if (!samePath(actualPath,dll.wstring())) throw std::runtime_error("Loaded module differs from private pinned path");
        afterLoad=modules(); emitModules("after-target-load",afterLoad);
        harness=std::make_unique<Harness>(bindApi(module)); tests(*harness);
    } catch (const std::exception& error) { fatal=error.what(); emit("fatal-gap",{{"error",quote(fatal)}}); }
      catch (...) { fatal="Unknown C++ exception at known ABI; stopped"; emit("fatal-gap",{{"error",quote(fatal)}}); }
    if (harness) cleanupComplete=harness->cleanup();
    if (module && cleanupComplete) {
        unloaded=FreeLibrary(module)!=FALSE; if (!unloaded && fatal.empty()) fatal="FreeLibrary failed";
        try {
            const auto after=modules(); emitModules("after-target-unload",after);
            const bool remains=std::any_of(after.begin(),after.end(),[&](const Module& m){return samePath(m.path,std::wstring(argv[1]));});
            emit("unloaded",{{"FreeLibrarySucceeded",unloaded?"true":"false"},{"targetStillListed",remains?"true":"false"}});
            if (remains && fatal.empty()) fatal="Target remains loaded after cleanup";
        } catch (const std::exception& error) { if (fatal.empty()) fatal=error.what(); }
    }
    if (harness) for (const auto& item:harness->cases) emit("case-result",{{"name",quote(item.name)},{"checks",integer(item.checks)},{"failures",integer(item.failures)}});
    const bool pass=fatal.empty() && harness && !harness->failures && cleanupComplete && unloaded;
    emit("result",{{"status",quote(pass?"pass":"fail")},{"checks",integer(harness?harness->checks:0)},{"failures",integer(harness?harness->failures:0)},{"cases",integer(harness?harness->cases.size():0)},{"created",integer(harness?harness->created:0)},{"deleted",integer(harness?harness->deleted:0)},{"processed",integer(harness?harness->processed:0)},{"cleanupComplete",cleanupComplete?"true":"false"},{"moduleUnloaded",unloaded?"true":"false"},{"fatal",quote(fatal)},{"autoConvergenceThresholdAsserted","false"},{"completeInitializationSideEffectAbsenceProven","false"}});
    return pass?0:1;
}
