// Scoped reuse of existing calibration self census. No foreign process reads.
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace ObserverCensus {
using Json=nlohmann::json;
static unsigned ownedHandles=0,joinableThreads=0,ownedModuleReferences=0;
static std::atomic<unsigned> pendingIo{0},workers{0};
static void need(bool ok,const char* operation){if(!ok)throw std::runtime_error(operation);}
struct Entry { HANDLE value; ULONG_PTR handles, pointers; ULONG access, type, attributes, reserved; };
struct Table { ULONG_PTR count, reserved; Entry entries[1]; };
struct Unicode { USHORT length, capacity; PWSTR value; };
using QueryProcess = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
using QueryObject = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
std::string utf8(const Unicode& text, const std::vector<unsigned char>& buffer) {
    if (!text.length) return {};
    const auto start = reinterpret_cast<std::uintptr_t>(buffer.data());
    const auto ptr = reinterpret_cast<std::uintptr_t>(text.value);
    need(text.length % sizeof(wchar_t) == 0 && ptr >= start && ptr - start <= buffer.size() &&
        text.length <= buffer.size() - (ptr - start), "UNICODE_STRING bounds");
    const int count = text.length / static_cast<int>(sizeof(wchar_t));
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.value, count, nullptr, 0, nullptr, nullptr);
    need(size > 0, "UTF-8 size");
    std::string result(static_cast<size_t>(size), '\0');
    need(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.value, count, result.data(), size, nullptr, nullptr) == size, "UTF-8 conversion");
    return result;
}
struct Row {
    std::uintptr_t handle = 0;
    ULONG access = 0, typeIndex = 0;
    std::string type, nameState = "not-file", name;
    LONG nameStatus = 0;
    DWORD targetThreadId=0,targetProcessId=0;
    auto key() const { return std::to_string(handle) + ":" + type + ":" + std::to_string(access); }
    Json json() const {
        Json row{{"handle", handle}, {"access", access}, {"type", type}, {"typeIndex", typeIndex}, {"nameState", nameState}};
        if (nameState == "queried-quiescent-self" || nameState == "retained-quiescent-self") {
            row["objectName"] = name; row["objectNameStatus"] = static_cast<ULONG>(nameStatus);
        }
        if(type=="Thread")row["targetThreadId"]=targetThreadId?Json(targetThreadId):Json(nullptr);
        if(type=="Process")row["targetProcessId"]=targetProcessId?Json(targetProcessId):Json(nullptr);
        return row;
    }
};
struct Snapshot {
    DWORD countBefore = 0, countAfter = 0;
    std::vector<Row> rows;
    unsigned io = 0, live = 0, owned = 0, joinable = 0, moduleReferences = 0;
    Json modules = Json::object();
    Json json() const {
        Json entries = Json::array(), types = Json::object();
        for (const auto& row : rows) { entries.push_back(row.json()); types[row.type] = types.value(row.type, 0) + 1; }
        return {{"processHandleCountBeforeQuery", countBefore}, {"processHandleCountAfterQuery", countAfter},
            {"tableHandleCount", rows.size()}, {"typeCounts", types}, {"handles", entries},
            {"loadedCryptoModules", modules}, {"scope", "self handle table/type census; business resources observed separately"}};
    }
};
class Probe {
    QueryProcess query_ = nullptr;
    QueryObject object_ = nullptr;
    bool initialized_ = false, queryNames_ = false;
    std::map<std::string, Row> previous_;
public:
    Json stages = Json::array(), rounds = Json::array();
    explicit Probe(bool queryNames = false) : queryNames_(queryNames) {
        const auto module = GetModuleHandleW(L"ntdll.dll");
        query_ = reinterpret_cast<QueryProcess>(GetProcAddress(module, "NtQueryInformationProcess"));
        object_ = reinterpret_cast<QueryObject>(GetProcAddress(module, "NtQueryObject"));
        need(query_ && object_, "self snapshot API availability");
    }
    std::pair<LONG, std::string> objectText(HANDLE h, ULONG infoClass) const {
        std::vector<unsigned char> bytes(4096);
        ULONG required = 0;
        LONG status = 0;
        for (unsigned attempt = 0; attempt < 4; ++attempt) {
            status = object_(h, infoClass, bytes.data(), static_cast<ULONG>(bytes.size()), &required);
            if (status >= 0) return {status, utf8(*reinterpret_cast<const Unicode*>(bytes.data()), bytes)};
            if (required <= bytes.size() || required > 1024 * 1024) break;
            bytes.resize(static_cast<size_t>(required) + 4096);
        }
        return {status, {}};
    }
    Snapshot take() {
        Snapshot s;
        s.moduleReferences = ownedModuleReferences;
        s.modules = {{"bcrypt.dll", GetModuleHandleW(L"bcrypt.dll") != nullptr}, {"bcryptprimitives.dll", GetModuleHandleW(L"bcryptprimitives.dll") != nullptr}, {"cryptsp.dll", GetModuleHandleW(L"cryptsp.dll") != nullptr}};
        s.io = pendingIo.load(); s.live = workers.load(); s.owned = ownedHandles; s.joinable = joinableThreads;
        need(GetProcessHandleCount(GetCurrentProcess(), &s.countBefore) != FALSE, "handle count before");
        std::vector<unsigned char> bytes(65536);
        ULONG required = 0; LONG status = 0;
        for (unsigned attempt = 0; attempt < 4; ++attempt) {
            status = query_(GetCurrentProcess(), 51, bytes.data(), static_cast<ULONG>(bytes.size()), &required);
            if (status >= 0) break;
            if (required <= bytes.size() || required > 16 * 1024 * 1024) break;
            bytes.resize(static_cast<size_t>(required) + 4096);
        }
        need(status >= 0, "NtQueryInformationProcess(self,51)");
        const auto* table = reinterpret_cast<const Table*>(bytes.data());
        need(table->count <= (bytes.size() - offsetof(Table, entries)) / sizeof(Entry), "handle table bounds");
        std::map<std::string, Row> next;
        for (ULONG_PTR i = 0; i < table->count; ++i) {
            const auto& e = table->entries[i];
            const auto type = objectText(e.value, 2); need(type.first >= 0, "NtQueryObject(self,type)");
            Row row; row.handle = reinterpret_cast<std::uintptr_t>(e.value); row.access = e.access; row.typeIndex = e.type; row.type = type.second;
            if(row.type=="Thread")row.targetThreadId=GetThreadId(e.value);
            if(row.type=="Process")row.targetProcessId=GetProcessId(e.value);
            row.nameState="not-queried-self-type-only";
            if (row.type == "Event" || row.type == "Key" || row.type == "Thread" || row.type == "Process") {
                const auto name = objectText(e.value, 1);
                row.nameState="queried-quiescent-self";row.name=name.second;row.nameStatus=name.first;
            }
            next.emplace(row.key(), row); s.rows.push_back(std::move(row));
        }
        previous_ = std::move(next); initialized_ = true;
        need(GetProcessHandleCount(GetCurrentProcess(), &s.countAfter) != FALSE, "handle count after");
        need(s.countBefore == s.countAfter && s.rows.size() == s.countAfter, "snapshot observer stability");
        return s;
    }
    static Json difference(const Snapshot& before, const Snapshot& after) {
        Json added = Json::array(), removed = Json::array();
        for (const auto& a : after.rows) if (std::none_of(before.rows.begin(), before.rows.end(), [&](const Row& b) {return a.key() == b.key();})) added.push_back(a.json());
        for (const auto& b : before.rows) if (std::none_of(after.rows.begin(), after.rows.end(), [&](const Row& a) {return a.key() == b.key();})) removed.push_back(b.json());
        return {{"delta", static_cast<long long>(after.rows.size()) - static_cast<long long>(before.rows.size())}, {"added", added}, {"removed", removed}};
    }
    void record(const std::string& name, const Snapshot& before, const Snapshot& after, const Json& detail = Json::object()) {
        stages.push_back({{"id", name}, {"before", before.json()}, {"after", after.json()}, {"difference", difference(before, after)}, {"calls", detail}});
    }
    template<class F> void step(const std::string& name, F&& action) {
        const auto before = take(); const Json detail = action(); const auto after = take(); record(name, before, after, detail);
    }
};
static Probe probe(false);
static std::string activeStage;
static std::function<Json(const char*,const Snapshot&)> sink;
static Snapshot take(const char* label){activeStage=label;auto value=probe.take();need(bool(sink),"Self census durable sink");probe.stages.push_back(sink(label,value));return value;}
} // namespace ObserverCensus
