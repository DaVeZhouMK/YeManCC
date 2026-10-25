#pragma once
// 195 §4（R2）：传感器"名称/单位"的**惰性折叠视图**（产品与隔离 harness 同源引用）。
//
// 背景：`monitorReadHWiNFO` 每轮为每一行构造两个小写副本（`ascii_lower(label)` /
// `ascii_lower(unit)`）再做相等/子串比较 ⇒ 每行两次 `std::string` 构造（**注意：短串走
// SSO 内联存储，通常不产生堆分配**；单位串多为 1–3 字节，因此"省下 N 次构造"不等于
// "省下 N 次堆分配"）。本视图不再构造字符串，只在比较发生时做 ASCII 大小写不敏感
// 扫描——**代价是比较时重新扫描文本**，其收益由 195 裁定的"实际匹配循环新旧短比较"决定
// （记录见 Batches\R195\bench\）。
//
// 等价性（与 `ascii_lower(x) == "lit"` / `ascii_lower(x).find("lit") != npos`）：
//   · 两处都只折 ASCII 字母（原实现用 std::tolower，进程为默认 "C" 区域 ⇒ 非 ASCII
//     字节保持不变）；
//   · `find` 返回与"先整体小写再 find"相同的下标，失败返回 std::string::npos。
//   · 既有规则（同名传感器聚合、温度优先级 tctl > package、历史最大/平均值语义）
//     不在本头内，未受本改动影响。
//
// 验证：`Patches/Tests/monitor_text_fold_verify.cpp`（同一头，对语料逐一与
// "先小写再比较"的 oracle 对拍）。
#include <cstring>
#include <string>
#include <string_view>

// Borrow only the parser's bounded, immutable scratch copy; never a live mapping.
inline std::string_view monitorTextFieldView(const void* data, size_t bytes) {
    if (!data || !bytes) return {};
    const auto* begin = static_cast<const char*>(data);
    const auto* end = static_cast<const char*>(std::memchr(begin, 0, bytes));
    return {begin, end ? static_cast<size_t>(end - begin) : bytes};
}

class MonitorFoldedText {
public:
    static constexpr size_t kNotFound = std::string::npos;
    // Preserve the old lvalue-string view behavior, including later string edits.
    explicit MonitorFoldedText(const std::string& raw) : string_(&raw) {}
    // New zero-copy path: the caller owns the immutable bounded bytes.
    explicit MonitorFoldedText(std::string_view raw) : raw_(raw) {}
    // 195 R2 复核（临时视图生命周期边界）：视图**只引用**调用方的字符串，必须活得比它短。
    // 显式删除右值构造 ⇒ 绑到临时对象（悬垂引用）在编译期被拒绝。
    MonitorFoldedText(std::string&&) = delete;
    MonitorFoldedText(const MonitorFoldedText&) = default;
    bool operator==(const char* literal) const {
        const auto raw = view();
        size_t i = 0;
        for (; i < raw.size(); ++i) {
            const char expected = literal[i];
            if (expected == '\0') return false;   // literal 比字段短 ⇒ 不相等
            if (monitorAsciiFold(raw[i]) != expected) return false;
        }
        return literal[i] == '\0';
    }
    // Bounded classification hint only; exact matches still decide admission.
    char asciiAt(size_t index) const {
        const auto raw = view();
        return index < raw.size() ? monitorAsciiFold(raw[index]) : '\0';
    }
    // Borrow a suffix of this bounded view for one parser classification.
    // The underlying scratch bytes must remain alive and immutable.
    MonitorFoldedText after(size_t count) const {
        const auto raw = view();
        return MonitorFoldedText(count <= raw.size() ? raw.substr(count) : std::string_view{});
    }
    bool startsWith(const char* prefix) const {
        const auto raw = view();
        for (size_t i=0; prefix[i]; ++i)
            if (i>=raw.size() || monitorAsciiFold(raw[i])!=prefix[i]) return false;
        return true;
    }
    bool operator!=(const char* literal) const { return !(*this == literal); }
    size_t find(const char* needle) const {
        const auto raw = view();
        const size_t n = std::strlen(needle);
        if (n == 0) return 0;                     // 与 std::string::find("") 一致
        if (raw.size() < n) return kNotFound;
        for (size_t start = 0; start + n <= raw.size(); ++start) {
            size_t k = 0;
            while (k < n && monitorAsciiFold(raw[start + k]) == needle[k]) ++k;
            if (k == n) return start;
        }
        return kNotFound;
    }

private:
    static char monitorAsciiFold(char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    std::string_view view() const { return string_ ? std::string_view(*string_) : raw_; }
    const std::string* string_ = nullptr;
    std::string_view raw_;
};