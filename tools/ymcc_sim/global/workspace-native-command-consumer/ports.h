#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <mutex>
#include <condition_variable>
#include <stdexcept>
#include <vector>
#include <map>
#include "json.hpp"
namespace wsfixture {
using J=nlohmann::json;namespace fs=std::filesystem;
inline fs::path root;
inline std::atomic<bool> steamRunning{false},staleNextCommit{false};
inline std::atomic<int> denied{0},workerCalls{0},searchEntered{0};
inline std::mutex mutex;inline std::condition_variable changed;inline bool releaseSearch=false;inline J events=J::array(),calls=J::array();
inline void fail(const char* reason){++denied;throw std::runtime_error(std::string("WORKSPACE_FORBIDDEN_TERMINAL:")+reason);}
inline bool owned(const fs::path& p){const auto r=fs::absolute(p).lexically_normal().lexically_relative(fs::absolute(root).lexically_normal());return !r.empty()&&!r.is_absolute()&&*r.begin()!=L"..";}
inline void path(const fs::path& p){if(root.empty()||!owned(p))fail("OUTSIDE_OWNED_ROOT");}
inline void emit(const J& j){std::lock_guard<std::mutex> l(mutex);events.push_back(j);changed.notify_all();}
inline DWORD WINAPI environment(LPCWSTR name,LPWSTR,DWORD){if(wcscmp(name,L"YMCC_LOCAL_IDENTITY_CORPUS")==0)return 0;fail("ENVIRONMENT");return 0;}
template<class... A> HANDLE snapshot(A...){fail("PROCESS_ENUMERATION");return INVALID_HANDLE_VALUE;}
template<class... A> HANDLE process(A...){fail("OPEN_PROCESS");return nullptr;}
template<class... A> BOOL spawn(A...){fail("CREATE_PROCESS");return FALSE;}
template<class... A> HINTERNET network(A...){fail("NETWORK");return nullptr;}
template<class... A> LSTATUS registry(A...){fail("REGISTRY");return ERROR_ACCESS_DENIED;}
template<class... A> HINSTANCE shell(A...){fail("SHELL");return nullptr;}
template<class... A> BOOL shellEx(A...){fail("SHELL_EX");return FALSE;}
}
extern "C" const char* wsFixtureWorker(const char* request);
extern "C" const char* wsFixtureVdf(const char* request);
#define CreateToolhelp32Snapshot wsfixture::snapshot
#define OpenProcess wsfixture::process
#define CreateProcessW wsfixture::spawn
#define ShellExecuteW wsfixture::shell
#define ShellExecuteExW wsfixture::shellEx
#define WinHttpOpen wsfixture::network
#define RegGetValueW wsfixture::registry
#define GetEnvironmentVariableW wsfixture::environment
