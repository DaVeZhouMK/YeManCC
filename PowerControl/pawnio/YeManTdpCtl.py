#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
YeManTdpCtl v2 - 贴近 Newko(KO助手) 原生逻辑的 TDP 控制器
============================================================================
AMD  : PawnIO + RyzenSMU.bin (Newko 同款) -> ioctl_send_smu_command 高层 API
       命令号: 20=STAPM 21=FAST 22=SLOW (单位 mW), 邮箱交互在内核驱动内完成,
       用户态只发 3 个 ioctl, 不再轮询 PCI 寄存器 (v1 裸 MP1 已废弃)。
Intel: KX.exe (Newko 同款, 不走 PawnIO):
       1) 探测 MCHBAR: /rdmem32 0xfedc0000 / 0xfed10000, 解析 "Return <val>"
       2) MMIO: /wrmem16 <MCHBAR+59>a0 0x<PL1hex16> + 100ms + /wrmem16 ...a4 0x<PL2hex16>
       3) MSR : /wrmsr 0x610 0x00438<PL2hex3> 00DD8<PL1hex3> (失败非致命)
冲突治理 (与 HWiNFO 等监控共存):
  - 每次写入 = 极短硬件占用窗口 (AMD 3 个 ioctl / Intel 数个 KX 调用)
  - 门忙 HRESULT 0x8007054F -> 10/25/50/100/200ms 递增退避, 最多 5 次 (并发抢门可恢复)
  - 无任何"读当前 TDP / 早退 / 调度"逻辑, 需要写就直接写

用法 (需管理员; PawnIO 驱动已装):
  YeManTdpCtl set 65                    # 自动识别厂商 (AMD stapm/fast/slow=W*1000mW; Intel PL1=W-1,PL2=W, PL2=PL1+1, UXTU 同款)
  YeManTdpCtl set 65 --intel-delta 10   # Intel: PL2=PL1+10
  YeManTdpCtl set 65 --vendor intel     # 强制走 Intel 路径
  YeManTdpCtl set-amd 65000 70000 65000 # 显式 stapm/fast/slow (mW)
  YeManTdpCtl set-intel 45 55           # 显式 PL1 PL2 (W)
  YeManTdpCtl get                       # AMD: SMU 版本(通道自检) / Intel: 读 MSR 0x610
  YeManTdpCtl restore [W]               # 恢复 (默认 AMD 170W / Intel 45W)
  YeManTdpCtl info                      # 厂商 / 路径 / 通道信息
  YeManTdpCtl set 65 --dry-run          # 只算不写

退出码: 0 成功 / 2 参数错 / 3 驱动打开失败 / 4 模块加载失败 / 5 厂商未识别 / 6 命令被拒
(--no-check / --on-wake 为 v1 遗留参数, 接受但忽略, 仅打印废弃提示)
"""
import sys, os, re, time, json, ctypes, shutil, subprocess, urllib.request, urllib.error, hashlib

try:
    import winreg
except Exception:
    winreg = None

# ---------- 跨进程互斥：确保 PawnIO 安装/卸载只并发执行一次，避免重复弹窗 ----------
_ENSURE_MUTEX_NAME = "Global\\YeManCC_PawnIO_Ensure"
_ensure_mutex = None

def _ensure_lock(timeout_ms=60000):
    global _ensure_mutex
    if _ensure_mutex is None:
        _ensure_mutex = ctypes.windll.kernel32.CreateMutexW(None, False, _ENSURE_MUTEX_NAME)
    if not _ensure_mutex:
        return True
    r = ctypes.windll.kernel32.WaitForSingleObject(_ensure_mutex, timeout_ms)
    return r == 0 or r == 128  # WAIT_OBJECT_0 / WAIT_ABANDONED_0

def _ensure_unlock():
    if _ensure_mutex:
        ctypes.windll.kernel32.ReleaseMutex(_ensure_mutex)

# ---------- 管理员权限自检 / 自提权 ----------
NEEDS_DRIVER = ("set", "get", "restore", "info", "set-amd", "set-intel", "uv")

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
# KX.exe = Newko 同款 Intel 写入工具 (MMIO + MSR)
KX_EXE = _first([os.path.join(BASE, "KX", "KX.exe"),
                 os.path.join(BASE, "_internal", "KX", "KX.exe"),
                 r"C:\SOFT\YeMan\PowerControl\pawnio\KX\KX.exe"])
SETUP = _first([os.path.join(BASE, "PawnIO_setup.exe"),
                os.path.join(BASE, "_internal", "PawnIO_setup.exe"),
                r"C:\SOFT\YeMan\PowerControl\pawnio\PawnIO_setup.exe"])
# LpcIO 模块 (namazso/PawnIO.Modules, LGPL 2.1): 提供 0x4E/0x4F 端口 I/O 能力
LPCIO_BIN = _first([os.path.join(BASE, "LpcIO.bin"),
                    os.path.join(BASE, "_internal", "LpcIO.bin"),
                    r"C:\SOFT\YeMan\PowerControl\pawnio\LpcIO.bin"])
# 风扇控制强制启用标记文件 (调试/非标准设备用)
FAN_FORCE_FILE = r"C:\SOFT\YeMan\PowerControl\fan_force.txt"

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

# 各家族 MP1 邮箱地址 + stapm/fast/slow 命令号 (UXTU / ryzenadj 同源)
FAM_DESKTOP_AM5 = {   # Granite Ridge / Raphael (9950X 等桌面 Zen4/5)
    "msg": 0x3B10530, "rsp": 0x3B1057C, "arg": 0x3B109C4,
    "stapm": 0x4F, "fast": 0x3E, "slow": 0x5F,
    "coall": 0x36, "coper": 0x35,          # UXTU AM5_V1
}
FAM_APU = {           # Strix Halo / Strix Point / Krackan / Phoenix / Hawk Point / Rembrandt (FT6/FP7/FP8)
    "msg": 0x3B10928, "rsp": 0x3B10978, "arg": 0x3B10998,
    "stapm": 0x14, "fast": 0x15, "slow": 0x16,
    "coall": 0x4C, "coper": 0x4B,          # UXTU FT6_FP7_FP8
}

def detect_amd_family():
    """返回 FAM_DESKTOP_AM5 或 FAM_APU。桌面 AM5 用 '数字X' 后缀(如 9950X)识别;其余按 APU 处理。
    注: Strix Halo 与桌面 Granite Ridge 同为 family 0x1A, 不能用 family 区分, 必须用型号名。"""
    try:
        k = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                           r"HARDWARE\DESCRIPTION\System\CentralProcessor\0")
        name, _ = winreg.QueryValueEx(k, "ProcessorNameString")
        winreg.CloseKey(k)
    except Exception:
        name = ""
    name_u = (name or "").upper()
    if re.search(r"\d{3,4}X\b", name_u):   # 9950X / 7950X / 7900X -> 桌面
        return FAM_DESKTOP_AM5
    # 其余(含 Strix Halo / Phoenix / Rembrandt / 移动端)均按 APU 家族 MP1 参数
    return FAM_APU

# ---------- Intel: KX.exe 常量 (Newko 同款) ----------
MCHBAR_CANDIDATES = ["0xfedc0000", "0xfed10000"]   # Newko sq
INTEL_MAX_W = 300                                   # Newko lq: 瓦数钳制 0..300
MSR_PKG_POWER_LIMIT = 0x610

# 门忙 HRESULT (Newko cI: 0x8007054F / 2147943759)
# 递增退避重试: 10/25/50/100/200ms, 最多 5 次 (并发抢门实测 1/12 会触发, 单次 25ms 不够)
GATE_BUSY_HR = 0x8007054F
GATE_RETRY_DELAYS = (0.010, 0.025, 0.050, 0.100, 0.200)
GATE_RETRY_DELAY = GATE_RETRY_DELAYS[1]  # 兼容旧引用 (25ms)

# 跨进程命名互斥体: 多个 YeManTdpCtl 进程并发调 SMU 时排队串行,
# 直接消除"并发抢门"导致的 0x8007054F / RyzenSMU 加载竞争。
SMU_GATE_MUTEX = "Local\\YeManTdpCtl_SMU_Gate"


class _SmuGate:
    """上下文管理器: 获取命名互斥体, 超时(10s)或互斥体被放弃时也继续(降级为不互斥)。"""

    def __init__(self, timeout_ms=10000):
        k32 = ctypes.windll.kernel32
        self._h = k32.CreateMutexW(None, False, SMU_GATE_MUTEX)
        self._held = False
        if self._h:
            r = k32.WaitForSingleObject(self._h, timeout_ms)
            if r == 0x00000102:  # WAIT_TIMEOUT: 前一个持有者卡住, 降级继续
                log("  [gate] SMU 互斥等待超时, 降级为直接执行")
            self._held = True

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        try:
            if self._h:
                if self._held:
                    ctypes.windll.kernel32.ReleaseMutex(self._h)
                ctypes.windll.kernel32.CloseHandle(self._h)
        except Exception:
            pass

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
        return hr, list(out[:out_count])

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
    hr, out = p.execute("ioctl_read_smu_register", [reg], 1)
    for delay in GATE_RETRY_DELAYS:
        if (hr & 0xFFFFFFFF) != GATE_BUSY_HR:
            break
        log("  [gate] 读寄存器门忙(0x%X), %dms 后重试" % (reg, int(delay * 1000)))
        time.sleep(delay)
        hr, out = p.execute("ioctl_read_smu_register", [reg], 1)
    return out[0]

def _smu_wr(p, reg, val):
    hr, out = p.execute("ioctl_write_smu_register", [reg, val & 0xFFFFFFFF], 0)
    for delay in GATE_RETRY_DELAYS:
        if (hr & 0xFFFFFFFF) != GATE_BUSY_HR:
            break
        log("  [gate] 写寄存器门忙(0x%X), %dms 后重试" % (reg, int(delay * 1000)))
        time.sleep(delay)
        hr, out = p.execute("ioctl_write_smu_register", [reg, val & 0xFFFFFFFF], 0)

def _send_mp1(p, cmd, arg0, cfg, tries=300):
    MP1_MSG, MP1_RSP, MP1_ARG = cfg["msg"], cfg["rsp"], cfg["arg"]
    for _ in range(tries):
        if _smu_rd(p, MP1_RSP) != 0: break
        time.sleep(0.001)
    _smu_wr(p, MP1_RSP, 0)
    _smu_wr(p, MP1_ARG, arg0 & 0xFFFFFFFF)
    for i in range(1, 6):
        _smu_wr(p, MP1_ARG + i * 4, 0)
    _smu_wr(p, MP1_MSG, cmd & 0xFFFFFFFF)
    r = 0
    for _ in range(tries):
        r = _smu_rd(p, MP1_RSP)
        if r != 0: break
        time.sleep(0.001)
    return r & 0xFF

def _amd_set_mp1(p, cfg, stapm_mw, fast_mw, slow_mw):
    """桌面端回退: 裸 MP1 邮箱写 STAPM/FAST/SLOW; 全部 status=1 才算成功。"""
    fail = 0
    for cmd, name, val in ((cfg["stapm"], "stapm", stapm_mw),
                           (cfg["fast"],  "fast",  fast_mw),
                           (cfg["slow"],  "slow",  slow_mw)):
        st = _send_mp1(p, cmd, int(val), cfg)
        log("  AMD-MP1 %-5s (0x%02X) %d mW -> %s" % (name, cmd, val, "OK" if st == 1 else "FAIL(status=%s)" % st))
        if st != 1:
            fail += 1
    return 0 if fail == 0 else 6

def amd_set(stapm_mw, fast_mw, slow_mw, dry=False):
    """写 AMD STAPM/FAST/SLOW (mW)。
    完全采用 UXTU/ryzenadj 的 MP1 邮箱直写逻辑: 按家族选用正确的 MP1 地址 + 命令号
    (Strix Halo/Phoenix 用 0x14/0x15/0x16 + 0x3b10928/0x3b10978/0x3b10998;
     桌面 AM5 用 0x4f/0x3e/0x5f + 0x3B10530/0x3B1057C/0x3B109C4)。
    MP1 直写失败时兜底回退 Newko 高层 ioctl_send_smu_command(仅旧 APU 兼容)。"""
    log("  AMD stapm=%d fast=%d slow=%d mW" % (stapm_mw, fast_mw, slow_mw))
    if dry:
        log("  [dry-run] 未实际写入"); return 0
    with _SmuGate():
        p, rc = _amd_open()
        if rc != 0:
            return rc
        try:
            cfg = detect_amd_family()
            fam = "Desktop-AM5" if cfg is FAM_DESKTOP_AM5 else "APU(Strix/Phoenix/...)"
            log("  [family] %s  MP1 stapm=0x%02X fast=0x%02X slow=0x%02X" %
                (fam, cfg["stapm"], cfg["fast"], cfg["slow"]))
            rc2 = _amd_set_mp1(p, cfg, stapm_mw, fast_mw, slow_mw)
            if rc2 != 0:
                log("  [fallback] MP1 直写失败, 尝试 Newko 高层 ioctl_send_smu_command (旧 APU 兼容)")
                fail = 0
                for cmd, name, val in ((SMU_SET_STAPM_LIMIT, "stapm", stapm_mw),
                                       (SMU_SET_FAST_LIMIT,  "fast",  fast_mw),
                                       (SMU_SET_SLOW_LIMIT,  "slow",  slow_mw)):
                    hr, out = smu_send(p, cmd, int(val))
                    if hr == 0:
                        log("  AMD %-5s (cmd=%d) %d mW -> OK" % (name, cmd, val))
                    else:
                        log("  AMD %-5s (cmd=%d) %d mW -> FAIL HRESULT=0x%08X" % (name, cmd, val, hr & 0xFFFFFFFF))
                        fail += 1
                rc2 = 0 if fail == 0 else 6
            return rc2
        finally:
            p.close()

def amd_get():
    """通道自检 + 读当前 STAPM/FAST/SLOW 实测值(PM 表, best-effort)。"""
    with _SmuGate():
        p, rc = _amd_open()
        if rc != 0:
            return rc
        try:
            hr, out = _smu_exec(p, "ioctl_get_smu_version", [], 1)
            if hr == 0:
                v = out[0]
                log("  AMD SMU version = 0x%08X (%d.%d.%d) 通道正常"
                    % (v, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF))
            else:
                log("  AMD ioctl_get_smu_version FAIL HRESULT=0x%08X" % (hr & 0xFFFFFFFF))
                return 6
            # 实测当前限制值 (PM 表, best-effort); 偏移 0x0/0x8/0x10 = stapm/fast/slow
            # ★仅 APU 家族打印: 该偏移是 ryzenadj 的 APU 表布局, 桌面 AM5 的 PM 表布局不同, 读出数值无意义
            cfg = detect_amd_family()
            if cfg is FAM_APU:
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

UV_AMD_MIN, UV_AMD_MAX = -60, 0
UV_INTEL_MIN, UV_INTEL_MAX = -150, 0

MSR_VOLTAGE_CTL = 0x150
UV_PLANES = {"core": 0x80000011, "cache": 0x80000211}

def _uv_mv_to_data32(mv):
    """UXTU convertVoltageToHexMSR: round(mv*1.024) << 21, 截断 32 位（补码）。"""
    return (int(round(mv * 1.024)) << 21) & 0xFFFFFFFF

def _msr_data_to_mv(v64):
    """从 MSR 0x150 读回的 64 位值中解析 core offset(mV)。"""
    if v64 is None:
        return None
    raw = v64 & 0xFFFFFFFF
    if raw & 0x80000000:
        raw -= 0x100000000
    offset = raw >> 21
    return int(round(offset / 1.024))

def _parse_msr_data(stdout):
    """匹配 KX 'Msr Data : 0xHHHHHHHH 0xLLLLLLLL'，返回完整 64 位。"""
    if not stdout:
        return None
    m = re.search(r"Msr Data\s*:\s*0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)", stdout)
    if m:
        return (int(m.group(1), 16) << 32) | int(m.group(2), 16)
    return None

# ----- AMD -----
def amd_uv_set(offset, dry=False):
    """全核 Curve Optimizer。offset: -60~0（0=还原）。"""
    if not (UV_AMD_MIN <= offset <= UV_AMD_MAX):
        log("  CO 超范围(%d~%d):" % (UV_AMD_MIN, UV_AMD_MAX), offset)
        return 2
    if dry:
        log("  [dry-run] AMD set-coall arg=0x%08X" % (offset & 0xFFFFFFFF))
        return 0
    with _SmuGate():
        p, rc = _amd_open()
        if rc != 0:
            return rc
        try:
            cfg = detect_amd_family()
            st = _send_mp1(p, cfg["coall"], offset & 0xFFFFFFFF, cfg)
            fam = "AM5" if cfg["msg"] == FAM_DESKTOP_AM5["msg"] else "APU"
            log("  AMD(%s) set-coall(0x%02X) %d -> %s" %
                (fam, cfg["coall"], offset, "OK" if st == 1 else "FAIL(status=%s)" % st))
            return 0 if st == 1 else 6
        finally:
            p.close()

def amd_uv_probe():
    """探测 AMD CO 是否可用：发送 set-coall 0 看邮箱是否接受。"""
    with _SmuGate():
        p, rc = _amd_open()
        if rc != 0:
            return {"vendor": "amd", "supported": False, "reason": "pawnio_open_failed", "current": 0}
        try:
            cfg = detect_amd_family()
            st = _send_mp1(p, cfg["coall"], 0, cfg)
            if st == 1:
                fam = "AM5" if cfg["msg"] == FAM_DESKTOP_AM5["msg"] else "APU"
                return {"vendor": "amd", "supported": True, "family": fam, "current": 0}
            return {"vendor": "amd", "supported": False, "reason": "smu_rejected", "current": 0}
        finally:
            p.close()

# ----- Intel -----
def intel_uv_set(mv, dry=False):
    """Intel FIVR offset: Core + Cache 必须同值写入。mv: -150~0。"""
    if not (UV_INTEL_MIN <= mv <= UV_INTEL_MAX):
        log("  Intel 电压偏移超范围(%d~0mV):" % UV_INTEL_MIN, mv)
        return 2
    data = _uv_mv_to_data32(mv)
    if dry:
        for name in ("core", "cache"):
            log("  [dry-run] Intel UV %s /wrmsr 0x150 0x%08X 0x%08X" %
                (name, UV_PLANES[name], data))
        return 0
    for name in ("core", "cache"):
        hi = "0x%08X" % UV_PLANES[name]
        lo = "0x%08X" % data
        r = kx_run(["/wrmsr", "0x150", hi, lo])
        if not kx_ok(r):
            log("  Intel UV %s %dmV 写入失败" % (name, mv))
            return 6
        log("  Intel UV %s %dmV -> OK" % (name, mv))
        time.sleep(0.05)
    return 0

def intel_uv_probe():
    """Intel 降压探测：先读 OC Lock，再写-读 -5mV 验证。"""
    if not is_admin():
        return {"vendor": "intel", "supported": False, "reason": "need_admin", "current": 0}
    # 1) OC Lock (FLEX_RATIO MSR 0x194 bit20)
    r1 = kx_run(["/rdmsr", "0x194"])
    v1 = _parse_msr_data(r1.stdout or "") if kx_ok(r1) else None
    if v1 is not None and ((v1 >> 20) & 1):
        return {"vendor": "intel", "supported": False, "reason": "oc_locked", "current": 0}
    # 2) 读当前 core offset
    kx_run(["/wrmsr", "0x150", "0x80000010", "0x0"])
    r2 = kx_run(["/rdmsr", "0x150"])
    cur = _msr_data_to_mv(_parse_msr_data(r2.stdout or "")) if kx_ok(r2) else None
    if cur is not None and cur != 0:
        return {"vendor": "intel", "supported": True, "current": cur}
    # 3) 写 -5mV 测试并读回
    test_val = -5
    if intel_uv_set(test_val) != 0:
        return {"vendor": "intel", "supported": False, "reason": "write_failed", "current": 0}
    kx_run(["/wrmsr", "0x150", "0x80000010", "0x0"])
    r3 = kx_run(["/rdmsr", "0x150"])
    cur2 = _msr_data_to_mv(_parse_msr_data(r3.stdout or "")) if kx_ok(r3) else None
    if cur2 is not None and abs(cur2 - test_val) <= 1:
        intel_uv_set(0)  # 恢复 0
        return {"vendor": "intel", "supported": True, "current": 0}
    return {"vendor": "intel", "supported": False, "reason": "write_ignored", "current": 0}

# ==========================================================================
# 风扇控制 (Fan Control) — GPD Win 5
# --------------------------------------------------------------------------
# 数据来源: HandheldCompanion (作者 Valkirie) GPDWin5.cs, 采用 CC BY-NC-SA 4.0
#   https://creativecommons.org/licenses/by-nc-sa/4.0/
# 移植说明: 仅搬运寄存器映射与语义, 不抄 C# 代码; EC 访问经 PawnIO LpcIO 模块。
# EC 端口: 命令/状态口 0x4E, 数据口 0x4F (GPD Win5 的 EC base 在非默认位置)。
# 协议: 标准 ACPI EC 命令 (RD_EC=0x80 / WR_EC=0x81) + IBF/OBF 握手; 内核级标准,
#       不会蓝屏, 最坏情况读到垃圾或超时 (HC 的 ECRamDirectWriteByte 语义同理)。
# 注意: EC RAM 地址按 8-bit 偏移写入; 若本机 RPM 读到异常值, 需在真机验证地址宽度。
# ==========================================================================
EC_SC, EC_DATA = 0x4E, 0x4F
EC_CMD_RD, EC_CMD_WR = 0x80, 0x81
EC_IBF, EC_OBF = 0x02, 0x01
FAN_RPM_HI, FAN_RPM_LO = 0x478, 0x479
FAN_DUTY1, FAN_DUTY2 = 0x47A, 0x47B
FAN_DUTY_MIN, FAN_DUTY_MAX = 0, 244

def _lpcio_open():
    """打开 PawnIO 并加载 LpcIO.bin (namazso/PawnIO.Modules, LGPL 2.1), 选 slot1=0x4E/0x4F。"""
    p = Pawn(DLL)
    rc = p.open()
    if rc != 0:
        log("FATAL: pawnio_open 0x%08X" % (rc & 0xFFFFFFFF)); p.close(); return None, 3
    if p.load(LPCIO_BIN) != 0:
        log("FATAL: 加载 LpcIO.bin 失败:", LPCIO_BIN); p.close(); return None, 4
    hr, _ = p.execute("ioctl_select_slot", [1], 0)   # slot1 -> 0x4E/0x4F
    if (hr & 0xFFFFFFFF) != 0:
        log("FATAL: LpcIO select slot1 失败 0x%08X" % (hr & 0xFFFFFFFF)); p.close(); return None, 4
    return p, 0

def _ec_pio_outb(p, port, val):
    p.execute("ioctl_pio_outb", [port & 0xFFFF, val & 0xFF], 0)

def _ec_pio_inb(p, port):
    hr, out = p.execute("ioctl_pio_inb", [port & 0xFFFF], 1)
    return out[0] if out else 0

def _ec_wait_ibf(p, timeout=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if (_ec_pio_inb(p, EC_SC) & EC_IBF) == 0:
            return True
        time.sleep(0.001)
    return False

def _ec_wait_obf(p, timeout=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if (_ec_pio_inb(p, EC_SC) & EC_OBF) != 0:
            return True
        time.sleep(0.001)
    return False

def _ec_read(p, addr):
    """标准 ACPI EC 读: RD_EC(0x80) + 地址(8-bit) + 读数据口。"""
    if not _ec_wait_ibf(p): return 0
    _ec_pio_outb(p, EC_SC, EC_CMD_RD)
    if not _ec_wait_ibf(p): return 0
    _ec_pio_outb(p, EC_DATA, addr & 0xFF)
    if not _ec_wait_obf(p): return 0
    return _ec_pio_inb(p, EC_DATA)

def _ec_write(p, addr, val):
    """标准 ACPI EC 写: WR_EC(0x81) + 地址(8-bit) + 写数据口。"""
    if not _ec_wait_ibf(p): return False
    _ec_pio_outb(p, EC_SC, EC_CMD_WR)
    if not _ec_wait_ibf(p): return False
    _ec_pio_outb(p, EC_DATA, addr & 0xFF)
    if not _ec_wait_ibf(p): return False
    _ec_pio_outb(p, EC_DATA, val & 0xFF)
    return True

def _detect_gpd_win5():
    """GPD Win5 检测: 强制文件 或 HID VID 0x2F24 注册表扫描。"""
    try:
        if os.path.exists(FAN_FORCE_FILE):
            log("  [fan] 检测到强制文件, 启用风扇控制"); return True
    except Exception:
        pass
    try:
        import winreg as _wr
        root = _wr.OpenKey(_wr.HKEY_LOCAL_MACHINE, r"SYSTEM\CurrentControlSet\Enum\HID")
        i = 0
        while True:
            try:
                dev = _wr.EnumKey(root, i); i += 1
            except Exception:
                break
            try:
                sub = _wr.OpenKey(root, dev)
            except Exception:
                continue
            j = 0
            while True:
                try:
                    inst_name = _wr.EnumKey(sub, j); j += 1
                except Exception:
                    break
                try:
                    inst = _wr.OpenKey(sub, inst_name)
                    hwid, _ = _wr.QueryValueEx(inst, "HardwareID")
                    if "VID_2F24" in " ".join(hwid).upper():
                        log("  [fan] HID 检测到 GPD (VID_2F24)"); return True
                except Exception:
                    pass
    except Exception as e:
        log("  [fan] HID 扫描异常: %s" % e)
    return False

def fan_read_rpm(p):
    hi = _ec_read(p, FAN_RPM_HI)
    lo = _ec_read(p, FAN_RPM_LO)
    return ((hi & 0xFF) << 8) | (lo & 0xFF)

def fan_cmd_detect():
    """探测 GPD Win5 并读取当前 RPM。非 GPD 返回 supported=false。"""
    is_gpd = _detect_gpd_win5()
    if not is_gpd:
        print(json.dumps({"supported": False, "isGPDWin5": False}))
        return 0
    p, rc = _lpcio_open()
    if rc != 0:
        print(json.dumps({"supported": False, "isGPDWin5": True, "reason": "lpcio_open_failed"}))
        return 0
    try:
        print(json.dumps({"supported": True, "isGPDWin5": True, "rpm": fan_read_rpm(p)}))
        return 0
    finally:
        p.close()

# ==========================================================================
# 通用 (台式机) 传输 — 通过 FanControl 的 WebServer 插件 (HTTP/JSON) 控制主板风扇
# --------------------------------------------------------------------------
# FanControl 本质 = LibreHardwareMonitor 的 UI; 其 WebServer 插件暴露:
#   GET  /api/sensors        传感器列表 [{Id,Name,Type,Value,...}]
#   GET  /api/fancontrollers 风扇控制器列表 [{Id,Name,...}]
#   POST /api/control        下发控制 {id, type:"Fixed"|"Curve", value:0-100}
# 端点/端口随版本可能不同, 故做成可配置(fan_generic.json) + 自动探测; 原始响应回显便于校准。
# ==========================================================================
FC_DEFAULT_PORTS = [25560, 8080, 9090, 8000]   # 仅常见端口, 缩短探测时间
FC_ENDPOINT_TTL = 30                            # 端点探测结果缓存秒数, 避免每次轮询都扫端口
FAN_ENDPOINT_CACHE = os.path.join(BASE, "fan_endpoint.cache")
FAN_GENERIC_CFG = os.path.join(BASE, "fan_generic.json")   # 可选覆盖: {url, ports, fan, sensor}

def _fc_load_cfg():
    try:
        if os.path.exists(FAN_GENERIC_CFG):
            with open(FAN_GENERIC_CFG, "r", encoding="utf-8") as f:
                return json.load(f) or {}
    except Exception:
        pass
    return {}

def _fc_http_get(url, timeout=2.0):
    req = urllib.request.Request(url, headers={"Accept": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8", "ignore"))

def _fc_http_post(url, payload, timeout=3.0):
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(url, data=data,
                                 headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.read().decode("utf-8", "ignore")

def _fc_cache_base(ttl=FC_ENDPOINT_TTL):
    """读缓存的 FanControl base URL; 返回 base 字符串 / '' (TTL 内已确认无) / 'MISS'。"""
    try:
        if os.path.exists(FAN_ENDPOINT_CACHE):
            if time.time() - os.path.getmtime(FAN_ENDPOINT_CACHE) <= ttl:
                with open(FAN_ENDPOINT_CACHE, "r", encoding="utf-8") as f:
                    return f.read().strip()
    except Exception:
        pass
    return "MISS"

def _fc_write_cache_base(base):
    try:
        with open(FAN_ENDPOINT_CACHE, "w", encoding="utf-8") as f:
            f.write(base or "")
    except Exception:
        pass

def _fc_scan_endpoints(cfg):
    """扫描本地候选端口, 返回 (base, sensors, fans) 或 (None, [], [])。"""
    ports = cfg.get("ports") or FC_DEFAULT_PORTS
    for port in ports:
        base = "http://localhost:%d" % int(port)
        try:
            sensors = _fc_http_get(base + "/api/sensors", 0.8)
            fans = _fc_http_get(base + "/api/fancontrollers", 0.8)
            if isinstance(sensors, list) and isinstance(fans, list):
                return base, sensors, fans
        except Exception:
            continue
    return None, [], []

def _fc_find_endpoint():
    """探测 FanControl WebServer: 返回 (base, sensors, fans) 或 None。带缓存避免轮询扫端口。"""
    cfg = _fc_load_cfg()
    if cfg.get("url"):
        base = str(cfg["url"]).rstrip("/")
        try:
            sensors = _fc_http_get(base + "/api/sensors", 2.0)
            fans = _fc_http_get(base + "/api/fancontrollers", 2.0)
            if isinstance(sensors, list) and isinstance(fans, list):
                return base, sensors, fans
        except Exception:
            return None
        return None
    cached = _fc_cache_base()
    if cached != "MISS":
        if cached == "":
            return None
        try:
            sensors = _fc_http_get(cached + "/api/sensors", 1.5)
            fans = _fc_http_get(cached + "/api/fancontrollers", 1.5)
            if isinstance(sensors, list) and isinstance(fans, list):
                return cached, sensors, fans
        except Exception:
            pass
        # 缓存 base 已失效, 重新扫描
    base, sensors, fans = _fc_scan_endpoints(cfg)
    _fc_write_cache_base(base or "")
    return (base, sensors, fans) if base else None

def _fc_pick_temp(sensors):
    cands = [s for s in sensors if str(s.get("Type", "")).lower() == "temperature"]
    for s in cands:
        n = (s.get("Name") or "").lower()
        if "cpu" in n or "package" in n or "core" in n:
            return s.get("Id"), s.get("Name")
    if cands:
        return cands[0].get("Id"), cands[0].get("Name")
    return None, None

def _fc_pick_fan(fans, cfg):
    if cfg.get("fan"):
        return cfg["fan"], cfg.get("fanName", cfg["fan"])
    for f in fans:
        n = (f.get("Name") or "").lower()
        if any(k in n for k in ("cpu", "sys", "chassis", "pump")):
            return f.get("Id"), f.get("Name")
    if fans:
        return fans[0].get("Id"), fans[0].get("Name")
    return None, None

def _fc_read_rpm(base, fan_id, sensors):
    if fan_id:
        for s in sensors:
            if str(s.get("Type", "")).lower() in ("fan", "control", "rpm") and fan_id in (s.get("Name", "") or ""):
                try:
                    return int(float(s.get("Value", 0)))
                except Exception:
                    pass
    for s in sensors:
        if str(s.get("Type", "")).lower() in ("fan", "control", "rpm"):
            try:
                return int(float(s.get("Value", 0)))
            except Exception:
                pass
    return 0

def _wmi_thermal_temp():
    """温度兜底来源: ACPI thermal zone (deciKelvin -> °C)。"""
    try:
        out = subprocess.run(
            ["powershell", "-NoProfile", "-Command",
             "Get-CimInstance -Namespace root/wmi -ClassName MSAcpi_ThermalZoneTemperature | "
             "ForEach-Object { [math]::Round(($_.CurrentTemperature-2732)/10.0) }"],
            capture_output=True, text=True, errors="ignore", timeout=5,
            creationflags=CREATE_NO_WINDOW).stdout
        vals = []
        for tok in out.split():
            tok = tok.strip()
            try:
                vals.append(float(tok))
            except Exception:
                pass
        if vals:
            return max(vals)
    except Exception:
        pass
    return 0

def _fan_transport():
    """当前应使用的风扇传输: 'dedicated' (GPD EC) | 'generic' (FanControl) | 'none'。"""
    if _detect_gpd_win5():
        return "dedicated"
    if _fc_find_endpoint() is not None:
        return "generic"
    return "none"

def fan_cmd_detect():
    """统一探测: 专用(GPD) / 通用(FanControl) / 通用未就绪(给出引导)。"""
    if _detect_gpd_win5():
        p, rc = _lpcio_open()
        if rc != 0:
            print(json.dumps({"mode": "dedicated", "supported": True, "isGPDWin5": True,
                              "available": False, "rpm": 0, "reason": "lpcio_open_failed"}))
            return 0
        try:
            rpm = fan_read_rpm(p)
        finally:
            p.close()
        print(json.dumps({"mode": "dedicated", "supported": True, "isGPDWin5": True,
                          "available": True, "rpm": rpm}))
        return 0
    ep = _fc_find_endpoint()
    if ep:
        base, sensors, fans = ep
        cfg = _fc_load_cfg()
        tid, tname = _fc_pick_temp(sensors)
        fid, fname = _fc_pick_fan(fans, cfg)
        rpm = _fc_read_rpm(base, fid, sensors)
        print(json.dumps({
            "mode": "generic", "supported": True, "isGPDWin5": False, "available": True,
            "rpm": rpm, "url": base,
            "fans": [{"id": f.get("Id"), "name": f.get("Name")} for f in fans],
            "sensors": [{"id": s.get("Id"), "name": s.get("Name"), "type": s.get("Type")} for s in sensors],
            "tempSensorId": tid, "fanId": fid,
        }))
        return 0
    # 通用但未检测到 FanControl: 仍显示卡片并给出安装引导
    print(json.dumps({
        "mode": "generic", "supported": True, "isGPDWin5": False, "available": False, "rpm": 0,
        "hint": "未检测到 FanControl。台式机通用方案需先安装并运行 FanControl，"
                "在 设置→插件 中启用 WebServer 插件 (默认 http://localhost:25560)，即可用温度坡度线性控制风扇。",
    }))
    return 0

def fan_cmd_temp():
    """返回当前温度 (°C)。优先 FanControl 传感器(任意机型运行 FC 均可), 兜底 WMI thermal zone。"""
    ep = _fc_find_endpoint()
    if ep:
        base, sensors, fans = ep
        tid, _ = _fc_pick_temp(sensors)
        if tid:
            for s in sensors:
                if s.get("Id") == tid:
                    try:
                        print(json.dumps({"temp": float(s.get("Value", 0)), "source": "fancontrol"}))
                        return 0
                    except Exception:
                        break
    print(json.dumps({"temp": _wmi_thermal_temp(), "source": "wmi"}))
    return 0

def fan_cmd_rpm():
    if _fan_transport() == "dedicated":
        p, rc = _lpcio_open()
        if rc != 0:
            print(json.dumps({"rpm": 0, "error": "lpcio_open_failed"})); return rc
        try:
            print(json.dumps({"rpm": fan_read_rpm(p)}))
            return 0
        finally:
            p.close()
    ep = _fc_find_endpoint()
    if ep:
        base, sensors, fans = ep
        fid, _ = _fc_pick_fan(fans, _fc_load_cfg())
        print(json.dumps({"rpm": _fc_read_rpm(base, fid, sensors)}))
        return 0
    print(json.dumps({"rpm": 0}))
    return 0

def fan_cmd_set_auto():
    """自动: 专用=双风扇占空比写 0 (主板控速); 通用=交还 FanControl 曲线。"""
    if _fan_transport() == "dedicated":
        p, rc = _lpcio_open()
        if rc != 0:
            print(json.dumps({"ok": False, "error": "lpcio_open_failed"})); return rc
        try:
            _ec_write(p, FAN_DUTY1, 0)
            _ec_write(p, FAN_DUTY2, 0)
            print(json.dumps({"ok": True, "mode": "auto"}))
            return 0
        finally:
            p.close()
    ep = _fc_find_endpoint()
    if ep:
        base, sensors, fans = ep
        fid, _ = _fc_pick_fan(fans, _fc_load_cfg())
        try:
            resp = _fc_http_post(base + "/api/control", {"id": fid, "type": "Curve"})
            print(json.dumps({"ok": True, "mode": "auto", "raw": (resp or "")[:160]}))
        except Exception as e:
            print(json.dumps({"ok": False, "error": str(e)}))
        return 0
    print(json.dumps({"ok": False, "error": "no_transport"}))
    return 1

def fan_cmd_set_duty(pct):
    """手动占空比 0-100%。专用=EC duty 写(双风扇同步); 通用=FanControl Fixed。"""
    pct = max(0, min(100, int(pct)))
    if _fan_transport() == "dedicated":
        duty = int(round(pct * FAN_DUTY_MAX / 100.0))
        duty = max(FAN_DUTY_MIN, min(FAN_DUTY_MAX, duty))
        p, rc = _lpcio_open()
        if rc != 0:
            print(json.dumps({"ok": False, "error": "lpcio_open_failed"})); return rc
        try:
            _ec_write(p, FAN_DUTY1, duty)
            _ec_write(p, FAN_DUTY2, duty)
            print(json.dumps({"ok": True, "percent": pct, "duty": duty}))
            return 0
        finally:
            p.close()
    ep = _fc_find_endpoint()
    if ep:
        base, sensors, fans = ep
        fid, _ = _fc_pick_fan(fans, _fc_load_cfg())
        try:
            resp = _fc_http_post(base + "/api/control", {"id": fid, "type": "Fixed", "value": pct})
            print(json.dumps({"ok": True, "percent": pct, "raw": (resp or "")[:160]}))
        except Exception as e:
            print(json.dumps({"ok": False, "error": str(e)}))
        return 0
    print(json.dumps({"ok": False, "error": "no_transport"}))
    return 1

# ==========================================================================
# Intel: Newko 原生路径 — KX.exe (MMIO wrmem16 + MSR wrmsr), 不走 PawnIO
# ==========================================================================
def _clamp_w(w):
    """Newko x9: 钳制到 0..300 并四舍五入。"""
    try:
        w = float(w)
    except Exception:
        return None
    if w != w:  # NaN
        return None
    return max(0, min(INTEL_MAX_W, int(round(w))))

def _hex16(w):
    """Newko z9: (32768 | W*8 & 32767) 的大写 hex, 用于 MMIO wrmem16。"""
    return format(32768 | ((_clamp_w(w) or 0) * 8 & 32767), "X")

def _hex3(w):
    """Newko U9: (W*8 & 4095) 的 3 位大写 hex, 用于 MSR 0x610。"""
    return format(((_clamp_w(w) or 0) * 8) & 4095, "X").zfill(3)

def kx_run(args, timeout=15):
    """调用 KX.exe; 返回 CompletedProcess 或 None。成败用 kx_ok() 看输出文本判定
    （KX 会把读回值作为退出码返回, returncode 可能非零, 不能依赖 ==0）。"""
    if not os.path.exists(KX_EXE):
        log("FATAL: KX.exe 不存在:", KX_EXE)
        return None
    cmd = [KX_EXE] + list(args)
    log("  KX: " + " ".join(args))
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, errors="ignore",
                           timeout=timeout, creationflags=CREATE_NO_WINDOW)
        if not kx_ok(r):
            log("  KX rc=%d %s" % (r.returncode, (r.stdout or r.stderr or "").strip()[:200]))
        return r
    except Exception as e:
        log("  KX EXCEPTION: %s" % e)
        return None

def kx_ok(r):
    """KX 成功判定: 输出无驱动加载错误即为成功。KX 读操作会把值作为退出码(可能非零),
    故不能依赖 returncode==0。仅当输出含驱动加载失败特征才算失败。"""
    if r is None:
        return False
    out = ((r.stdout or "") + (r.stderr or "")).lower()
    if "can not be loaded" in out:
        return False
    # ErrorCode 32 = 文件被其他进程独占(如 HWiNFO 占着 KX 驱动文件)
    if "errorcode" in out and ("无法访问" in out or "in use" in out or "sharing" in out):
        return False
    return True

def _parse_kx_return(stdout):
    """Newko sx: 匹配 'Return <hex|dec>'; -1/0xFFFFFFFF 视为无效。"""
    if not stdout:
        return None
    m = re.search(r"\bReturn\s+(-?0x[0-9a-fA-F]+|-?\d+)", stdout)
    if not m:
        return None
    tok = m.group(1).lower()
    neg = tok.startswith("-")
    if neg:
        tok = tok[1:]
    try:
        v = int(tok, 16) if tok.startswith("0x") else int(tok, 10)
    except Exception:
        return None
    if neg:
        v = -v
    if v == 0xFFFFFFFF or v == -1:
        return None
    return v

_mchbar_cache = "unset"  # "unset" | None | str

def probe_mchbar():
    """Newko aq: 依次 /rdmem32 候选地址, 读到有效 Return 即取 '<候选>59' 作寄存器前缀。
    (MCHBAR+0x59A0/0x59A4 = PKG RAPL MMIO 限制寄存器)"""
    global _mchbar_cache
    if _mchbar_cache != "unset":
        return _mchbar_cache
    for cand in MCHBAR_CANDIDATES:
        r = kx_run(["/rdmem32", cand])
        if r is None or not kx_ok(r):
            continue
        v = _parse_kx_return(r.stdout or "")
        if v is None:
            continue
        prefix = cand + "59"
        log("  [Intel MCHBAR] Detected: %s (rdmem32 %s = %s)" % (prefix, cand, v))
        _mchbar_cache = prefix
        return prefix
    log("  [Intel MCHBAR] 所有候选地址无效 — MMIO TDP 不可用")
    _mchbar_cache = None
    return None

def intel_set(pl1_w, pl2_w, dry=False):
    """Newko jZ 同款: MMIO(必须成功) + MSR(失败非致命)。"""
    o, f = _clamp_w(pl1_w), _clamp_w(pl2_w)
    if o is None or f is None:
        log("  Intel TDP 参数无效"); return 2
    # 保证 PL2 > PL1 (UXTU 硬性规则): 封顶时压 PL1, 否则抬 PL2
    if f <= o:
        if f < INTEL_MAX_W:
            f = o + 1
        else:
            o = max(0, f - 1)
        log("  [Intel] 修正 PL2<=PL1 -> PL1=%dW PL2=%dW" % (o, f))
    log("  Intel (Newko KX) PL1=%dW PL2=%dW" % (o, f))
    if dry:
        log("  [dry-run] MMIO a0<-0x%s a4<-0x%s ; MSR 0x610 <- 0x00438%s 00DD8%s (未写入)"
            % (_hex16(o), _hex16(f), _hex3(f), _hex3(o)))
        return 0
    prefix = probe_mchbar()
    if not prefix:
        log("  MCHBAR 探测失败 — Intel MMIO TDP 不可用"); return 6
    # 1) MMIO: PL1 -> +a0, 100ms, PL2 -> +a4 (Newko 精确时序)
    r1 = kx_run(["/wrmem16", prefix + "a0", "0x" + _hex16(o)])
    if r1 is None or not kx_ok(r1):
        log("  MMIO PL1 写入失败"); return 6
    time.sleep(0.1)
    r2 = kx_run(["/wrmem16", prefix + "a4", "0x" + _hex16(f)])
    if r2 is None or not kx_ok(r2):
        log("  MMIO PL2 写入失败"); return 6
    log("  [Intel TDP] MMIO OK: PL1=%dW PL2=%dW (MCHBAR=%s)" % (o, f, prefix))
    # 2) MSR: /wrmsr 0x610 0x00438<PL2> 00DD8<PL1> (Newko: 失败非致命)
    hi = "0x00438" + _hex3(f)
    lo = "00DD8" + _hex3(o)
    r3 = kx_run(["/wrmsr", "0x610", hi, lo])
    if kx_ok(r3):
        log("  [Intel TDP] MSR OK: /wrmsr 0x610 %s %s" % (hi, lo))
    else:
        log("  [Intel TDP] MSR 写入失败 (非致命, MMIO 已生效)")
    return 0

def intel_get(dry=False):
    if dry:
        log("  [dry-run] Intel 跳过读取"); return 0
    r = kx_run(["/rdmsr", "0x610"])
    out = (r.stdout or "") if r else ""
    if not kx_ok(r):
        log("  Intel /rdmsr 0x610 失败 (KX 驱动未加载? 关闭 HWiNFO/ThrottleStop 后重试)"); return 6
    # KX 成功时输出 "Msr Data : 0xHHHHHHHH 0xLLLLLLLL" (高/低 32 位), 退出码可能非零
    m = re.search(r"Msr Data\s*:\s*0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)", out)
    if m:
        high = int(m.group(1), 16); low = int(m.group(2), 16)
        pl1 = (low & 0x7FFF) / 8.0
        pl2 = (high & 0x7FFF) / 8.0
        log("  Intel MSR 0x610 = 0x%08X%08X  PL1=%.1fW PL2=%.1fW" % (high, low, pl1, pl2))
    else:
        # 兜底: 解析 "Return <value>"
        v = _parse_kx_return(out)
        if v is not None and v > 0:
            pl1 = (v & 0x7FFF) / 8.0
            pl2 = ((v >> 32) & 0x7FFF) / 8.0
            log("  Intel MSR 0x610 (Return) PL1=%.1fW PL2=%.1fW" % (pl1, pl2))
        else:
            log("  Intel MSR 0x610 无有效数据")
    return 0

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

def _optiscaler_cfg():
    base = os.path.expandvars(r"%APPDATA%\OptiscalerClient")
    cache = os.path.join(base, "Cache")
    return {
        "client_base": base,
        "src_opti": os.path.join(cache, "OptiScaler", "0.9.4"),
        "src_extra": os.path.join(cache, "Extras", "FSR_4.1.1"),
        "src_patch": os.path.join(cache, "OptiPatcher", "rolling"),
        "inject": "dxgi.dll",
    }

def _optiscaler_plan(game_dir, cfg):
    """返回写入计划 list[{src, dst, rel, role}]（基础包与 Extra 同名时 Extra 后写确保最终生效）。"""
    plan = []
    # 1) 主 DLL -> 注入名
    main = os.path.join(cfg["src_opti"], "OptiScaler.dll")
    if os.path.isfile(main):
        plan.append({"src": main, "dst": os.path.join(game_dir, cfg["inject"]),
                     "rel": cfg["inject"], "role": "主DLL->注入"})
    # 2) 基础包其余文件（排除 OptiScaler.dll，含子目录）
    if os.path.isdir(cfg["src_opti"]):
        for root, dirs, files in os.walk(cfg["src_opti"]):
            for fn in files:
                if fn.lower() == "optiscaler.dll":
                    continue
                sp = os.path.join(root, fn)
                rel = os.path.relpath(sp, cfg["src_opti"]).replace("/", "\\")
                plan.append({"src": sp, "dst": os.path.join(game_dir, rel),
                             "rel": rel, "role": "基础包"})
    # 3) FSR Extra 覆盖（amd_fidelityfx_upscaler_dx12.dll，后写确保覆盖基础包同名）
    extra = os.path.join(cfg["src_extra"], "amd_fidelityfx_upscaler_dx12.dll")
    if os.path.isfile(extra):
        plan.append({"src": extra,
                     "dst": os.path.join(game_dir, "amd_fidelityfx_upscaler_dx12.dll"),
                     "rel": "amd_fidelityfx_upscaler_dx12.dll", "role": "FSR Extra 覆盖"})
    # 4) OptiPatcher -> plugins/OptiPatcher.asi
    pat = os.path.join(cfg["src_patch"], "OptiPatcher.asi")
    if os.path.isfile(pat):
        plan.append({"src": pat,
                     "dst": os.path.join(game_dir, "plugins", "OptiPatcher.asi"),
                     "rel": "plugins\\OptiPatcher.asi", "role": "OptiPatcher"})
    return plan

def _optiscaler_status(game_dir, cfg):
    """installed = 注入 DLL 存在且哈希与缓存 OptiScaler.dll 一致。
    仅 hash 匹配才算已装：很多游戏自带 dxgi.dll，不能用「存在即已装」判断。"""
    inject_dst = os.path.join(game_dir, cfg["inject"])
    src = os.path.join(cfg["src_opti"], "OptiScaler.dll")
    if not os.path.isfile(inject_dst):
        return {"installed": False, "reason": "inject_missing"}
    if not os.path.isfile(src):
        # 源缺失无法比对哈希，改用 OptiScaler.ini 作为弱信号
        return {"installed": os.path.isfile(os.path.join(game_dir, "OptiScaler.ini")),
                "reason": "inject_present_no_src"}
    if _sha256(inject_dst) == _sha256(src):
        return {"installed": True, "reason": "hash_match"}
    return {"installed": False, "reason": "inject_present_hash_diff"}

def _ymcc_backup_dir(game_dir):
    key = hashlib.md5(game_dir.lower().encode("utf-8", "ignore")).hexdigest()[:12]
    return os.path.join(os.path.expandvars(r"%APPDATA%"), "YeManCC", "optiscaler_backups", key)

def _optiscaler_install(game_dir, cfg, dry):
    plan = _optiscaler_plan(game_dir, cfg)
    missing = [p["src"] for p in plan if not os.path.isfile(p["src"])]
    if missing:
        return {"ok": False, "msgs": ["源文件缺失:"] + missing}
    if not os.path.isdir(game_dir):
        return {"ok": False, "msgs": ["游戏目录不存在: " + game_dir]}
    if dry:
        return {"ok": True, "dry": True, "count": len(plan),
                "plan": [p["rel"] for p in plan]}
    bdir = _ymcc_backup_dir(game_dir)
    files_dir = os.path.join(bdir, "files")
    manifest = {"game_dir": game_dir,
                "installed_at": time.strftime("%Y-%m-%d %H:%M:%S"),
                "os_version": "0.9.4", "inject": cfg["inject"],
                "items": []}
    os.makedirs(files_dir, exist_ok=True)
    written = 0
    for p in plan:
        dst = p["dst"]
        d = os.path.dirname(dst)
        if d:
            os.makedirs(d, exist_ok=True)
        had_original = os.path.isfile(dst)
        bak_rel = None
        if had_original:
            # 备份原文件（按目标相对路径命名，仅首次）
            bak_name = p["rel"].replace("\\", "__")
            bak_path = os.path.join(files_dir, bak_name)
            if not os.path.exists(bak_path):
                try:
                    shutil.copy2(dst, bak_path)
                except Exception as e:
                    _olog("install backup failed", p["rel"], e)
            bak_rel = bak_name
        try:
            shutil.copy2(p["src"], dst)
            written += 1
            manifest["items"].append({"rel": p["rel"],
                                       "had_original": had_original,
                                       "backup": bak_rel})
        except Exception as e:
            _olog("install copy failed", p["rel"], e)
            return {"ok": False,
                    "msgs": ["写入失败: " + p["rel"] + " -> " + str(e)],
                    "written": written}
    try:
        with open(os.path.join(bdir, "manifest.json"), "w", encoding="utf-8") as f:
            json.dump(manifest, f, ensure_ascii=False, indent=2)
    except Exception as e:
        _olog("install manifest write failed", e)
    return {"ok": True, "written": written, "backup": bdir}

def _find_client_backup(game_dir):
    """在 OptiScalerClient 的 Backups 里找匹配本游戏目录的 manifest（用于卸载其安装的游戏）。"""
    bdir = os.path.join(os.path.expandvars(r"%APPDATA%\OptiscalerClient"), "Backups")
    if not os.path.isdir(bdir):
        return None
    target = game_dir.replace("/", "\\").lower().rstrip("\\")
    for name in os.listdir(bdir):
        mfp = os.path.join(bdir, name, "manifest.json")
        if not os.path.isfile(mfp):
            continue
        try:
            with open(mfp, "r", encoding="utf-8", errors="ignore") as f:
                m = json.load(f)
        except Exception:
            continue
        gd = (m.get("InstalledGameDirectory") or "").replace("/", "\\").lower().rstrip("\\")
        if gd == target:
            return m, os.path.join(bdir, name)
    return None

def _optiscaler_uninstall(game_dir, cfg, dry):
    ybdir = _ymcc_backup_dir(game_dir)
    ymanifest = os.path.join(ybdir, "manifest.json")
    removed = 0
    restored = 0
    actions = []

    if os.path.isfile(ymanifest):
        # 主路径：YeManCC 自管备份（我们安装的游戏）
        try:
            with open(ymanifest, "r", encoding="utf-8", errors="ignore") as f:
                m = json.load(f)
        except Exception:
            return {"ok": False, "msgs": ["读取 YeManCC 备份 manifest 失败"]}
        files_dir = os.path.join(ybdir, "files")
        for it in m.get("items", []):
            rel = it["rel"]
            dst = os.path.join(game_dir, rel)
            bak = os.path.join(files_dir, it["backup"]) if it.get("backup") else None
            if it.get("had_original") and bak and os.path.isfile(bak):
                if dry:
                    actions.append("restore " + rel)
                else:
                    try:
                        shutil.copy2(bak, dst); restored += 1
                        actions.append("restore " + rel)
                    except Exception as e:
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
        return {"ok": True, "via": "yemancc", "removed": removed,
                "restored": restored, "dry": dry, "actions": actions}

    # 回退路径：OptiScalerClient 自带 Backups（游戏是它装的）
    found = _find_client_backup(game_dir)
    if not found:
        return {"ok": False,
                "msgs": ["未找到该游戏的 OptiScaler 安装记录，无法安全卸载。"
                         "请手动删除游戏目录下的 OptiScaler 相关文件"
                         "(dxgi.dll / OptiScaler.ini / D3D12_Optiscaler / Licenses 等)。"]}
    m, cdir = found
    files_dir = os.path.join(cdir, "files")
    # 直接以 Backups/<dir>/files/ 里的原始备份为准：
    #  - 某 InstalledFile 在 files/ 中有同名备份 -> 还原原件（覆盖 OptiScaler 版）
    #  - 否则该文件是 OptiScaler 新建/覆盖且无原备份 -> 删除
    # 这样即使 manifest 的 BackedUpFiles 字段为空，也能正确还原（files/ 始终含原件）。
    for rel in m.get("InstalledFiles", []):
        dst = os.path.join(game_dir, rel)
        bak = os.path.join(files_dir, rel)
        if os.path.isfile(bak):
            if dry:
                actions.append("restore " + rel)
            else:
                try:
                    shutil.copy2(bak, dst); restored += 1
                    actions.append("restore " + rel)
                except Exception as e:
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
                    actions.append("delete FAIL " + rel + " " + str(e))
    # 清理空目录
    if not dry:
        for d in m.get("InstalledDirectories", []):
            dd = os.path.join(game_dir, d)
            if os.path.isdir(dd):
                try:
                    if not os.listdir(dd):
                        os.rmdir(dd)
                except Exception:
                    pass
    return {"ok": True, "via": "optiscalerclient", "removed": removed,
            "restored": restored, "dry": dry, "actions": actions}

def optiscaler_cmd(argv, dry=False):
    if len(argv) < 2:
        print(json.dumps({"ok": False, "msgs": ["usage: optiscaler <status|install|uninstall> <game_dir> [--dry-run]"]}))
        return 2
    sub = argv[0].lower()
    game_dir = argv[1]
    cfg = _optiscaler_cfg()
    if sub == "status":
        if not os.path.isdir(game_dir):
            print(json.dumps({"ok": False, "installed": False,
                              "msgs": ["游戏目录不存在: " + game_dir]}))
            return 2
        st = _optiscaler_status(game_dir, cfg)
        print(json.dumps({"ok": True, "installed": st["installed"], "reason": st["reason"]}))
        return 0
    if sub == "install":
        r = _optiscaler_install(game_dir, cfg, dry)
        print(json.dumps(r))
        return 0 if r.get("ok") else 7
    if sub == "uninstall":
        r = _optiscaler_uninstall(game_dir, cfg, dry)
        print(json.dumps(r))
        return 0 if r.get("ok") else 7
    print(json.dumps({"ok": False, "msgs": ["unknown optiscaler subcommand: " + sub]}))
    return 2

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
    if cmd in NEEDS_DRIVER and not is_admin() and not dry:
        log("当前非管理员，自提权以访问硬件通道...")
        if relaunch_elevated():
            return 0
        msgbox("TDP 调节需要管理员权限。\n请右键以管理员身份运行，或在 UAC 提示时点击\"是\"。",
               "需要管理员权限", MB_ERR)
        return 3
    # 风扇专用(GPD EC)需要管理员+PawnIO; 通用(FanControl/HTTP)不需要提权
    if cmd == "fan" and _detect_gpd_win5() and not is_admin() and not dry:
        log("GPD 专用风扇控制需要管理员权限...")
        if relaunch_elevated():
            return 0
        msgbox("GPD 风扇控制需要管理员权限。\n请右键以管理员身份运行，或在 UAC 提示时点击\"是\"。",
               "需要管理员权限", MB_ERR)
        return 3

    # AMD 路径依赖 PawnIO; Intel 路径走 KX.exe 不需要; 风扇路径(任意厂商)依赖 PawnIO。
    v_for_ensure = (vendor_ov or detect_vendor())
    need_pawnio = (cmd in ("set", "get", "restore", "info", "set-amd", "uv") and v_for_ensure == "amd")
    if need_pawnio and not dry:
        if not ensure_pawnio():
            log("FATAL: PawnIO 不可用，且自动安装失败。"); return 3

    if cmd == "info":
        v = vendor_ov or detect_vendor()
        log("vendor =", v)
        log("PawnIOLib =", DLL)
        log("RyzenSMU.bin =", RYZEN_SMU, "(Newko 同款)")
        log("KX.exe =", KX_EXE, "(Newko 同款)")
        if v == "amd": return amd_get()
        if v == "intel": return intel_get(dry)
        return 0

    if cmd == "get":
        v = vendor_ov or detect_vendor()
        if v == "amd":   return amd_get()
        if v == "intel": return intel_get(dry)
        log("厂商未识别"); return 5

    if cmd == "restore":
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
            if v == "amd":
                print(json.dumps(amd_uv_probe()))
            elif v == "intel":
                print(json.dumps(intel_uv_probe()))
            else:
                print(json.dumps({"vendor": "", "supported": False, "reason": "unknown_vendor", "current": 0}))
            return 0
        log("未知 uv 子命令:", sub); return 2

    if cmd == "fan":
        if not rest:
            log("usage: fan detect | fan temp | fan set-auto | fan set <percent 0-100> | fan rpm"); return 2
        sub = rest[0].lower(); rest2 = rest[1:]
        if sub == "detect":
            return fan_cmd_detect()
        if sub == "temp":
            return fan_cmd_temp()
        if sub == "set-auto":
            return fan_cmd_set_auto()
        if sub == "set":
            if not rest2:
                log("usage: fan set <percent 0-100>"); return 2
            try:
                pct = int(rest2[0])
            except Exception:
                log("fan set 参数必须是整数(0-100)"); return 2
            return fan_cmd_set_duty(pct)
        if sub == "rpm":
            return fan_cmd_rpm()
        log("未知 fan 子命令:", sub); return 2

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
    sys.exit(main())
