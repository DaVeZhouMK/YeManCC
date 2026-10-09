#pragma once
// A9（191 §3.1）：游戏进程身份判定的**共享纯逻辑**——产品 native/main.cpp 与隔离
// harness 引用同一份实现，避免测试自写一份"永远自洽"的复刻。
//
// 背景：Toolhelp 快照名只描述"快照时刻的某个 PID"，与随后 OpenProcess 打开的
// 对象之间可能发生 PID 复用。"打开后一直持有句柄"只能保证句柄所指对象不变，
// 不能把更早快照里的名字变成该对象的身份。因此准入/发布前，必须用**同一句柄**
// 上取得的镜像名与快照提示做规范化比对。
//
// 规范化（取文件名、去 .exe、小写）与产品 sgBaseName 同体：产品侧 sgBaseName
// 转发到本函数，保证只有一份实现。
#include <string>
#include <algorithm>
#include <cwctype>

static inline std::wstring nativeIdentityNormalizeName(const std::wstring& path) {
    std::wstring f = path;
    auto p = f.find_last_of(L"\\/");
    if (p != std::wstring::npos) f = f.substr(p + 1);
    // A9：扩展名判定必须大小写不敏感——旧实现只认 ".exe"/".EXE"，于是
    // "C:\x\game.Exe" 会保留扩展名，而 Toolhelp 名字（多写作 "game.exe"）已去掉，
    // 两侧不一致会被误判为 Mismatch（把"大小写/扩展名规范"当成 PID 复用）。
    if (f.size() > 4) {
        std::wstring ext = f.substr(f.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        if (ext == L".exe") f = f.substr(0, f.size() - 4);
    }
    std::transform(f.begin(), f.end(), f.begin(), ::towlower);
    return f;
}

// 判定三态：
//   Match        —— 身份一致，按既有规则继续；
//   Mismatch     —— **已确认**不一致（该 PID 现在是别的程序）⇒ 本轮拒绝该候选，
//                   不按旧名字获得白名单/配置优先准入，也不按旧名字发布；本轮不写
//                   永久负缓存，下一轮正常重新发现；
//   Unverifiable —— 取不到镜像路径/名字（受保护进程、权限受限）⇒ 保留既有兼容路径，
//                   但必须显式记为"无法验证"，不得当作"已验证通过"。
enum class NativeNameIdentity { Match, Mismatch, Unverifiable };

// 输入必须是**已规范化**的名字（调用方用 nativeIdentityNormalizeName 处理）。
static inline NativeNameIdentity nativeNameIdentityVerdictNormalized(
    const std::wstring& snapshotName,
    const std::wstring& imageName) {
    if (snapshotName.empty() || imageName.empty()) return NativeNameIdentity::Unverifiable;
    return snapshotName == imageName ? NativeNameIdentity::Match
                                     : NativeNameIdentity::Mismatch;
}