#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include "log_jsonl_boundary.h"

namespace ymcc::logging {
// Caller serializes access. One bounded CRT stream buffer per domain, no new
// thread/timer or growing queue, and no row downsampling.
class BufferedDomainLog {
public:
    static constexpr std::uint64_t CounterLimit=9007199254740991ULL;
    struct FailureRecord {
        const char* stage=nullptr;
        std::uint64_t tick=0;
        bool tickKnown=false;
        unsigned bufferedRows=0;
        int errorCode=0;
    };
    struct Evidence {
        std::uint64_t failureObservations=0;
        FailureRecord first,last;
        unsigned pendingRows=0;
        bool streamOpen=false,streamGood=false;
    };
    Evidence evidenceSnapshot() const noexcept {
        return {failureObservations_,firstFailure_,lastFailure_,pending_,stream_.is_open(),stream_.good()};
    }
    template<class Rotate>
    bool append(const std::wstring& path, std::string_view line, std::uint64_t now,
                bool urgent, std::uint64_t maxBytes, Rotate rotate) {
        try { return appendImpl(path,line,now,urgent,maxBytes,rotate); }
        catch (...) { rememberFailure("append-exception",now,true);throw; }
    }

    void flush(std::uint64_t tick=0,bool tickKnown=false) noexcept {
        try {
            if (stream_.is_open() && pending_) {
                stream_.flush();
                if(!stream_)rememberFailure("stream-flush",tick,tickKnown);
            }
            pending_ = 0;
        } catch (...) {rememberFailure("stream-flush-exception",tick,tickKnown);}
    }
    void close() noexcept {
        flush();
        const bool wasOpen=stream_.is_open(),wasGood=stream_.good();
        try {
            stream_.close();
            if(wasOpen&&wasGood&&!stream_)rememberFailure("stream-close",0,false);
            stream_.clear();
        } catch (...) {if(wasOpen)rememberFailure("stream-close-exception",0,false);}
        bytes_ = 0;
    }
    bool isOpen() const noexcept { return stream_.is_open(); }
    unsigned pendingRows() const noexcept { return pending_; }

private:
    void rememberFailure(const char* stage,std::uint64_t tick,bool tickKnown,int errorCode=0) noexcept {
        const FailureRecord current{stage,tick,tickKnown,pending_,errorCode};
        if(!firstFailure_.stage)firstFailure_=current;
        lastFailure_=current;
        if(failureObservations_<CounterLimit)++failureObservations_;
    }
    template<class Rotate>
    bool appendImpl(const std::wstring& path,std::string_view line,std::uint64_t now,
                    bool urgent,std::uint64_t maxBytes,Rotate& rotate) {
        if (path_ != path) {
            close();
            path_ = path;
            rotationAttempted_ = false;
        }
        if (!open(now)) return false;
        // Track bytes in memory; only open/rotation boundaries query file size.
        // Failed rotations retain evidence and retry at most once per second.
        if (bytes_ >= maxBytes && (!rotationAttempted_ || now - lastRotationTick_ >= 1000)) {
            close();
            lastRotationTick_ = now;
            rotationAttempted_ = true;
            rotate(path_, maxBytes);
            if (!open(now)) return false;
            if (bytes_ < maxBytes) rotationAttempted_ = false; // success has no retry delay
            else rememberFailure("rotation-size-still-at-limit",now,true);
        }
        stream_.write(line.data(), static_cast<std::streamsize>(line.size()));
        stream_.put('\n');
        if (!stream_) { rememberFailure("stream-write",now,true);close(); return false; }
        bytes_ += line.size() + 1;
        ++pending_;
        // Flush at the next write after 1s (not a periodic wakeup), on a full
        // 16-row batch, or immediately for an explicit failure/error event.
        if (urgent || pending_ >= 16 || now - lastFlushTick_ >= 1000) {
            flush(now,true);
            lastFlushTick_ = now;
        }
        if (!stream_.good()) { close(); return false; }
        return true;
    }
    bool open(std::uint64_t tick) {
        if (stream_.is_open()) {if(!stream_.good())rememberFailure("stream-not-ready",tick,true);return stream_.good();}
        stream_.clear();
        if (!openJsonLineAppend(stream_, std::filesystem::path(path_))) {rememberFailure("stream-open-or-tail",tick,true);close();return false;}
        std::error_code ec;
        const auto size = std::filesystem::file_size(std::filesystem::path(path_), ec);
        if(ec)rememberFailure("file-size",tick,true,ec.value());
        bytes_ = ec ? 0 : size;
        return true;
    }
    std::uint64_t failureObservations_=0;
    FailureRecord firstFailure_,lastFailure_;
    std::ofstream stream_;
    std::wstring path_;
    std::uint64_t bytes_ = 0;
    std::uint64_t lastFlushTick_ = 0;
    std::uint64_t lastRotationTick_ = 0;
    unsigned pending_ = 0;
    bool rotationAttempted_ = false;
};
}
