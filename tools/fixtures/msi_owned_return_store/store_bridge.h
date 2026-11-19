#pragma once
#include <string>

// SDK-free boundary for the original consumer's uintptr_t HANDLE translation unit.
// One initialization attempt per EXE. Calls are serialized; traces are cumulative.
// root must already exist, be private, absolute and contain no reparse component.
// Only root/msi-controller-return.json is accepted by subsequent public calls.
namespace msiStoreFixture {
bool initialize(const std::wstring& root, unsigned long& error);
bool atomicWrite(const std::wstring& path, const std::string& text);
bool readOwned(const std::wstring& path, std::string& text, unsigned long& error);
bool acquireNoDeleteLease(const std::wstring& path, unsigned long& error);
// A failed close retains the owned handle and permits an explicit retry.
bool releaseNoDeleteLease(unsigned long& error);
bool shutdown(unsigned long& error);
std::string traceJson();
unsigned long processId();
// Exact unsigned FILETIME ticks as decimal text; empty on real API failure.
std::string processCreation();
}
