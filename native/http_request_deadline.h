#pragma once
// One absolute deadline from native IPC admission through send, headers and body.
// Async WinHTTP lets timeout/shutdown cancel a stalled operation safely; no timer thread.
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
namespace ymcc::http {
inline DWORD timeoutBudget(long long value) { return static_cast<DWORD>((std::max)(250LL, (std::min)(120000LL, value))); }
inline void requireDeadline(ULONGLONG deadline, const char* phase) {
    if (GetTickCount64() >= deadline)
        throw std::runtime_error(std::string("HTTP request deadline exceeded (phase=") + phase + ")");
}
class AsyncRequest {
    struct State {
        std::mutex mutex;
        std::condition_variable cv;
        DWORD completed = 0;
        DWORD error = 0;
        DWORD available = 0;
        std::array<char, 32768> buffer{};
        std::string chunk;
        std::string body;
    };
    // Establish handle ownership before allocating callback state, so a failed
    // constructor also releases all adopted handles exactly once.
    struct Handles {
        HINTERNET session, connect, request;
        ~Handles() {
            if (request) WinHttpCloseHandle(request);
            if (connect) WinHttpCloseHandle(connect);
            if (session) WinHttpCloseHandle(session);
        }
    } handles_;
    std::shared_ptr<State> state_ = std::make_shared<State>();
    ULONGLONG deadline_;
    DWORD_PTR context_ = 0;
    std::function<bool()> cancelled_;
    static void CALLBACK callback(HINTERNET, DWORD_PTR context, DWORD status, LPVOID info, DWORD length) {
        if (!context) return;
        auto* owner = reinterpret_cast<std::shared_ptr<State>*>(context);
        auto state = *owner; // HANDLE_CLOSING is the final callback; keep buffers alive until it.
        if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) { delete owner; return; }
        std::lock_guard<std::mutex> lock(state->mutex);
        if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
            state->error = static_cast<WINHTTP_ASYNC_RESULT*>(info)->dwError;
        } else if (status == WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE) {
            state->available = *static_cast<DWORD*>(info);
            state->completed = status;
        } else if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
            state->chunk.assign(static_cast<const char*>(info), length);
            state->completed = status;
        } else if (status == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE || status == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE) {
            state->completed = status;
        } else { return; }
        state->cv.notify_one();
    }
    void prepare(const char* phase) {
        requireDeadline(deadline_, phase);
        if (cancelled_ && cancelled_()) throw std::runtime_error("HTTP request cancelled during shutdown");
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->completed = 0; state_->error = 0; state_->available = 0; state_->chunk.clear();
    }
    void started(BOOL ok, const char* phase) {
        if (!ok && GetLastError() != ERROR_IO_PENDING)
            throw std::runtime_error(std::string("HTTP request failed (WinHTTP ") + std::to_string(GetLastError()) + ", phase=" + phase + ")");
    }
    void wait(DWORD expected, const char* phase) {
        std::unique_lock<std::mutex> lock(state_->mutex);
        for (;;) {
            requireDeadline(deadline_, phase);
            if (cancelled_ && cancelled_()) throw std::runtime_error("HTTP request cancelled during shutdown");
            if (state_->error) throw std::runtime_error("HTTP request failed (WinHTTP " + std::to_string(state_->error) + ", phase=" + phase + ")");
            if (state_->completed == expected) return;
            const auto left = deadline_ - GetTickCount64();
            state_->cv.wait_for(lock, std::chrono::milliseconds((std::min)(left, 20ULL)));
        }
    }
public:
    AsyncRequest(HINTERNET session, HINTERNET connect, HINTERNET request, ULONGLONG deadline, std::function<bool()> cancelled)
        : handles_{session, connect, request}, deadline_(deadline), cancelled_(std::move(cancelled)) {
        if (WinHttpSetStatusCallback(handles_.request, callback,
            WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE | WINHTTP_CALLBACK_FLAG_HEADERS_AVAILABLE |
            WINHTTP_CALLBACK_FLAG_READ_COMPLETE | WINHTTP_CALLBACK_FLAG_DATA_AVAILABLE |
            WINHTTP_CALLBACK_FLAG_REQUEST_ERROR | WINHTTP_CALLBACK_FLAG_HANDLES, 0)
            == WINHTTP_INVALID_STATUS_CALLBACK) {
            throw std::runtime_error("WinHTTP callback registration failed");
        }
        auto* owner = new std::shared_ptr<State>(state_);
        DWORD_PTR context = reinterpret_cast<DWORD_PTR>(owner);
        context_ = context;
        if (!WinHttpSetOption(handles_.request, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context))) {
            delete owner;
            throw std::runtime_error("WinHTTP request context failed");
        }
    }
    ~AsyncRequest() = default;
    AsyncRequest(const AsyncRequest&) = delete;
    AsyncRequest& operator=(const AsyncRequest&) = delete;
    void send(const std::string& body) {
        prepare("send"); state_->body = body;
        const auto length = static_cast<DWORD>(state_->body.size());
        started(WinHttpSendRequest(handles_.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            length ? state_->body.data() : WINHTTP_NO_REQUEST_DATA, length, length, context_), "send");
        wait(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, "send");
    }
    void headers() {
        prepare("headers"); started(WinHttpReceiveResponse(handles_.request, nullptr), "headers");
        wait(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, "headers");
    }
    // Streaming callers may use an idle deadline for each new operation.
    // Existing HTTP callers keep their single original absolute deadline.
    void deadlineForNextOperation(ULONGLONG deadline) { deadline_ = deadline; }
    DWORD available() {
        prepare("body");
        started(WinHttpQueryDataAvailable(handles_.request, nullptr), "body");
        wait(WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE, "body");
        return state_->available;
    }
    std::string readChunk(size_t maximum = 32768) {
        if (!maximum) throw std::runtime_error("HTTP read size must be positive");
        prepare("body");
        const auto bytes = static_cast<DWORD>((std::min)(maximum, state_->buffer.size()));
        started(WinHttpReadData(handles_.request, state_->buffer.data(), bytes, nullptr), "body");
        wait(WINHTTP_CALLBACK_STATUS_READ_COMPLETE, "body");
        return state_->chunk;
    }
    std::string readAll(size_t limit) {
        std::string result;
        for (;;) {
            auto chunk = readChunk();
            if (chunk.empty()) return result;
            if (chunk.size() > limit - result.size()) throw std::runtime_error("HTTP response exceeded 8 MiB limit");
            result += chunk;
        }
    }
};
}