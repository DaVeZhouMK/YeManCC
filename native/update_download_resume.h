#pragma once
#include <cstdint>
#include <limits>
#include <string>

// HTTP byte-resume admission only. Final file identity always requires SHA-256.
// Kept independent of WinHTTP so the production policy is exercised by fixtures.
namespace ymcc::update_download {
inline std::string trimHeader(const std::string& text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}
inline bool parseUnsigned64(const std::string& text, uint64_t& value) {
    if (text.empty()) return false;
    uint64_t parsed = 0;
    for (const unsigned char ch : text) {
        if (ch < '0' || ch > '9') return false;
        const auto digit = static_cast<uint64_t>(ch - '0');
        if (parsed > ((std::numeric_limits<uint64_t>::max)() - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    value = parsed;
    return true;
}
struct ContentRange {
    bool unsatisfied = false;
    bool totalKnown = false;
    uint64_t start = 0, end = 0, total = 0;
};
inline bool parseContentRange(const std::string& raw, ContentRange& output) {
    const auto value = trimHeader(raw);
    if (value.rfind("bytes ", 0) != 0) return false;
    const auto slash = value.find('/', 6);
    if (slash == std::string::npos) return false;
    const auto bounds = value.substr(6, slash - 6);
    const auto total = value.substr(slash + 1);
    ContentRange parsed;
    if (total != "*") {
        if (!parseUnsigned64(total, parsed.total)) return false;
        parsed.totalKnown = true;
    }
    if (bounds == "*") {
        // The unsatisfied form requires a complete numeric length (including zero).
        if (!parsed.totalKnown) return false;
        parsed.unsatisfied = true;
    } else {
        const auto dash = bounds.find('-');
        if (dash == std::string::npos ||
            !parseUnsigned64(bounds.substr(0, dash), parsed.start) ||
            !parseUnsigned64(bounds.substr(dash + 1), parsed.end) ||
            parsed.end < parsed.start ||
            (parsed.totalKnown && parsed.end >= parsed.total)) return false;
    }
    output = parsed;
    return true;
}
inline bool isStrongEntityTag(const std::string& text) {
    if (text.size() < 2 || text.front() != '"' || text.back() != '"') return false;
    for (size_t i = 1; i + 1 < text.size(); ++i) {
        const auto ch = static_cast<unsigned char>(text[i]);
        if (ch == '"' || ch < 0x21 || ch == 0x7f) return false;
    }
    return true;
}
inline std::string resumeValidator(const std::string& etag) {
    // Weak ETags and unproven Last-Modified dates must not be sent as If-Range.
    return isStrongEntityTag(etag) ? etag : std::string{};
}
struct ResponsePlan {
    bool accepted = false;
    bool alreadyComplete = false;
    bool restartRequired = false;
    bool rangeAccepted = false;
    uint64_t totalBytes = 0;
    uint64_t bodyEnd = 0;
    bool bodyLengthKnown = false;
    std::string error;
};
inline ResponsePlan evaluateResponse(unsigned status, uint64_t offset,
    const std::string& lengthHeader, const std::string& rangeHeader,
    const std::string& contentEncoding, const std::string& ifRange,
    const std::string& responseEtag) {
    ResponsePlan plan;
    auto reject = [&](const char* error, bool restart) {
        plan.error = error;
        plan.restartRequired = restart;
        return plan;
    };
    if (status == 416 && offset > 0) {
        ContentRange range;
        if (!parseContentRange(rangeHeader, range) || !range.unsatisfied || range.total != offset)
            return reject("Unsatisfied range does not match the partial file", true);
        plan.accepted = plan.alreadyComplete = true;
        plan.totalBytes = plan.bodyEnd = offset;
        plan.bodyLengthKnown = true;
        return plan; // Caller still hashes the full file; 416 is not identity proof.
    }
    if (status != 200 && status != 206) return reject("Unexpected download HTTP status", false);
    if (offset > 0 && status == 200) return reject("Server did not honor the resume range", true);
    auto encoding = trimHeader(contentEncoding);
    for (char& ch : encoding) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    if (!encoding.empty() && encoding != "identity")
        return reject("Encoded response cannot be appended to a byte-resume file", offset > 0);
    uint64_t length = 0;
    const auto lengthText = trimHeader(lengthHeader);
    const bool hasLength = !lengthText.empty();
    if (hasLength && !parseUnsigned64(lengthText, length))
        return reject("Invalid 64-bit Content-Length", offset > 0);
    if (status == 206) {
        ContentRange range;
        if (!parseContentRange(rangeHeader, range) || range.unsatisfied ||
            !range.totalKnown || range.start != offset)
            return reject("Invalid Content-Range for resumed download", offset > 0);
        const uint64_t bodyLength = range.end - range.start + 1;
        if (hasLength && length != bodyLength)
            return reject("Content-Length does not match Content-Range", offset > 0);
        if (offset > 0 && !ifRange.empty() && (!isStrongEntityTag(ifRange) ||
            (!responseEtag.empty() && responseEtag != ifRange)))
            return reject("Resume entity validator changed", offset > 0);
        plan.rangeAccepted = offset > 0;
        plan.totalBytes = range.total;
        plan.bodyEnd = range.end + 1;
        plan.bodyLengthKnown = true;
    } else {
        plan.totalBytes = plan.bodyEnd = length;
        plan.bodyLengthKnown = hasLength;
    }
    plan.accepted = true;
    return plan;
}
} // namespace ymcc::update_download
