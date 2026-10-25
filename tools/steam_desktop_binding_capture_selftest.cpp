#include "../native/steam_desktop_binding_capture.h"
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using json = nlohmann::json;
namespace capture = ymcc::steamDesktopCapture;
namespace fixture {
using DWORD = std::uint32_t;
struct Client { std::wstring root = L"S:\\PRIVATE_Steam"; DWORD account = 42, pid = 99; std::uint64_t created = 123; };
struct ReadValue { std::string bytes; bool available = true, truncated = false; std::uint64_t originalBytes = 0; std::string reason = "PRIVATE_READ_REASON"; };
struct ReadCall { std::wstring path; DWORD cap; };
static unsigned checks = 0;
static json cases = json::array();
static void check(bool ok, const char* label) { ++checks; if (!ok) throw std::runtime_error(label); }
static std::wstring key(std::wstring path) { for (auto& c : path) { if (c == L'/') c = L'\\'; if (c >= L'A' && c <= L'Z') c += L'a' - L'A'; } return path; }
// Deliberate digest stand-in for the Ops boundary, not a cryptographic test.
// Colliding mode verifies stability compares the exact strings, never hashes.
static std::string fingerprint(const std::string& bytes) {
    std::uint64_t value = 1469598103934665603ull;
    for (const auto c : bytes) { value ^= static_cast<unsigned char>(c); value *= 1099511628211ull; }
    std::ostringstream out; for (unsigned i = 0; i < 4; ++i) out << std::hex << std::setfill('0') << std::setw(16) << value;
    return out.str();
}
struct FakeOps {
    Client current{}; bool valid = true, forceDifferentSession = false, collideHashes = false, badHash = false;
    unsigned calls = 0, sameCalls = 0, changeAfterRead = 0, throwSameAt = 0;
    std::uint64_t tick = 100, nowStep = 0, readDelay = 0;
    std::string throwing;
    std::map<std::wstring, std::vector<ReadValue>> files;
    std::map<std::wstring, std::size_t> versions;
    std::vector<ReadCall> reads;
    std::vector<std::string> hashed;
    void trip(const char* point) { if (throwing == point) throw std::runtime_error("PRIVATE_EXCEPTION account=89250578 path=S:/PRIVATE URL=https://PRIVATE"); }
    Client session() { ++calls; trip("session"); return current; }
    bool validSession(const Client&) { ++calls; trip("valid"); return valid; }
    bool sameSession(const Client& c) {
        ++calls; ++sameCalls; trip("same");
        if (throwSameAt && sameCalls == throwSameAt) throw std::runtime_error("PRIVATE_FINAL_SESSION");
        return !forceDifferentSession && c.root == current.root && c.account == current.account && c.pid == current.pid && c.created == current.created;
    }
    json clientIdentity(const Client&) { ++calls; trip("identity"); return {{"sessionToken", "sanitized-fixture"}}; }
    ReadValue read(const std::wstring& path, DWORD cap) {
        ++calls; reads.push_back({path, cap}); tick += readDelay; trip("read");
        ReadValue result; const auto found = files.find(key(path));
        if (found == files.end()) { result.available = false; result.originalBytes = 0; }
        else {
            auto& index = versions[key(path)]; const auto slot = (std::min)(index++, found->second.size() - 1);
            result = found->second[slot];
            if (result.bytes.size() > cap) { result.bytes.resize(cap); result.truncated = true; }
        }
        if (changeAfterRead && reads.size() == changeAfterRead) { ++current.account; ++current.created; }
        return result;
    }
    std::wstring widen(const std::string& value) { ++calls; trip("widen"); return std::wstring(value.begin(), value.end()); }
    std::string hash(const std::string& bytes) { ++calls; trip("hash"); hashed.push_back(bytes); return badHash ? "PRIVATE_BAD_HASH" : collideHashes ? std::string(64, 'a') : fingerprint(bytes); }
    std::uint64_t now() { ++calls; trip("now"); tick += nowStep; return tick; }
    void put(const std::wstring& path, const std::string& bytes) { files[key(path)] = {ReadValue{bytes, true, false, bytes.size(), "PRIVATE_REASON"}}; }
    void sequence(const std::wstring& path, const std::vector<std::string>& bytes) {
        auto& target = files[key(path)]; target.clear(); for (const auto& text : bytes) target.push_back({text, true, false, text.size(), "PRIVATE_REASON"});
    }
};
static std::wstring root(const std::wstring& value) { std::wstring normalized; if (!capture::detail::normalizeRoot(value, normalized)) throw std::runtime_error("bad fixture root"); return normalized; }
static std::wstring config(const std::wstring& library, DWORD account = 42) { return root(library) + L"steamapps\\common\\Steam Controller Configs\\" + std::to_wstring(account) + L"\\config\\"; }
static std::wstring selection(const std::wstring& library, DWORD account = 42) { return config(library, account) + L"configset_controller_neptune.vdf"; }
static std::wstring body(const std::wstring& library, DWORD account = 42) { return config(library, account) + L"413080\\controller_neptune.vdf"; }
static std::wstring folders(const FakeOps& ops) { return root(ops.current.root) + L"steamapps\\libraryfolders.vdf"; }
static const std::string autosave = "controller_config { 413080 { autosave 1 } }";
static std::string layout(bool partial = false) {
    return "controller_mappings { version 3 controller_type controller_neptune title PRIVATE_TITLE creator 89250578 url https://PRIVATE_URL "
        "group { id 7 mode switches inputs { left_bumper { activators { Full_Press { bindings { binding \"key_press LEFT_CONTROL\" " +
        std::string(partial ? "binding \"macro PRIVATE_MACRO\" " : "") + "} } } } right_bumper { activators { Full_Press { bindings { binding \"key_press LEFT_ALT\" } } } } } } "
        "preset { id 0 name PRIVATE_PRESET group_source_bindings { 7 \"switch active\" } } }";
}
static void add(FakeOps& ops, const std::wstring& library, const std::string& data = layout(), DWORD account = 42) {
    ops.put(selection(library, account), autosave); ops.put(body(library, account), data);
}
static std::string declared(const std::vector<std::string>& paths) {
    std::string result = "libraryfolders { "; unsigned i = 0;
    for (const auto& path : paths) { std::string escaped; for (const char c : path) { if (c == '\\' || c == '"') escaped += '\\'; escaped += c; } result += std::to_string(i++) + " { path \"" + escaped + "\" } "; }
    return result + "}";
}
static void ready(FakeOps& ops, bool partial = false) { ops.put(folders(ops), declared({})); add(ops, ops.current.root, layout(partial), ops.current.account); }
static void invariants(const json& out, const FakeOps& ops) {
    for (const char* field : {"active","activePresetKnown","producer","sourceAttributionKnown","deliveryProven","absenceProven","exclusiveLayoutProven"}) check(out[field] == false, "collector asserts live/source/delivery/absence authority");
    check(out["storedDesktopCandidates"].size() <= 4 && out["libraryCount"].get<unsigned>() <= 16 && ops.reads.size() <= 66, "collector count budgets exceeded");
    check(out["readBytes"].get<std::uint64_t>() <= capture::totalByteCap, "collector total returned-byte budget exceeded");
    for (const auto& call : ops.reads) check(call.cap <= capture::perFileCap, "per-file budget exceeded");
    for (const auto& c : out["storedDesktopCandidates"]) check(c["active"] == false && c["activePresetKnown"] == false && c["producer"] == false && c["absenceProven"] == false, "candidate authority asserted");
    const auto text = out.dump();
    for (const char* canary : {"PRIVATE", "89250578", "https://", "key_press", "controller_mappings", "Steam Controller Configs"}) check(text.find(canary) == std::string::npos, "private raw data exported");
}
static void disabledAndInvalidSession() {
    FakeOps disabled; const auto d = capture::collect(false, disabled); check(disabled.calls == 0 && d["status"] == "disabled", "disabled capture calls Ops");
    for (const bool zeroAccount : {false,true}) {
        FakeOps ops; ready(ops); ops.forceDifferentSession = true; if (zeroAccount) ops.current.account = 0; else ops.valid = false;
        const auto out = capture::collect(true, ops); check(out["status"] == "invalid-session" && ops.reads.empty(), "invalid session/current account allowed IO");
        check(out["sessionState"] == "unverified" && out["staleSession"] == false && out["sameSessionAtEnd"].is_null() && ops.sameCalls == 0, "invalid initial session was claimed changed/stale"); invariants(out, ops);
    }
    FakeOps unverified; unverified.forceDifferentSession = true;
    const auto initial = capture::collect(true, unverified);
    check(initial["status"] == "initial-session-unverified" && initial["sessionState"] == "unverified" && initial["staleSession"] == false && initial["sameSessionAtEnd"].is_null() && unverified.reads.empty(), "unverified initial sameSession falsely claims later change"); invariants(initial, unverified);
    FakeOps invalid; invalid.current.root = L"relative\\PRIVATE"; const auto out = capture::collect(true, invalid);
    check(out["status"] == "invalid-session-root" && invalid.reads.empty(), "relative Steam root permitted"); invariants(out, invalid);
}
static void storedSummaryAndHashes() {
    FakeOps ops; ready(ops); const auto data = layout(); const auto out = capture::collect(true, ops); const auto& c = out["storedDesktopCandidates"][0];
    check(out["status"] == "captured" && out["sameSessionAtEnd"] == true && !out["staleSession"].get<bool>() && ops.reads.size() == 5, "stable existing-session capture status/read count");
    check(c["summary"] == ymcc::steamdeck::bindingevidence::summarize(data) && c["summary"]["matches"].size() == 2, "collector replaced/filtered Goodall summary");
    check(c["selectionBytesStable"] == true && c["bodyBytesStable"] == true && c["exactBytesStable"] == true, "stable pair not confirmed");
    check(c["bodyFirst"]["sha256"] == fingerprint(data) && c["bodySecond"]["sha256"] == fingerprint(data) && c["selectionFirst"]["sha256"] == fingerprint(autosave), "hashes are not over exact Ops read bytes");
    check(c["bodyFirst"]["readBytes"] == data.size() && c["libraryOrdinal"] == 0 && out["client"] == json{{"sessionToken","sanitized-fixture"}}, "sanitized identity/byte counts/ordinal");
    check(ops.reads[0].cap == capture::libraryFoldersCap && ops.reads[1].cap == capture::selectionCap && ops.reads[2].cap == capture::perFileCap && ops.reads[3].cap == capture::selectionCap && ops.reads[4].cap == capture::perFileCap, "libraryfolders/selection/body caps changed");
    check(out["captureBeginTick"] == 100 && out["captureEndTick"] == 100 && out["budgets"]["singleIoHardBound"] == false, "ticks or IO limit semantics wrong"); invariants(out, ops);
}
static void partialSummaryPreserved() {
    FakeOps ops; ready(ops, true); const auto out = capture::collect(true, ops); const auto& summary = out["storedDesktopCandidates"][0]["summary"];
    check(summary == ymcc::steamdeck::bindingevidence::summarize(layout(true)) && summary["ok"] == true && summary["status"] == "partial" && summary["matches"].size() == 2, "useful partial matches were discarded");
    check(out["status"] == "partial" && out["declaredLibraryCoverageComplete"] == true && out["overallCoverageComplete"] == false, "partial parser summary confused declared libraries with overall coverage"); invariants(out, ops);
}
static void mutationAndHashCollisions() {
    for (const bool changeSelection : {false,true}) {
        FakeOps ops; ready(ops); ops.collideHashes = true;
        if (changeSelection) ops.sequence(selection(ops.current.root), {autosave,"controller_config { 413080 { autosave 0 } }"});
        else ops.sequence(body(ops.current.root), {layout(),layout()+"\n"});
        const auto out = capture::collect(true, ops); const auto& c = out["storedDesktopCandidates"][0];
        check(c["exactBytesStable"] == false && c[changeSelection ? "selectionBytesStable" : "bodyBytesStable"] == false, "stability used colliding hashes instead of exact strings");
        check(c["bodyFirst"]["sha256"] == c["bodySecond"]["sha256"] && c["selectionFirst"]["sha256"] == c["selectionSecond"]["sha256"], "collision scenario not exercised");
        check(ops.reads.size() == 5 && out["status"] == "partial" && c["summary"]["matches"].size() == 2, "mutation discarded initial stored evidence or skipped paired read"); invariants(out, ops);
    }
}
static void currentAccountOnlyAndStaleSession() {
    FakeOps wrong; wrong.put(folders(wrong), declared({})); add(wrong, wrong.current.root, layout(), 43);
    const auto missing = capture::collect(true, wrong); check(missing["storedDesktopCandidates"].empty() && wrong.reads.size() == 2, "fallback last account was read");
    for (const auto& read : wrong.reads) check(read.path.find(L"\\43\\") == std::wstring::npos, "other account path used"); invariants(missing, wrong);
    for (unsigned after = 1; after <= 5; ++after) {
        FakeOps ops; ready(ops); ops.changeAfterRead = after;
        const auto out = capture::collect(true, ops); check(out["status"] == "stale-session" && out["staleSession"] == true && out["sameSessionAtEnd"] == false, "account/process/session switch was not marked stale");
        check(ops.reads.size() == after, "new IO issued after known session switch"); invariants(out, ops);
    }
}
static void incompleteReadsRejected() {
    for (const bool bodyFile : {false,true}) for (const bool truncated : {false,true}) {
        FakeOps ops; ready(ops); const auto path = bodyFile ? body(ops.current.root) : selection(ops.current.root);
        auto& value = ops.files[key(path)][0]; value.truncated = truncated; if (!truncated) value.originalBytes += 1;
        const auto out = capture::collect(true, ops);
        if (bodyFile) check(out["storedDesktopCandidates"].size() == 1 && out["storedDesktopCandidates"][0]["summary"].is_null() && out["storedDesktopCandidates"][0]["bodyBytesStable"].is_null(), "incomplete body was parsed or treated stable");
        else check(out["storedDesktopCandidates"].empty() && ops.reads.size() == 2, "incomplete selection authorized body resolution");
        check(out["declaredLibraryCoverageComplete"] == true && out["overallCoverageComplete"] == false, "incomplete body/selection confused declared and overall coverage"); invariants(out, ops);
    }
}
static void declaredRootsDedupAndRejection() {
    FakeOps ops; ready(ops);
    ops.put(folders(ops), declared({"s:/PRIVATE_Steam/./","S:\\PRIVATE_Steam\\", "E:/One/../Library", "e:\\Library\\", "relative/PRIVATE", "F:PRIVATE", "F:/../PRIVATE", "\\\\server\\share\\Library"}));
    add(ops,L"E:\\Library"); add(ops,L"\\\\server\\share\\Library");
    const auto out = capture::collect(true, ops);
    check(out["libraryCount"] == 3 && out["duplicateLibraryRoots"] == 3 && out["rejectedLibraryRoots"] == 3, "absolute lexical normalization/dedup/rejection");
    check(out["storedDesktopCandidates"].size() == 3 && out["ambiguousStoredCandidates"] == true && out["observedDesktopCandidateCount"] == 3, "multiple stored candidates not preserved as ambiguous");
    for (const auto& read : ops.reads) check(read.path.find(L"..") == std::wstring::npos && read.path.find(L'/') == std::wstring::npos, "unnormalized path used for IO"); invariants(out, ops);
}
static void librariesAndBodyLimits() {
    FakeOps ops; ready(ops); std::vector<std::string> paths;
    for (unsigned i = 0; i < 20; ++i) { const auto path = "E:/Library"+std::to_string(i); paths.push_back(path); if (i < 15) add(ops,std::wstring(path.begin(),path.end())); }
    paths.push_back("e:/Library19/."); ops.put(folders(ops),declared(paths));
    const auto out = capture::collect(true,ops);
    check(out["libraryCount"] == 16 && out["omittedLibraryRoots"] == 5 && out["duplicateLibraryRoots"] == 1, "16-library bound or dedup of omitted declarations");
    check(out["storedDesktopCandidates"].size() == 4 && out["observedDesktopCandidateCount"] == 16 && out["omittedBodyCandidates"] == 12 && ops.reads.size() == 29, "four-body bound or omitted candidate counts");
    check(out["declaredLibraryCoverageComplete"] == false, "bounded library/body omissions claimed complete coverage"); invariants(out,ops);
}
static void foldersUnavailableAndSelectionsAmbiguous() {
    FakeOps unavailable; add(unavailable,unavailable.current.root);
    const auto rootOnly = capture::collect(true,unavailable); check(rootOnly["storedDesktopCandidates"].size()==1 && rootOnly["declaredLibraryCoverageComplete"]==false && rootOnly["declaredLibrariesStatus"]=="unavailable-or-incomplete", "missing libraryfolders blocked root or claimed completeness"); invariants(rootOnly,unavailable);
    for (const std::string selectionText : {std::string("controller_config { 413080 { autosave 1 template PRIVATE_TEMPLATE } }"),std::string("controller_config { 413080 { autosave 1 } 413080 { autosave 1 } }"),std::string("controller_config { 413080 { autosave 1 workshop PRIVATE_URL } }")}) {
        FakeOps ops; ready(ops); ops.put(selection(ops.current.root),selectionText); const auto out=capture::collect(true,ops);
        check(out["storedDesktopCandidates"].empty() && ops.reads.size()==2,"ambiguous/template/workshop selection resolved guessed body"); invariants(out,ops);
    }
}
static void byteAndCheckpointBudgets() {
    FakeOps slow; ready(slow); slow.readDelay=1200;
    const auto timed=capture::collect(true,slow); check(timed["status"]=="checkpoint-time-budget" && slow.reads.size()==1 && timed["captureEndTick"]==1300 && timed["budgets"]["singleIoHardBound"]==false,"checkpoint deadline claims hard IO limit or schedules later reads"); invariants(timed,slow);
    FakeOps byte; ready(byte); const std::string other="E:/Other"; byte.put(folders(byte),declared({other}));
    auto large=layout(); large.resize(capture::perFileCap,' '); add(byte,byte.current.root,large); add(byte,L"E:\\Other",large);
    const auto limited=capture::collect(true,byte); check(limited["status"]=="total-byte-budget" && limited["readBytes"]==capture::totalByteCap,"eight-MiB total byte budget not enforced"); invariants(limited,byte);
    FakeOps calls; calls.put(L"S:\\probe","x"); const auto client=calls.current; capture::detail::Reader<FakeOps,Client> reader{calls,client,100,100};
    for (unsigned i=0;i<66;++i) (void)reader.read(L"S:\\probe",1);
    const auto blocked=reader.read(L"S:\\probe",1); check(calls.reads.size()==66 && blocked.evidence["status"]=="read-call-budget" && reader.reads==66,"66-call guard allowed another Ops read");
}
static void throwingOpsAndSanitizedErrors() {
    for (const char* point : {"session","valid","same","identity","read","widen","hash","now"}) {
        FakeOps ops; ready(ops); ops.put(folders(ops),declared({"E:/PRIVATE_Extra"})); ops.throwing=point;
        const auto out=capture::collect(true,ops); check(out["status"]!="captured", "throwing Ops produced complete capture");
        if (std::string(point)=="identity" || std::string(point)=="session" || std::string(point)=="valid" || std::string(point)=="same") check(out["declaredLibraryCoverageComplete"]==false,"declaration coverage claimed before declaration read/parse");
        invariants(out,ops);
    }
    FakeOps good; ready(good); const auto initial=capture::collect(true,good); (void)initial;
    FakeOps final; ready(final); final.throwSameAt=good.sameCalls;
    const auto out=capture::collect(true,final); check(out["status"]=="session-check-error" && out["sameSessionAtEnd"].is_null(),"final sameSession error promoted stale evidence to current"); invariants(out,final);
    FakeOps badHash; ready(badHash); badHash.badHash=true; const auto invalid=capture::collect(true,badHash);
    check(invalid["storedDesktopCandidates"][0]["bodyFirst"]["sha256"].is_null() && invalid["captureErrors"].get<unsigned>()>0,"non-digest Ops hash exported arbitrary text"); invariants(invalid,badHash);
}
static void run(const char* name,void(*test)()) {
    const auto before=checks;
    try{test();cases.push_back({{"case",name},{"pass",true},{"checks",checks-before}});}
    catch(const std::exception& error){cases.push_back({{"case",name},{"pass",false},{"checks",checks-before},{"error",error.what()}});}
}
}
int main(){using namespace fixture;
    run("disabled-zero-ops-and-invalid-session",disabledAndInvalidSession);
    run("stored-summary-hashes-and-exact-stability",storedSummaryAndHashes);
    run("partial-Goodall-summary-preserved",partialSummaryPreserved);
    run("selection-body-mutation-and-hash-collisions",mutationAndHashCollisions);
    run("current-account-only-and-session-switch",currentAccountOnlyAndStaleSession);
    run("short-and-truncated-reads-rejected",incompleteReadsRejected);
    run("declared-absolute-roots-normalized-deduped",declaredRootsDedupAndRejection);
    run("sixteen-libraries-four-bodies-omissions",librariesAndBodyLimits);
    run("root-fallback-and-ambiguous-selection",foldersUnavailableAndSelectionsAmbiguous);
    run("byte-read-and-checkpoint-budgets",byteAndCheckpointBudgets);
    run("throwing-ops-and-private-error-redaction",throwingOpsAndSanitizedErrors);
    bool pass=true;for(const auto& row:cases)if(!row["pass"].get<bool>())pass=false;
    std::cout<<json({{"pass",pass},{"checks",checks},{"cases",cases},{"allOpsInMemory",true},{"actualOSInput",false},{"actualSteamOrFileIo",false},{"productExecuted",false},{"hashFixture","opaque deterministic stand-in; production Ops owns SHA-256"}}).dump(2)<<'\n';return pass?0:1;}
