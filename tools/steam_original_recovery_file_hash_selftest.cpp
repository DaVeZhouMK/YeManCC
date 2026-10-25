#define NOMINMAX
#include "../native/steam_original_recovery_file_hash.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>

namespace fs = std::filesystem;
using ymcc::steamOriginalHash::FileDigest;
using ymcc::steamOriginalHash::hashFileFully;
constexpr std::size_t blockBytes = 256 * 1024;
void require(bool ok, const std::string& why) {
    if (!ok) throw std::runtime_error(why);
}
// Exact historical input algorithm: binary ifstream, iterator-built whole-file
// vector<char>, then a single BCryptHashData call. No reserve or bulk read.
FileDigest legacyHash(const std::wstring& path) {
    std::ifstream f(fs::path(path), std::ios::binary);
    require(f.good(), "legacy-open");
    std::vector<char> data((std::istreambuf_iterator<char>(f)), {});
    require(!f.bad(), "legacy-read");
    require(data.size() <= std::numeric_limits<ULONG>::max(), "legacy-size");
    struct Algorithm {
        BCRYPT_ALG_HANDLE value = nullptr;
        ~Algorithm() { if (value) BCryptCloseAlgorithmProvider(value, 0); }
    } algorithm;
    require(BCryptOpenAlgorithmProvider(&algorithm.value, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0, "legacy-provider");
    struct Hash {
        BCRYPT_HASH_HANDLE value = nullptr;
        ~Hash() { if (value) BCryptDestroyHash(value); }
    } hash;
    require(BCryptCreateHash(algorithm.value, &hash.value, nullptr, 0, nullptr, 0, 0) >= 0, "legacy-create");
    require(BCryptHashData(hash.value, reinterpret_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), 0) >= 0, "legacy-data");
    unsigned char bytes[32]{};
    require(BCryptFinishHash(hash.value, bytes, sizeof bytes, 0) >= 0, "legacy-finish");
    FileDigest result;
    result.bytes = data.size();
    for (auto b : bytes) {
        result.sha256 += "0123456789abcdef"[b >> 4];
        result.sha256 += "0123456789abcdef"[b & 15];
    }
    return result;
}
void writeFile(const fs::path& path, const std::vector<char>& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    require(f.good(), "fixture-open");
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    f.close();
    require(!f.fail(), "fixture-write");
}
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
int checks = 0;
int failures = 0;
void pass(const std::string& name) { ++checks; std::cout << "PASS " << name << '\n'; }
void rejects(const fs::path& path, const char* reason, const std::string& name) {
    try {
        const auto accepted = hashFileFully(path.wstring());
        std::cout << "UNEXPECTED_ACCEPT name=" << name << " bytes=" << accepted.bytes << " digest=" << accepted.sha256 << '\n';
    }
    catch (const std::runtime_error& e) {
        require(std::string(e.what()) == reason, name + ": unexpected rejection: " + e.what());
        pass(name + " reason=" + e.what());
        return;
    }
    ++failures;
    std::cout << "FAIL " << name << ": accepted unreadable file\n";
}
void functionalTests(const fs::path& root) {
    fs::create_directories(root);
    const auto path = root / L"fixture.bin";
    writeFile(path, {});
    auto digest = hashFileFully(path.wstring());
    require(digest.bytes == 0 && digest.sha256 == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "empty-known-sha-and-count");
    pass("empty-known-sha-and-count");
    writeFile(path, {'a','b','c'});
    digest = hashFileFully(path.wstring());
    require(digest.bytes == 3 && digest.sha256 == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "abc-known-sha-and-count");
    pass("abc-known-sha-and-count");
    for (std::size_t size : {blockBytes-1, blockBytes, blockBytes+1, 2*blockBytes, 2*blockBytes+37}) {
        std::vector<char> data(size);
        for (std::size_t i=0; i<size; ++i) data[i]=static_cast<char>((i*131+(i>>8)*17) & 255);
        writeFile(path, data);
        const auto actual=hashFileFully(path.wstring());
        const auto expected=legacyHash(path.wstring());
        require(actual.bytes==size && expected.bytes==size && actual.sha256==expected.sha256, "boundary-digest-count");
        pass("boundary-digest-and-count bytes=" + std::to_string(size));
        // Same path, length, and last-write timestamp: must still read all bytes.
        const auto stamp=fs::last_write_time(path);
        data.back() ^= 0x5a;
        writeFile(path, data);
        fs::last_write_time(path, stamp);
        const auto changed=hashFileFully(path.wstring());
        require(changed.bytes==size && changed.sha256!=actual.sha256 && changed.sha256==legacyHash(path.wstring()).sha256, "same-length-change");
        pass("same-length-change bytes=" + std::to_string(size));
        fs::resize_file(path, size-1);
        const auto shorter=hashFileFully(path.wstring());
        require(shorter.bytes==size-1 && shorter.sha256!=changed.sha256 && shorter.sha256==legacyHash(path.wstring()).sha256, "truncate-change");
        pass("truncate-change bytes=" + std::to_string(size-1));
    }
    rejects(root/L"missing.bin", "hash-file-open", "missing-file-rejected");
    rejects(root, "hash-file-open", "directory-rejected");
    {
        Handle exclusive{CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        require(exclusive.value!=INVALID_HANDLE_VALUE, "exclusive-lock-setup");
        rejects(path, "hash-file-open", "exclusive-file-lock-rejected");
    }
    {
        // Read-only lock ownership remains compatible with production FILE_SHARE_READ.
        // Exclusive LockFileEx still causes a real subsequent ReadFile failure.
        Handle shared{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        require(shared.value!=INVALID_HANDLE_VALUE, "range-lock-open");
        OVERLAPPED range{};
        range.Offset=static_cast<DWORD>(blockBytes);
        require(LockFileEx(shared.value, LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &range)!=0, "range-lock-setup");
        // Prove opening and the first block succeed before production reads
        // the second block, whose first byte is exclusively locked.
        std::ifstream probe(path, std::ios::binary);
        require(probe.good(), "range-lock-probe-open");
        std::vector<char> first(blockBytes);
        probe.read(first.data(), static_cast<std::streamsize>(first.size()));
        require(probe.good() && probe.gcount()==static_cast<std::streamsize>(blockBytes), "range-lock-first-block");
                Handle reader{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        require(reader.value!=INVALID_HANDLE_VALUE, "range-lock-native-reader");
        LARGE_INTEGER offset{}; offset.QuadPart=blockBytes;
        require(SetFilePointerEx(reader.value, offset, nullptr, FILE_BEGIN)!=0, "range-lock-native-seek");
        char byte{}; DWORD got=0;
        const BOOL readOk=ReadFile(reader.value,&byte,1,&got,nullptr);
        const DWORD readError=GetLastError();
        require(!readOk && readError==ERROR_LOCK_VIOLATION, "range-lock-must-cause-native-read-failure");
        std::cout << "INJECTION native_ReadFile_error=" << readError << " (ERROR_LOCK_VIOLATION) offset=" << blockBytes << '\n';
        probe.read(first.data(), static_cast<std::streamsize>(first.size()));
        std::cout << "INJECTION ifstream_second_read gcount=" << probe.gcount()
                  << " bad=" << probe.bad() << " fail=" << probe.fail() << " eof=" << probe.eof() << '\n';
        rejects(path, "hash-file-read", "mid-read-failure-rejected-after-first-block");
    }
    digest=hashFileFully(path.wstring());
    require(digest.bytes==fs::file_size(path) && digest.sha256==legacyHash(path.wstring()).sha256, "unlock-readable");
    pass("unlocked-file-readable");
    std::cout << "FUNCTIONAL_RESULT passed=" << checks << " failed=" << failures << '\n';
}
struct Timed { FileDigest digest; double ms; };
Timed measure(const fs::path& file, bool old) {
    const auto start=std::chrono::steady_clock::now();
    auto result=old ? legacyHash(file.wstring()) : hashFileFully(file.wstring());
    const auto end=std::chrono::steady_clock::now();
    return {std::move(result), std::chrono::duration<double,std::milli>(end-start).count()};
}
double median(std::array<double,3> values) {
    std::sort(values.begin(),values.end()); return values[1];
}
void benchmark(const fs::path& file) {
    const auto size=fs::file_size(file);
    const auto stamp=fs::last_write_time(file);
    std::array<double,3> oldMs{},newMs{};
    std::string reference;
    for (int run=0;run<3;++run) {
        const bool oldFirst=(run%2)==0;
        auto first=measure(file,oldFirst);
        auto second=measure(file,!oldFirst);
        auto& old=oldFirst ? first : second;
        auto& fresh=oldFirst ? second : first;
        require(old.digest.sha256==fresh.digest.sha256 && old.digest.bytes==size && fresh.digest.bytes==size, "benchmark-digest-or-count-mismatch");
        if (reference.empty()) reference=old.digest.sha256;
        require(reference==old.digest.sha256 && fs::file_size(file)==size && fs::last_write_time(file)==stamp, "benchmark-file-changed");
        oldMs[run]=old.ms; newMs[run]=fresh.ms;
        std::cout << "RUN file=" << file.filename().string() << " trial=" << run+1
                  << " order=" << (oldFirst?"old,new":"new,old") << " bytes=" << size
                  << " old_ms=" << old.ms << " new_ms=" << fresh.ms << " digest=" << reference << " MATCH\n";
    }
    std::cout << "MEDIAN file=" << file.filename().string() << " bytes=" << size
              << " old_ms=" << median(oldMs) << " new_ms=" << median(newMs)
              << " local_ratio=" << median(oldMs)/median(newMs) << '\n';
}
int wmain(int argc,wchar_t** argv) {
    std::cout << std::fixed << std::setprecision(3) << std::unitbuf;
    try {
        require(argc==2,"usage: selftest.exe <receipt-directory>");
        const auto fixtures=fs::path(argv[1])/(L"fixtures-"+std::to_wstring(GetCurrentProcessId()));
        std::cout << "Production header: ../native/steam_original_recovery_file_hash.h\n"
                     "Build: MSVC Release /O2 /MT; steady_clock times include open/read/hash/hex/cleanup.\n"
                     "Local sequential file benchmark; OS cache uncontrolled; no prewarm; not a G3E measurement.\n";
        functionalTests(fixtures);
        benchmark(L"D:\\Game\\Steam\\steamclient64.dll");
        benchmark(L"D:\\Game\\Steam\\SteamUI.dll");
        std::cout << (failures ? "OVERALL_FAIL" : "ALL_PASS") << " functional_passed=" << checks << " functional_failed=" << failures << " benchmark_pairs=6 digest_matches=6\n";
        return failures ? 1 : 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n'; return 1;
    }
}


