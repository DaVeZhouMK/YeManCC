#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <cstdint>
#include <filesystem>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>
#pragma comment(lib,"bcrypt.lib")

namespace ymcc::steamOriginalHash {
struct FileDigest {
    std::string sha256;
    std::uint64_t bytes=0;
};
// Every call still reads and hashes the complete file. No time/path/hash cache.
// Bounded blocks avoid iterator-per-byte reads and whole-image reallocations.
inline FileDigest hashFileFully(const std::wstring& file) {
    struct File {
        HANDLE value=INVALID_HANDLE_VALUE;
        ~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    } input;
    input.value=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(input.value==INVALID_HANDLE_VALUE)throw std::runtime_error("hash-file-open");
    LARGE_INTEGER originalSize{};
    if(GetFileType(input.value)!=FILE_TYPE_DISK||!GetFileSizeEx(input.value,&originalSize)||originalSize.QuadPart<0)
        throw std::runtime_error("hash-file-size");
    struct Algorithm {
        BCRYPT_ALG_HANDLE value=nullptr;
        ~Algorithm(){if(value)BCryptCloseAlgorithmProvider(value,0);}
    } algorithm;
    if(BCryptOpenAlgorithmProvider(&algorithm.value,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
        throw std::runtime_error("hash-provider");
    struct Hash {
        BCRYPT_HASH_HANDLE value=nullptr;
        ~Hash(){if(value)BCryptDestroyHash(value);}
    } hash;
    if(BCryptCreateHash(algorithm.value,&hash.value,nullptr,0,nullptr,0,0)<0)
        throw std::runtime_error("hash-create");
    constexpr std::size_t blockBytes=256*1024;
    std::vector<unsigned char> block(blockBytes);
    FileDigest result;
    const auto expected=static_cast<std::uint64_t>(originalSize.QuadPart);
    while(result.bytes<expected) {
        const auto count=static_cast<DWORD>((std::min)(static_cast<std::uint64_t>(block.size()),expected-result.bytes));
        DWORD received=0;
        if(!ReadFile(input.value,block.data(),count,&received,nullptr)||received!=count)
            throw std::runtime_error("hash-file-read");
        if(BCryptHashData(hash.value,block.data(),received,0)<0)
            throw std::runtime_error("hash-result");
        result.bytes+=received;
    }
    LARGE_INTEGER finalSize{};
    unsigned char extra=0;DWORD extraBytes=0;
    if(!GetFileSizeEx(input.value,&finalSize)||finalSize.QuadPart!=originalSize.QuadPart||
       !ReadFile(input.value,&extra,1,&extraBytes,nullptr)||extraBytes!=0)
        throw std::runtime_error("hash-file-changed");
    unsigned char bytes[32]{};
    if(BCryptFinishHash(hash.value,bytes,sizeof bytes,0)<0)throw std::runtime_error("hash-result");
    result.sha256.reserve(64);
    for(const auto byte:bytes){result.sha256+="0123456789abcdef"[byte>>4];result.sha256+="0123456789abcdef"[byte&15];}
    return result;
}
}
