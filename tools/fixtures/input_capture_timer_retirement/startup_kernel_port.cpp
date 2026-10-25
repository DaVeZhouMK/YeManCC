// Isolated Win64 kernel port. The caller owns every returned thread handle and
// is responsible for joining/closing it and deciding lifecycle acceptance.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <memory>
#include <new>

#if !defined(_WIN64)
#error startup_kernel_port.cpp requires Win64; x86 entry conventions are unsupported.
#endif
static_assert(sizeof(void*) == 8, "startup_kernel_port.cpp requires Win64");

namespace {
struct StartupThreadContext {
    unsigned long (*entry)(void*);
    void* arg;
};

DWORD WINAPI startupThreadTrampoline(void* opaque) noexcept {
    std::unique_ptr<StartupThreadContext> context(
        static_cast<StartupThreadContext*>(opaque));
    try {
        return context->entry(context->arg);
    } catch (...) {
        // Report an exceptional thread exit without unwinding across the C ABI.
        return ERROR_UNHANDLED_EXCEPTION;
    }
}
} // namespace

extern "C" {
void* startupKernelCreateThread(unsigned long (*entry)(void*), void* arg) {
    if (entry == nullptr) {
        ::SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    auto* context = new (std::nothrow) StartupThreadContext{entry, arg};
    if (context == nullptr) {
        ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }

    // Success transfers context directly to the trampoline, which may already
    // have finished before CreateThread returns. Only failure retains ownership.
    HANDLE thread = ::CreateThread(nullptr, 0, startupThreadTrampoline,
                                   context, 0, nullptr);
    if (thread == nullptr) {
        const DWORD error = ::GetLastError();
        delete context;
        ::SetLastError(error);
    }
    return thread;
}

unsigned long startupKernelWait(void* handle, unsigned long budget) {
    return ::WaitForSingleObject(static_cast<HANDLE>(handle), budget);
}

int startupKernelClose(void* handle) {
    return ::CloseHandle(static_cast<HANDLE>(handle)) ? 1 : 0;
}

void* startupKernelDuplicate(void* handle) {
    // Precondition: handle is a caller-owned thread handle in this process.
    const HANDLE process = ::GetCurrentProcess();
    HANDLE duplicate = nullptr;
    if (!::DuplicateHandle(process, static_cast<HANDLE>(handle), process,
                           &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        return nullptr;
    }
    return duplicate;
}

void startupKernelSleep(unsigned long budget) {
    // Delay compatibility leaf uses only our own unnamed kernel event. No
    // Sleep or power transition is invoked by the fixture.
    HANDLE delayEvent=::CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!delayEvent)return;
    (void)::WaitForSingleObject(delayEvent,budget);
    (void)::CloseHandle(delayEvent);
}

unsigned long startupKernelGetLastError() {
    return ::GetLastError();
}

void startupKernelSetLastError(unsigned long error) {
    ::SetLastError(error);
}
} // extern "C"
