// 194 §2（2026-09-20，P3 收尾）：协议自测的"严格拒绝"判定（产品与隔离 harness 同源引用）。
//
// 原判定形如 `!receipt.is_object() || (status/detail 匹配)`：**没拿到可检查的回执**也被
// 算作"Host 正确拒绝了请求"。194 §2 裁定该口径不成立——只有**实际观测到契约规定的
// 拒绝**才可通过；缺回执、null、字段类型不对、错误 status/detail、无关会话的响应都必须
// 让自测正常失败（非零退出且不崩溃）。
//
// 判定输入不是原始回执字节，而是请求路径回填的观察结果 ReceiptObservation，它区分：
//   · 传输层故障（回执不可用）与"回执可解析但不是本会话的拒绝"；
//   · 是否通过生产 tuple 校验（= 既有身份校验；194 §2 要求 SUBMIT_FRAME 拒绝必须经过它）；
//   · status/detail 是否为字符串（类型不对 = 判失败，而不是让 nlohmann value() 抛类型异常）。
//
// 为什么不能只看 responseOut：Host 对**解析期**错误（未知命令等）按契约回 Error(null)
// ——没有 tuple（Program.cs:943 + :1918-1925），生产 tuple 校验必然不匹配；此时用回执
// 携带的 hostInstanceId 与本自测会话（HELLO 回执观测值）绑定，取代 tuple 校验。
#pragma once

#include <string>

namespace inputHostSelfTestRules {

// 一次请求实际观察到的回执事实（仅自测路径回填；生产调用传 nullptr，零行为变化）。
struct ReceiptObservation {
    bool attempted = false;      // 请求路径进入回执阶段（false = 未发出/未走到协议）
    bool transportFault = false; // 传输层失败：回执不可用（read 失败/不可解析/解析异常）
    bool receiptObject = false;  // 收到可解析的 JSON 对象回执
    bool tupleVerified = false;  // 通过生产 tuple 校验（既有身份校验）
    bool statusIsString = false; // status 存在且为字符串
    bool detailIsString = false; // detail 存在且为字符串
    std::string status;          // 仅当 statusIsString
    std::string detail;          // 仅当 detailIsString
    std::string hostInstanceId;  // 回执携带的宿主实例 id（可解析回执必有；空 = 无法绑定会话）
};

// 安全取字符串字段：缺失/类型不对 ⇒ 标记"非字符串"（判定为失败），绝不抛类型异常。
// 模板化以保持本头无外部依赖：产品传 nlohmann::json，harness 传同一 nlohmann 头构造
// 的 json 值（null/array/数字/字符串）——两处执行的是**同一份**代码。
template <class JsonObject>
inline void readStringField(const JsonObject& source, const char* key, bool& isString, std::string& into) {
    const auto it = source.find(key);
    if (it == source.end() || !it->is_string()) { isString = false; into.clear(); return; }
    isString = true;
    into = it->get<std::string>();   // 已确认 is_string ⇒ get 不抛类型异常
}

// 从一个"已确认是对象"的回执里提取自测判定所需的事实（不解释、不推断）。
template <class JsonObject>
inline void observeReceiptFields(const JsonObject& source, bool tupleVerified, ReceiptObservation& into) {
    into.receiptObject = true;
    into.tupleVerified = tupleVerified;
    readStringField(source, "status", into.statusIsString, into.status);
    readStringField(source, "detail", into.detailIsString, into.detail);
    bool hostInstanceIdIsString = false;
    readStringField(source, "hostInstanceId", hostInstanceIdIsString, into.hostInstanceId);
    if (!hostInstanceIdIsString) into.hostInstanceId.clear();
}

// 严格判定：仅当"本会话"给出了与契约一致的对象拒绝回执时为 true。
//   expectedStatus/expectedDetail：Host 契约规定的拒绝（调用点注明源码锚）。
//   sessionHostInstanceId：本自测会话在 HELLO 成功回执中观测到的宿主实例 id
//     （空 = 会话未建立，判定必然为假）。
//   requireTupleVerified：true = 该拒绝必须经过既有 tuple 身份校验（带完整 tuple 的
//     拒绝，如 SUBMIT_FRAME）；false = Host 契约中无 tuple 的解析期错误（Error(null)），
//     改用 hostInstanceId 绑定本会话。
inline bool rejectionObserved(const ReceiptObservation& obs,
                              const std::string& expectedStatus,
                              const std::string& expectedDetail,
                              const std::string& sessionHostInstanceId,
                              bool requireTupleVerified) {
    if (!obs.attempted || obs.transportFault || !obs.receiptObject) return false;
    if (!obs.statusIsString || !obs.detailIsString) return false;
    if (obs.status != expectedStatus || obs.detail != expectedDetail) return false;
    if (requireTupleVerified && !obs.tupleVerified) return false;
    if (sessionHostInstanceId.empty()) return false;
    return obs.hostInstanceId == sessionHostInstanceId;
}

} // namespace inputHostSelfTestRules