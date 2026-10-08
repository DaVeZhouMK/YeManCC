#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace ymcc::logging {
// Caller serializes access. One bounded CRT stream buffer per domain, no new
// thread/timer or growing queue, and no row downsampling.
class BufferedDomainLog {
public:
    template<class Rotate>
    bool append(const std::wstring& path, std::string_view line, std::uint64_t now,
                bool urgent, std::uint64_t maxBytes, Rotate rotate) {
        if (path_ != path) {
            close();
            path_ = path;
            rotationAttempted_ = false;
        }
        if (!open()) return false;
        // Track bytes in memory; only open/rotation boundaries query file size.
        // Failed rotations retain evidence and retry at most once per second.
        if (bytes_ >= maxBytes && (!rotationAttempted_ || now - lastRotationTick_ >= 1000)) {
            close();
            lastRotationTick_ = now;
            rotationAttempted_ = true;
            rotate(path_, maxBytes);
            if (!open()) return false;
            if (bytes_ < maxBytes) rotationAttempted_ = false; // success has no retry delay
        }
        stream_.write(line.data(), static_cast<std::streamsize>(line.size()));
        stream_.put('\n');
        if (!stream_) { close(); return false; }
        bytes_ += line.size() + 1;
        ++pending_;
        // Flush at the next write after 1s (not a periodic wakeup), on a full
        // 16-row batch, or immediately for an explicit failure/error event.
        if (urgent || pending_ >= 16 || now - lastFlushTick_ >= 1000) {
            flush();
            lastFlushTick_ = now;
        }
        if (!stream_.good()) { close(); return false; }
        return true;
    }

    void flush() noexcept {
        try {
            if (stream_.is_open() && pending_) stream_.flush();
            pending_ = 0;
        } catch (...) {}
    }
    void close() noexcept {
        flush();
        try { stream_.close(); stream_.clear(); } catch (...) {}
        bytes_ = 0;
    }
    bool isOpen() const noexcept { return stream_.is_open(); }
    unsigned pendingRows() const noexcept { return pending_; }

private:
    bool open() {
        if (stream_.is_open()) return stream_.good();
        stream_.clear();
        stream_.open(std::filesystem::path(path_), std::ios::binary | std::ios::app);
        if (!stream_) { close(); return false; }
        std::error_code ec;
        const auto size = std::filesystem::file_size(std::filesystem::path(path_), ec);
        bytes_ = ec ? 0 : size;
        return true;
    }
    std::ofstream stream_;
    std::wstring path_;
    std::uint64_t bytes_ = 0;
    std::uint64_t lastFlushTick_ = 0;
    std::uint64_t lastRotationTick_ = 0;
    unsigned pending_ = 0;
    bool rotationAttempted_ = false;
};
}
