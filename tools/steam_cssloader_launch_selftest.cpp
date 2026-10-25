#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "steam_cssloader_launch.h"
#include <fstream>
#include <iostream>
using namespace ymcc;
static unsigned checks = 0;
static void check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); ++checks; std::cout << "PASS " << text << '\n'; }
struct Owned {
 PROCESS_INFORMATION process{}; HANDLE job = nullptr;
 ~Owned() { if (job) CloseHandle(job); if (process.hProcess) { if (WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) { TerminateProcess(process.hProcess, 1); WaitForSingleObject(process.hProcess, 2000); } CloseHandle(process.hProcess); } if (process.hThread) CloseHandle(process.hThread); }
};
static bool elevation(HANDLE process) { deckylaunch::Handle token; check(OpenProcessToken(process, TOKEN_QUERY, &token.value) != FALSE, "owned token readable"); TOKEN_ELEVATION level{}; DWORD size = 0; check(GetTokenInformation(token.value, TokenElevation, &level, sizeof(level), &size) != FALSE, "owned token elevation readable"); return level.TokenIsElevated != 0; }
int wmain(int argc, wchar_t** argv) { try {
 if (argc == 3 && std::wstring(argv[1]) == L"--child") { std::ofstream(std::filesystem::path(argv[2])) << "started"; Sleep(INFINITE); return 0; }
 if (argc != 2) throw std::runtime_error("explicit evidence directory required");
 const auto root = std::filesystem::absolute(argv[1]); std::filesystem::create_directories(root);
 wchar_t module[32768]{}; GetModuleFileNameW(nullptr, module, 32768); const std::wstring executable(module);
 const auto marker = root / (L"started-" + std::to_wstring(GetCurrentProcessId()) + L".txt");
 const bool expectedElevated = elevation(GetCurrentProcess());
 Owned child; STARTUPINFOW startup{sizeof(startup)}; startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
 auto command = L"\"" + executable + L"\" --child \"" + marker.wstring() + L"\"";
 const auto result = steamcsslaunch::create(executable, command, nullptr, root.wstring(), startup, child.process);
 check(result.mode == "ymcc-current-token", "CSS helper uses caller-token route");
 check(result.created && result.stage == "css-child-current-token-verified", "CSS helper launch verifies caller and child identity");
 check(elevation(child.process.hProcess) == expectedElevated, "CSS helper preserves the caller elevation without shell-token downgrade");
 Sleep(50); check(!std::filesystem::exists(marker), "CSS helper remains suspended before job ownership");
 child.job = CreateJobObjectW(nullptr, nullptr); JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
 check(child.job && SetInformationJobObject(child.job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)), "owned bounded helper job configured");
 check(AssignProcessToJobObject(child.job, child.process.hProcess) != FALSE, "CSS helper admitted to bounded job before resuming");
 check(ResumeThread(child.process.hThread) != DWORD(-1), "owned helper resumes after ownership");
 const auto end = GetTickCount64() + 3000; while (!std::filesystem::exists(marker) && GetTickCount64() < end) Sleep(10);
 check(std::filesystem::exists(marker), "only the isolated helper runs its marker payload");
 CloseHandle(child.job); child.job = nullptr;
 check(WaitForSingleObject(child.process.hProcess, 2000) == WAIT_OBJECT_0, "helper job closure destroys the owned child in bounds");
 PROCESS_INFORMATION failed{}; auto missing = root / L"does-not-exist.exe"; auto missingCommand = L"\"" + missing.wstring() + L"\"";
 const auto failure = steamcsslaunch::create(missing.wstring(), missingCommand, nullptr, root.wstring(), startup, failed);
 check(!failure.created && failure.stage == "css-create-process-current-token" && failure.error != 0 && !failed.hProcess && !failed.hThread, "failed CSS creation reports exact error without another launch path");
 std::ofstream(root / L"result.json") << "{\"ok\":true,\"passed\":" << checks << ",\"callerElevated\":" << (expectedElevated ? "true" : "false") << ",\"callerTokenPreserved\":true,\"ownedHelperDestroyed\":true,\"realSteamOperations\":0,\"realCssOperations\":0}";
 return 0;
 } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
