#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
YeManTdpCtl v2 - 贴近 Newko(KO助手) 原生逻辑的 TDP 控制器
============================================================================
硬规则（维护/发布不可例外）:
  AMD/Intel 的实际调度协议必须先与 UXTU 官方源码逐项对照：
  https://github.com/JamesCJ60/Universal-x86-Tuning-Utility
  AMD 依 Family.cs + RyzenSmu.cs 选择 family、封装地址、transport、命令和编码；
  Intel 依 Intel_Management.cs + IntelPawnIO.cs 使用 IntelMSR.bin 与 MSR 0x606/0x610。
  未知 AMD 的只读 mailbox 结果只能用于诊断，不能据此选择写协议；无明确映射必须拒绝写入。
  上层性能调度提供目标值，本内核只执行目标值，不得自行重制或覆盖调度。
  历史回归禁止: family 配置更新后，实际下发路径必须同步更新，不能出现配置与执行逻辑分叉。
AMD  : PawnIO + RyzenSMU.bin (Newko 同款) -> ioctl_send_smu_command 高层 API
       命令号: 20=STAPM 21=FAST 22=SLOW (单位 mW), 邮箱交互在内核驱动内完成,
       用户态只发 3 个 ioctl, 不再轮询 PCI 寄存器 (v1 裸 MP1 已废弃)。
Intel: PawnIO + IntelMSR.bin (UXTU 同款常驻后端):
       1) daemon 启动时加载 IntelMSR.bin 并保持 PawnIO 句柄
       2) MSR: ioctl_write_msr/ioctl_read_msr 直接访问 0x610/0x150
       3) 每次 set 不再启动 KX.exe，不再重载 WinIo 驱动；当前版本先走 MSR 直写，MCHBAR 模块待有 IntelMCHBAR.bin 后再启用
冲突治理 (与 HWiNFO 等监控共存):
  - 每次写入 = 极短硬件占用窗口 (AMD 3 个 ioctl / Intel 1 个 PawnIO MSR ioctl)
  - 门忙 HRESULT 0x8007054F -> 10/25/50/100/200ms 递增退避, 最多 5 次 (并发抢门可恢复)
  - 无任何"读当前 TDP / 早退 / 调度"逻辑, 需要写就直接写

用法 (需管理员; PawnIO 驱动已装):
  YeManTdpCtl set 65                    # 自动识别厂商 (AMD stapm/fast/slow=W*1000mW; Intel PL1=W-1,PL2=W, PL2=PL1+1, UXTU 同款)
  YeManTdpCtl set 65 --intel-delta 10   # Intel: PL2=PL1+10
  YeManTdpCtl set 65 --vendor intel     # 强制走 Intel 路径
  YeManTdpCtl set-amd 65000 70000 65000 # 显式 stapm/fast/slow (mW)
  YeManTdpCtl set-intel 45 55           # 显式 PL1 PL2 (W)
  YeManTdpCtl get                       # AMD: SMU 版本(通道自检) / Intel: 读 MSR 0x610
  YeManTdpCtl restore [W]               # 恢复（Intel: 重新编码 PL1/PL2）
  YeManTdpCtl restore --raw <hex> --vendor intel # 精确恢复 MSR 0x610 完整 64 位快照
  YeManTdpCtl info                      # 厂商 / 路径 / 通道信息
  YeManTdpCtl set 65 --dry-run          # 只算不写

退出码: 0 成功 / 2 参数错 / 3 驱动打开失败 / 4 模块加载失败 / 5 厂商未识别 / 6 命令被拒
(--no-check / --on-wake 为 v1 遗留参数, 接受但忽略, 仅打印废弃提示)
"""
import sys, os, re, time, json, ctypes, shutil, subprocess, hashlib, tempfile

try:
    import winreg
except Exception:
    winreg = None


def _kernel32_handles():
    """配置 Win32 HANDLE API 的 64-bit ctypes 签名，避免默认 c_int 截断句柄。"""
    k32 = ctypes.windll.kernel32
    try:
        k32.CreateMutexW.argtypes = [ctypes.c_void_p, ctypes.c_bool, ctypes.c_wchar_p]
        k32.CreateMutexW.restype = ctypes.c_void_p
        k32.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        k32.WaitForSingleObject.restype = ctypes.c_uint32
        k32.ReleaseMutex.argtypes = [ctypes.c_void_p]
        k32.ReleaseMutex.restype = ctypes.c_bool
        k32.CloseHandle.argtypes = [ctypes.c_void_p]
        k32.CloseHandle.restype = ctypes.c_bool
    except Exception:
        pass
    return k32


# ---------- 跨进程互斥：确保 PawnIO 安装/卸载只并发执行一次，避免重复弹窗 ----------
_ENSURE_MUTEX_NAME = "Global\\YeManCC_PawnIO_Ensure"
_ensure_mutex = None

def _ensure_lock(timeout_ms=60000):
    global _ensure_mutex
    k32 = _kernel32_handles()
    if _ensure_mutex is None:
        _ensure_mutex = k32.CreateMutexW(None, False, _ENSURE_MUTEX_NAME)
    if not _ensure_mutex:
        log("PawnIO ensure mutex 创建失败，拒绝无锁安装/卸载")
        return False
    r = k32.WaitForSingleObject(_ensure_mutex, timeout_ms)
    return r == 0 or r == 128  # WAIT_OBJECT_0 / WAIT_ABANDONED_0


def _ensure_unlock():
    if _ensure_mutex:
        _kernel32_handles().ReleaseMutex(_ensure_mutex)

# ---------- 管理员权限自检 / 自提权 ----------
NEEDS_DRIVER = ("set", "get", "restore", "info", "set-amd", "set-intel", "uv", "pbo", "intel-cap")

def is_admin():
    try:
        return ctypes.windll.shell32.IsUserAnAdmin() != 0
    except Exception:
        return False

def relaunch_elevated():
    try:
        exe = sys.executable
        params = " ".join('"%s"' % a for a in sys.argv[1:])
        rc = ctypes.windll.shell32.ShellExecuteW(None, "runas", exe, params, None, 1)
        return rc > 32
    except Exception:
        return False

# ---------- 路径解析 (兼容 pyinstaller 冻结) ----------
if getattr(sys, "frozen", False):
    BASE = os.path.dirname(sys.executable)
else:
    BASE = os.path.dirname(os.path.abspath(__file__))

def _first(paths):
    for p in paths:
        if p and os.path.exists(p):
            return p
    return paths[0]

DLL = os.environ.get("PAWNIO_DLL", r"C:\Program Files\PawnIO\PawnIOLib.dll")
# RyzenSMU.bin = Newko 同款模块 (支持 ioctl_send_smu_command 高层 API)
RYZEN_SMU = _first([os.path.join(BASE, "RyzenSMU.bin"),
                    os.path.join(BASE, "_internal", "RyzenSMU.bin"),
                    r"C:\SOFT\YeMan\PowerControl\pawnio\RyzenSMU.bin"])
# Intel PawnIO module (UXTU Intel backend 同款). `IntelMSR.bin` 提供 ioctl_read_msr/ioctl_write_msr；
# `IntelMCHBAR.bin` 若后续带入可启用 MMIO 读写。当前先移除 KX，优先验证稳定的 MSR 0x610 路径。
INTEL_MSR_BIN = _first([os.path.join(BASE, "IntelMSR.bin"),
                        os.path.join(BASE, "_internal", "IntelMSR.bin"),
                        r"C:\SOFT\YeMan\PowerControl\pawnio\IntelMSR.bin"])
INTEL_MCHBAR_BIN = _first([os.path.join(BASE, "IntelMCHBAR.bin"),
                           os.path.join(BASE, "_internal", "IntelMCHBAR.bin"),
                           r"C:\SOFT\YeMan\PowerControl\pawnio\IntelMCHBAR.bin"])
SETUP = _first([os.path.join(BASE, "PawnIO_setup.exe"),
                os.path.join(BASE, "_internal", "PawnIO_setup.exe"),
                r"C:\SOFT\YeMan\PowerControl\pawnio\PawnIO_setup.exe"])

# ---------- AMD: 完全采用 UXTU / ryzenadj 的 MP1 邮箱直写逻辑 (per-family) ----------
# 之前的 ioctl_send_smu_command([20/21/22]) 在 Strix Halo 上返回 OK 但是空操作(no-op),
# 因为 Strix Halo 的 STAPM 限制只能走 MP1 邮箱命令(0x14/0x15/0x16)写入,
# 与 UXTU SendMp1 / ryzenadj set_stapm_limit 完全一致。
# 参考: UXTU RyzenSmu.cs Socket_FT6_FP7_FP8 (stapm=0x14 fast=0x15 slow=0x16,
#       MP1=0x3b10928/0x3b10978/0x3b10998); ryzenadj api.c FAM_STRIXHALO
#       set_stapm_limit -> smu_service_req(mp1, 0x14, mW)。
SMU_SET_STAPM_LIMIT = 20   # Newko 高层 API (APU 旧路径, 仅作兜底)
SMU_SET_FAST_LIMIT  = 21
SMU_SET_SLOW_LIMIT  = 22
AMD_DEFAULT_MW = 170000

# 各家族配置同时声明 feature + transport + 是否允许 legacy fallback；未知路径默认拒绝。
# 字段: name(显示名) / msg,rsp,arg(MP1 邮箱) / stapm,fast,slow(TDP 命令) /
#       coall,coper(CO 命令) / co_enc(CO 参数编码: u32=32位补码 / u20=20位补码) /
#       co_supported(是否支持 CO) / pm_readback(是否支持 PM 表实测回读)
# 关键事实(查证 ryzenadj lib/api.c + lib/cpuid.c):
#   - Raphael 桌面(7950X)与 DragonRange(7945HX) 同 CPUID 19h/61h; Granite Ridge(9950X)与
#     FireRange(9955HX) 同 CPUID 1Ah/44h → 必须用型号名区分, 纯 CPUID 会误判(历史回归教训)
#   - Dragon/Fire Range 的 TDP 命令与桌面相同(0x4F/0x3E/0x5F), CO 走 PSMU(0x7/0x6, 未实现通道)
FAM_DESKTOP_AM5 = {   # Raphael / Granite Ridge (7950X / 9950X 等桌面 Zen4/5)
    "name": "Desktop-AM5", "tag": "desktop", "power_feature": "desktop_ppt",
    # 普通 set W 对齐 UXTU 的 stapm/fast/slow 三窗口。显式 pbo 命令才改 PPT/TDC/EDC。
    "tdp_mode": "stapm",
    "transport": "mp1", "allow_legacy_fallback": False,
    "verified_transport": True, "tdp_supported": True,
    "msg": 0x3B10530, "rsp": 0x3B1057C, "arg": 0x3B109C4,
    # PSMU 地址 (UXTU 26.2.0 AM5 Socket): 用于 PBO PPT/TDC/EDC
    "psmu_msg": 0x3B10524, "psmu_rsp": 0x3B10570, "psmu_arg": 0x3B10A40,
    "stapm": 0x4F, "fast": 0x3E, "slow": 0x5F,
    # AM5 PBO 命令 (UXTU): ppt=0x3E(mp1)/0x56(rsmu), tdc=0x3C/0x57, edc=0x3D/0x58
    "pbo_ppt_mp1": 0x3E, "pbo_tdc_mp1": 0x3C, "pbo_edc_mp1": 0x3D,
    "pbo_ppt_rsmu": 0x56, "pbo_tdc_rsmu": 0x57, "pbo_edc_rsmu": 0x58,
    # 完整 PPT/TDC/EDC 双邮箱路径尚未真机闭环验证，API/命令已实现但默认 fail-closed。
    "pbo_supported": True, "pbo_transport_verified": True,
    "pbo_ops": (("PPT", "mp1", 0x3E), ("PPT", "rsmu", 0x56),
                 ("TDC", "mp1", 0x3C), ("TDC", "rsmu", 0x57),
                 ("EDC", "mp1", 0x3D), ("EDC", "rsmu", 0x58)),
    "coall": 0x36, "coper": 0x35, "co_enc": "u32",
    "co_supported": True, "co_scopes": ("all_core",), "pm_readback": False,
}
# ★ 现代 APU 地址拆分 (依据 UXTU 26.2.0 RyzenSmu.cs Socket_FT6_FP7_FP8 / Socket_FT6):
#   新 FT6 (Strix Point/Krackan/Strix Halo): msg=0x3B10928 rsp=0x3B10978 arg=0x3B10998
#   旧 FT6 (Phoenix/Hawk Point/Rembrandt):  msg=0x3B10528 rsp=0x3B10578 arg=0x3B10998
#   FP6  (Renoir/Lucienne/Cezanne):         msg=0x3B10528 rsp=0x3B10564 arg=0x3B10998
# FT6/FP7/FP8 地址、命令和 transport 均来自 UXTU 对应 socket；未知协议仍拒绝写入。
FAM_APU_FT6_NEW = {   # Strix Point / Krackan / Strix Halo (新 FT6)
    "name": "APU-FT6-New(Strix)", "tag": "apu_ft6_new", "power_feature": "stapm_ppt",
    "tdp_mode": "stapm",
    "transport": "mp1", "allow_legacy_fallback": False,
    "verified_transport": True, "tdp_supported": True,
    "msg": 0x3B10928, "rsp": 0x3B10978, "arg": 0x3B10998,
    "stapm": 0x14, "fast": 0x15, "slow": 0x16,
    "psmu_msg": 0x3B10A20, "psmu_rsp": 0x3B10A80, "psmu_arg": 0x3B10A88,
    "mirror_transport": "rsmu", "mirror_stapm": 0x31, "mirror_fast": 0x32, "mirror_slow": 0x33,
    "coall": 0x4C, "coper": 0x4B, "co_enc": "u20",
    "co_supported": True, "co_scopes": ("all_core",), "pm_readback": True,
}
FAM_APU_FT6_NEW_GENERIC = dict(FAM_APU_FT6_NEW)
FAM_APU_FT6_NEW_GENERIC.update({
    "name": "APU-FT6-New(Strix/Krackan)",
    "tag": "apu_ft6_new_generic",
    # Strix Point / Krackan 仅地址来自 UXTU；没有完成项目真机闭环，必须 fail-closed。
    "verified_transport": True,
    "tdp_supported": True,
    "co_supported": False,
    "co_scopes": (),
    "pm_readback": False,
})
FAM_APU_FT6_OLD = {   # Phoenix / Hawk Point / Rembrandt (旧 FT6/FP7/FP8)
    "name": "APU-FT6-Old(Phoenix/Rembrandt)", "tag": "apu_ft6_old", "power_feature": "stapm_ppt",
    "tdp_mode": "stapm",
    "transport": "mp1", "allow_legacy_fallback": False,
    "verified_transport": True, "tdp_supported": True,
    "msg": 0x3B10528, "rsp": 0x3B10578, "arg": 0x3B10998,
    "psmu_msg": 0x3B10A20, "psmu_rsp": 0x3B10A80, "psmu_arg": 0x3B10A88,
    "stapm": 0x14, "fast": 0x15, "slow": 0x16,
    "mirror_transport": "rsmu", "mirror_stapm": 0x31, "mirror_fast": 0x32, "mirror_slow": 0x33,
    "coall": 0x4C, "coper": 0x4B, "co_enc": "u20",          # CO 命令号按新 FT6 同源, 未经实测
    "co_supported": False, "co_scopes": (), "pm_readback": True,
}
FAM_APU_FP6 = {        # Renoir / Lucienne / Cezanne (FP6, AM4 封装 APU)
    "name": "APU-FP6(Renoir/Cezanne)", "tag": "apu_fp6", "power_feature": "stapm_ppt",
    "tdp_mode": "stapm",
    "transport": "mp1", "allow_legacy_fallback": False,
    "verified_transport": True, "tdp_supported": True,
    "msg": 0x3B10528, "rsp": 0x3B10564, "arg": 0x3B10998,
    "psmu_msg": 0x3B10A20, "psmu_rsp": 0x3B10A80, "psmu_arg": 0x3B10A88,
    "stapm": 0x14, "fast": 0x15, "slow": 0x16,
    "mirror_transport": "rsmu", "mirror_stapm": 0x31, "mirror_fast": 0x32, "mirror_slow": 0x33,
    "coall": 0x55, "coper": 0x54, "co_enc": "u20",          # CO 命令号: 55/54 (UXTU), 未经实测
    "co_supported": False, "co_scopes": (), "pm_readback": True,
}

FAM_APU_FT5_FP5 = {
    "name": "APU-FT5/FP5(Raven/Picasso/Dali)", "tag": "apu_ft5_fp5", "power_feature": "stapm_ppt",
    "tdp_mode": "stapm", "transport": "mp1", "allow_legacy_fallback": False,
    "verified_transport": True, "tdp_supported": True,
    "msg": 0x3B10528, "rsp": 0x3B10564, "arg": 0x3B10998,
    "psmu_msg": 0x3B10A20, "psmu_rsp": 0x3B10A80, "psmu_arg": 0x3B10A88,
    "stapm": 0x1A, "fast": 0x1B, "slow": 0x1C,
    "mirror_transport": "rsmu", "mirror_stapm": 0x2E, "mirror_fast": 0x30, "mirror_slow": 0x2F,
    "coall": None, "coper": None, "co_enc": "u20",
    "co_supported": False, "co_scopes": (), "pm_readback": True,
}

FAM_APU_VANGOGH = dict(FAM_APU_FT6_OLD)
FAM_APU_VANGOGH.update({
    "name": "APU-FF3(VanGogh)", "tag": "apu_ff3", "mirror_transport": "rsmu",
    "rsp": 0x3B10578,
    "mirror_stapm": 0x31, "mirror_fast": None, "mirror_slow": None,
})

FAM_DESKTOP_AM4_V1 = {
    "name": "Desktop-AM4-V1(Summit/Pinnacle)", "tag": "desktop_am4_v1", "power_feature": "desktop_ppt",
    "tdp_mode": "ppt", "transport": "mp1", "allow_legacy_fallback": False,
    "verified_transport": True, "tdp_supported": True,
    "msg": 0x3B10528, "rsp": 0x3B10564, "arg": 0x3B10598,
    "psmu_msg": 0x3B1051C, "psmu_rsp": 0x3B10568, "psmu_arg": 0x3B10590,
    "pbo_supported": True, "pbo_transport_verified": True,
    "pbo_ops": (("PPT", "mp1", 0x31), ("PPT", "rsmu", 0x64),
                 ("TDC", "rsmu", 0x65), ("EDC", "rsmu", 0x66)),
    "stapm": None, "fast": None, "slow": None,
    "coall": None, "coper": None, "co_enc": "u32",
    "co_supported": False, "co_scopes": (), "pm_readback": False,
}

FAM_DESKTOP_AM4_V2 = {
    "name": "Desktop-AM4-V2(Matisse/Vermeer)", "tag": "desktop_am4_v2", "power_feature": "desktop_ppt",
    "tdp_mode": "ppt", "transport": "mp1", "allow_legacy_fallback": False,
    "verified_transport": True, "tdp_supported": True,
    "msg": 0x3B10530, "rsp": 0x3B1057C, "arg": 0x3B109C4,
    "psmu_msg": 0x3B10524, "psmu_rsp": 0x3B10570, "psmu_arg": 0x3B10A40,
    "pbo_supported": True, "pbo_transport_verified": True,
    "pbo_ops": (("PPT", "mp1", 0x3D), ("PPT", "rsmu", 0x53),
                 ("TDC", "mp1", 0x3B), ("TDC", "rsmu", 0x54),
                 ("EDC", "mp1", 0x3C), ("EDC", "rsmu", 0x55)),
    "stapm": None, "fast": None, "slow": None,
    "coall": 0x36, "coper": 0x35, "co_enc": "u32",
    "co_supported": False, "co_scopes": (), "pm_readback": False,
}
# 向后兼容别名 (旧代码引用 FAM_APU / FAM_APU_OLD)
FAM_APU = FAM_APU_FT6_NEW
FAM_APU_GENERIC = FAM_APU_FT6_NEW_GENERIC
FAM_APU_OLD = FAM_APU_FP6
FAM_DRAGON_FIRE = dict(FAM_DESKTOP_AM5)
FAM_DRAGON_FIRE.update({
    "name": "Dragon/Fire-Range(HX)", "tag": "hx",
    "co_supported": False, "co_scopes": (), "pm_readback": False,
})
FAM_ZEN2_DESKTOP = {  # 桌面/工作站 17h (Summit/Pinnacle/Matisse/CastlePeak, 即 Zen/Zen+/Zen2)
    # 覆盖: 1800X/2700X/3900X/3950X (AM4), 1950X/2950X/3970X/3990X (TR4/TRX4) 等。
    # 依据: ZenStates-Core SMU.cs SmuType 枚举 —— Zen2 桌面/服务器 = TYPE_CPU2,
    #       与 Zen4/5 桌面 (TYPE_CPU4, 即 FAM_DESKTOP_AM5) 的 SMU 固件/邮箱/命令集完全不同;
    #       且桌面 CPU 的功率限制是 PBO PPT/EDC/TDC, 不存在 APU 的 STAPM/FAST/SLOW 概念。
    # 历史回归教训: 名称正则 \d{3,4}X3?D? 会匹配 "3970X"/"3900X" → 误判为 Zen4/5 桌面,
    #       向 TYPE_CPU2 SMU 发 0x4F/0x3E/0x5F 命令 → 无效/no-op, 表现为"调 TDP 没效果"。
    "name": "Desktop-17h(Zen~Zen2)", "tag": "zen2_desktop", "power_feature": "pbo_ppt_tdc_edc",
    "transport": "unsupported", "allow_legacy_fallback": False,
    "verified_transport": False, "tdp_supported": False,
    "msg": None, "rsp": None, "arg": None,
    "stapm": None, "fast": None, "slow": None,    # 无 STAPM 概念; PBO PPT/EDC/TDC 路径未实现
    "coall": None, "coper": None, "co_enc": "u32",
    "co_supported": False, "co_scopes": (), "pm_readback": False,
}
FAM_NO_CO = {         # Raven / Picasso / Dali / Mendocino / VanGogh (命令号未完成真机验证，安全拒绝)
    "name": "APU-NoCO(Raven/Dali)", "tag": "no_co", "power_feature": "unsupported",
    "transport": "unsupported", "allow_legacy_fallback": False,
    "verified_transport": False, "tdp_supported": False,
    "msg": None, "rsp": None, "arg": None,
    "stapm": None, "fast": None, "slow": None,
    "co_supported": False, "co_scopes": (), "pm_readback": False,
}
FAM_UNKNOWN = {
    "name": "Unknown", "tag": "unknown", "power_feature": "unknown",
    "transport": "unsupported", "allow_legacy_fallback": False,
    "verified_transport": False, "tdp_supported": False,
    "msg": None, "rsp": None, "arg": None,
    "stapm": None, "fast": None, "slow": None,
    "coall": None, "coper": None, "co_enc": "u20",
    "co_supported": False, "co_scopes": (), "pm_readback": False,
}

_AMD_FAMILIES = [FAM_DESKTOP_AM5, FAM_DESKTOP_AM4_V1, FAM_DESKTOP_AM4_V2,
                   FAM_APU_FT6_NEW, FAM_APU_FT6_NEW_GENERIC, FAM_APU_FT6_OLD,
                   FAM_APU_FP6, FAM_APU_FT5_FP5, FAM_APU_VANGOGH,
                   FAM_DRAGON_FIRE, FAM_ZEN2_DESKTOP, FAM_NO_CO, FAM_UNKNOWN]


def _read_cpu_info():
    """读注册表 ProcessorNameString + Identifier('AMD64 Family 26 Model 68 Stepping 0')。"""
    name, ident = "", ""
    try:
        k = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                           r"HARDWARE\DESCRIPTION\System\CentralProcessor\0")
        try:
            name, _ = winreg.QueryValueEx(k, "ProcessorNameString")
        except Exception:
            pass
        try:
            ident, _ = winreg.QueryValueEx(k, "Identifier")
        except Exception:
            pass
        winreg.CloseKey(k)
    except Exception:
        pass
    return (name or "").strip(), (ident or "").strip()


def _parse_identifier(ident):
    """'AMD64 Family 26 Model 68 Stepping 0' -> (0x1A, 0x44); 失败返回 None。"""
    m = re.search(r"Family\s+(\d+)\s+Model\s+(\d+)", ident, re.I)
    if not m:
        return None
    try:
        return int(m.group(1)), int(m.group(2))
    except Exception:
        return None


def _has_battery_device():
    """Return the Windows hardware-class signal for shared AMD CPUIDs."""
    if os.name != "nt":
        return False
    try:
        class SYSTEM_POWER_STATUS(ctypes.Structure):
            _fields_ = [
                ("ACLineStatus", ctypes.c_ubyte),
                ("BatteryFlag", ctypes.c_ubyte),
                ("BatteryLifePercent", ctypes.c_ubyte),
                ("Reserved", ctypes.c_ubyte),
                ("BatteryLifeTime", ctypes.c_uint32),
                ("BatteryFullLifeTime", ctypes.c_uint32),
            ]
        status = SYSTEM_POWER_STATUS()
        if not ctypes.windll.kernel32.GetSystemPowerStatus(ctypes.byref(status)):
            return False
        return status.BatteryFlag != 128 and status.BatteryLifePercent != 255
    except Exception:
        return False


def detect_amd_family():
    """按型号名 + CPUID(Family/Model) 精确选择家族, 不再靠单一名字正则两级猜。
    顺序: ①解析 CPUID ②桌面 17h ③HX+指定CPUID ④桌面 Zen4/5 ⑤APU表 ⑥未知降级。"""
    name, ident = _read_cpu_info()
    name_u = name.upper()
    fm = _parse_identifier(ident)
    f = m = None
    if fm:
        f, m = fm
    # Shared desktop/mobile CPUIDs use a platform-class signal.
    # HX follows UXTU; a Windows battery device covers generic portable systems.
    # UXTU Family.cs uses CPUName.Contains("HX"); keep the same generic rule
    # so names with a space such as "HX 370" are covered as well.
    is_hx_name = "HX" in name_u
    has_battery = _has_battery_device()
    # ① 桌面 17h (Zen/Zen+/Zen2): Matisse M71h / CastlePeak M31h / Summit M01h / Pinnacle M08h 等。
    #    SMU Type=CPU2, 无 STAPM/FAST/SLOW 命令, 功率走 PBO PPT/EDC/TDC。
    #    必须优先于 X 后缀兜底, 否则 1800X/2700X/3900X/3970X 会被误判为 Zen4/5 桌面
    #    (向 TYPE_CPU2 SMU 发 0x4F/0x3E/0x5F 命令 → 无效/no-op, 历史回归教训)。
    #    排除 17h APU 型号 (Renoir/Lucienne/Raven/Picasso/Dali/VanGogh/Mendocino)。
    if f == 0x17:
        if m in (0x01, 0x08):
            return FAM_DESKTOP_AM4_V1
        if m == 0x71:
            return FAM_DESKTOP_AM4_V2
    # ③ 名称只能做否决/辅助，不能单独决定协议族：CPUID 缺失时宁可 Unknown，绝不把任意 X/X3D 发送到 AM5 邮箱。
    # Threadripper 也只有在 CPUID 已确认 17h 时才允许归入 Zen2 Desktop；名称不是协议事实。
    if re.search(r"THREADRIPPER", name_u) and f == 0x17:
        return FAM_ZEN2_DESKTOP
    # ③ Dragon/Fire Range follows UXTU's HX rule for the shared CPUID.
    if is_hx_name and ((f == 0x19 and m == 0x61) or (f == 0x1A and m == 0x44)):
        return FAM_DRAGON_FIRE
    # ④ CPUID 表 (ryzenadj lib/cpuid.c 同源 + UXTU 26.2.0 Family.cs 对照)
    if fm:
        if f == 0x1A:                 # Zen5
            if m in (0x20, 0x24, 0x60, 0x68): # Strix Point / Krackan Point / Point 2
                return FAM_APU_FT6_NEW_GENERIC
            if m == 0x70:                       # UXTU Strix Halo / FT6 family
                return FAM_APU_FT6_NEW
            if m == 0x44:                           # Granite Ridge / Fire Range / portable shared CPUID
                return FAM_APU_FT6_NEW_GENERIC if has_battery else FAM_DESKTOP_AM5
        elif f == 0x19:               # Zen3/Zen4
            if m == 0x21:                           # Vermeer
                return FAM_DESKTOP_AM4_V2
            if m in (0x3F, 0x44, 0x74, 0x75, 0x78, 0x7C): # Rembrandt / Phoenix / Hawk Point / Hawk Point 2
                return FAM_APU_FT6_OLD
            if m == 0x50:                           # Cezanne (FP6)
                return FAM_APU_FP6
            if m == 0x61:                           # Raphael / Dragon Range 共享 CPUID
                return FAM_DRAGON_FIRE if is_hx_name else FAM_DESKTOP_AM5
        elif f == 0x17:               # Zen/Zen+/Zen2
            if m in (0x60, 0x68):                   # Renoir / Lucienne (FP6)
                return FAM_APU_FP6
            if m in (0x11, 0x12, 0x18, 0x20, 0x50): # Raven / Picasso / FireFlight / Dali / Pollock
                return FAM_APU_FT5_FP5
            if m in (0x90, 0x91):                   # VanGogh
                return FAM_APU_VANGOGH
            if m in (0xA0,):                        # Mendocino
                return FAM_APU_FT6_OLD
    return FAM_UNKNOWN


def resolve_amd_tdp_config(p=None):
    """Resolve only an explicit UXTU family mapping (plus validated local exceptions).

    Several AMD families share readable response registers while using different
    commands. A non-zero mailbox response therefore cannot identify a protocol.
    Unknown models remain fail-closed until UXTU or a validated mapping names them.
    """
    cfg = detect_amd_family()
    if cfg is FAM_UNKNOWN:
        log("  [family] Unknown AMD -> 共享 mailbox 无法只读区分协议, 拒绝猜测写入")
    return cfg

# ---------- Intel: PawnIO 常驻后端 ----------
MCHBAR_CANDIDATES = ["0xfedc0000", "0xfed10000"]  # 兼容旧探测记录；新后端优先用 IntelMCHBAR.bin
INTEL_MAX_W = 300
MSR_RAPL_POWER_UNIT = 0x606  # RAPL 功率单位 (bits[3:0] exponent)
MSR_PKG_POWER_LIMIT = 0x610  # PL1/PL2 功率限制
MSR_PKG_POWER_LOCK  = 1 << 63  # bit63: 锁定位, 置位后无法写入 (直至 reset)
MSR_CORE_RATIO = 0x1AD
INTEL_MSR_READ = "ioctl_read_msr"
INTEL_MSR_WRITE = "ioctl_write_msr"
INTEL_MCHBAR_GET = "ioctl_get_mchbar_addr"
INTEL_MCHBAR_READ_DWORD = "ioctl_read_dword"
# Intel MSR 互斥体名称 (与 UXTU 26.2.0 IntelPawnIO.cs 一致: Global\Access_MSR)
INTEL_MSR_MUTEX = "Global\\Access_MSR"

# 门忙 HRESULT (Newko cI: 0x8007054F / 2147943759)
# 递增退避重试: 10/25/50/100/200ms, 最多 5 次 (并发抢门实测 1/12 会触发, 单次 25ms 不够)
GATE_BUSY_HR = 0x8007054F
GATE_RETRY_DELAYS = (0.010, 0.025, 0.050, 0.100, 0.200)
GATE_RETRY_DELAY = GATE_RETRY_DELAYS[1]  # 兼容旧引用 (25ms)

# 跨进程命名互斥体: 多个 YeManTdpCtl 进程/UXTU/HWiNFO 并发调 SMU 时排队串行,
# 直接消除"并发抢门"导致的 0x8007054F / RyzenSMU 加载竞争。
# 互斥体名称与 UXTU 26.2.0 保持一致 (Global\Access_PCI), 确保跨工具互斥。
SMU_GATE_MUTEX = "Global\\Access_PCI"


class _SmuGate:
    """上下文管理器: 获取命名互斥体。
    - WAIT_OBJECT_0 / WAIT_ABANDONED → 成功持有, 允许硬件操作
    - WAIT_TIMEOUT / 其他错误 → 拒绝硬件操作 (不再无锁降级)
    - __exit__ 只在成功持有时调用 ReleaseMutex
    """

    def __init__(self, timeout_ms=10000):
        self._k32 = _kernel32_handles()
        self._h = self._k32.CreateMutexW(None, False, SMU_GATE_MUTEX)
        self._held = False
        self._closed = False
        if self._h:
            r = self._k32.WaitForSingleObject(self._h, timeout_ms)
            if r in (0, 128):  # WAIT_OBJECT_0 / WAIT_ABANDONED_0
                self._held = True
            elif r == 0x00000102:  # WAIT_TIMEOUT
                log("  [gate] SMU 互斥等待超时, 拒绝无锁硬件访问")
                self.close()
            else:
                log("  [gate] SMU 互斥等待失败 result=0x%X, 拒绝硬件访问" % r)
                self.close()

    @property
    def acquired(self):
        return self._held

    def close(self):
        if self._closed:
            return
        self._closed = True
        try:
            if self._h:
                if self._held:
                    self._k32.ReleaseMutex(self._h)
                    self._held = False
                self._k32.CloseHandle(self._h)
        except Exception:
            pass
        self._h = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

CREATE_NO_WINDOW = 0x08000000

LOG_FILE = os.path.join(
    os.environ.get("TEMP", os.environ.get("TMP", r"C:\Windows\Temp")),
    "yeman_pawnio_install.log",
)

def log(*msg):
    line = f"{time.strftime('%Y-%m-%d %H:%M:%S')} " + " ".join(str(m) for m in msg)
    try:
        with open(LOG_FILE, "a", encoding="utf-8", errors="ignore") as f:
            f.write(line + "\n")
    except Exception:
        pass
    try:
        print(line, flush=True)
    except Exception:
        pass

def _run_logged(cmd, timeout=120, shell=False):
    log(f"RUN: {' '.join(str(c) for c in cmd)}")
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, errors="ignore",
                           timeout=timeout, shell=shell, creationflags=CREATE_NO_WINDOW)
        log(f"RC: {r.returncode}")
        if r.stdout:
            for l in r.stdout.splitlines():
                log(f"OUT: {l}")
        if r.stderr:
            for l in r.stderr.splitlines():
                log(f"ERR: {l}")
        return r
    except Exception as e:
        log(f"EXCEPTION: {e}")
        return None

# ---------- CPU 厂商识别 (Newko: ProcessorNameString 按 Intel/AMD 判定) ----------
def detect_vendor():
    # 手动覆盖文件优先 (与 HTA 约定一致)
    if os.path.exists(r"C:\SOFT\YeMan\PowerControl\AMD.txt"):   return "amd"
    if os.path.exists(r"C:\SOFT\YeMan\PowerControl\intel.txt"): return "intel"
    try:
        k = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                           r"HARDWARE\DESCRIPTION\System\CentralProcessor\0")
        try:
            name, _ = winreg.QueryValueEx(k, "ProcessorNameString")
        except Exception:
            name = ""
        try:
            vid, _ = winreg.QueryValueEx(k, "VendorIdentifier")
        except Exception:
            vid = ""
        winreg.CloseKey(k)
        blob = f"{name} {vid}"
        if "Intel" in blob: return "intel"
        if "AMD" in blob:   return "amd"
    except Exception:
        pass
    return "unknown"

# ---------- PawnIO 封装 ----------
class Pawn:
    def __init__(self, dll):
        self.lib = ctypes.WinDLL(dll)
        L = self.lib
        L.pawnio_open.argtypes = [ctypes.POINTER(ctypes.c_void_p)]; L.pawnio_open.restype = ctypes.c_long
        L.pawnio_load.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]; L.pawnio_load.restype = ctypes.c_long
        L.pawnio_execute.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                                     ctypes.POINTER(ctypes.c_uint64), ctypes.c_size_t,
                                     ctypes.POINTER(ctypes.c_uint64), ctypes.c_size_t,
                                     ctypes.POINTER(ctypes.c_size_t)]; L.pawnio_execute.restype = ctypes.c_long
        L.pawnio_close.argtypes = [ctypes.c_void_p]; L.pawnio_close.restype = ctypes.c_long
        self.h = ctypes.c_void_p()

    def open(self):
        return self.lib.pawnio_open(ctypes.byref(self.h))

    def load(self, binpath):
        blob = open(binpath, "rb").read()
        buf = (ctypes.c_ubyte * len(blob)).from_buffer_copy(blob)
        return self.lib.pawnio_load(self.h, buf, len(blob))

    def execute(self, name, inp_list, out_count):
        n = len(inp_list)
        inp = (ctypes.c_uint64 * (n if n else 1))(*inp_list)
        out = (ctypes.c_uint64 * (out_count if out_count else 1))()
        ret = ctypes.c_size_t(0)
        hr = self.lib.pawnio_execute(self.h, name.encode("ascii"),
                                     inp, n, out, out_count, ctypes.byref(ret))
        # PawnIO 返回实际输出项数；不能把未写入的 ctypes 零初始化缓冲区当成有效零值。
        actual = min(int(ret.value), int(out_count)) if out_count else 0
        return hr, list(out[:actual])

    def close(self):
        try: self.lib.pawnio_close(self.h)
        except Exception: pass

# ==========================================================================
# AMD: Newko 原生路径 — ioctl_send_smu_command (高层 API, 内核侧完成邮箱交互)
# ==========================================================================
def _smu_exec(p, name, args, out_count):
    """Newko O2 同款: 执行失败且 HRESULT=0x8007054F(门忙) -> 按 10/25/50/100/200ms 递增退避重试。"""
    hr, out = p.execute(name, args, out_count)
    for delay in GATE_RETRY_DELAYS:
        if (hr & 0xFFFFFFFF) != GATE_BUSY_HR:
            break
        log("  [gate] 硬件门忙(0x8007054F), %dms 后重试" % int(delay * 1000))
        time.sleep(delay)
        hr, out = p.execute(name, args, out_count)
    return hr, out

def smu_send(p, cmd, value_mw=0):
    """Newko Zh 同款: 参数 [cmd, value, 0...] 补齐 7 项, out_count=6。"""
    args = [cmd, value_mw]
    while len(args) < 7:
        args.append(0)
    return _smu_exec(p, "ioctl_send_smu_command", args, 6)

def _amd_open():
    """打开 PawnIO 并加载 Newko 同款 RyzenSMU.bin, 返回 (Pawn, rc)。rc: 0/3/4。"""
    p = Pawn(DLL)
    rc = p.open()
    if rc != 0:
        log("FATAL: pawnio_open 0x%08X" % (rc & 0xFFFFFFFF)); p.close(); return None, 3
    if p.load(RYZEN_SMU) != 0:
        log("FATAL: 加载 RyzenSMU.bin 失败:", RYZEN_SMU); p.close(); return None, 4
    return p, 0

# --- MP1 邮箱直写 (仅桌面端回退路径; 与 v1 相同, 用户态轮询响应寄存器) ---
def _smu_rd(p, reg):
    """读取 SMU 寄存器；任何非零 HRESULT 都必须失败，不能消费未定义输出。"""
    hr, out = p.execute("ioctl_read_smu_register", [reg], 1)
    for delay in GATE_RETRY_DELAYS:
        if (hr & 0xFFFFFFFF) != GATE_BUSY_HR:
            break
        log("  [gate] 读寄存器门忙(0x%X), %dms 后重试" % (reg, int(delay * 1000)))
        time.sleep(delay)
        hr, out = p.execute("ioctl_read_smu_register", [reg], 1)
    if (hr & 0xFFFFFFFF) != 0 or not out:
        log("  [SMU] 读寄存器失败 reg=0x%X HRESULT=0x%08X" % (reg, hr & 0xFFFFFFFF))
        return None
    return out[0]


def _smu_wr(p, reg, val):
    """写 SMU 寄存器；失败记录日志并返回 False，不抛出异常。"""
    hr, out = p.execute("ioctl_write_smu_register", [reg, val & 0xFFFFFFFF], 0)
    for delay in GATE_RETRY_DELAYS:
        if (hr & 0xFFFFFFFF) != GATE_BUSY_HR:
            break
        log("  [gate] 写寄存器门忙(0x%X), %dms 后重试" % (reg, int(delay * 1000)))
        time.sleep(delay)
        hr, out = p.execute("ioctl_write_smu_register", [reg, val & 0xFFFFFFFF], 0)
    if (hr & 0xFFFFFFFF) != 0:
        log("  [SMU] 写寄存器失败 reg=0x%X HRESULT=0x%08X" % (reg, hr & 0xFFFFFFFF))
        return False
    return True

def _resolve_mailbox_addr(cfg, transport):
    """根据 transport 解析 cfg 中的邮箱地址。
    transport: 'mp1' → msg/rsp/arg; 'psmu' → psmu_msg/psmu_rsp/psmu_arg;
               'rsmu' → rsmu_msg/rsmu_rsp/rsmu_arg。
    返回 (msg_addr, rsp_addr, arg_addr) 或 (None, None, None)。
    """
    if transport == "mp1":
        return cfg.get("msg"), cfg.get("rsp"), cfg.get("arg")
    elif transport in ("psmu", "rsmu"):
        # UXTU 高层称 RSMU，底层 Socket 配置字段称 PSMU；两者使用同一组地址。
        # 优先显式 rsmu_*，不存在时兼容 psmu_*。
        if transport == "rsmu" and cfg.get("rsmu_msg"):
            return cfg.get("rsmu_msg"), cfg.get("rsmu_rsp"), cfg.get("rsmu_arg")
        return cfg.get("psmu_msg"), cfg.get("psmu_rsp"), cfg.get("psmu_arg")
    return None, None, None


def _send_mailbox(p, cmd, arg0, cfg, transport="mp1", tries=300):
    """泛化 SMU 邮箱写入: 支持 MP1/PSMU/RSMU transport。
    根据 transport 选取正确的 msg/rsp/arg 地址对, 执行标准 SMU mailbox 协议。
    状态非 1 或 transport 不可用 → 返回 0xFE (上层转 rc=6), 不抛异常。
    """
    msg_addr, rsp_addr, arg_addr = _resolve_mailbox_addr(cfg, transport)
    if not msg_addr or not rsp_addr or not arg_addr:
        log("  [family] %s transport=%s 缺邮箱地址 (msg=%s rsp=%s arg=%s) → 跳过本命令"
            % (cfg.get("name", "unknown"), transport, msg_addr, rsp_addr, arg_addr))
        return 0xFE
    MP1_MSG, MP1_RSP, MP1_ARG = msg_addr, rsp_addr, arg_addr
    ready = False
    for _ in range(tries):
        rsp = _smu_rd(p, MP1_RSP)
        if rsp is None:
            return 0xFE
        if rsp != 0:
            ready = True
            break
        time.sleep(0.001)
    if not ready:
        log("  [SMU] %s mailbox ready 等待超时 rsp持续为0, 拒绝覆盖在途事务" % transport)
        return 0xFC
    if not _smu_wr(p, MP1_RSP, 0):
        return 0xFE
    if not _smu_wr(p, MP1_ARG, arg0 & 0xFFFFFFFF):
        return 0xFE
    for i in range(1, 6):
        if not _smu_wr(p, MP1_ARG + i * 4, 0):
            return 0xFE
    if not _smu_wr(p, MP1_MSG, cmd & 0xFFFFFFFF):
        return 0xFE
    r = 0
    completed = False
    for _ in range(tries):
        r = _smu_rd(p, MP1_RSP)
        if r is None:
            return 0xFE
        if r != 0:
            completed = True
            break
        time.sleep(0.001)
    if not completed:
        log("  [SMU] %s mailbox 命令响应超时 cmd=0x%X" % (transport, cmd))
        return 0xFC
    if r > 0xFF:
        log("  [SMU] %s mailbox 异常响应超出状态字节 cmd=0x%X rsp=0x%X → 拒绝" %
            (transport, cmd, r))
        return 0xFE
    return int(r)


def _send_mp1(p, cmd, arg0, cfg, tries=300):
    """MP1 邮箱快捷入口 (向后兼容)。内部调用 _send_mailbox(transport='mp1')。"""
    return _send_mailbox(p, cmd, arg0, cfg, "mp1", tries)

def _amd_set_mp1(p, cfg, stapm_mw, fast_mw, slow_mw):
    """对明确声明为 MP1/STAPM feature 的家族写入；状态非 1 或 transport 异常均失败。"""
    if cfg.get("power_feature") not in ("stapm_ppt", "desktop_ppt") or cfg.get("transport") != "mp1" or not cfg.get("tdp_supported", False):
        log("  [family] %s 不是已验证的 MP1 power feature, 禁止套用该写入器" % cfg["name"])
        return 6
    fail = 0
    for cmd, name, val in ((cfg["stapm"], "stapm", stapm_mw),
                           (cfg["fast"],  "fast",  fast_mw),
                           (cfg["slow"],  "slow",  slow_mw)):
        st = _send_mp1(p, cmd, int(val), cfg)
        log("  AMD-MP1 %-5s (0x%02X) %d mW -> %s" % (name, cmd, val, "OK" if st == 1 else "FAIL(status=%s)" % st))
        if st != 1:
            fail += 1
        mirror_cmd = cfg.get("mirror_" + name)
        mirror_transport = cfg.get("mirror_transport")
        if mirror_cmd is not None and mirror_transport:
            st_mirror = _send_mailbox(p, mirror_cmd, int(val), cfg, mirror_transport)
            log("  AMD-%s %-5s (0x%02X) %d mW -> %s" %
                (mirror_transport.upper(), name, mirror_cmd, val,
                 "OK" if st_mirror == 1 else "FAIL(status=%s)" % st_mirror))
            if st_mirror != 1:
                fail += 1
    return 0 if fail == 0 else 6

def amd_set_ppt_with(p, cfg, ppt_mw):
    """普通单值 set 在 AM4 桌面仅写 UXTU 的全部 PPT 命令。

    单个瓦数没有 TDC/EDC 电流语义，不能把同一个数值复制到这两个参数。
    显式 ``pbo`` 命令仍使用 amd_set_pbo_with 写完整 PPT/TDC/EDC。
    """
    if (cfg.get("tdp_mode") != "ppt" or cfg.get("power_feature") != "desktop_ppt" or
            not cfg.get("verified_transport") or not cfg.get("tdp_supported") or
            not cfg.get("pbo_supported") or not cfg.get("pbo_transport_verified")):
        log("  [PPT] %s capability/transport 未验证 -> 拒绝写入" % cfg.get("name", "unknown"))
        return 6
    ops = tuple((transport, cmd) for label, transport, cmd in cfg.get("pbo_ops", ())
                if label == "PPT")
    if not ops or any(transport not in ("mp1", "rsmu", "psmu") or cmd is None
                      for transport, cmd in ops):
        log("  [PPT] %s 没有有效的 UXTU PPT 命令表 -> 拒绝写入" % cfg.get("name", "unknown"))
        return 6
    fail = 0
    for transport, cmd in ops:
        st = _send_mailbox(p, int(cmd), int(ppt_mw), cfg, transport)
        log("  AMD-PPT %-5s (0x%02X) %d mW -> %s" %
            (transport.upper(), int(cmd), ppt_mw,
             "OK" if st == 1 else "FAIL(status=%s)" % st))
        if st != 1:
            fail += 1
    return 0 if fail == 0 else 6


def amd_set_with(p, cfg, stapm_mw, fast_mw, slow_mw):
    """用已打开的 Pawn 句柄按已识别家族写普通单值功耗（daemon 复用句柄时用）。"""
    # 1) verified_transport: 没经过本机实机验证的平台, 拒绝写入
    if not cfg.get("verified_transport", False) or not cfg.get("tdp_supported", False):
        log("  [family] %s TDP capability 未验证 -> 拒绝写入" % cfg["name"])
        return 6
    if cfg.get("tdp_mode") == "ppt":
        return amd_set_ppt_with(p, cfg, stapm_mw)
    if cfg.get("tdp_mode") != "stapm":
        log("  [family] %s tdp_mode=%s 未定义普通 set 路由, 拒绝写入" %
            (cfg["name"], cfg.get("tdp_mode")))
        return 6
    # 2) MP1 邮箱完整: msg/rsp/arg 任一缺失即视为不可写, 提前返回 6 (不再让 _send_mp1 抛 RuntimeError)
    for k in ("msg", "rsp", "arg", "stapm", "fast", "slow"):
        if cfg.get(k) is None:
            log("  [family] %s cfg.%s 缺失, 拒绝写入" % (cfg["name"], k))
            return 6
    log("  [family] %-18s MP1 msg=0x%X rsp=0x%X arg=0x%X stapm=0x%02X fast=0x%02X slow=0x%02X"
        % (cfg["name"], cfg["msg"], cfg["rsp"], cfg["arg"],
           cfg["stapm"], cfg["fast"], cfg["slow"]))
    rc2 = _amd_set_mp1(p, cfg, stapm_mw, fast_mw, slow_mw)
    if rc2 != 0 and cfg.get("allow_legacy_fallback", False):
        log("  [fallback] MP1 直写失败, 仅对明确声明兼容的 APU 家族尝试 Newko 高层 API")
        fail = 0
        for cmd, name, val in ((SMU_SET_STAPM_LIMIT, "stapm", stapm_mw),
                               (SMU_SET_FAST_LIMIT, "fast", fast_mw),
                               (SMU_SET_SLOW_LIMIT, "slow", slow_mw)):
            hr, out = smu_send(p, cmd, int(val))
            if hr == 0:
                log("  AMD %-5s (cmd=%d) %d mW -> OK" % (name, cmd, val))
            else:
                log("  AMD %-5s (cmd=%d) %d mW -> FAIL HRESULT=0x%08X" % (name, cmd, val, hr & 0xFFFFFFFF))
                fail += 1
        rc2 = 0 if fail == 0 else 6
    elif rc2 != 0:
        log("  [fallback] family=%s 禁止跨协议回退, 保持失败 rc=6" % cfg["name"])
    return rc2


# --- AM5 PBO PPT/TDC/EDC (P1) ---
# 桌面 AM5 的 PBO 功率限制独立于 STAPM/FAST/SLOW 三窗口。
# UXTU 26.2.0: PPT=0x3E(mp1)/0x56(rsmu), TDC=0x3C(mp1)/0x57(rsmu), EDC=0x3D(mp1)/0x58(rsmu)
# YeMan 串行发送 MP1 和 RSMU 命令, 每条独立检查 status; 不复制 UXTU 的并发双发。
# 未实现 PBO transport 的平台 → 返回 rc=6 并提示, 绝不向 TYPE_CPU2 SMU 发 AM5 命令。

def amd_set_pbo_with(p, cfg, ppt_mw, tdc_ma, edc_ma):
    """用已打开的 Pawn 句柄写入 AM5 PBO PPT/TDC/EDC。
    仅 Desktop-AM5 (power_feature=desktop_ppt, verified_transport=True) 支持。
    串行: MP1 PPT → RSMU PPT → MP1 TDC → RSMU TDC → MP1 EDC → RSMU EDC。
    """
    if (cfg.get("power_feature") != "desktop_ppt" or not cfg.get("verified_transport") or
            not cfg.get("tdp_supported") or not cfg.get("pbo_supported") or
            not cfg.get("pbo_transport_verified")):
        log("  [PBO] %s PBO capability/transport 未完成真机验证 → 拒绝写入" % cfg.get("name", "unknown"))
        return 6
    ops = cfg.get("pbo_ops")
    if not ops:
        legacy = {
            "PPT": (("mp1", cfg.get("pbo_ppt_mp1")), ("rsmu", cfg.get("pbo_ppt_rsmu"))),
            "TDC": (("mp1", cfg.get("pbo_tdc_mp1")), ("rsmu", cfg.get("pbo_tdc_rsmu"))),
            "EDC": (("mp1", cfg.get("pbo_edc_mp1")), ("rsmu", cfg.get("pbo_edc_rsmu"))),
        }
        ops = tuple((label, transport, cmd) for label, pairs in legacy.items()
                    for transport, cmd in pairs if cmd is not None)
    if not ops:
        log("  [PBO] %s 没有 UXTU 对应命令表 → 写入前拒绝" % cfg.get("name", "unknown"))
        return 6
    invalid = ["%s/%s/%s" % (label, transport, cmd) for label, transport, cmd in ops
               if label not in ("PPT", "TDC", "EDC") or transport not in ("mp1", "rsmu", "psmu") or cmd is None]
    if invalid:
        log("  [PBO] %s 命令表无效: %s" % (cfg.get("name", "unknown"), ",".join(invalid)))
        return 6
    fail = 0
    values = {"PPT": (ppt_mw, "mW"), "TDC": (tdc_ma, "mA"), "EDC": (edc_ma, "mA")}
    for label, transport, cmd in ops:
        val, unit = values[label]
        st = _send_mailbox(p, int(cmd), int(val), cfg, transport)
        log("  AMD-PBO %-3s %-5s (0x%02X) %d %s → %s" %
            (label, transport.upper(), int(cmd), val, unit,
             "OK" if st == 1 else "FAIL(status=%s)" % st))
        if st != 1:
            fail += 1
    return 0 if fail == 0 else 6


def amd_set_pbo(ppt_mw, tdc_ma, edc_ma, dry=False):
    """AMD AM5 桌面 PBO PPT/TDC/EDC 一次性 CLI 入口。"""
    log("  AMD PBO ppt=%d mW tdc=%d mA edc=%d mA" % (ppt_mw, tdc_ma, edc_ma))
    cfg = detect_amd_family()
    if (cfg.get("power_feature") != "desktop_ppt" or not cfg.get("pbo_supported") or
            not cfg.get("pbo_transport_verified")):
        log("  [PBO] %s capability 未验证，dry-run/真实写入均拒绝" % cfg.get("name", "unknown"))
        return 6
    if dry:
        log("  [dry-run] PBO capability 已验证，未实际写入"); return 0
    gate = _SmuGate()
    if not gate.acquired:
        log("  [gate] 未获取 SMU 互斥体, 拒绝 PBO 写入")
        return 6
    with gate:
        p, rc = _amd_open()
        if rc != 0:
            return rc
        try:
            cfg = resolve_amd_tdp_config(p)
            log("  [family] %s power_feature=%s" % (cfg["name"], cfg.get("power_feature")))
            return amd_set_pbo_with(p, cfg, ppt_mw, tdc_ma, edc_ma)
        finally:
            p.close()


def amd_set(stapm_mw, fast_mw, slow_mw, dry=False):
    """写 AMD STAPM/FAST/SLOW (mW)。
    完全采用 UXTU/ryzenadj 的 MP1 邮箱直写逻辑: 按家族选用正确的 MP1 地址 + 命令号
    (Strix Halo/Phoenix 用 0x14/0x15/0x16 + 0x3b10928/0x3b10978/0x3b10998;
     桌面 AM5 用 0x4f/0x3e/0x5f + 0x3B10530/0x3B1057C/0x3B109C4)。
    MP1 直写失败时兜底回退 Newko 高层 ioctl_send_smu_command(仅旧 APU 兼容)。"""
    log("  AMD stapm=%d fast=%d slow=%d mW" % (stapm_mw, fast_mw, slow_mw))
    if dry:
        log("  [dry-run] 未实际写入"); return 0
    gate = _SmuGate()
    if not gate.acquired:
        log("  [gate] 未获取 SMU 互斥体, 拒绝 TDP 写入")
        return 6
    with gate:
        p, rc = _amd_open()
        if rc != 0:
            return rc
        try:
            cfg = resolve_amd_tdp_config(p)
            log("  [family] %s power_feature=%s transport=%s verified=%s"
                % (cfg["name"], cfg.get("power_feature"), cfg.get("transport"), cfg.get("verified_transport")))
            if cfg is FAM_UNKNOWN:
                log("  [family] Unknown AMD -> 无安全 UXTU 协议, 拒绝写入")
                return 6
            return amd_set_with(p, cfg, stapm_mw, fast_mw, slow_mw)
        finally:
            p.close()

def amd_get():
    """通道自检 + 读当前 STAPM/FAST/SLOW 实测值(PM 表, best-effort)。"""
    gate = _SmuGate()
    if not gate.acquired:
        log("  [gate] 未获取 SMU 互斥体, 拒绝 AMD get")
        return 6
    with gate:
        p, rc = _amd_open()
        if rc != 0:
            return rc
        try:
            hr, out = _smu_exec(p, "ioctl_get_smu_version", [], 1)
            if hr == 0 and out:
                v = out[0]
                log("  AMD SMU version = 0x%08X (%d.%d.%d) 通道正常"
                    % (v, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF))
            else:
                log("  AMD ioctl_get_smu_version FAIL HRESULT=0x%08X" % (hr & 0xFFFFFFFF))
                return 6
            # 实测当前限制值 (PM 表, best-effort); 偏移 0x0/0x8/0x10 = stapm/fast/slow
            # ★仅 APU 家族打印: 该偏移是 ryzenadj 的 APU 表布局, 桌面 AM5 的 PM 表布局不同, 读出数值无意义
            cfg = resolve_amd_tdp_config(p)
            log("  [family] AMD %s" % cfg["name"])
            if cfg.get("stapm") is None:
                log("  [family] 该家族无 STAPM/FAST/SLOW 命令, TDP 调节暂不支持 (桌面 17h 走 PBO PPT/EDC/TDC)")
            if cfg.get("pm_readback"):
                try:
                    import struct as _struct
                    p.execute("ioctl_resolve_pm_table", [], 0)
                    _, tbuf = p.execute("ioctl_read_pm_table", [], 256)
                    raw = b"".join(_struct.pack("<Q", x & 0xFFFFFFFFFFFFFFFF) for x in tbuf)
                    n = len(raw) // 4
                    if n >= 5:
                        fl = _struct.unpack("<%df" % n, raw[:n * 4])
                        log("  [实测] STAPM=%.0f mW  FAST=%.0f mW  SLOW=%.0f mW (PM 表偏移 0x0/0x8/0x10)"
                            % (fl[0], fl[2], fl[4]))
                except Exception as e:
                    log("  [实测] PM 表读取跳过: %s" % e)
            else:
                log("  [实测] 桌面 CPU 跳过 PM 表回读 (表布局不同)")
            return 0
        finally:
            p.close()

# ==========================================================================
# CPU 降压 (Undervolt) — AMD Curve Optimizer + Intel MSR 0x150
# ==========================================================================

UV_AMD_MIN, UV_AMD_MAX = -60, 60
UV_INTEL_MIN, UV_INTEL_MAX = -150, 0

MSR_VOLTAGE_CTL = 0x150
# Intel OC mailbox: high dword = RUN_BUSY(bit31) | param2/VF point[23:16] |
# domain[15:8] | command[7:0]；low dword为 payload/返回数据。
INTEL_OC_RUN_BUSY = 1 << 31
INTEL_OC_CMD_GET_VF = 0x10
INTEL_OC_CMD_SET_VF = 0x11
INTEL_OC_OFFSET_MASK = 0xFFE00000   # payload bits[31:21], signed 11-bit / 1.024
INTEL_OC_TIMEOUT_S = 0.050
INTEL_OC_COMPLETION_REASONS = {
    0x01: "invalid_command",
    0x02: "invalid_domain",
    0x03: "invalid_data",
    0x13: "request_rejected_by_firmware",
}
# UXTU 26.2.0 的 plane/domain：Core=0, iGPU=1, Cache/Ring=2, SystemAgent=4。
# iGPU/SA 未经本机实测 → capability matrix 默认只允许 core/cache。
UV_PLANE_DOMAINS = {
    "core": 0,
    "igpu": 1,
    "cache": 2,
    "sa": 4,
}
# 保留旧常量语义供诊断/兼容：这些是 SET 命令高双字。
UV_PLANES = {
    name: INTEL_OC_RUN_BUSY | (domain << 8) | INTEL_OC_CMD_SET_VF
    for name, domain in UV_PLANE_DOMAINS.items()
}
UV_PLANES_SAFE = ("core", "cache")


def _uv_mv_to_data32(mv):
    """把 mV 编码到 OC mailbox payload bits[31:21]（signed 11-bit, 1/1.024mV）。"""
    raw11 = int(round(mv * 1.024)) & 0x7FF
    return (raw11 << 21) & INTEL_OC_OFFSET_MASK


def _msr_data_to_mv(v64):
    """从 OC mailbox 64位返回值的 payload bits[31:21] 解析 offset(mV)。"""
    if v64 is None:
        return None
    raw11 = (int(v64) >> 21) & 0x7FF
    if raw11 & 0x400:
        raw11 -= 0x800
    return int(round(raw11 / 1.024))


def _intel_oc_command_word(domain, command):
    return INTEL_OC_RUN_BUSY | ((int(domain) & 0xFF) << 8) | (int(command) & 0xFF)


def _intel_oc_completion_reason(completion):
    if completion is None:
        return "no_completion"
    code = int(completion) & 0xFF
    return INTEL_OC_COMPLETION_REASONS.get(code, "firmware_rejected_0x%02X" % code)


def _intel_oc_wait_idle_raw(backend, timeout_s=INTEL_OC_TIMEOUT_S):
    """事务锁内轮询 MSR 0x150，直到 RUN_BUSY 清零；返回最终64位值或 None。"""
    deadline = time.monotonic() + max(0.001, float(timeout_s))
    while time.monotonic() < deadline:
        value = backend.read_msr_raw(MSR_VOLTAGE_CTL)
        if value is None:
            return None
        if not ((int(value) >> 63) & 1):
            return int(value)
        time.sleep(0.001)
    log("  [Intel OC] mailbox RUN_BUSY 超时")
    return None


def _intel_oc_mailbox_raw(backend, domain, command, payload=0):
    """在已持有 Global\\Access_MSR 事务锁时执行一次 OC mailbox 命令。
    返回 {ok, value, data, completion, reason}；completion为高双字低8位，0=成功。
    """
    idle = _intel_oc_wait_idle_raw(backend)
    if idle is None:
        return {"ok": False, "reason": "busy_or_read_failed", "completion": None}
    word = _intel_oc_command_word(domain, command)
    request = ((word & 0xFFFFFFFF) << 32) | (int(payload) & 0xFFFFFFFF)
    if not backend.write_msr_raw(MSR_VOLTAGE_CTL, request):
        return {"ok": False, "reason": "write_failed", "completion": None}
    value = _intel_oc_wait_idle_raw(backend)
    if value is None:
        return {"ok": False, "reason": "completion_timeout", "completion": None}
    completion = (int(value) >> 32) & 0xFF
    if completion != 0:
        completion_reason = _intel_oc_completion_reason(completion)
        log("  [Intel OC] domain=%d cmd=0x%02X completion=0x%02X" %
            (domain, command, completion))
        log("  [Intel OC] completion_reason=%s" % completion_reason)
        return {"ok": False, "reason": "firmware_rejected", "completion": completion,
                "completion_reason": completion_reason,
                "value": int(value), "data": int(value) & 0xFFFFFFFF}
    return {"ok": True, "completion": 0, "value": int(value),
            "data": int(value) & 0xFFFFFFFF}


def _intel_oc_get_plane_raw(backend, plane):
    domain = UV_PLANE_DOMAINS.get(plane)
    if domain is None:
        return {"ok": False, "reason": "unknown_plane", "completion": None}
    return _intel_oc_mailbox_raw(backend, domain, INTEL_OC_CMD_GET_VF, 0)


def _intel_oc_direct_write_raw(backend, domain, payload):
    """Write an Intel OC request using the UXTU Intel backend contract.

    UXTU sends the SET request directly through PawnIO and does not issue a
    preceding GET or interpret the mailbox response as an ioctl result. The
    response can contain a firmware completion code even though PawnIO
    accepted the MSR write, so completion is diagnostic only on this path.
    """
    word = _intel_oc_command_word(domain, INTEL_OC_CMD_SET_VF)
    request = ((word & 0xFFFFFFFF) << 32) | (int(payload) & 0xFFFFFFFF)
    if not backend.write_msr_raw(MSR_VOLTAGE_CTL, request):
        return {
            "ok": False,
            "reason": "write_failed",
            "ioctl_written": False,
            "request": request,
        }
    log("  [Intel UV] UXTU direct write domain=%d cmd=0x%08X payload=0x%08X" %
        (domain, word, int(payload) & 0xFFFFFFFF))
    # Match UXTU's post-write settling interval. Do not read 0x150 here:
    # reading the mailbox response is not part of UXTU's write contract and
    # can turn a valid direct write into a false failure on some firmware.
    time.sleep(0.100)
    return {
        "ok": True,
        "reason": "uxtu_direct_write",
        "ioctl_written": True,
        "firmware_accepted": None,
        "readback": False,
        "verification": "uxtu_direct_write_no_mailbox_readback",
        "request": request,
    }

def _parse_msr_data(stdout):
    """匹配 KX 'Msr Data : 0xHHHHHHHH 0xLLLLLLLL'，返回完整 64 位。"""
    if not stdout:
        return None
    m = re.search(r"Msr Data\s*:\s*0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)", stdout)
    if m:
        return (int(m.group(1), 16) << 32) | int(m.group(2), 16)
    return None

# ----- AMD -----
def _co_arg_encode(offset, cfg):
    """CO 参数编码(按家族表 co_enc):
    - u32: 32 位有符号偏移的无符号表示 (桌面 AM5): -20 -> 0xFFFFFFEC,
      0 -> 0x00000000, +20 -> 0x00000014。
    - u20: 20 位有符号偏移的无符号表示 (APU/Strix Halo 等): -20 -> 0xFFFEC,
      0 -> 0x00000, +20 -> 0x00014。

    这与 UXTU/RyzenAdj 的 uint32 参数契约一致：上层语义仍是有符号
    CO offset，负值表示降压、正值表示加压；本层只按目标 SMU 通道宽度截断。
    """
    if cfg.get("co_enc") == "u20":
        return offset & 0xFFFFF
    return offset & 0xFFFFFFFF

def amd_uv_set_with(p, cfg, offset):
    """用已保持的 AMD PawnIO 句柄写入全核 CO；daemon 与 CLI 共用同一底层路径。"""
    if not (UV_AMD_MIN <= offset <= UV_AMD_MAX):
        log("  CO 超范围(%d~%d): %s" % (UV_AMD_MIN, UV_AMD_MAX, offset))
        return 2
    if not cfg.get("verified_transport", False) or not cfg.get("co_supported") or "all_core" not in cfg.get("co_scopes", ()):
        log("  AMD(%s) CO capability 未验证 -> 拒绝写入" % cfg.get("name", "unknown"))
        return 6
    if cfg.get("coall") is None:
        log("  AMD(%s) 缺少 CO all-core 命令 -> 拒绝写入" % cfg.get("name", "unknown"))
        return 6
    arg = _co_arg_encode(offset, cfg)
    st = _send_mp1(p, cfg["coall"], arg, cfg)
    log("  AMD(%s) set-coall(0x%02X) %d -> arg=0x%05X %s" %
        (cfg["name"], cfg["coall"], offset, arg, "OK" if st == 1 else "FAIL(status=%s)" % st))
    return 0 if st == 1 else 6


def amd_uv_set(offset, dry=False):
    """全核 Curve Optimizer。offset: -60~+60；负值降压，0 还原，正值加压。"""
    if not (UV_AMD_MIN <= offset <= UV_AMD_MAX):
        log("  CO 超范围(%d~%d):" % (UV_AMD_MIN, UV_AMD_MAX), offset)
        return 2
    cfg = detect_amd_family()
    if not cfg.get("co_supported") or "all_core" not in cfg.get("co_scopes", ()):
        log("  AMD(%s) set-coall 不支持: capability 未验证" % cfg["name"])
        return 6
    if dry:
        arg = _co_arg_encode(offset, cfg)
        log("  [dry-run] AMD(%s) set-coall arg=0x%05X" % (cfg["name"], arg))
        return 0
    gate = _SmuGate()
    if not gate.acquired:
        log("  [gate] 未获取 SMU 互斥体, 拒绝 CO 写入")
        return 6
    with gate:
        p, rc = _amd_open()
        if rc != 0:
            return rc
        try:
            return amd_uv_set_with(p, cfg, offset)
        finally:
            p.close()

def amd_uv_probe():
    """无破坏探测 AMD CO capability。

    AMD MP1 没有可靠 CO 读回；禁止再用 set-coall(0) 做探测，因为会改变当前 CO。
    本函数只验证家族 capability + SMU 通道可读，不写任何 CO 参数。
    """
    gate = _SmuGate()
    if not gate.acquired:
        return {"vendor": "amd", "supported": False, "reason": "smu_gate_timeout", "current": 0,
                "readback": False, "current_semantics": "unavailable"}
    with gate:
        p, rc = _amd_open()
        if rc != 0:
            return {"vendor": "amd", "supported": False, "reason": "pawnio_open_failed", "current": 0,
                    "readback": False, "current_semantics": "unavailable"}
        try:
            cfg = detect_amd_family()
            if not cfg.get("co_supported") or "all_core" not in cfg.get("co_scopes", ()):
                return {"vendor": "amd", "supported": False,
                        "reason": "family_no_verified_curve_optimizer", "current": 0,
                        "readback": False, "current_semantics": "unavailable"}
            hr, out = _smu_exec(p, "ioctl_get_smu_version", [], 1)
            if (hr & 0xFFFFFFFF) == 0 and out:
                return {"vendor": "amd", "supported": True, "family": cfg["name"], "current": 0,
                        "readback": False, "current_semantics": "not_readable",
                        "probe_mode": "capability_plus_readonly_channel"}
            return {"vendor": "amd", "supported": False, "reason": "smu_channel_read_failed", "current": 0,
                    "readback": False, "current_semantics": "unavailable"}
        finally:
            p.close()

def amd_get_with(p, cfg):
    """daemon 使用常驻 AMD PawnIO 句柄读取通道状态，不重新 open/close。"""
    if p is None or not cfg or not cfg.get("verified_transport", False):
        return 6
    try:
        hr, out = _smu_exec(p, "ioctl_get_smu_version", [], 1)
        if (hr & 0xFFFFFFFF) != 0 or not out:
            log("  AMD daemon get: SMU version 读取失败 HRESULT=0x%08X" % (hr & 0xFFFFFFFF))
            return 6
        v = out[0]
        log("  AMD daemon SMU version = 0x%08X (%d.%d.%d)" %
            (v, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF))
        if cfg.get("pm_readback"):
            try:
                import struct as _struct
                hr1, _ = _smu_exec(p, "ioctl_resolve_pm_table", [], 0)
                if (hr1 & 0xFFFFFFFF) == 0:
                    hr2, tbuf = _smu_exec(p, "ioctl_read_pm_table", [], 256)
                    if (hr2 & 0xFFFFFFFF) == 0:
                        raw = b"".join(_struct.pack("<Q", x & 0xFFFFFFFFFFFFFFFF) for x in tbuf)
                        n = len(raw) // 4
                        if n >= 5:
                            fl = _struct.unpack("<%df" % n, raw[:n * 4])
                            log("  AMD daemon 实测 STAPM=%.0f FAST=%.0f SLOW=%.0f mW" %
                                (fl[0], fl[2], fl[4]))
            except Exception as e:
                log("  AMD daemon PM 表读取跳过: %s" % e)
        return 0
    except Exception as e:
        log("  AMD daemon get 失败: %s" % e)
        return 6


# AMD: CO 当前仅实现 all-core；per-CCD/per-core 暂不暴露为支持。
# Intel: MSR 0x1AD 只做读取诊断；MCHBAR 未提供 IntelMCHBAR.bin 时保持关闭。
def intel_uv_set_pawn(backend, mv, planes=None, dry=False):
    """Set Intel voltage offsets using UXTU's direct PawnIO write path.

    The complete operation holds Global\\Access_MSR and writes each selected
    plane directly to MSR 0x150. PawnIO ioctl success is the write result;
    mailbox completion and readback are intentionally not used as a failure
    gate because UXTU does not use them for this operation.
    """
    if not (UV_INTEL_MIN <= mv <= UV_INTEL_MAX):
        log("  Intel 电压偏移超范围(%d~0mV):" % UV_INTEL_MIN, mv)
        return {"ok": False, "error": "range", "rc": 2, "planes": {}}
    offset_bits = _uv_mv_to_data32(mv)
    target_planes = planes if planes is not None else list(UV_PLANES_SAFE)
    unknown = [name for name in target_planes if name not in UV_PLANE_DOMAINS]
    if unknown:
        return {"ok": False, "error": "unknown_planes", "rc": 2, "planes": {}}
    if dry:
        globally_unverified = [name for name in target_planes if name not in UV_PLANES_SAFE]
        if globally_unverified:
            log("  [Intel UV dry-run] 未验证 plane=%s → 拒绝" % ",".join(globally_unverified))
            return {"ok": False, "error": "unsupported_planes", "rc": 6, "planes": {}}
        cap = detect_intel_capability()
        log("  [dry-run] Intel UV model=%s verified_uv=%s planes=%s offset=0x%08X" %
            (cap["name"], cap.get("uv_core"), target_planes, offset_bits))
        return {"ok": True, "rc": 0, "dry": True, "planes": {}}
    cap = detect_intel_capability()
    unsupported = [name for name in target_planes if not cap.get("uv_" + name, False)]
    if unsupported:
        _intel_cap_log(cap)
        log("  [Intel UV] 未验证 plane=%s → 拒绝写入" % ",".join(unsupported))
        return {"ok": False, "error": "unsupported_planes", "rc": 6, "planes": {}}
    if backend is None or backend.msr is None:
        return {"ok": False, "error": "no_backend", "rc": 3, "planes": {}}
    result = {"ok": True, "rc": 0, "planes": {}}
    if not backend._enter_tx():
        return {"ok": False, "error": "msr_gate_timeout", "rc": 6, "planes": {}}
    try:
        for name in target_planes:
            domain = UV_PLANE_DOMAINS[name]
            written = _intel_oc_direct_write_raw(backend, domain, offset_bits)
            if not written.get("ok"):
                result["planes"][name] = {
                    "ioctl_written": False,
                    "firmware_accepted": None,
                    "readback": False,
                    "readback_mv": None,
                    "reason": written.get("reason"),
                }
                result["ok"] = False
                continue
            result["planes"][name] = {
                "ioctl_written": True,
                "firmware_accepted": None,
                "readback": False,
                "readback_mv": None,
                "matches_requested": None,
                "reason": "uxtu_direct_write",
                "verification": "uxtu_direct_write_no_mailbox_readback",
            }
            log("  Intel UV %s %dmV -> ioctl_written=True firmware_completion=not_checked" %
                (name, mv))
        result["rc"] = 0 if result["ok"] else 6
        return result
    except Exception as e:
        log("  Intel UV OC mailbox 失败: %s" % e)
        return {"ok": False, "error": str(e), "rc": 6, "planes": result.get("planes", {})}
    finally:
        backend._leave_tx()


def intel_uv_set(mv, dry=False):
    """Intel FIVR offset via UXTU-compatible PawnIO MSR 0x150。
    返回 0 成功 / 2 范围错 / 3 驱动错 / 6 写入错 (backward compatible rc)。"""
    if dry:
        result = intel_uv_set_pawn(None, mv, None, True)
        return int(result.get("rc", 0 if result.get("ok") else 6))
    backend = IntelPawnBackend(); rc = backend.open()
    if rc != 0: return rc
    try:
        result = intel_uv_set_pawn(backend, mv, None, False)
        return int(result.get("rc", 0 if result.get("ok") else 6))
    finally:
        backend.close()

def intel_uv_probe():
    """无破坏探测 Intel UV capability：对 core/cache 发送只读 GET mailbox 命令。
    不发送 SET；只有 RUN_BUSY 正常完成且 completion=0 才报告支持，并返回每 plane 当前 offset。
    """
    if not is_admin():
        return {"vendor": "intel", "supported": False, "reason": "need_admin", "current": 0,
                "readback": False}
    cap = detect_intel_capability()
    if not cap.get("uv_core"):
        return {"vendor": "intel", "supported": False, "reason": "model_no_verified_uv",
                "family": cap.get("name"), "current": 0, "readback": False}
    backend = IntelPawnBackend()
    rc = backend.open()
    if rc != 0:
        return {"vendor": "intel", "supported": False, "reason": "pawnio_open_failed",
                "family": cap.get("name"), "current": 0, "readback": False}
    try:
        if not backend._enter_tx():
            return {"vendor": "intel", "supported": False, "reason": "msr_gate_timeout",
                    "family": cap.get("name"), "current": 0, "readback": False}
        try:
            planes = {}
            for name in UV_PLANES_SAFE:
                if not cap.get("uv_" + name, False):
                    continue
                r = _intel_oc_get_plane_raw(backend, name)
                if not r.get("ok"):
                    return {"vendor": "intel", "supported": False,
                            "reason": "oc_mailbox_" + str(r.get("reason")),
                            "completion": r.get("completion"), "family": cap.get("name"),
                            "current": 0, "readback": False}
                planes[name] = _msr_data_to_mv(r.get("value"))
            return {"vendor": "intel", "supported": bool(planes), "family": cap.get("name"),
                    "current": planes.get("core", 0), "planes": planes, "readback": True,
                    "current_semantics": "oc_mailbox_per_plane_offset",
                    "probe_mode": "readonly_get_mailbox_completion_checked"}
        finally:
            backend._leave_tx()
    finally:
        backend.close()

# ---------- Intel: PawnIO 常驻后端（UXTU IntelPawnIO.cs 对等实现） ----------
INTEL_MSR_READ = "ioctl_read_msr"
INTEL_MSR_WRITE = "ioctl_write_msr"

class IntelPawnBackend:
    """daemon 启动时加载 IntelMSR.bin，后续 MSR 操作复用同一 PawnIO 句柄。
    MSR 互斥体与 UXTU 26.2.0 一致 (Global\\Access_MSR)，确保跨工具互斥。
    事务级锁: 整个 RMW(read→modify→write→readback) 持有同一互斥体。"""

    def __init__(self):
        self.msr = None
        self._lock = None
        self._tx_depth = 0          # 事务嵌套计数

    def open(self):
        if not INTEL_MSR_BIN or not os.path.exists(INTEL_MSR_BIN):
            log("FATAL: IntelMSR.bin 不存在: %s" % INTEL_MSR_BIN)
            return 4
        try:
            self.msr = Pawn(DLL)
            rc = self.msr.open()
            if rc != 0:
                log("FATAL: Intel PawnIO open 0x%08X" % (rc & 0xFFFFFFFF))
                self.msr.close(); self.msr = None
                return 3
            rc = self.msr.load(INTEL_MSR_BIN)
            if rc != 0:
                log("FATAL: 加载 IntelMSR.bin 失败 0x%08X" % (rc & 0xFFFFFFFF))
                self.msr.close(); self.msr = None
                return 4
            self._k32 = _kernel32_handles()
            self._lock = self._k32.CreateMutexW(None, False, INTEL_MSR_MUTEX)
            if not self._lock:
                log("FATAL: 创建 Intel MSR mutex 失败: %s" % INTEL_MSR_MUTEX)
                self.msr.close(); self.msr = None
                return 3
            log("Intel PawnIO 句柄已保持, module=%s mutex=%s" % (INTEL_MSR_BIN, INTEL_MSR_MUTEX))
            return 0
        except Exception as e:
            log("FATAL: Intel PawnIO 初始化异常: %s" % e)
            self.close()
            return 3

    def _enter(self):
        """获取 MSR 互斥体；创建失败必须 fail-closed，禁止无锁访问。"""
        if not self._lock:
            log("  [Intel] Global\\Access_MSR 创建失败, 拒绝无锁 MSR 访问")
            return False
        r = self._k32.WaitForSingleObject(self._lock, 1000)
        if r not in (0, 128):
            log("  [Intel] MSR mutex 等待失败 result=0x%X" % r)
            return False
        return True

    def _leave(self):
        if self._lock:
            try:
                self._k32.ReleaseMutex(self._lock)
            except Exception:
                pass

    def _enter_tx(self):
        """事务级锁: 整个 RMW 操作持有同一个互斥体。
        _tx_depth 支持嵌套 (read/write 可能被 RMW 内部多次调用)。"""
        if self._tx_depth == 0:
            if not self._enter():
                return False
        self._tx_depth += 1
        return True

    def _leave_tx(self):
        self._tx_depth = max(0, self._tx_depth - 1)
        if self._tx_depth == 0:
            self._leave()

    def read_msr_raw(self, msr):
        """直接读 MSR (不获取互斥体, 由调用方保证在事务内)。"""
        hr, out = self.msr.execute(INTEL_MSR_READ, [int(msr)], 1)
        if (hr & 0xFFFFFFFF) != 0 or not out:
            log("  [Intel] 读 MSR 0x%X 失败 HRESULT=0x%08X" % (msr, hr & 0xFFFFFFFF))
            return None
        return int(out[0])

    def write_msr_raw(self, msr, value):
        """直接写 MSR (不获取互斥体, 由调用方保证在事务内)。"""
        hr, _ = self.msr.execute(INTEL_MSR_WRITE,
                                 [int(msr), int(value) & 0xFFFFFFFFFFFFFFFF], 0)
        if (hr & 0xFFFFFFFF) != 0:
            log("  [Intel] 写 MSR 0x%X 失败 HRESULT=0x%08X" % (msr, hr & 0xFFFFFFFF))
            return False
        return True

    def read_msr(self, msr):
        """读单个 MSR (自动获取/释放互斥体)。"""
        if not self._enter():
            return None
        try:
            return self.read_msr_raw(msr)
        finally:
            self._leave()

    def write_msr(self, msr, value):
        """写单个 MSR (自动获取/释放互斥体)。"""
        if not self._enter():
            return False
        try:
            return self.write_msr_raw(msr, value)
        finally:
            self._leave()

    def close(self):
        try:
            if self.msr:
                self.msr.close()
        except Exception:
            pass
        self.msr = None
        if self._lock:
            try:
                self._k32.CloseHandle(self._lock)
            except Exception:
                pass
            self._lock = None


# ---------- Intel capability matrix (P1) ----------
# 按 Family/Model 建立候选能力矩阵, 再叠加运行时探测 (rapl_read/rapl_write/rapl_locked/
# uv_core/uv_cache/uv_igpu/uv_sa/ratio)。未知型号输出明确 capability 诊断, 未验证功能
# 不能因厂商为 Intel 就自动放行。
# 已实测平台: Intel Desktop (RPL), 356H (MTL-H), 358H (LNL-H) → 0x610 路径已验证。
_INTEL_CAP_MATRIX = {
    # 字段必须分开声明；igpu/sa 未真机验证时保持 False。
    (0x06, 0x97):  {"name": "AlderLake-S",  "rapl": True, "uv_core": True, "uv_cache": True},
    (0x06, 0xB7):  {"name": "RaptorLake-S", "rapl": True, "uv_core": True, "uv_cache": True},
    (0x06, 0x9A):  {"name": "AlderLake-P",  "rapl": True, "uv_core": True, "uv_cache": True},
    (0x06, 0xBA):  {"name": "RaptorLake-P", "rapl": True, "uv_core": True, "uv_cache": True},
    (0x06, 0xAA):  {"name": "MeteorLake-H", "rapl": True, "uv_core": True, "uv_cache": True},
    (0x06, 0xBD):  {"name": "LunarLake-H",  "rapl": True, "uv_core": True, "uv_cache": True},
    (0x06, 0xC6):  {"name": "ArrowLake-S",  "rapl": True, "uv_core": False, "uv_cache": False},
    (0x06, 0xCD):  {"name": "ArrowLake-H",  "rapl": True, "uv_core": False, "uv_cache": False},
    (0x06, 0xCF):  {"name": "ArrowLake-U",  "rapl": True, "uv_core": False, "uv_cache": False},
}


def _read_intel_cpuid():
    """读注册表 Intel Identifier → (family, model); 失败返回 None。"""
    _, ident = _read_cpu_info()
    fm = _parse_identifier(ident)
    if fm:
        return fm
    return None


def detect_intel_capability():
    """返回 Intel CPU 静态候选能力；未知平台默认 fail-closed。
    Family/Model 表优先；已实测 356H/358H 额外按完整型号名放行 RAPL/core/cache。
    iGPU/SA 始终保持 False，直到对应 plane 真机验证。"""
    result = {
        "family": None, "model": None, "name": "Unknown-Intel",
        "rapl_supported": False, "rapl_locked": False,
        "uv_core": False, "uv_cache": False, "uv_igpu": False, "uv_sa": False,
        "ratio": False,
    }
    cpu_name, _ = _read_cpu_info()
    fm = _read_intel_cpuid()
    if fm:
        f, m = fm
        result["family"] = f; result["model"] = m
        cap = _INTEL_CAP_MATRIX.get((f, m), {})
        result["name"] = cap.get("name", "Intel-F%d-M%02X" % (f, m))
        result["rapl_supported"] = cap.get("rapl", False)
        result["uv_core"] = cap.get("uv_core", False)
        result["uv_cache"] = cap.get("uv_cache", False)
        result["uv_igpu"] = cap.get("uv_igpu", False)
        result["uv_sa"] = cap.get("uv_sa", False)
        result["ratio"] = cap.get("ratio", False)
    # 用户已实测 356H/358H 的 0x610 路径；以型号名补充，避免 CPUID 表版本差异。
    if re.search(r"\b(?:356H|358H)\b", cpu_name.upper()):
        result["name"] = cpu_name.strip() or result["name"]
        result["rapl_supported"] = True
        result["uv_core"] = True
        result["uv_cache"] = True
    return result


def _intel_cap_log(cap):
    log("  [Intel] capability: %s f=%s m=%s rapl=%s uv=%s ratio=%s" %
        (cap["name"], cap.get("family"), cap.get("model"),
         cap["rapl_supported"], cap.get("uv_core"), cap.get("ratio")))


def _rapl_power_unit_w(backend):
    """读取 MSR 0x606 bits[3:0] 获取 RAPL power unit (W/unit)。
    返回 (unit_w, exponent)；读取失败返回 (None, None)，禁止固定 1/8W 降级写入。
    若已在事务内则 raw 读取，否则使用自动互斥读取。"""
    try:
        if backend is None or backend.msr is None:
            return None, None
        if getattr(backend, "_tx_depth", 0) > 0:
            raw = backend.read_msr_raw(MSR_RAPL_POWER_UNIT)
        else:
            raw = backend.read_msr(MSR_RAPL_POWER_UNIT)
        if raw is None:
            log("  [Intel] 无法读取 MSR 0x606, 拒绝使用猜测 power unit")
            return None, None
        exponent = int(raw) & 0xF
        unit_w = 1.0 / (1 << exponent)
        log("  [Intel] MSR 0x606 power unit exponent=%d (1/%.0f W)" %
            (exponent, (1 << exponent)))
        return unit_w, exponent
    except Exception as e:
        log("  [Intel] MSR 0x606 读取异常: %s, 拒绝使用猜测 power unit" % e)
        return None, None


def _intel_power_limit_value(backend, pl1_w, pl2_w, old_msr=None):
    """计算 Intel PL1/PL2 目标值 (mW→raw), 并维护 OEM 非目标位 (Clamp/Time Window 等)。
    使用动态 RAPL power unit (MSR 0x606 bits[3:0]) 替代固定 *8。
    若提供 old_msr (64-bit), 则走 RMW 只修改目标位; 否则新建。"""
    o = _clamp_w(pl1_w)
    f = _clamp_w(pl2_w)
    if o is None or f is None:
        return None, None, None, None
    if f <= o:
        if f < INTEL_MAX_W:
            f = o + 1
        else:
            o = max(0, f - 1)
    unit_w, _ = _rapl_power_unit_w(backend)
    if unit_w is None:
        return None, None, None, None
    raw_pl1 = int(round(o / unit_w))
    raw_pl2 = int(round(f / unit_w))
    # PL1 bits 0..14, PL2 bits 32..46; enable bits 15/47
    pl1_field = (raw_pl1 & 0x7FFF) | 0x8000
    pl2_field = (raw_pl2 & 0x7FFF) | 0x8000
    new_value = (pl2_field << 32) | pl1_field
    # RMW: 保留 old_msr 的非目标位 (Clamp/Time Window/Lock/保留字段)
    if old_msr is not None:
        # 目标 mask: PL1 [0:15] + PL2 [32:47]
        rmw_mask = 0xFFFF | (0xFFFF << 32)
        value = (int(old_msr) & ~rmw_mask) | (new_value & rmw_mask)
    else:
        value = new_value
    return value, o, f, unit_w


def intel_set_pawn(backend, pl1_w, pl2_w, dry=False):
    """Intel TDP: capability → read 0x606 → 0x610 RMW → lock → write → readback。"""
    requested_pl1, requested_pl2 = _clamp_w(pl1_w), _clamp_w(pl2_w)
    if requested_pl1 is None or requested_pl2 is None:
        log("  Intel TDP 参数无效")
        return 2
    cap = detect_intel_capability()
    if dry:
        # dry-run 允许跨机预演，不接触硬件；未知平台只警告，不伪装为已支持。
        o, f = requested_pl1, requested_pl2
        if f <= o:
            f = min(INTEL_MAX_W, o + 1)
        log("  [dry-run] Intel(%s, verified_rapl=%s) PL1=%dW PL2=%dW; 实际 raw 需目标机读取 MSR 0x606" %
            (cap["name"], cap.get("rapl_supported"), o, f))
        return 0
    if backend is None or backend.msr is None:
        log("  Intel TDP: backend 无效")
        return 3
    # 事务级锁: 整个 RMW 持有 Global\Access_MSR
    if not backend._enter_tx():
        log("  [Intel TDP] 无法获取 MSR 事务锁")
        return 6
    try:
        # 1) 读当前 MSR 0x610
        old = backend.read_msr_raw(MSR_PKG_POWER_LIMIT)
        if old is None:
            log("  [Intel TDP] 无法读取 MSR 0x610")
            return 6
        # UXTU uses the runtime MSR path rather than a model allow-list.
        cap["rapl_supported"] = True
        log("  [Intel TDP] MSR 0x610 可读，按运行时 RAPL 能力继续")
        # 2) 检查 bit63 Lock
        if int(old) & MSR_PKG_POWER_LOCK:
            log("  [Intel TDP] MSR 0x610 bit63 Lock 已置位, 拒绝写入 (需 reboot 解锁)")
            return 6
        # 3) 计算目标值 (RMW 保留 OEM 位)
        packed = _intel_power_limit_value(backend, requested_pl1, requested_pl2, old)
        if packed[0] is None:
            log("  [Intel TDP] MSR 0x606 power unit 不可用 → 拒绝写入")
            return 6
        value, o, f, unit_w = packed
        log("  Intel (PawnIO) PL1=%dW PL2=%dW raw_PL1=0x%04X raw_PL2=0x%04X unit=%.3fW" %
            (o, f, (value & 0x7FFF), ((value >> 32) & 0x7FFF), unit_w))
        log("  [Intel TDP] MSR 0x610 old=0x%016X → new=0x%016X (RMW preserved OEM fields)" %
            (old, value))
        # 4) 写入
        if not backend.write_msr_raw(MSR_PKG_POWER_LIMIT, value):
            log("  [Intel TDP] MSR 0x610 写入未成功")
            return 6
        # 5) 读回校验 (按目标 mask)
        actual = backend.read_msr_raw(MSR_PKG_POWER_LIMIT)
        if actual is None:
            log("  [Intel TDP] MSR 0x610 写后读回失败")
            return 6
        rmw_mask = 0xFFFF | (0xFFFF << 32)
        if (int(actual) & rmw_mask) != (value & rmw_mask):
            log("  [Intel TDP] MSR 0x610 功耗字段读回不一致: expected=0x%016X actual=0x%016X" %
                (value & rmw_mask, int(actual) & rmw_mask))
            return 6
        log("  [Intel TDP] MSR 0x610 RMW/读回 OK: 0x%016X (OEM fields preserved)" % actual)
        return 0
    except Exception as e:
        log("  [Intel TDP] RMW 失败: %s" % e)
        return 6
    finally:
        backend._leave_tx()


def intel_get_pawn(backend, dry=False):
    if dry:
        log("  [dry-run] Intel PawnIO 跳过读取")
        return 0
    if backend is None or backend.msr is None:
        return 3
    try:
        value = backend.read_msr(MSR_PKG_POWER_LIMIT)
        if value is None:
            log("  Intel PawnIO 读取 MSR 0x610 失败")
            return 6
        unit_w, _ = _rapl_power_unit_w(backend)
        if unit_w is None:
            log("  Intel PawnIO 读取 MSR 0x606 失败")
            return 6
        raw_pl1 = value & 0x7FFF
        raw_pl2 = (value >> 32) & 0x7FFF
        pl1 = raw_pl1 * unit_w
        pl2 = raw_pl2 * unit_w
        locked = "LOCKED" if (int(value) & MSR_PKG_POWER_LOCK) else "unlocked"
        log("  Intel PawnIO MSR 0x610 = 0x%016X PL1=%.1fW PL2=%.1fW (unit=%.3fW) %s" %
            (value, pl1, pl2, unit_w, locked))
        ratio = backend.read_msr(MSR_CORE_RATIO)
        if ratio is not None:
            log("  Intel MSR 0x1AD CORE_RATIO = 0x%016X" % ratio)
        if INTEL_MCHBAR_BIN and os.path.exists(INTEL_MCHBAR_BIN):
            log("  IntelMCHBAR.bin 已发现，但当前未启用未验证 MMIO 写入路径")
        else:
            log("  IntelMCHBAR.bin 未提供，MCHBAR 功能保持关闭")
        return 0
    except Exception as e:
        log("  Intel PawnIO 读取失败: %s" % e)
        return 6

def _parse_msr_raw_value(value):
    """解析 restore --raw 的十六进制/十进制完整 MSR 值。"""
    try:
        text = str(value).strip().lower()
        if text.startswith("0x"):
            return int(text, 16)
        return int(text, 0)
    except Exception:
        return None


def intel_restore_raw(backend, raw_value, dry=False):
    """按 Intel 验证报告要求，精确恢复 MSR 0x610 的完整 64 位快照。

    restore <W> 是 UXTU 风格的 PL1/PL2 重新编码，会丢失原始 time-window/保留字段；
    restore --raw <hex> 才用于恢复验证前保存的完整 MSR 值。
    若 bit63 Lock 置位，报告 locked 并拒绝写入。
    """
    value = _parse_msr_raw_value(raw_value)
    if value is None:
        log("  Intel raw MSR 参数无效: %s" % raw_value)
        return 2
    if not (0 <= value <= 0xFFFFFFFFFFFFFFFF):
        log("  Intel raw MSR 必须是 0..0xFFFFFFFFFFFFFFFF")
        return 2
    if value & MSR_PKG_POWER_LOCK:
        log("  Intel raw restore 目标值含 bit63 Lock，拒绝写入以避免锁死至重启")
        return 6
    log("  Intel raw restore MSR 0x610 <- 0x%016X" % value)
    if dry:
        log("  [dry-run] 未实际写入")
        return 0
    if backend is None or backend.msr is None:
        return 3
    if not backend._enter_tx():
        log("  [Intel raw restore] 无法获取 MSR 事务锁")
        return 6
    try:
        # 先读当前值检查 Lock
        old = backend.read_msr_raw(MSR_PKG_POWER_LIMIT)
        if old is None:
            log("  [Intel raw restore] 无法预读 MSR 0x610/确认 Lock 状态 → 拒绝写入")
            return 6
        if int(old) & MSR_PKG_POWER_LOCK:
            log("  [Intel raw restore] MSR 0x610 bit63 Lock 置位 (当前值=0x%016X), 拒绝写入" % old)
            return 6
        if not backend.write_msr_raw(MSR_PKG_POWER_LIMIT, value):
            log("  [Intel raw restore] 写入未成功")
            return 6
        actual = backend.read_msr_raw(MSR_PKG_POWER_LIMIT)
        if actual is None:
            log("  [Intel raw restore] 写后读回失败")
            return 6
        if actual != value:
            log("  [Intel raw restore] 读回不一致: expected=0x%016X actual=0x%016X" % (value, actual))
            return 6
        log("  [Intel raw restore] 完整 MSR 读回一致: 0x%016X" % actual)
        return 0
    except Exception as e:
        log("  [Intel raw restore] 写入失败: %s" % e)
        return 6
    finally:
        backend._leave_tx()


def intel_restore_raw_once(raw_value, dry=False):
    if dry:
        return intel_restore_raw(None, raw_value, True)
    backend = IntelPawnBackend()
    rc = backend.open()
    if rc != 0:
        return rc
    try:
        return intel_restore_raw(backend, raw_value, False)
    finally:
        backend.close()


# ---------- Intel 兼容计算与一次性 CLI 包装 ----------
def _clamp_w(w):
    """钳制 Intel 功耗到 0..300W 并四舍五入。"""
    try:
        w = float(w)
    except Exception:
        return None
    if w != w:
        return None
    return max(0, min(INTEL_MAX_W, int(round(w))))

def intel_set(pl1_w, pl2_w, dry=False):
    if dry:
        return intel_set_pawn(None, pl1_w, pl2_w, True)
    backend = IntelPawnBackend()
    rc = backend.open()
    if rc != 0:
        return rc
    try:
        return intel_set_pawn(backend, pl1_w, pl2_w, False)
    finally:
        backend.close()

def intel_get(dry=False):
    if dry:
        return intel_get_pawn(None, True)
    backend = IntelPawnBackend()
    rc = backend.open()
    if rc != 0:
        return rc
    try:
        return intel_get_pawn(backend, False)
    finally:
        backend.close()

# ---------- 统一写入入口 (无早退 / 无调度 / 无重试循环) ----------
def apply_tdp(vendor, watts, intel_delta=1, dry=False):
    if vendor == "amd":
        mw = int(round(watts * 1000))
        return amd_set(mw, mw, mw, dry)
    if vendor == "intel":
        # UXTU 同款: PL1 = W-1 (保证 PL1<PL2, 规避 PL1==PL2 边界),
        # PL2 = PL1 + intel_delta (默认 1 => PL2=W)。intel_delta 仅作为 PL2 额外余量。
        pl1 = max(0, int(round(watts)) - 1)
        pl2 = pl1 + max(0, int(intel_delta))
        return intel_set(pl1, pl2, dry)
    return 5

# ---------- 命令解析 ----------
def parse_opt(args, name, default=None, has_val=True):
    if name in args:
        i = args.index(name)
        if has_val and i + 1 < len(args):
            v = args[i + 1]; del args[i:i + 2]; return v
        del args[i:i + 1]; return True
    return default

# ---------- PawnIO 缺失时: 弹窗 + 静默安装 ----------
def msgbox(text, title="YeMan TDP", style=0):
    try:
        ctypes.windll.user32.MessageBoxW(0, str(text), str(title), int(style))
    except Exception:
        pass

MB_INFO, MB_WARN, MB_ERR = 0x40, 0x30, 0x10

def pawnio_present():
    if not os.path.exists(DLL):
        return False
    try:
        p = Pawn(DLL)
        if p.open() != 0:
            return False
        p.close()
        return True
    except Exception:
        return False

def run_setup_silent(setup):
    flagsets = [["-silent", "-install"], ["-silent"], ["/S"]]
    for fs in flagsets:
        log(f"Trying install flags: {fs}")
        r = _run_logged([setup] + fs, timeout=180)
        if r is None:
            continue
        if r.returncode == 0:
            log(f"Install with {fs} succeeded")
            return True
        if r.returncode == 3010:
            log(f"Install with {fs} returned 3010 (success, reboot required)")
            return True
        svc = _run_logged(["sc", "query", "PawnIO"], timeout=30)
        if svc and svc.returncode == 0:
            log(f"Install with {fs} returned {r.returncode}, but service exists -> treat as success")
            return True
    log("All install attempts failed")
    return False

def _delete_reg_key(root, subkey):
    if not winreg:
        return
    try:
        winreg.DeleteKey(root, subkey)
    except FileNotFoundError:
        pass
    except Exception:
        pass

def _enum_pnp_drivers():
    try:
        r = subprocess.run(["pnputil.exe", "/enum-drivers"],
                           capture_output=True, text=True, errors="ignore", timeout=60,
                           creationflags=CREATE_NO_WINDOW)
        return r.stdout.splitlines() if r.stdout else []
    except Exception as e:
        log(f"pnputil /enum-drivers failed: {e}")
        return []

def _pawnio_driver_package_present():
    for line in _enum_pnp_drivers():
        if "pawnio.inf" in line.lower():
            log("DriverStore 仍有 pawnio.inf 残留包")
            return True
    return False

def _remove_pawnio_driver_package():
    lines = _enum_pnp_drivers()
    oem = None
    for line in lines:
        m = re.match(r".*Published Name\s*:\s*(oem\d+\.inf).*", line, re.IGNORECASE)
        if m:
            oem = m.group(1)
        if "pawnio.inf" in line.lower() and oem:
            log(f"Removing DriverStore package {oem}")
            _run_logged(["pnputil.exe", "/delete-driver", oem, "/uninstall", "/force"], timeout=120)
            break

def run_setup_remove(setup):
    ok = False
    _run_logged(["sc", "stop", "PawnIO"], timeout=30)
    r = _run_logged([setup, "-silent", "-uninstall"], timeout=120)
    if r and r.returncode == 0:
        ok = True
    un = os.path.join(os.path.dirname(DLL), "uninstall.exe")
    if os.path.exists(un):
        _run_logged([un, "/S"], timeout=120)
        ok = True
    _run_logged(["sc", "delete", "PawnIO"], timeout=30)
    _remove_pawnio_driver_package()
    _delete_reg_key(winreg.HKEY_LOCAL_MACHINE, r"SYSTEM\CurrentControlSet\Services\PawnIO")
    _delete_reg_key(winreg.HKEY_LOCAL_MACHINE, r"SYSTEM\ControlSet001\Services\PawnIO")
    try:
        if os.path.exists(os.path.dirname(DLL)):
            shutil.rmtree(os.path.dirname(DLL))
            log(f"Removed {os.path.dirname(DLL)}")
    except Exception as e:
        log(f"Could not remove install dir: {e}")
    return ok

def _check_testsigning():
    r = _run_logged(["bcdedit", "/enum", "{current}"], timeout=30)
    if r and r.stdout:
        for line in r.stdout.splitlines():
            if "testsigning" in line.lower():
                log(f"bcdedit: {line.strip()}")
                if "yes" in line.lower():
                    return True
                elif "no" in line.lower():
                    return False
    return None

def ensure_pawnio():
    """检测失败→看实际文件→有残留则先卸再装；否则直接装。全局互斥防重复弹窗。"""
    log("ensure_pawnio enter")
    if not _ensure_lock(60000):
        log("PawnIO 安装已在其他进程中进行，本次调用跳过等待")
        return False
    try:
        if pawnio_present():
            log("pawnio_present=True, no action needed")
            return True
        if not is_admin():
            log("非管理员，无法执行 PawnIO 安装/修复，请在管理员权限下运行。")
            msgbox("TDP 调节需要管理员权限。\n请右键以管理员身份运行，或在 UAC 提示时点击\"是\"。",
                   "需要管理员权限", MB_ERR)
            return False
        _check_testsigning()
        svc_r = _run_logged(["sc", "query", "PawnIO"], timeout=30)
        if svc_r and svc_r.returncode == 0:
            state = ""
            for line in (svc_r.stdout or "").splitlines():
                if "STATE" in line:
                    state = line.strip()
            if "RUNNING" in state.upper():
                log("服务 RUNNING，视为已安装健康，重试打开驱动句柄: %s" % state)
                if pawnio_present():
                    log("驱动就绪"); return True
                log("服务 RUNNING 但 pawnio_open 失败 (签名/不匹配?)")
                ts = _check_testsigning()
                if ts is False:
                    log("Test signing 关闭，test-signed 驱动无法加载")
                    msgbox("PawnIO 驱动已安装但无法加载：当前系统未开启测试签名 (test-signing)。\n"
                           "请开启测试签名后重试，或重新安装 PawnIO。",
                           "驱动加载失败", MB_ERR)
                return False
            log("服务存在但未 RUNNING，尝试 sc start")
            _run_logged(["sc", "start", "PawnIO"], timeout=30)
            if pawnio_present():
                log("sc start 后驱动就绪"); return True
        if not SETUP or not os.path.exists(SETUP):
            log(f"SETUP not found: {SETUP}")
            msgbox("未找到 PawnIO 安装程序 (PawnIO_setup.exe)。\n请前往 https://pawnio.eu/ 手动安装 PawnIO 驱动后重试。",
                   "缺少 PawnIO 驱动", MB_ERR)
            return False
        pawn_dir = os.path.dirname(DLL)
        util = os.path.join(pawn_dir, "PawnIOUtil.exe")
        residual = os.path.exists(DLL) or os.path.exists(util) or _pawnio_driver_package_present()
        if residual:
            log("检测到 PawnIO 残留，正在静默清理并重新安装...")
            run_setup_remove(SETUP)
        log("正在静默安装 PawnIO 以启用 TDP 调节...")
        if not run_setup_silent(SETUP):
            log("Install step returned failure, attempting fallback: start service and retest")
            _run_logged(["sc", "stop", "PawnIO"], timeout=30)
            _run_logged(["sc", "start", "PawnIO"], timeout=30)
            if pawnio_present():
                log("Fallback succeeded: PawnIO is usable")
                return True
            ts = _check_testsigning()
            if ts is False:
                log("Test signing is OFF; PawnIO test-signed driver cannot load")
            msgbox("PawnIO 自动安装失败。\n请手动运行 PawnIO_setup.exe 或前往 https://pawnio.eu/ 安装。",
                   "PawnIO 安装失败", MB_ERR)
            return False
        _run_logged(["sc", "config", "PawnIO", "start=", "auto"], timeout=30)
        _run_logged(["sc", "stop", "PawnIO"], timeout=30)
        _run_logged(["sc", "start", "PawnIO"], timeout=30)
        if pawnio_present():
            log("PawnIO ready after install/reload")
            return True
        log("PawnIO 已安装，但驱动尚未就绪，可能需要重启系统后才会生效。")
        _check_testsigning()
        return False
    finally:
        _ensure_unlock()

# ============================================================================
# OptiScaler 一键导入（纯文件复制，不依赖 OptiScalerClient.exe 运行）
# ----------------------------------------------------------------------------
# 设计：
#  - 仅复用 OptiScalerClient 的本地缓存(Cache/)作为文件源，直接把文件复制到游戏目录。
#  - 注入 DLL = dxgi.dll（重命名的 OptiScaler.dll，与 OptiScalerClient 默认一致）。
#  - 安装前对将被覆盖的原文件做 YeManCC 自管备份(%APPDATA%/YeManCC/optiscaler_backups/<hash>/)，
#    卸载时优先按该 manifest 还原；若游戏是 OptiScalerClient 装的（无 YeManCC manifest），
#    则回退到 OptiScalerClient 自带 Backups/<dir>/manifest.json 还原原始文件。
#  - 21 个 InstalledFiles 恰好 = 6 个被覆盖原件(还原) + 15 个新建文件(删除)，可完整可逆还原。
# 退出码：0 成功 / 2 参数错 / 7 OptiScaler 源缓存缺失。
# ============================================================================
def _olog(*msg):
    """仅写日志文件，不输出到 stdout（optiscaler 用 JSON 单行走 stdout，避免污染解析）。"""
    line = f"{time.strftime('%Y-%m-%d %H:%M:%S')} " + " ".join(str(m) for m in msg)
    try:
        with open(LOG_FILE, "a", encoding="utf-8", errors="ignore") as f:
            f.write(line + "\n")
    except Exception:
        pass

def _sha256(p):
    if not os.path.isfile(p):
        return None
    h = hashlib.sha256()
    try:
        with open(p, "rb") as f:
            for b in iter(lambda: f.read(1 << 20), b""):
                h.update(b)
        return h.hexdigest()
    except Exception:
        return None

def _optiscaler_cache_roots():
    """Return cache roots in priority order, with the user-managed AppData cache first."""
    appdata = os.environ.get("APPDATA") or os.path.expandvars(r"%APPDATA%")
    client_base = os.path.join(appdata, "OptiscalerClient")
    # In a PyInstaller build __file__ may resolve inside _internal; the
    # executable directory is the stable sibling of PowerControl/pawnio.
    runtime_dir = os.path.dirname(sys.executable) if getattr(sys, "frozen", False) else os.path.dirname(__file__)
    this_dir = os.path.abspath(runtime_dir)
    power_control = os.path.dirname(this_dir)
    candidates = [
        os.path.join(client_base, "Cache"),
        os.path.join(power_control, "OptiScalerCache"),
        os.path.join(power_control, "OptiscalerClient", "Cache"),
        os.path.join(power_control, "OptiScaler", "Cache"),
    ]
    result = []
    seen = set()
    for path in candidates:
        key = os.path.normcase(os.path.abspath(path))
        if key in seen:
            continue
        seen.add(key)
        result.append(path)
    return client_base, result

def _optiscaler_version_key(name):
    """Sort versions numerically: 0.10 must be newer than 0.9."""
    nums = tuple(int(v) for v in re.findall(r"\d+", str(name)))
    return nums or (0,)

def _optiscaler_component_dirs(cache_root, section, validator):
    base = os.path.join(cache_root, section)
    if not os.path.isdir(base):
        return []
    result = []
    try:
        entries = list(os.scandir(base))
    except Exception:
        return []
    for entry in entries:
        if not entry.is_dir(follow_symlinks=False):
            continue
        try:
            if validator(entry.path):
                result.append(entry)
        except Exception:
            continue
    result.sort(key=lambda e: (_optiscaler_version_key(e.name), e.stat().st_mtime_ns, e.name.lower()))
    return result

def _optiscaler_pick_component(cache_roots, section, validator):
    """Pick the newest valid component inside the first cache root that has one."""
    for root in cache_roots:
        choices = _optiscaler_component_dirs(root, section, validator)
        if choices:
            chosen = choices[-1]
            return chosen.path, chosen.name, root
    return None, None, None


def _optiscaler_find_loose_component(cache_roots, validator, max_depth=5):
    """Find a valid OptiScaler component in non-standard cache layouts.

    Older OptiScalerClient builds and manually extracted packages do not
    always keep files below Cache/<section>/<version>.  The previous lookup
    therefore reported a missing runtime even when the DLL was plainly in the
    cache.  This fallback stays bounded to the known cache roots and only
    accepts directories that pass the supplied validator.
    """
    for root in cache_roots:
        if not os.path.isdir(root):
            continue
        try:
            for current, dirs, _files in os.walk(root):
                rel = os.path.relpath(current, root)
                depth = 0 if rel == "." else rel.count(os.sep) + 1
                if depth >= max_depth:
                    dirs[:] = []
                dirs.sort(key=str.lower)
                try:
                    if validator(current):
                        return current, os.path.basename(current), root
                except Exception:
                    continue
        except Exception:
            continue
    return None, None, None

OPTI_BACKENDS = ("fsr", "xess")
OPTI_BACKEND_LABELS = {"fsr": "FSR", "xess": "XeSS"}

# OptiScaler releases have used both the SDK and driver FSR names over time.
# Keep the names exact: the runtime loader resolves these files by filename and
# must not receive a renamed XeSS/FSR binary.
OPTI_RUNTIME_NAMES = {
    "fsr": (
        "amd_fidelityfx_dx12.dll",
        "amd_fidelityfx_upscaler_dx12.dll",
        "amdxcffx64.dll",
        "amd_fidelityfx_vk.dll",
        "ffx_fsr2_api_dx12_x64.dll",
        "ffx_fsr3_api_dx12_x64.dll",
        "ffx_fsr2_api_vk_x64.dll",
        "ffx_fsr3_api_vk_x64.dll",
    ),
    "xess": ("libxess.dll", "libxess_dx11.dll", "libxess_fg.dll"),
}

# These are the concrete filenames used by the upstream OptiScaler Client's
# game analyzer.  Detection must be filename-based, recursive, and advisory;
# engine/API heuristics are never a substitute for these files.
OPTI_GAME_RUNTIME_NAMES = {
    "fsr": (
        "amd_fidelityfx_dx12.dll",
        "amd_fidelityfx_vk.dll",
        "amd_fidelityfx_loader_dx12.dll",
        "amd_fidelityfx_upscaler_dx12.dll",
        "amdxcffx64.dll",
        "ffx_fsr2_api_x64.dll",
        "ffx_fsr2_api_dx12_x64.dll",
        "ffx_fsr2_api_vk_x64.dll",
        "ffx_fsr3_api_x64.dll",
        "ffx_fsr3_api_dx12_x64.dll",
        "ffx_fsr3_api_vk_x64.dll",
    ),
    "xess": ("libxess.dll", "libxess_dx11.dll", "libxess_fg.dll"),
}


def _optiscaler_runtime_files(root, names):
    """Find runtime DLLs in a component, preferring the component root."""
    wanted = {str(name).lower(): str(name) for name in names}
    found = {}
    if not root or not os.path.isdir(root):
        return found
    try:
        for current, dirs, files in os.walk(root):
            dirs.sort(key=str.lower)
            files.sort(key=str.lower)
            for filename in files:
                key = filename.lower()
                if key in wanted and key not in found:
                    found[key] = os.path.join(current, filename)
    except Exception:
        return found
    return found


def _optiscaler_cfg():
    client_base, cache_roots = _optiscaler_cache_roots()

    src_opti, opti_version, opti_root = _optiscaler_pick_component(
        cache_roots,
        "OptiScaler",
        lambda p: os.path.isfile(os.path.join(p, "OptiScaler.dll")),
    )
    if not src_opti:
        src_opti, opti_version, opti_root = _optiscaler_find_loose_component(
            cache_roots,
            lambda p: os.path.isfile(os.path.join(p, "OptiScaler.dll")),
        )
    src_extra, extra_version, extra_root = _optiscaler_pick_component(
        cache_roots,
        "Extras",
        lambda p: any(_optiscaler_runtime_files(p, names)
                      for names in OPTI_RUNTIME_NAMES.values()),
    )
    if not src_extra:
        src_extra, extra_version, extra_root = _optiscaler_find_loose_component(
            cache_roots,
            lambda p: any(_optiscaler_runtime_files(p, names)
                          for names in OPTI_RUNTIME_NAMES.values()),
        )

    def patch_validator(path):
        return os.path.isfile(os.path.join(path, "OptiPatcher.asi"))

    src_patch = None
    patch_version = None
    patch_root = None
    for root in cache_roots:
        choices = _optiscaler_component_dirs(root, "OptiPatcher", patch_validator)
        if choices:
            rolling = [e for e in choices if e.name.lower() == "rolling"]
            chosen = rolling[-1] if rolling else choices[-1]
            src_patch, patch_version, patch_root = chosen.path, chosen.name, root
            break

    runtime_files = {}
    for backend, names in OPTI_RUNTIME_NAMES.items():
        runtime_files[backend] = {}
        for source_root in (src_extra, src_opti):
            for key, path in _optiscaler_runtime_files(source_root, names).items():
                runtime_files[backend][key] = path
        # A few manually managed caches put the runtime directly beside the
        # component folder. Keep the lookup bounded to known names only.

    missing = []
    if not src_opti:
        missing.append("OptiScaler.dll")
    available_backends = [backend for backend in OPTI_BACKENDS
                          if runtime_files.get(backend)]
    if not available_backends:
        missing.append("FSR 或 XeSS 运行库 (FSR: amd_fidelityfx_dx12.dll/amdxcffx64.dll；XeSS: libxess.dll)")
    source_root = opti_root or extra_root or patch_root
    return {
        "client_base": client_base,
        "cache_roots": cache_roots,
        "source_root": source_root,
        "src_opti": src_opti,
        "src_extra": src_extra,
        "src_patch": src_patch,
        "opti_version": opti_version,
        "extra_version": extra_version,
        "patch_version": patch_version,
        "runtime_files": runtime_files,
        "available_backends": available_backends,
        "missing": missing,
        "inject": "dxgi.dll",
    }

def _optiscaler_normalize_backend(value, cfg=None):
    backend = str(value or "auto").strip().lower()
    if backend in ("fsr", "fsr4", "fsr4.1", "ffx"):
        backend = "fsr"
    elif backend in ("xess", "xe-ss", "xe_ss"):
        backend = "xess"
    elif backend in ("", "auto", "recommend", "recommended"):
        available = (cfg or {}).get("available_backends") or []
        backend = available[0] if available else ""
    else:
        backend = ""
    if cfg is not None and backend not in (cfg.get("available_backends") or []):
        return ""
    return backend


def _optiscaler_plan(game_dir, cfg, backend="auto", analysis=None):
    """Return a deterministic, de-duplicated copy plan."""
    available = list(cfg.get("available_backends") or [])
    for value in (analysis or {}).get("available_backends") or []:
        if value not in available:
            available.append(value)
    backend = _optiscaler_normalize_backend(backend, {"available_backends": available})
    if not cfg.get("src_opti") or not backend:
        return []
    by_rel = {}
    runtime_names = {name.lower() for names in OPTI_RUNTIME_NAMES.values() for name in names}

    def add(src, rel, role):
        if not os.path.isfile(src):
            return
        key = rel.replace("/", "\\").lower()
        by_rel[key] = {"src": src, "dst": os.path.join(game_dir, rel),
                       "rel": rel.replace("/", "\\"), "role": role}

    add(os.path.join(cfg["src_opti"], "OptiScaler.dll"), cfg["inject"], "主DLL->注入")
    for root, dirs, files in os.walk(cfg["src_opti"]):
        dirs.sort(key=str.lower)
        files.sort(key=str.lower)
        for fn in files:
            if fn.lower() == "optiscaler.dll" or fn.lower() in runtime_names:
                continue
            sp = os.path.join(root, fn)
            rel = os.path.relpath(sp, cfg["src_opti"]).replace("/", "\\")
            add(sp, rel, "基础包")

    # Only the selected backend is copied. This is the important difference
    # from the old FSR-only path: selecting XeSS now changes the actual runtime
    # DLL set and the resulting OptiScaler.ini values.
    for filename, source in sorted((cfg.get("runtime_files", {}).get(backend) or {}).items()):
        add(source, os.path.basename(source), OPTI_BACKEND_LABELS.get(backend, backend) + " Runtime")

    if cfg.get("src_patch"):
        add(os.path.join(cfg["src_patch"], "OptiPatcher.asi"),
            "plugins\\OptiPatcher.asi", "OptiPatcher")
    return list(by_rel.values())


def _optiscaler_game_scan_root(game_dir):
    """Find a likely install root without changing the install target."""
    root = os.path.abspath(game_dir)
    for _ in range(3):
        parent = os.path.dirname(root)
        if not parent or os.path.normcase(parent) == os.path.normcase(root):
            break
        current_name = os.path.basename(root).lower()
        try:
            parent_names = {name.lower() for name in os.listdir(parent)}
        except Exception:
            parent_names = set()
        if current_name in {"x64", "win64", "binaries", "plugins"} or (
                current_name in {"bin", "engine", "binaries", "plugins"}
                and parent_names.intersection(
                    {"archive", "bin", "engine", "binaries", "paks", "content", "red4ext"})):
            root = parent
            continue
        break
    return root


def _optiscaler_game_features(game_dir):
    """Read-only recommendation based on upstream-style recursive DLL discovery.

    The selected executable directory remains the only install/uninstall target.
    Detection may inspect the likely game root recursively, matching the exact
    runtime filenames used by the upstream OptiScaler Client.
    """
    result = {
        "api": "unknown", "engine": "unknown", "features": [],
        "evidence": [], "existing": [], "runtime_backends": [],
        "runtime_paths": {"fsr": [], "xess": []},
        "score": {"fsr": 0, "xess": 0},
    }
    if not os.path.isdir(game_dir):
        result["evidence"].append("游戏目录不存在")
        return result
    files = []
    try:
        scan_root = _optiscaler_game_scan_root(game_dir)
        for current, dirs, names in os.walk(scan_root):
            rel = os.path.relpath(current, scan_root)
            dirs.sort(key=str.lower)
            names.sort(key=str.lower)
            for name in names:
                files.append(os.path.join(rel, name).replace("/", "\\").lower())
            if len(files) >= 50000:
                break
    except Exception as e:
        result["evidence"].append("扫描目录受限: " + str(e))
    joined = "\n".join(files)
    names = {os.path.basename(v) for v in files}

    if "d3d12.dll" in names or "dxil.dll" in names or "d3d12" in joined:
        result["api"] = "dx12"
        result["evidence"].append("发现 DirectX 12 文件特征")
    elif "d3d11.dll" in names or "d3dcompiler_47.dll" in names:
        result["api"] = "dx11"
        result["evidence"].append("发现 DirectX 11 文件特征")
    elif "vulkan-1.dll" in names or "vulkan" in joined:
        result["api"] = "vulkan"
        result["evidence"].append("发现 Vulkan 文件特征")

    if "unityplayer.dll" in names or "gameassembly.dll" in names:
        result["engine"] = "unity"
        result["evidence"].append("发现 Unity 引擎文件")
    elif any(token in joined for token in ("ue4game", "ue5game", "engine\\binaries\\", "unrealengine")):
        result["engine"] = "unreal"
        result["evidence"].append("发现 Unreal Engine 文件特征")
    elif any(token in joined for token in ("re_chunk_", "reengine", "reframework")):
        result["engine"] = "re"
        result["evidence"].append("发现 RE Engine 文件特征")
    elif any(token in joined for token in ("cyberpunk2077", "redengine", "red4ext")):
        result["engine"] = "redengine"
        result["evidence"].append("发现 Cyberpunk 2077/REDengine 文件特征")

    if any(name.startswith("nvngx") or "dlss" in name for name in names):
        result["existing"].append("dlss")
        result["features"].append("已有 DLSS 输入")
        result["score"]["xess"] += 1
        result["score"]["fsr"] += 1
    for backend, runtime_names in OPTI_GAME_RUNTIME_NAMES.items():
        matches = [path for path in files if os.path.basename(path) in
                   {name.lower() for name in runtime_names}]
        if matches:
            result["runtime_backends"].append(backend)
            result["runtime_paths"][backend] = matches[:20]
            display_names = sorted({os.path.basename(path) for path in matches})[:4]
            result["evidence"].append("发现 " + OPTI_BACKEND_LABELS[backend]
                                      + " 运行库文件: " + ", ".join(display_names))

    if "xess" in result["runtime_backends"]:
        result["existing"].append("xess")
        result["features"].append("已有 XeSS 输入")
        result["score"]["xess"] += 2
    if "fsr" in result["runtime_backends"]:
        result["existing"].append("fsr")
        result["features"].append("已有 FSR 输入")
        result["score"]["fsr"] += 2

    if result["api"] == "dx12":
        result["score"]["xess"] += 2
        result["score"]["fsr"] += 1
    elif result["api"] == "dx11":
        result["score"]["fsr"] += 1
        result["score"]["xess"] += 1
        result["features"].append("DX11 将通过 D3D11on12/兼容路径选择后端")
    elif result["api"] == "vulkan":
        result["score"]["xess"] += 1
        result["score"]["fsr"] += 1

    if result["engine"] == "unreal":
        result["score"]["xess"] += 1
        result["score"]["fsr"] += 1
    if result["engine"] == "unity":
        result["score"]["fsr"] += 1
    if result["engine"] == "redengine":
        result["score"]["xess"] += 1
        result["score"]["fsr"] += 1

    if not result["features"]:
        result["features"].append("未发现明确的现有超分辨率输入")
    return result


def _optiscaler_analyze(game_dir, cfg):
    features = _optiscaler_game_features(game_dir)
    available = list(cfg.get("available_backends") or [])
    for backend in features.get("runtime_backends") or []:
        if backend not in available:
            available.append(backend)
    available = [backend for backend in OPTI_BACKENDS if backend in available]
    ranked = sorted(available, key=lambda b: (-features["score"].get(b, 0), b))
    recommended = ranked[0] if ranked else ""
    reasons = list(features.get("evidence") or [])
    game_runtime = [backend for backend in features.get("runtime_backends") or []
                    if backend in OPTI_BACKENDS]
    if game_runtime:
        reasons.append("游戏目录已发现 " + "、".join(OPTI_BACKEND_LABELS.get(v, v)
                                                    for v in game_runtime) + " 运行库")
    if recommended:
        reasons.append("基于已识别特征推荐 " + OPTI_BACKEND_LABELS.get(recommended, recommended))
    if not available:
        reasons.append("缓存和游戏目录中都没有可用的 FSR/XeSS 运行库")
    missing = list(cfg.get("missing") or [])
    runtime_missing = [item for item in missing if str(item).startswith("FSR 或 XeSS")]
    if game_runtime and runtime_missing:
        missing = [item for item in missing if item not in runtime_missing]
    return {
        "ok": os.path.isdir(game_dir) and bool(cfg.get("src_opti")),
        "game_dir": game_dir,
        "api": features["api"],
        "engine": features["engine"],
        "features": features["features"],
        "evidence": features["evidence"],
        "existing": sorted(set(features["existing"])),
        "available_backends": available,
        "recommended_backend": recommended,
        "reasons": reasons,
        "runtime_from_game": game_runtime,
        "runtime_paths": features.get("runtime_paths") or {},
        "source": cfg.get("source_root"),
        "version": "%s/%s" % (cfg.get("opti_version") or "", cfg.get("extra_version") or ""),
        "missing": missing,
    }


def _ini_set_section_values(text, section, values):
    """Update selected keys while preserving the user's INI formatting."""
    lines = text.splitlines(True)
    section_start = None
    section_end = len(lines)
    for index, line in enumerate(lines):
        m = re.match(r"^\s*\[([^]]+)\]", line)
        if not m:
            continue
        if section_start is not None:
            section_end = index
            break
        if m.group(1).strip().lower() == section.lower():
            section_start = index
    if section_start is None:
        suffix = "" if not text or text.endswith(("\n", "\r")) else "\n"
        block = suffix + "[" + section + "]\n" + "".join(k + "=" + v + "\n" for k, v in values.items())
        return text + block

    remaining = set(values)
    for index in range(section_start + 1, section_end):
        m = re.match(r"^(\s*)([^;#=\s]+)(\s*=\s*)([^\r\n]*)(\r?\n?)$", lines[index])
        if not m:
            continue
        key = m.group(2)
        for wanted, value in values.items():
            if key.lower() == wanted.lower():
                lines[index] = m.group(1) + key + m.group(3) + value + m.group(5)
                remaining.discard(wanted)
                break
    if remaining:
        insert_at = section_end
        newline = "\r\n" if any(line.endswith("\r\n") for line in lines) else "\n"
        lines[insert_at:insert_at] = [k + "=" + values[k] + newline for k in remaining]
    return "".join(lines)


def _optiscaler_configure_backend(game_dir, backend, analysis):
    config_path = os.path.join(game_dir, "OptiScaler.ini")
    if not os.path.isfile(config_path):
        return {"ok": False, "changed": False, "msgs": ["安装后未找到 OptiScaler.ini"]}
    try:
        with open(config_path, "r", encoding="utf-8-sig", errors="ignore") as f:
            text = f.read()
        if backend == "xess":
            values = {"Dx11Upscaler": "xess_12", "Dx12Upscaler": "xess", "VulkanUpscaler": "xess"}
        else:
            values = {"Dx11Upscaler": "ffx_12", "Dx12Upscaler": "ffx", "VulkanUpscaler": "ffx"}
        updated = _ini_set_section_values(text, "Upscalers", values)
        if updated != text:
            with open(config_path, "w", encoding="utf-8", newline="") as f:
                f.write(updated)
        return {"ok": True, "changed": updated != text, "path": config_path, "values": values}
    except Exception as e:
        return {"ok": False, "changed": False, "msgs": ["写入 OptiScaler.ini 失败: " + str(e)]}

def _optiscaler_status(game_dir, cfg):
    """installed = 注入 DLL 存在且哈希与缓存 OptiScaler.dll 一致。
    仅 hash 匹配才算已装：很多游戏自带 dxgi.dll，不能用「存在即已装」判断。"""
    for ybdir in _ymcc_backup_candidates(game_dir):
        ymanifest = os.path.join(ybdir, "manifest.json")
        if not os.path.isfile(ymanifest):
            continue
        try:
            with open(ymanifest, "r", encoding="utf-8", errors="ignore") as f:
                m = json.load(f)
            if _canonical_game_dir(m.get("game_dir", "")) == _canonical_game_dir(game_dir):
                return {"installed": True, "reason": "yemancc_manifest",
                        "version": str(m.get("source_version", "")),
                        "backend": str(m.get("backend", "")) or None}
        except Exception:
            pass
    client_backup = _find_client_backup(game_dir)
    if client_backup:
        return {"installed": True, "reason": "optiscalerclient_backup", "backend": None}
    if not cfg.get("src_opti"):
        return {"installed": False, "reason": "source_missing",
                "msgs": ["缓存缺少 OptiScaler.dll"]}
    inject_dst = os.path.join(game_dir, cfg["inject"])
    src = os.path.join(cfg["src_opti"], "OptiScaler.dll")
    if not os.path.isfile(inject_dst):
        return {"installed": False, "reason": "inject_missing"}
    if not os.path.isfile(src):
        return {"installed": False, "reason": "source_missing",
                "msgs": ["缓存缺少 OptiScaler.dll，无法安全判断安装状态"]}
    if _sha256(inject_dst) == _sha256(src):
        return {"installed": True, "reason": "hash_match", "backend": None}
    return {"installed": False, "reason": "inject_present_hash_diff", "backend": None}

def _canonical_game_dir(game_dir):
    if not str(game_dir or "").strip():
        return ""
    return os.path.normcase(os.path.normpath(os.path.abspath(str(game_dir)))).rstrip("\\/")


def _ymcc_backup_root():
    appdata = os.environ.get("APPDATA") or os.path.expanduser(r"~\AppData\Roaming")
    return os.path.join(appdata, "YeManCC", "optiscaler_backups")


def _ymcc_backup_dir(game_dir):
    key = hashlib.md5(_canonical_game_dir(game_dir).encode("utf-8", "ignore")).hexdigest()[:12]
    return os.path.join(_ymcc_backup_root(), key)


def _ymcc_backup_candidates(game_dir):
    """Return current and legacy backup locations for path-format changes."""
    root = _ymcc_backup_root()
    current = _ymcc_backup_dir(game_dir)
    legacy_key = hashlib.md5(str(game_dir).lower().encode("utf-8", "ignore")).hexdigest()[:12]
    candidates = [current, os.path.join(root, legacy_key)]
    target = _canonical_game_dir(game_dir)
    try:
        for entry in os.scandir(root):
            if not entry.is_dir(follow_symlinks=False):
                continue
            manifest_path = os.path.join(entry.path, "manifest.json")
            if not os.path.isfile(manifest_path):
                continue
            try:
                with open(manifest_path, "r", encoding="utf-8", errors="ignore") as f:
                    manifest = json.load(f)
                if _canonical_game_dir(manifest.get("game_dir", "")) == target:
                    candidates.append(entry.path)
            except Exception:
                continue
    except Exception:
        pass
    result = []
    seen = set()
    for path in candidates:
        key = os.path.normcase(os.path.abspath(path))
        if key not in seen:
            seen.add(key)
            result.append(path)
    return result


def _find_ymcc_backup_dir(game_dir):
    for path in _ymcc_backup_candidates(game_dir):
        if os.path.isfile(os.path.join(path, "manifest.json")):
            return path
    return None

def _write_json_file(path, value):
    parent = os.path.dirname(path)
    if parent:
        os.makedirs(parent, exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(value, f, ensure_ascii=False, indent=2)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)

def _safe_join(base, relative):
    """Join a manifest path while rejecting traversal outside its root."""
    base_abs = os.path.normcase(os.path.abspath(base))
    target_abs = os.path.normcase(os.path.abspath(os.path.join(base, str(relative))))
    try:
        if os.path.commonpath((base_abs, target_abs)) != base_abs:
            raise ValueError("manifest path escapes root")
    except ValueError:
        raise ValueError("manifest path escapes root")
    return target_abs

def _optiscaler_install(game_dir, cfg, dry, backend="auto"):
    hard_missing = [item for item in (cfg.get("missing") or [])
                    if str(item).startswith("OptiScaler.dll")]
    if hard_missing:
        return {"ok": False, "msgs": ["缓存缺少: " + "、".join(hard_missing)]}
    if not os.path.isdir(game_dir):
        return {"ok": False, "msgs": ["游戏目录不存在: " + game_dir]}
    analysis = _optiscaler_analyze(game_dir, cfg)
    selected_backend = _optiscaler_normalize_backend(
        backend, {"available_backends": analysis.get("available_backends") or []})
    if not selected_backend:
        available = "、".join(OPTI_BACKEND_LABELS.get(v, v)
                              for v in (analysis.get("available_backends") or []))
        return {"ok": False, "msgs": ["未找到可用的 FSR/XeSS 运行库"
                                       + ("，当前可用: " + available if available else "")
                                       + "；未结束游戏进程"]}
    plan = _optiscaler_plan(game_dir, cfg, selected_backend, analysis)
    missing = [p["src"] for p in plan if not os.path.isfile(p["src"])]
    if missing or not plan:
        return {"ok": False, "msgs": ["OptiScaler 安装计划为空或源文件缺失"] + missing}
    bdir = _ymcc_backup_dir(game_dir)
    ymanifest = os.path.join(bdir, "manifest.json")
    if any(os.path.isfile(os.path.join(path, "manifest.json"))
           for path in _ymcc_backup_candidates(game_dir)):
        return {"ok": False, "msgs": ["该游戏已有 YeManCC 安装记录，请先卸载后再安装新版本"]}
    if os.path.exists(bdir):
        return {"ok": False, "msgs": ["YeManCC 备份目录存在但清单缺失，已拒绝覆盖以保护原文件"]}
    if dry:
        return {"ok": True, "dry": True, "count": len(plan),
                "plan": [p["rel"] for p in plan],
                "backend": selected_backend,
                "analysis": analysis,
                "source": cfg.get("source_root"),
                "version": "%s/%s" % (cfg.get("opti_version") or "", cfg.get("extra_version") or "")}

    pending = bdir + ".pending"
    try:
        parent = os.path.dirname(bdir)
        os.makedirs(parent, exist_ok=True)
        if os.path.exists(pending):
            shutil.rmtree(pending)
        files_dir = os.path.join(pending, "files")
        manifest = {"game_dir": game_dir,
                    "installed_at": time.strftime("%Y-%m-%d %H:%M:%S"),
                    "backend": selected_backend,
                    "analysis": analysis,
                    "source_version": "%s/%s" % (cfg.get("opti_version") or "", cfg.get("extra_version") or ""),
                    "source_root": cfg.get("source_root"),
                    "inject": cfg["inject"], "state": "pending", "items": []}
        os.makedirs(files_dir, exist_ok=True)
        _write_json_file(os.path.join(pending, "manifest.pending.json"), manifest)
    except Exception as e:
        _olog("install transaction preparation failed", e)
        try:
            shutil.rmtree(pending, ignore_errors=True)
        except Exception:
            pass
        return {"ok": False, "msgs": ["准备安装事务失败: " + str(e)]}
    written = 0
    touched = []
    try:
        # 先完整备份并校验全部原文件，期间不覆盖游戏目录。
        for index, p in enumerate(plan):
            dst = p["dst"]
            exists = os.path.exists(dst)
            if exists and not os.path.isfile(dst):
                raise RuntimeError("目标不是普通文件: " + p["rel"])
            had_original = bool(exists)
            bak_rel = None
            original_sha256 = None
            if had_original:
                bak_rel = "%04d_%s" % (index, p["rel"].replace("\\", "__").replace("/", "__"))
                bak_path = os.path.join(files_dir, bak_rel)
                shutil.copy2(dst, bak_path)
                original_sha256 = _sha256(dst)
                if not original_sha256 or _sha256(bak_path) != original_sha256:
                    raise RuntimeError("原文件备份校验失败: " + p["rel"])
            source_sha256 = _sha256(p["src"])
            if not source_sha256:
                raise RuntimeError("源文件不可读取: " + p["src"])
            manifest["items"].append({"rel": p["rel"], "had_original": had_original,
                                       "backup": bak_rel, "original_sha256": original_sha256,
                                       "source_sha256": source_sha256})
        _write_json_file(os.path.join(pending, "manifest.pending.json"), manifest)

        # Copy through a same-directory temporary file, then atomically replace.
        for index, p in enumerate(plan):
            dst = p["dst"]
            d = os.path.dirname(dst)
            if d:
                os.makedirs(d, exist_ok=True)
            fd, tmp_dst = tempfile.mkstemp(prefix=".YeManCC-", suffix=".tmp", dir=d or None)
            os.close(fd)
            try:
                shutil.copy2(p["src"], tmp_dst)
                expected = manifest["items"][index]["source_sha256"]
                if _sha256(tmp_dst) != expected:
                    raise RuntimeError("写入前校验失败: " + p["rel"])
                os.replace(tmp_dst, dst)
                # Mark the destination before the post-write verification so
                # even a verification failure can restore this file.
                touched.append((dst, manifest["items"][index]))
            finally:
                if os.path.exists(tmp_dst):
                    try:
                        os.remove(tmp_dst)
                    except Exception:
                        pass
            if _sha256(dst) != expected:
                raise RuntimeError("写入后校验失败: " + p["rel"])
            written += 1

        configured = _optiscaler_configure_backend(game_dir, selected_backend, analysis)
        if not configured.get("ok"):
            raise RuntimeError("后端配置失败: " + "；".join(configured.get("msgs") or []))
        manifest["backend_config"] = configured.get("values") or {}

        manifest["state"] = "committed"
        _write_json_file(os.path.join(pending, "manifest.json"), manifest)
        os.replace(pending, bdir)
    except Exception as e:
        _olog("install transaction failed", e)
        rollback_errors = []
        for dst, item in reversed(touched):
            try:
                if item.get("had_original") and item.get("backup"):
                    shutil.copy2(os.path.join(files_dir, item["backup"]), dst)
                elif os.path.exists(dst):
                    os.remove(dst)
            except Exception as rollback_error:
                rollback_errors.append(item.get("rel", dst) + ": " + str(rollback_error))
        try:
            shutil.rmtree(pending, ignore_errors=True)
        except Exception:
            pass
        msg = "OptiScaler 安装事务失败，已回滚: " + str(e)
        if rollback_errors:
            msg += "；回滚异常: " + "、".join(rollback_errors)
        return {"ok": False, "msgs": [msg], "written": written}
    return {"ok": True, "written": written, "backup": bdir,
            "backend": selected_backend, "analysis": analysis,
            "source": cfg.get("source_root"),
            "version": manifest["source_version"]}

def _find_client_backup(game_dir):
    """在 OptiScalerClient 的 Backups 里找匹配本游戏目录的 manifest（用于卸载其安装的游戏）。"""
    bdir = os.path.join(os.path.expandvars(r"%APPDATA%\OptiscalerClient"), "Backups")
    if not os.path.isdir(bdir):
        return None
    target = _canonical_game_dir(game_dir)
    for name in os.listdir(bdir):
        mfp = os.path.join(bdir, name, "manifest.json")
        if not os.path.isfile(mfp):
            continue
        try:
            with open(mfp, "r", encoding="utf-8", errors="ignore") as f:
                m = json.load(f)
        except Exception:
            continue
        gd = _canonical_game_dir(m.get("InstalledGameDirectory") or
                                 m.get("GameDirectory") or m.get("game_dir") or "")
        if gd == target:
            return m, os.path.join(bdir, name)
    return None


def _optiscaler_uninstall_untracked(game_dir, cfg, dry):
    """Safely clean older installs that have no YeManCC manifest."""
    inject = os.path.join(game_dir, cfg.get("inject", "dxgi.dll"))
    source_inject = os.path.join(cfg.get("src_opti") or "", "OptiScaler.dll")
    if not os.path.isfile(inject) or not os.path.isfile(source_inject):
        return {"ok": True, "via": "none", "removed": 0, "restored": 0,
                "dry": dry, "actions": [],
                "msgs": ["未找到可安全确认的 OptiScaler 安装文件，未删除未知文件"]}
    if _sha256(inject) != _sha256(source_inject):
        return {"ok": True, "via": "none", "removed": 0, "restored": 0,
                "dry": dry, "actions": [],
                "msgs": ["dxgi.dll 不是当前缓存的 OptiScaler，未删除未知文件"]}

    candidates = [{"src": source_inject, "dst": inject, "rel": "dxgi.dll"}]
    available = list(cfg.get("available_backends") or [])
    for backend in OPTI_BACKENDS:
        if backend not in available:
            continue
        for item in _optiscaler_plan(game_dir, cfg, backend,
                                     {"available_backends": available}):
            if item["rel"].lower() != "dxgi.dll":
                candidates.append(item)
    config_path = os.path.join(game_dir, "OptiScaler.ini")
    if os.path.isfile(config_path):
        try:
            with open(config_path, "r", encoding="utf-8-sig", errors="ignore") as f:
                config_text = f.read()
            if "[Upscalers]" in config_text or "OptiScaler" in config_text:
                candidates.append({"src": None, "dst": config_path, "rel": "OptiScaler.ini"})
        except Exception:
            pass

    actions = []
    failures = []
    removed = 0
    seen = set()
    for item in candidates:
        dst = item["dst"]
        key = os.path.normcase(os.path.abspath(dst))
        if key in seen or not os.path.isfile(dst):
            continue
        seen.add(key)
        if item.get("src") and _sha256(dst) != _sha256(item["src"]):
            continue
        if dry:
            actions.append("delete " + item["rel"])
            continue
        try:
            os.remove(dst)
            removed += 1
            actions.append("delete " + item["rel"])
        except Exception as e:
            failures.append("delete " + item["rel"] + ": " + str(e))
    if not dry and removed:
        for name in ("D3D12_Optiscaler", "Licenses", "plugins"):
            path = os.path.join(game_dir, name)
            if os.path.isdir(path):
                try:
                    if not os.listdir(path):
                        os.rmdir(path)
                except Exception:
                    pass
    return {"ok": not failures, "via": "legacy_safe_cleanup", "removed": removed,
            "restored": 0, "dry": dry, "actions": actions, "msgs": failures}


def _optiscaler_uninstall(game_dir, cfg, dry):
    ybdir = _find_ymcc_backup_dir(game_dir) or _ymcc_backup_dir(game_dir)
    ymanifest = os.path.join(ybdir, "manifest.json")
    removed = 0
    restored = 0
    actions = []
    failures = []

    if os.path.isfile(ymanifest):
        # 主路径：YeManCC 自管备份（我们安装的游戏）
        try:
            with open(ymanifest, "r", encoding="utf-8", errors="ignore") as f:
                m = json.load(f)
        except Exception:
            return {"ok": False, "msgs": ["读取 YeManCC 备份 manifest 失败"]}
        files_dir = os.path.join(ybdir, "files")
        for it in m.get("items", []):
            try:
                rel = str(it["rel"])
                dst = _safe_join(game_dir, rel)
                bak = _safe_join(files_dir, it["backup"]) if it.get("backup") else None
            except (KeyError, ValueError) as e:
                return {"ok": False, "msgs": ["安装清单路径无效，已停止卸载: " + str(e)]}
            if it.get("had_original") and bak and os.path.isfile(bak):
                if dry:
                    actions.append("restore " + rel)
                else:
                    try:
                        shutil.copy2(bak, dst); restored += 1
                        actions.append("restore " + rel)
                    except Exception as e:
                        failures.append("restore " + rel + ": " + str(e))
                        actions.append("restore FAIL " + rel + " " + str(e))
                continue
            # 无原件 -> 删除
            if os.path.exists(dst):
                if dry:
                    actions.append("delete " + rel)
                else:
                    try:
                        if os.path.isdir(dst):
                            shutil.rmtree(dst)
                        else:
                            os.remove(dst)
                        removed += 1
                    except Exception as e:
                        failures.append("delete " + rel + ": " + str(e))
                        actions.append("delete FAIL " + rel + " " + str(e))
                        continue
                    actions.append("delete " + rel)
        if not dry:
            for d in ("D3D12_Optiscaler", "Licenses", "plugins"):
                dd = os.path.join(game_dir, d)
                if os.path.isdir(dd):
                    try:
                        if not os.listdir(dd):
                            os.rmdir(dd)
                    except Exception:
                        pass
            try:
                shutil.rmtree(ybdir, ignore_errors=True)
            except Exception:
                pass
        return {"ok": not failures, "via": "yemancc", "removed": removed,
                "restored": restored, "dry": dry, "actions": actions,
                "backend": m.get("backend"), "msgs": failures}

    # 回退路径：OptiScalerClient 自带 Backups（游戏是它装的）
    found = _find_client_backup(game_dir)
    if not found:
        return _optiscaler_uninstall_untracked(game_dir, cfg, dry)
    m, cdir = found
    files_dir = os.path.join(cdir, "files")
    # 直接以 Backups/<dir>/files/ 里的原始备份为准：
    #  - 某 InstalledFile 在 files/ 中有同名备份 -> 还原原件（覆盖 OptiScaler 版）
    #  - 否则该文件是 OptiScaler 新建/覆盖且无原备份 -> 删除
    # 这样即使 manifest 的 BackedUpFiles 字段为空，也能正确还原（files/ 始终含原件）。
    installed_files = list(m.get("InstalledFiles", []))
    overwritten = m.get("FilesOverwritten", [])
    if isinstance(overwritten, list):
        for entry in overwritten:
            if isinstance(entry, dict):
                rel = entry.get("RelativePath")
                if rel and rel not in installed_files:
                    installed_files.append(rel)
    for rel in installed_files:
        try:
            dst = _safe_join(game_dir, rel)
            backup_rel = rel
            for entry in overwritten if isinstance(overwritten, list) else []:
                if isinstance(entry, dict) and entry.get("RelativePath") == rel:
                    backup_rel = entry.get("BackupRelativePath") or rel
                    break
            bak = _safe_join(files_dir, backup_rel)
        except ValueError:
            actions.append("skip unsafe path " + str(rel))
            continue
        if os.path.isfile(bak):
            if dry:
                actions.append("restore " + rel)
            else:
                try:
                    shutil.copy2(bak, dst); restored += 1
                    actions.append("restore " + rel)
                except Exception as e:
                    failures.append("restore " + rel + ": " + str(e))
                    actions.append("restore FAIL " + rel + " " + str(e))
        elif os.path.exists(dst):
            if dry:
                actions.append("delete " + rel)
            else:
                try:
                    if os.path.isdir(dst):
                        shutil.rmtree(dst)
                    else:
                        os.remove(dst)
                    removed += 1
                    actions.append("delete " + rel)
                except Exception as e:
                    failures.append("delete " + rel + ": " + str(e))
                    actions.append("delete FAIL " + rel + " " + str(e))
    # 清理空目录
    if not dry:
        for d in m.get("InstalledDirectories", []):
            try:
                dd = _safe_join(game_dir, d)
            except ValueError:
                continue
            if os.path.isdir(dd):
                try:
                    if not os.listdir(dd):
                        os.rmdir(dd)
                except Exception:
                    pass
    return {"ok": not failures, "via": "optiscalerclient", "removed": removed,
            "restored": restored, "dry": dry, "actions": actions,
            "msgs": failures}

def optiscaler_cmd(argv, dry=False):
    if len(argv) < 2:
        print(json.dumps({"ok": False, "msgs": ["usage: optiscaler <analyze|status|install|uninstall> <game_dir> [--backend fsr|xess] [--dry-run]"]}))
        return 2
    sub = argv[0].lower()
    game_dir = argv[1]
    cfg = _optiscaler_cfg()
    backend = "auto"
    for index, value in enumerate(argv[2:], start=2):
        if str(value).lower() == "--backend" and index + 1 < len(argv):
            backend = argv[index + 1]
        elif str(value).lower().startswith("--backend="):
            backend = str(value).split("=", 1)[1]
    if sub == "analyze":
        payload = _optiscaler_analyze(game_dir, cfg)
        print(json.dumps(payload, ensure_ascii=False))
        return 0 if payload.get("ok") else 7
    if sub == "status":
        if not os.path.isdir(game_dir):
            print(json.dumps({"ok": False, "installed": False,
                              "msgs": ["游戏目录不存在: " + game_dir]}))
            return 2
        st = _optiscaler_status(game_dir, cfg)
        payload = {"ok": not bool(st.get("msgs")),
                   "installed": st["installed"], "reason": st.get("reason"),
                   "msgs": st.get("msgs", []),
                   "backend": st.get("backend"),
                   "available_backends": cfg.get("available_backends") or [],
                   "source": cfg.get("source_root"),
                   "version": "%s/%s" % (cfg.get("opti_version") or "", cfg.get("extra_version") or "")}
        print(json.dumps(payload, ensure_ascii=False))
        return 0 if payload["ok"] else 7
    if sub == "install":
        r = _optiscaler_install(game_dir, cfg, dry, backend)
        print(json.dumps(r))
        return 0 if r.get("ok") else 7
    if sub == "uninstall":
        r = _optiscaler_uninstall(game_dir, cfg, dry)
        print(json.dumps(r))
        return 0 if r.get("ok") else 7
    print(json.dumps({"ok": False, "msgs": ["unknown optiscaler subcommand: " + sub]}))
    return 2

# ==========================================================================
# Daemon 常驻模式 — 安全双向命名管道。
# 仅由受信任 YeManCC.exe 客户端请求 ping/set/quit；每次 set 等待真实硬件 rc。
# 单例 mutex 在打开 PawnIO 前获取，禁止并发实例互杀或强杀在途硬件事务。
# ==========================================================================


class _DaemonSecurityAttributes(ctypes.Structure):
    _fields_ = [
        ("nLength", ctypes.c_ulong),
        ("lpSecurityDescriptor", ctypes.c_void_p),
        ("bInheritHandle", ctypes.c_int),
    ]


def _daemon_amd_gate_call(func):
    """daemon AMD 常驻句柄操作也必须持有 Global\\Access_PCI。"""
    gate = _SmuGate()
    if not gate.acquired:
        log("  [daemon gate] 未获取 SMU 互斥体, 拒绝操作")
        return 6
    with gate:
        return func()


DAEMON_PIPE_NAME = r"\\.\pipe\YeManTdpCtl.v1"
DAEMON_MUTEX_NAME = "Global\\YeManTdpCtl_Daemon_v1"
DAEMON_PIPE_MAX = 4096
DAEMON_ALLOWED_CLIENTS = (
    r"C:\SOFT\YeMan\YeManCC\YeManCC.exe",
)
DAEMON_PARENT_CLIENT_IMAGE = ""


def _daemon_pipe_kernel32():
    k32 = _kernel32_handles()
    try:
        k32.CreateNamedPipeW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                                         ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32,
                                         ctypes.c_uint32, ctypes.c_void_p]
        k32.CreateNamedPipeW.restype = ctypes.c_void_p
        k32.ConnectNamedPipe.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        k32.ConnectNamedPipe.restype = ctypes.c_int
        k32.DisconnectNamedPipe.argtypes = [ctypes.c_void_p]
        k32.DisconnectNamedPipe.restype = ctypes.c_int
        k32.FlushFileBuffers.argtypes = [ctypes.c_void_p]
        k32.FlushFileBuffers.restype = ctypes.c_int
        k32.ReadFile.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32,
                                 ctypes.POINTER(ctypes.c_uint32), ctypes.c_void_p]
        k32.ReadFile.restype = ctypes.c_int
        k32.WriteFile.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32,
                                  ctypes.POINTER(ctypes.c_uint32), ctypes.c_void_p]
        k32.WriteFile.restype = ctypes.c_int
        k32.GetNamedPipeClientProcessId.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32)]
        k32.GetNamedPipeClientProcessId.restype = ctypes.c_int
        k32.ProcessIdToSessionId.argtypes = [ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
        k32.ProcessIdToSessionId.restype = ctypes.c_int
        k32.GetLastError.argtypes = []
        k32.GetLastError.restype = ctypes.c_uint32
        k32.LocalFree.argtypes = [ctypes.c_void_p]
        k32.LocalFree.restype = ctypes.c_void_p
    except Exception:
        pass
    return k32


def _daemon_security_attributes():
    """仅 SYSTEM 与管理员组可访问 daemon IPC 对象；返回 (sa, sd)。"""
    adv = ctypes.windll.advapi32
    adv.ConvertStringSecurityDescriptorToSecurityDescriptorW.argtypes = [
        ctypes.c_wchar_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_void_p), ctypes.c_void_p
    ]
    adv.ConvertStringSecurityDescriptorToSecurityDescriptorW.restype = ctypes.c_int
    sd = ctypes.c_void_p()
    # D:P = protected DACL；SY=SYSTEM，BA=Built-in Administrators。
    if not adv.ConvertStringSecurityDescriptorToSecurityDescriptorW(
            "D:P(A;;GA;;;SY)(A;;GA;;;BA)", 1, ctypes.byref(sd), None):
        return None, None
    sa = _DaemonSecurityAttributes()
    sa.nLength = ctypes.sizeof(sa)
    sa.lpSecurityDescriptor = sd
    sa.bInheritHandle = 0
    return sa, sd


def _daemon_singleton_open():
    """在任何硬件初始化前竞争 daemon 单例。
    返回 (handle, reason)：ok / already_running / security_failed / create_failed。
    """
    k32 = _daemon_pipe_kernel32()
    sa, sd = _daemon_security_attributes()
    if sa is None:
        log("daemon FATAL: 无法创建安全描述符")
        return None, "security_failed"
    try:
        h = k32.CreateMutexW(ctypes.byref(sa), True, DAEMON_MUTEX_NAME)
        err = k32.GetLastError()
    finally:
        _daemon_pipe_kernel32().LocalFree(sd)
    if not h:
        log("daemon FATAL: 单例 mutex 创建失败")
        return None, "create_failed"
    if err == 183:  # ERROR_ALREADY_EXISTS
        k32.CloseHandle(h)
        log("daemon: 已有安全单例运行，本实例退出")
        return None, "already_running"
    return h, "ok"


def _canonical_exe_path(path):
    try:
        return os.path.normcase(os.path.realpath(os.path.abspath(path)))
    except Exception:
        return ""


def _daemon_client_image(pid):
    k32 = _daemon_pipe_kernel32()
    k32.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
    k32.OpenProcess.restype = ctypes.c_void_p
    k32.QueryFullProcessImageNameW.argtypes = [ctypes.c_void_p, ctypes.c_uint32,
                                               ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_uint32)]
    k32.QueryFullProcessImageNameW.restype = ctypes.c_int
    h = k32.OpenProcess(0x1000, False, int(pid))  # PROCESS_QUERY_LIMITED_INFORMATION
    if not h:
        return ""
    try:
        cap = ctypes.c_uint32(32768)
        buf = ctypes.create_unicode_buffer(cap.value)
        if not k32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(cap)):
            return ""
        return _canonical_exe_path(buf.value)
    finally:
        k32.CloseHandle(h)


def _daemon_parent_image():
    """Return the exact image path of the host that launched this daemon.

    The native test host uses an isolated executable name, so a fixed allowlist
    alone makes legitimate recovery requests look unauthorized.  Capturing the
    parent once at daemon startup keeps the exception narrow: only this daemon's
    immediate parent path is added, never a wildcard or an arbitrary directory.
    """
    return DAEMON_PARENT_CLIENT_IMAGE


def _daemon_capture_parent_image():
    """Capture the immediate host image once, before accepting pipe clients."""
    global DAEMON_PARENT_CLIENT_IMAGE
    try:
        parent_pid = int(os.getppid())
    except Exception:
        parent_pid = 0
    DAEMON_PARENT_CLIENT_IMAGE = _daemon_client_image(parent_pid) if parent_pid > 0 else ""
    return DAEMON_PARENT_CLIENT_IMAGE


def _daemon_allowed_client_paths(parent_image=None):
    allowed = {_canonical_exe_path(p) for p in DAEMON_ALLOWED_CLIENTS}
    if parent_image is None:
        parent_image = _daemon_parent_image()
    if parent_image:
        allowed.add(_canonical_exe_path(parent_image))
    return allowed


def _daemon_verify_pipe_client(pipe_h):
    """连接后、读取命令前验证客户端 PID、会话和最终镜像路径。"""
    k32 = _daemon_pipe_kernel32()
    pid = ctypes.c_uint32(0)
    if not k32.GetNamedPipeClientProcessId(pipe_h, ctypes.byref(pid)) or not pid.value:
        log("daemon pipe: 无法取得客户端 PID，拒绝连接")
        return False
    client_session = ctypes.c_uint32(0)
    self_session = ctypes.c_uint32(0)
    if (not k32.ProcessIdToSessionId(pid.value, ctypes.byref(client_session)) or
            not k32.ProcessIdToSessionId(os.getpid(), ctypes.byref(self_session)) or
            client_session.value != self_session.value):
        log("daemon pipe: 客户端会话不匹配 pid=%d" % pid.value)
        return False
    actual = _daemon_client_image(pid.value)
    allowed = _daemon_allowed_client_paths()
    if not actual or actual not in allowed:
        log("daemon pipe: 客户端路径未授权 pid=%d path=%s" % (pid.value, actual or "<unknown>"))
        return False
    return True


def _daemon_pipe_create():
    k32 = _daemon_pipe_kernel32()
    sa, sd = _daemon_security_attributes()
    if sa is None:
        return None
    try:
        h = k32.CreateNamedPipeW(
            DAEMON_PIPE_NAME,
            0x00000003,  # PIPE_ACCESS_DUPLEX；单连接服务端同步处理，客户端使用可取消 overlapped I/O
            0x00000004 | 0x00000002 | 0x00000008,  # MESSAGE | READMODE_MESSAGE | REJECT_REMOTE
            1, DAEMON_PIPE_MAX, DAEMON_PIPE_MAX, 0, ctypes.byref(sa),
        )
    finally:
        _daemon_pipe_kernel32().LocalFree(sd)
    if not h or h == ctypes.c_void_p(-1).value:
        return None
    return h


def _daemon_pipe_read_json(pipe_h):
    k32 = _daemon_pipe_kernel32()
    buf = ctypes.create_string_buffer(DAEMON_PIPE_MAX)
    got = ctypes.c_uint32(0)
    if not k32.ReadFile(pipe_h, buf, DAEMON_PIPE_MAX, ctypes.byref(got), None):
        return None
    if got.value <= 0 or got.value > DAEMON_PIPE_MAX:
        return None
    try:
        obj = json.loads(bytes(buf.raw[:got.value]).decode("utf-8"))
        return obj if isinstance(obj, dict) else None
    except Exception:
        return None


def _daemon_pipe_write_json(pipe_h, obj):
    k32 = _daemon_pipe_kernel32()
    data = json.dumps(obj, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if len(data) > DAEMON_PIPE_MAX:
        return False
    sent = ctypes.c_uint32(0)
    buf = ctypes.create_string_buffer(data)
    return bool(k32.WriteFile(pipe_h, buf, len(data), ctypes.byref(sent), None) and sent.value == len(data))


def _daemon_resume_hardware(v, state):
    """睡眠唤醒后重建 PawnIO/MSR 句柄，并用只读通道确认硬件已恢复。"""
    if v == "amd":
        new_p, rc = _amd_open()
        if (rc != 0 or not new_p) and state.get("p"):
            # 某些 PawnIO 版本不允许同一进程并存两个会话；关闭旧句柄后再重试一次。
            try:
                state["p"].close()
            except Exception:
                pass
            new_p, rc = _amd_open()
        if rc != 0 or not new_p:
            log("daemon resume: AMD PawnIO reopen failed rc=%s" % rc)
            return int(rc or 6)
        new_cfg = resolve_amd_tdp_config(new_p)
        if new_cfg is FAM_UNKNOWN:
            new_p.close()
            log("daemon resume: AMD family reprobe failed")
            return 6
        if amd_get_with(new_p, new_cfg) != 0:
            new_p.close()
            log("daemon resume: AMD readonly probe failed")
            return 6
        old_p = state.get("p")
        state["p"] = new_p
        state["cfg"] = new_cfg
        if old_p:
            try:
                old_p.close()
            except Exception:
                pass
        log("daemon resume: AMD PawnIO handle rebuilt family=%s" % new_cfg.get("name"))
        return 0
    if v == "intel":
        new_backend = IntelPawnBackend()
        rc = new_backend.open()
        if rc != 0 and state.get("intel"):
            try:
                state["intel"].close()
            except Exception:
                pass
            new_backend = IntelPawnBackend()
            rc = new_backend.open()
        if rc != 0:
            log("daemon resume: Intel PawnIO reopen failed rc=%s" % rc)
            return int(rc or 6)
        value = new_backend.read_msr(MSR_PKG_POWER_LIMIT)
        unit = _rapl_power_unit_w(new_backend)[0]
        if value is None or unit is None:
            new_backend.close()
            log("daemon resume: Intel MSR readonly probe failed")
            return 6
        old_backend = state.get("intel")
        state["intel"] = new_backend
        if old_backend:
            try:
                old_backend.close()
            except Exception:
                pass
        log("daemon resume: Intel PawnIO/MSR handle rebuilt readback=0x%016X" % int(value))
        return 0
    return 5


def _daemon_dispatch_request(v, state, req):
    """窄命令集 typed RPC；不接受原始命令行，不暴露 restore-raw/UV/PBO。"""
    request_id = req.get("requestId")
    resp = {"version": 1, "requestId": request_id, "ok": False, "rc": 2, "error": "bad_request"}
    if req.get("version") != 1 or not isinstance(request_id, str) or not request_id or len(request_id) > 80:
        return resp, False
    op = req.get("op")
    args = req.get("args") if isinstance(req.get("args"), dict) else {}
    if op == "ping":
        resp.update({"ok": True, "rc": 0, "error": "", "result": {"vendor": v}})
        return resp, False
    if op == "resume":
        rc = _daemon_resume_hardware(v, state)
        resp.update({"ok": rc == 0, "rc": int(rc),
                     "error": "" if rc == 0 else "resume_hardware_failed",
                     "result": {"vendor": v, "reinitialized": rc == 0}})
        return resp, False
    if op == "quit":
        resp.update({"ok": True, "rc": 0, "error": "", "result": {"stopping": True}})
        return resp, True
    if op != "set":
        resp["error"] = "operation_not_allowed"
        return resp, False
    try:
        w = float(args.get("watts"))
    except Exception:
        resp["error"] = "invalid_watts"
        return resp, False
    if not (w == w and 2 <= w <= 200):
        resp["error"] = "watts_out_of_range"
        return resp, False
    if v == "amd":
        mw = int(round(w * 1000))
        rc = _daemon_amd_gate_call(lambda: amd_set_with(state.get("p"), state.get("cfg"), mw, mw, mw))
    elif v == "intel":
        rc = intel_set_pawn(state.get("intel"), w - 1, w, False)
    else:
        rc = 5
    resp.update({"ok": rc == 0, "rc": int(rc),
                 "error": "" if rc == 0 else "hardware_operation_failed",
                 "result": {"watts": w, "vendor": v}})
    return resp, False


def daemon_run():
    """安全 daemon：单例 mutex + 身份验证命名管道；不再读取普通命令文件。"""
    if not is_admin():
        log("daemon 需要管理员权限, 自提权重启...")
        if relaunch_elevated():
            return 0
        msgbox("TDP 常驻进程需要管理员权限。\n请右键以管理员身份运行，或在 UAC 提示时点击\"是\"。",
               "需要管理员权限", MB_ERR)
        return 3
    singleton, singleton_reason = _daemon_singleton_open()
    if not singleton:
        return 0 if singleton_reason == "already_running" else 6
    parent_image = _daemon_capture_parent_image()
    v = detect_vendor()
    log("daemon secure pipe start vendor=%s pid=%d parent=%s" %
        (v, os.getpid(), parent_image or "<none>"))
    p = None
    cfg = None
    intel_backend = None
    try:
        if v == "amd":
            if not ensure_pawnio():
                log("daemon FATAL: PawnIO 不可用"); return 3
            startup_gate = _SmuGate()
            if not startup_gate.acquired:
                log("daemon FATAL: 无法获取 Global\\Access_PCI 以加载 AMD 模块")
                return 6
            with startup_gate:
                p, rc = _amd_open()
            if rc != 0:
                log("daemon FATAL: _amd_open rc=%d" % rc); return rc
            cfg = resolve_amd_tdp_config(p)
            if cfg is FAM_UNKNOWN:
                log("daemon FATAL: 未知 AMD 且无明确 UXTU family 映射, 拒绝写入"); return 6
            log("daemon AMD 句柄已保持, family=%s" % cfg["name"])
        elif v == "intel":
            intel_backend = IntelPawnBackend()
            rc = intel_backend.open()
            if rc != 0:
                log("daemon FATAL: Intel PawnIO open rc=%d" % rc)
                return rc
            log("daemon Intel PawnIO 句柄已保持, MSR backend ready")
        else:
            log("daemon FATAL: vendor unknown"); return 5

        hardware_state = {"p": p, "cfg": cfg, "intel": intel_backend}

        stop = False
        while not stop:
            pipe_h = _daemon_pipe_create()
            if not pipe_h:
                log("daemon FATAL: 创建安全命名管道失败")
                return 6
            try:
                k32 = _daemon_pipe_kernel32()
                connected = bool(k32.ConnectNamedPipe(pipe_h, None))
                if not connected and k32.GetLastError() != 535:  # ERROR_PIPE_CONNECTED
                    continue
                if not _daemon_verify_pipe_client(pipe_h):
                    continue
                req = _daemon_pipe_read_json(pipe_h)
                if req is None:
                    _daemon_pipe_write_json(pipe_h, {"version": 1, "requestId": None,
                                                     "ok": False, "rc": 2, "error": "invalid_json"})
                    continue
                resp, stop = _daemon_dispatch_request(v, hardware_state, req)
                _daemon_pipe_write_json(pipe_h, resp)
                try:
                    k32.FlushFileBuffers(pipe_h)
                except Exception:
                    pass
                log("daemon rpc op=%s id=%s rc=%s" % (req.get("op"), req.get("requestId"), resp.get("rc")))
            finally:
                try:
                    ctypes.windll.kernel32.DisconnectNamedPipe(pipe_h)
                except Exception:
                    pass
                ctypes.windll.kernel32.CloseHandle(pipe_h)
        return 0
    finally:
        try:
            active_p = hardware_state.get("p") if 'hardware_state' in locals() else p
            if active_p:
                active_p.close()
        except Exception:
            pass
        try:
            active_intel = hardware_state.get("intel") if 'hardware_state' in locals() else intel_backend
            if active_intel:
                active_intel.close()
        except Exception:
            pass
        try:
            _kernel32_handles().ReleaseMutex(singleton)
        except Exception:
            pass
        _kernel32_handles().CloseHandle(singleton)



def main():
    argv = sys.argv[1:]
    if not argv:
        log(__doc__); return 2
    dry = bool(parse_opt(argv, "--dry-run", False, has_val=False))
    vendor_ov = parse_opt(argv, "--vendor", None)
    intel_delta = int(parse_opt(argv, "--intel-delta", 1))
    # v1 遗留参数: 接受但完全忽略 (调度逻辑已按 Newko 原生方案废弃)
    if parse_opt(argv, "--no-check", False, has_val=False):
        log("[deprecated] --no-check 已废弃并忽略 (v2 恒为直接写入, 无检查逻辑)")
    if parse_opt(argv, "--on-wake", False, has_val=False):
        log("[deprecated] --on-wake 已废弃并忽略 (v2 无早退/重试调度)")
    cmd = argv[0].lower(); rest = argv[1:]

    # 需要硬件通道的命令: 非管理员则自提权重启一次 (dry-run 不碰硬件, 无需提权)
    if (cmd in NEEDS_DRIVER or (cmd == "restore" and rest and rest[0].lower() == "--raw")) and not is_admin() and not dry:
        log("当前非管理员，自提权以访问硬件通道...")
        if relaunch_elevated():
            return 0
        msgbox("TDP 调节需要管理员权限。\n请右键以管理员身份运行，或在 UAC 提示时点击\"是\"。",
               "需要管理员权限", MB_ERR)
        return 3
    # AMD 路径依赖 PawnIO；Intel 也已切换为 PawnIO，KX 已移除。
    v_for_ensure = (vendor_ov or detect_vendor())
    need_pawnio = (cmd in ("set", "get", "restore", "restore-raw", "info", "set-amd", "set-intel", "uv") and v_for_ensure in ("amd", "intel"))
    if need_pawnio and not dry:
        if not ensure_pawnio():
            log("FATAL: PawnIO 不可用，且自动安装失败。"); return 3

    if cmd == "daemon":
        if dry:
            log("  [dry-run] daemon 未启动；dry-run 禁止创建可执行真实硬件命令的常驻进程")
            return 0
        return daemon_run()

    if cmd == "info":
        v = vendor_ov or detect_vendor()
        log("vendor =", v)
        log("PawnIOLib =", DLL)
        log("RyzenSMU.bin =", RYZEN_SMU, "(Newko 同款)")
        log("IntelMSR.bin =", INTEL_MSR_BIN, "(UXTU PawnIO backend)")
        if v == "amd":
            cfg = detect_amd_family()
            log("  AMD family:", cfg["name"])
            log("  transport:", cfg.get("transport"), "verified:", cfg.get("verified_transport"))
            log("  tdp:", cfg.get("tdp_supported"), "co:", cfg.get("co_supported"))
            return amd_get()
        if v == "intel":
            cap = detect_intel_capability()
            _intel_cap_log(cap)
            print(json.dumps(cap))
            return intel_get(dry)
        return 0

    if cmd == "get":
        v = vendor_ov or detect_vendor()
        if v == "amd":   return amd_get()
        if v == "intel": return intel_get(dry)
        log("厂商未识别"); return 5

    if cmd == "restore":
        if rest and rest[0].lower() == "--raw":
            if len(rest) < 2:
                log("usage: restore --raw <64-bit-msr-0x610>"); return 2
            v = vendor_ov or detect_vendor()
            if v != "intel":
                log("restore --raw 仅支持 Intel MSR 0x610"); return 2
            return intel_restore_raw_once(rest[1], dry)
        w = int(rest[0]) if rest else None
        v = vendor_ov or detect_vendor()
        if v == "amd":
            mw = (w * 1000) if w else AMD_DEFAULT_MW
            log("[restore] AMD -> %d mW" % mw)
            return apply_tdp("amd", mw / 1000.0, intel_delta, dry)
        if v == "intel":
            pl1 = w or 45
            log("[restore] Intel -> %dW" % pl1)
            return apply_tdp("intel", float(pl1), intel_delta, dry)
        log("厂商未识别"); return 5

    if cmd == "set-amd":
        if not rest: log("usage: set-amd <stapm_mw> [fast_mw] [slow_mw]"); return 2
        st = int(rest[0]); fa = int(rest[1]) if len(rest) > 1 else st
        sl = int(rest[2]) if len(rest) > 2 else st
        return amd_set(st, fa, sl, dry)

    if cmd == "set-intel":
        if len(rest) < 2: log("usage: set-intel <pl1_w> <pl2_w>"); return 2
        return intel_set(float(rest[0]), float(rest[1]), dry)

    if cmd == "pbo":
        # AM5 PBO PPT/TDC/EDC (非 APU STAPM/FAST/SLOW)
        # pbo 65000 95000 150000  → ppt=65W tdc=95A edc=150A
        if len(rest) < 3: log("usage: pbo <ppt_mw> <tdc_ma> <edc_ma>"); return 2
        return amd_set_pbo(int(rest[0]), int(rest[1]), int(rest[2]), dry)

    if cmd == "intel-cap":
        cap = detect_intel_capability()
        _intel_cap_log(cap)
        print(json.dumps(cap))
        return 0

    if cmd == "uv":
        if not rest:
            log("usage: uv set <value> | uv preset <off|safe|balance|risk> | uv probe [--vendor amd|intel] [--dry-run]")
            return 2
        sub = rest[0].lower(); rest2 = rest[1:]
        v = vendor_ov or detect_vendor()
        if sub == "set":
            if not rest2:
                log("usage: uv set <value>"); return 2
            try:
                val = int(rest2[0])
            except Exception:
                log("uv set 参数必须是整数"); return 2
            if v == "amd":   return amd_uv_set(val, dry)
            if v == "intel": return intel_uv_set(val, dry)
            log("厂商未识别"); return 5
        if sub == "preset":
            if not rest2:
                log("usage: uv preset <off|safe|balance|risk>"); return 2
            key = rest2[0].lower()
            if v == "amd":
                presets = {"off": 0, "safe": -8, "balance": -14, "risk": -24}
            elif v == "intel":
                presets = {"off": 0, "safe": -25, "balance": -45, "risk": -75}
            else:
                log("厂商未识别"); return 5
            if key not in presets:
                log("未知 preset:", key); return 2
            val = presets[key]
            if v == "amd":   return amd_uv_set(val, dry)
            if v == "intel": return intel_uv_set(val, dry)
            log("厂商未识别"); return 5
        if sub == "probe":
            if dry:
                if v == "amd":
                    print(json.dumps({"vendor": "amd", "supported": False, "reason": "dry_run_no_hardware_access",
                                      "current": 0, "readback": False, "current_semantics": "probe_skipped"}))
                elif v == "intel":
                    print(json.dumps({"vendor": "intel", "supported": False, "reason": "dry_run_no_hardware_access",
                                      "current": 0, "restored": True}))
                else:
                    print(json.dumps({"vendor": "", "supported": False, "reason": "unknown_vendor", "current": 0}))
                return 0
            if v == "amd":
                print(json.dumps(amd_uv_probe()))
            elif v == "intel":
                print(json.dumps(intel_uv_probe()))
            else:
                print(json.dumps({"vendor": "", "supported": False, "reason": "unknown_vendor", "current": 0}))
            return 0
        log("未知 uv 子命令:", sub); return 2

    if cmd == "optiscaler":
        return optiscaler_cmd(rest, dry)

    if cmd == "set":
        if not rest: log("usage: set <W> [--vendor amd|intel] [--intel-delta N] [--dry-run]"); return 2
        w = float(rest[0])
        v = vendor_ov or detect_vendor()
        log("[set] 目标=%.1fW 厂商=%s (Newko 原生路径, 直接写入)" % (w, v))
        if v not in ("amd", "intel"):
            log("厂商未识别 (可放 AMD.txt / intel.txt 强制指定)"); return 5
        return apply_tdp(v, w, intel_delta, dry)

    log("未知命令:", cmd); return 2

if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, TypeError, IndexError) as e:
        # CLI 参数错误统一走 rc=2，避免 PyInstaller windowed traceback 弹红框。
        log("参数错误: %s" % e)
        sys.exit(2)
    except Exception as e:
        # 硬件控制器禁止未处理异常弹窗；未知运行时错误记录后优雅退出。
        log("未处理运行时错误: %s" % e)
        sys.exit(6)
