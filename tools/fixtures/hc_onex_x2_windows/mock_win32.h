#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace test {
inline std::string section, scenario;
inline std::uint64_t checks = 0;
inline unsigned scenarios = 0;
inline std::vector<std::string> completed;
inline std::string escape(const std::string& s) {
    std::string out;
    for (char c : s) { if (c == '"' || c == '\\') out += '\\'; if (c == '\n') out += "\\n"; else out += c; }
    return out;
}
[[noreturn]] inline void fail(const std::string& message) {
    std::printf("{\"status\":\"fail\",\"section\":\"%s\",\"scenario\":\"%s\",\"message\":\"%s\",\"checks\":%llu}\n",
        escape(section).c_str(), escape(scenario).c_str(), escape(message).c_str(), static_cast<unsigned long long>(checks));
    std::fflush(stdout); std::exit(1);
}
inline void check(bool value, const std::string& message) { ++checks; if (!value) fail(message); }
template<class A, class B> inline void eq(const A& a, const B& b, const std::string& message) { check(a == b, message); }
}

// Fake pointers are owned by World. None of these objects is an OS handle.
namespace mock {
enum class Kind { Set, Probe, Device, Event };
struct Device {
    std::wstring path = L"mock-vendor";
    USHORT vid = 0x1A86, pid = 0xFE00, page = 0xFF00, usage = 1, input = 64, output = 64;
    bool probeOk = true, attributesOk = true, preparsedOk = true, capsOk = true, openOk = true;
    bool detailOk = true;
    DWORD detailSize = 0;
    std::shared_ptr<Device> reopened;
};
struct Object {
    Kind kind; std::size_t index; bool closed = false, signaled = false;
    DWORD waitOverride = 0xFFFFFFFEu;
    unsigned closeAttempts = 0, closeFailures = 0;
    bool failClose = false;
    DWORD closeError = ERROR_ACCESS_DENIED;
};
enum class Mode { Pending, Immediate, Fail };
struct Plan {
    Mode mode = Mode::Pending;
    DWORD error = ERROR_ACCESS_DENIED, bytes = (std::numeric_limits<DWORD>::max)();
    std::vector<unsigned char> frame;
};
struct Io {
    HANDLE device{}, event{}; OVERLAPPED* ov{}; void* buffer{}; DWORD size = 0; bool writing = false;
    bool terminal = false, observed = false, success = false;
    DWORD bytes = 0, error = ERROR_IO_INCOMPLETE, observationError = ERROR_IO_INCOMPLETE;
    unsigned cancellations = 0;
    std::vector<unsigned char> retained;
};
struct Submission { bool writing; HANDLE device; void* buffer; OVERLAPPED* ov; DWORD size; std::vector<unsigned char> data; };
struct World {
    std::vector<Device> devices;
    std::vector<std::unique_ptr<Object>> objects;
    std::vector<std::unique_ptr<Io>> ios;
    std::vector<Submission> submissions;
    std::deque<Plan> reads, writes;
    DWORD error = ERROR_SUCCESS;
    unsigned guidCalls = 0, enumerations = 0, interfaces = 0, probes = 0, dataOpens = 0, destroys = 0;
    unsigned readsCalled = 0, writesCalled = 0, waits = 0, results = 0, resets = 0, cancellations = 0;
    unsigned closeDevices = 0, closeEvents = 0, createEvents = 0, prepAllocated = 0, prepFreed = 0, attributeCalls = 0, capsCalls = 0;
    unsigned closeCalls = 0, failedCloses = 0, errorsRead = 0;
    bool failSet = false, cancelSucceeds = true, failProbeClose = false;
    unsigned failEventAt = 0, failResetAt = 0;
};
inline World w;
inline Object* object(HANDLE h) {
    for (auto& x : w.objects) if (reinterpret_cast<HANDLE>(x.get()) == h) return x.get();
    test::fail("foreign handle would escape memory-only Win32 boundary");
}
inline Object* live(HANDLE h, Kind kind) {
    auto* p = object(h); test::check(!p->closed && p->kind == kind, "live mock handle and expected kind"); return p;
}
inline HANDLE make(Kind kind, std::size_t index = 0) {
    w.objects.push_back(std::make_unique<Object>(Object{kind, index}));
    return reinterpret_cast<HANDLE>(w.objects.back().get());
}
inline void reset() { w = World{}; }
inline Device device(std::wstring path = L"mock-vendor") { Device d; d.path = path; return d; }
inline unsigned outstanding() { unsigned n = 0; for (const auto& io : w.ios) if (!io->observed) ++n; return n; }
inline void retained() {
    for (const auto& io : w.ios) if (!io->observed) {
        live(io->device, Kind::Device); live(io->event, Kind::Event);
        test::eq(io->ov->hEvent, io->event, "pending OVERLAPPED retains its event");
        test::eq(io->ov->Offset, DWORD{0}, "pending OVERLAPPED offset unchanged");
        test::eq(io->ov->OffsetHigh, DWORD{0}, "pending OVERLAPPED high offset unchanged");
        test::check(std::equal(io->retained.begin(), io->retained.end(), static_cast<const unsigned char*>(io->buffer)),
            "outstanding operation retains its buffer contents");
    }
}
inline Io& pending(bool writing) {
    for (auto it = w.ios.rbegin(); it != w.ios.rend(); ++it) if ((*it)->writing == writing && !(*it)->observed) return **it;
    test::fail(writing ? "expected pending write" : "expected pending read");
}
inline void complete(Io& io, bool success = true, DWORD bytes = (std::numeric_limits<DWORD>::max)(),
                     DWORD error = ERROR_OPERATION_ABORTED, const std::vector<unsigned char>& frame = {}, bool signal = true) {
    retained(); test::check(!io.observed && !io.terminal, "completion applies once to outstanding IO");
    io.terminal = true; io.success = success; io.bytes = bytes == (std::numeric_limits<DWORD>::max)() ? io.size : bytes; io.error = error;
    if (!io.writing && !frame.empty()) {
        test::check(frame.size() <= io.size, "mock DMA stays within submitted read length");
        std::copy(frame.begin(), frame.end(), static_cast<unsigned char*>(io.buffer));
        io.retained.assign(static_cast<unsigned char*>(io.buffer), static_cast<unsigned char*>(io.buffer) + io.size);
    }
    io.ov->Internal = success ? 0 : static_cast<ULONG_PTR>(0xC0000120u); io.ov->InternalHigh = io.bytes;
    live(io.event, Kind::Event)->signaled = signal;
}
inline void assertClean() {
    test::eq(outstanding(), 0u, "all submitted operations terminal and observed");
    for (const auto& o : w.objects) test::check(o->closed, "mock handle closed after terminal drain");
    test::eq(w.prepAllocated, w.prepFreed, "preparsed data freed");
    test::eq(w.enumerations, w.destroys + (w.failSet ? 1u : 0u), "enumeration set closed");
}
inline void WINAPI hidGuid(LPGUID guid) { ++w.guidCalls; *guid = GUID{}; }
inline HDEVINFO WINAPI getClassDevs(const GUID*, PCWSTR enumerator, HWND parent, DWORD flags) {
    ++w.enumerations; test::check(!enumerator && !parent && flags == (DIGCF_PRESENT | DIGCF_DEVICEINTERFACE), "bounded HID enumeration arguments");
    if (w.failSet) { w.error = ERROR_ACCESS_DENIED; return INVALID_HANDLE_VALUE; } return make(Kind::Set);
}
inline BOOL WINAPI enumInterfaces(HDEVINFO set, PSP_DEVINFO_DATA info, const GUID*, DWORD index, PSP_DEVICE_INTERFACE_DATA data) {
    live(set, Kind::Set); ++w.interfaces; test::check(!info && data && data->cbSize == sizeof(*data), "interface enumeration structure");
    if (index >= w.devices.size()) { w.error = ERROR_NO_MORE_ITEMS; return FALSE; }
    data->Reserved = static_cast<ULONG_PTR>(index); return TRUE;
}
inline BOOL WINAPI detail(HDEVINFO set, PSP_DEVICE_INTERFACE_DATA iface, PSP_DEVICE_INTERFACE_DETAIL_DATA_W data,
                         DWORD capacity, PDWORD required, PSP_DEVINFO_DATA info) {
    live(set, Kind::Set); test::check(iface && !info && iface->Reserved < w.devices.size(), "detail only for enumerated interface");
    auto& d = w.devices[static_cast<std::size_t>(iface->Reserved)];
    const DWORD bytes = d.detailSize ? d.detailSize : static_cast<DWORD>(sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) + (d.path.size()+1)*sizeof(wchar_t));
    if (required) *required = bytes;
    if (!data) { test::eq(capacity, DWORD{0}, "detail size query uses zero buffer"); w.error = ERROR_INSUFFICIENT_BUFFER; return FALSE; }
    test::check(data->cbSize == sizeof(*data) && capacity >= bytes, "bounded interface detail buffer");
    if (!d.detailOk) { w.error = ERROR_INVALID_DATA; return FALSE; }
    std::copy(d.path.c_str(), d.path.c_str() + d.path.size() + 1, data->DevicePath); return TRUE;
}
inline BOOL WINAPI destroy(HDEVINFO set) { live(set, Kind::Set)->closed = true; ++w.destroys; return TRUE; }
inline HANDLE WINAPI open(LPCWSTR path, DWORD access, DWORD sharing, LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD flags, HANDLE templateFile) {
    test::check(path && !security && !templateFile && sharing == (FILE_SHARE_READ | FILE_SHARE_WRITE) && disposition == OPEN_EXISTING, "mock open contract");
    std::size_t i = 0; while (i < w.devices.size() && w.devices[i].path != path) ++i;
    test::check(i < w.devices.size(), "open only enumerated path"); auto& d = w.devices[i];
    if (!access) { ++w.probes; test::eq(flags, DWORD{FILE_ATTRIBUTE_NORMAL}, "probe has zero IO access");
        if (!d.probeOk) { w.error = ERROR_ACCESS_DENIED; return INVALID_HANDLE_VALUE; } return make(Kind::Probe, i); }
    ++w.dataOpens; test::eq(access, DWORD{GENERIC_READ | GENERIC_WRITE}, "vendor open access");
    test::eq(flags, DWORD{FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED}, "data handle is asynchronous");
    test::check(d.vid == 0x1A86 && d.page == 0xFF00 && d.usage == 1, "no gamepad keyboard or foreign collection IO open");
    if (!d.openOk) { w.error = ERROR_ACCESS_DENIED; return INVALID_HANDLE_VALUE; } return make(Kind::Device, i);
}
inline Device& descriptor(HANDLE h) {
    auto* o=object(h);test::check(!o->closed&&(o->kind==Kind::Probe||o->kind==Kind::Device),"HID attributes allowed on probe and actual owner handle");
    auto& d=w.devices[o->index];return o->kind==Kind::Device&&d.reopened?*d.reopened:d;
}
inline BOOLEAN WINAPI attributes(HANDLE h, PHIDD_ATTRIBUTES out) {
    ++w.attributeCalls; auto& d = descriptor(h); test::eq(out->Size, ULONG{sizeof(*out)}, "HID attributes initialized");
    if (!d.attributesOk) return FALSE; out->VendorID = d.vid; out->ProductID = d.pid; return TRUE;
}
inline BOOLEAN WINAPI preparsed(HANDLE h, PHIDP_PREPARSED_DATA* out) {
    auto& d = descriptor(h); if (!d.preparsedOk) return FALSE;
    *out = reinterpret_cast<PHIDP_PREPARSED_DATA>(&d); ++w.prepAllocated; return TRUE;
}
inline NTSTATUS __stdcall caps(PHIDP_PREPARSED_DATA p, PHIDP_CAPS out) {
    ++w.capsCalls; Device* d = nullptr; for (auto& item : w.devices) {
        if (reinterpret_cast<PHIDP_PREPARSED_DATA>(&item) == p) d = &item;
        if (item.reopened && reinterpret_cast<PHIDP_PREPARSED_DATA>(item.reopened.get()) == p) d = item.reopened.get();
    }
    test::check(d != nullptr, "caps only from mock preparsed data"); if (!d->capsOk) return HIDP_STATUS_INVALID_PREPARSED_DATA;
    *out = HIDP_CAPS{}; out->UsagePage=d->page; out->Usage=d->usage; out->InputReportByteLength=d->input; out->OutputReportByteLength=d->output; return HIDP_STATUS_SUCCESS;
}
inline BOOLEAN WINAPI freePreparsed(PHIDP_PREPARSED_DATA p) {
    bool found = false; for (auto& d : w.devices) if (reinterpret_cast<PHIDP_PREPARSED_DATA>(&d) == p || (d.reopened && reinterpret_cast<PHIDP_PREPARSED_DATA>(d.reopened.get()) == p)) found = true;
    test::check(found, "free only mock preparsed data"); ++w.prepFreed; return TRUE;
}
inline HANDLE WINAPI event(LPSECURITY_ATTRIBUTES security, BOOL manual, BOOL initial, LPCWSTR name) {
    ++w.createEvents; test::check(!security && manual == TRUE && initial == FALSE && !name, "private manual-reset event");
    if (w.failEventAt == w.createEvents) { w.error = ERROR_NOT_ENOUGH_MEMORY; return nullptr; } return make(Kind::Event);
}
inline BOOL WINAPI close(HANDLE h) {
    auto* o = object(h); test::check(!o->closed, "no double-close");
    for (const auto& io : w.ios) if (!io->observed) test::check(io->device != h && io->event != h, "never close unobserved IO handle/event");
    ++w.closeCalls; ++o->closeAttempts;
    if (o->failClose || o->closeFailures || (o->kind == Kind::Probe && w.failProbeClose)) {
        if (o->closeFailures) --o->closeFailures;
        ++w.failedCloses; w.error = o->closeError; return FALSE;
    }
    o->closed = true; if (o->kind == Kind::Device) ++w.closeDevices; if (o->kind == Kind::Event) ++w.closeEvents; return TRUE;
}
inline BOOL WINAPI resetEvent(HANDLE h) {
    auto* o = live(h, Kind::Event); ++w.resets;
    for (const auto& io : w.ios) if (!io->observed) test::check(io->event != h, "never reset event owned by retained operation");
    if (w.failResetAt == w.resets) { w.error = ERROR_INVALID_HANDLE; return FALSE; } o->signaled = false; return TRUE;
}
inline DWORD WINAPI wait(HANDLE h, DWORD timeout) {
    ++w.waits; test::eq(timeout, DWORD{0}, "EVERY wait has timeout zero"); auto* o = live(h, Kind::Event);
    return o->waitOverride != 0xFFFFFFFEu ? o->waitOverride : o->signaled ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
}
inline BOOL submit(bool writing, HANDLE h, void* buffer, DWORD size, LPDWORD bytes, LPOVERLAPPED ov) {
    live(h, Kind::Device); live(ov->hEvent, Kind::Event); retained();
    auto& d = descriptor(h); test::check(d.vid==0x1A86&&d.page==0xFF00&&d.usage==1,"ReadFile/WriteFile never target replaced gamepad/keyboard"); test::eq(size, DWORD{writing ? d.output : d.input}, "independent input/output descriptor lengths");
    test::check(buffer && bytes && ov && ov->Internal == 0 && ov->Offset == 0 && ov->OffsetHigh == 0, "fresh zeroed OVERLAPPED and owned buffer");
    for (const auto& io : w.ios) if (!io->observed) test::check(io->ov != ov && io->buffer != buffer, "pending buffer and OVERLAPPED never reused");
    w.submissions.push_back({writing,h,buffer,ov,size,{}});
    auto& sub = w.submissions.back(); if (writing) sub.data.assign(static_cast<unsigned char*>(buffer), static_cast<unsigned char*>(buffer)+size);
    auto& plans = writing ? w.writes : w.reads; Plan plan; if (!plans.empty()) { plan = plans.front(); plans.pop_front(); }
    if (plan.mode == Mode::Fail) { w.error = plan.error; return FALSE; }
    auto io = std::make_unique<Io>(); io->device=h; io->event=ov->hEvent; io->ov=ov; io->buffer=buffer; io->size=size; io->writing=writing;
    io->retained.assign(static_cast<unsigned char*>(buffer), static_cast<unsigned char*>(buffer)+size);
    if (plan.mode == Mode::Immediate) {
        io->observed=true; io->terminal=true; io->success=true; io->bytes=plan.bytes == (std::numeric_limits<DWORD>::max)() ? size : plan.bytes;
        if (!writing && !plan.frame.empty()) { test::check(plan.frame.size() <= size, "immediate DMA bounded"); std::copy(plan.frame.begin(),plan.frame.end(),static_cast<unsigned char*>(buffer)); }
        *bytes=io->bytes; ov->Internal=0; ov->InternalHigh=*bytes; w.ios.push_back(std::move(io)); return TRUE;
    }
    ov->Internal=static_cast<ULONG_PTR>(0x103); ov->InternalHigh=0; w.ios.push_back(std::move(io)); w.error=ERROR_IO_PENDING; return FALSE;
}
inline BOOL WINAPI read(HANDLE h, LPVOID buffer, DWORD size, LPDWORD bytes, LPOVERLAPPED ov) { ++w.readsCalled; return submit(false,h,buffer,size,bytes,ov); }
inline BOOL WINAPI write(HANDLE h, LPCVOID buffer, DWORD size, LPDWORD bytes, LPOVERLAPPED ov) { ++w.writesCalled; return submit(true,h,const_cast<void*>(buffer),size,bytes,ov); }
inline BOOL WINAPI cancel(HANDLE h, LPOVERLAPPED ov) {
    live(h, Kind::Device); retained(); ++w.cancellations;
    for (auto& io : w.ios) if (io->device==h && io->ov==ov && !io->observed) {
        test::eq(++io->cancellations, 1u, "CancelIoEx at most once per operation");
        if (!w.cancelSucceeds) { w.error=ERROR_NOT_FOUND; return FALSE; } return TRUE;
    }
    test::fail("cancel only outstanding submitted IO");
}
inline BOOL WINAPI result(HANDLE h, LPOVERLAPPED ov, LPDWORD bytes, BOOL blocking) {
    live(h, Kind::Device); test::eq(blocking, FALSE, "GetOverlappedResult never waits"); retained(); ++w.results;
    for (auto& io : w.ios) if (io->device==h && io->ov==ov && !io->observed) {
        if (!io->terminal) { w.error=io->observationError; return FALSE; }
        io->observed=true; *bytes=io->bytes; if (!io->success) w.error=io->error; return io->success ? TRUE : FALSE;
    }
    test::fail("GetOverlappedResult only for owned unobserved IO");
}
inline DWORD WINAPI getError() { ++w.errorsRead; return w.error; }
}

// Include the real SDK declarations FIRST. Header API tokens below resolve only
// to the mocks above; no function forwards to Win32. HasOverlappedIoCompleted is
// intentionally the SDK macro operating on the real OVERLAPPED::Internal field.
#define HidD_GetHidGuid mock::hidGuid
#define SetupDiGetClassDevsW mock::getClassDevs
#define SetupDiEnumDeviceInterfaces mock::enumInterfaces
#define SetupDiGetDeviceInterfaceDetailW mock::detail
#define SetupDiDestroyDeviceInfoList mock::destroy
#define CreateFileW mock::open
#define HidD_GetAttributes mock::attributes
#define HidD_GetPreparsedData mock::preparsed
#define HidP_GetCaps mock::caps
#define HidD_FreePreparsedData mock::freePreparsed
#define CreateEventW mock::event
#define CloseHandle mock::close
#define ResetEvent mock::resetEvent
#define WaitForSingleObject mock::wait
#define ReadFile mock::read
#define WriteFile mock::write
#define CancelIoEx mock::cancel
#define GetOverlappedResult mock::result
#define GetLastError mock::getError
