#pragma once
#include "decky_sidebar_launch.h"
// CSS appearance work must retain YMCC's token for the same-user Steam queries.
// This is separate from Decky's intentionally unelevated loader policy.
namespace ymcc::steamcsslaunch {
inline deckylaunch::Result create(const std::wstring& executable, std::wstring& command, wchar_t* environment,
    const std::wstring& directory, STARTUPINFOW& startup, PROCESS_INFORMATION& child) {
    deckylaunch::Result result; result.mode = "ymcc-current-token";
    auto fail = [&](const char* stage, DWORD error) {
        result.stage = stage; result.error = error ? error : ERROR_ACCESS_DENIED; return result;
    };
    deckylaunch::Handle caller;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &caller.value))
        return fail("css-caller-token-open", GetLastError());
    TOKEN_ELEVATION expected{}; DWORD size = 0;
    if (!GetTokenInformation(caller.value, TokenElevation, &expected, sizeof(expected), &size))
        return fail("css-caller-token-elevation", GetLastError());
    const auto expectedIntegrity = deckylaunch::tokenData(caller.value, TokenIntegrityLevel);
    if (expectedIntegrity.empty()) return fail("css-caller-token-integrity", ERROR_ACCESS_DENIED);
    const DWORD flags = CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT;
    if (!ymcc::processEfficiency::createProcess(executable.c_str(), command.data(), nullptr, nullptr,
        FALSE, flags, environment, directory.c_str(), &startup, &child))
        return fail("css-create-process-current-token", GetLastError());
    deckylaunch::Handle token; TOKEN_ELEVATION actual{};
    bool verified = OpenProcessToken(child.hProcess, TOKEN_QUERY, &token.value) &&
        GetTokenInformation(token.value, TokenElevation, &actual, sizeof(actual), &size) &&
        actual.TokenIsElevated == expected.TokenIsElevated &&
        deckylaunch::equalUser(caller.value, token.value) && deckylaunch::sameSession(caller.value, token.value) &&
        deckylaunch::sameLogon(caller.value, token.value);
    const auto integrity = verified ? deckylaunch::tokenData(token.value, TokenIntegrityLevel) : std::vector<unsigned char>{};
    verified = verified && !integrity.empty() && EqualSid(
        reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(expectedIntegrity.data())->Label.Sid,
        reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(integrity.data())->Label.Sid);
    if (!verified) {
        // Only our still-suspended helper can be retired here; never an existing CSS/Steam process.
        const bool retired = TerminateProcess(child.hProcess, 1) && WaitForSingleObject(child.hProcess, 1000) == WAIT_OBJECT_0;
        const DWORD error = retired ? ERROR_ACCESS_DENIED : GetLastError();
        CloseHandle(child.hThread); CloseHandle(child.hProcess); child = {};
        return fail(retired ? "css-child-token-verification" : "css-child-retirement-unconfirmed", error);
    }
    ymcc::processEfficiency::applyOwnedChild(child.hProcess);
    result.created = true; result.stage = "css-child-current-token-verified"; return result;
}
}
