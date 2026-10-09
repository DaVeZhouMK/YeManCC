// hub_cycle_probe.cpp —— 本机验证 hidHideCyclePortHub 调用链可行性（dry-run）。
//
// 目的（第三十三轮延续，用户目标"正反推直到无新增未知缺口"）：
//   1) 对本机真实 USB 设备树执行与 main.cpp hidHideCyclePortHub 完全相同的
//      寻径：CM_Locate -> parent 链找 USBHUB 服务节点 -> composite 的
//      Device_Address(端口) -> GUID_DEVINTERFACE_USB_HUB 接口路径 -> CreateFile。
//   2) dry-run 默认不执行 IOCTL_USB_HUB_CYCLE_PORT（避免打断本机外设）；
//      加 --ioctl <instanceId> 才真实执行（供可控验证 IOCTL=0x220444 返回）。
//   3) 输出 KV 行，人工核对 hub/port/hubPath/handleOK。
//
// 编译（VS2022 x64 native tools）：
//   cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DUNICODE /D_UNICODE hub_cycle_probe.cpp /Fe:hub_cycle_probe.exe setupapi.lib cfgmgr32.lib

#define UNICODE 1
#define _UNICODE 1
#define WIN32_LEAN_AND_MEAN
#define INITGUID
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <devpkey.h>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

static constexpr ULONG kIoctlUsbHubCyclePort = 0x00220444; // 第三十三轮反编译实证值（HC 捆绑 DLL 2229316）
struct UsbCyclePortParams { ULONG ConnectionIndex; ULONG StatusReturned; };

static std::wstring EnumeratorOf(DEVINST dn) {
    WCHAR buf[64]{};
    ULONG len = sizeof(buf);
    if (CM_Get_DevNode_Registry_PropertyW(dn, 0x17 /*CM_DRP_ENUMERATOR_NAME*/,
        nullptr, buf, &len, 0) == CR_SUCCESS && len > 2)
        return std::wstring(buf, (len / sizeof(wchar_t)) - 1);
    return {};
}
static bool ServiceOf(DEVINST dn, std::wstring& out) {
    WCHAR buf[512]{};
    ULONG len = sizeof(buf);
    if (CM_Get_DevNode_Registry_PropertyW(dn, 0x05 /*CM_DRP_SERVICE*/,
        nullptr, buf, &len, 0) != CR_SUCCESS || len <= 2) return false;
    out.assign(buf, (len / sizeof(wchar_t)) - 1);
    return true;
}
static bool AddressOf(DEVINST dn, ULONG& out) {
    ULONG len = sizeof(out);
    return CM_Get_DevNode_Registry_PropertyW(dn, 0x1D /*CM_DRP_ADDRESS*/,
        nullptr, &out, &len, 0) == CR_SUCCESS;
}

// 与 main.cpp hidHideCyclePortHub 相同的寻径；executeIoctl=false 时只验证链路可构造。
static bool CyclePortHub(const std::wstring& instanceId, bool executeIoctl) {
    wprintf(L"probe instance=%ls\n", instanceId.c_str());
    DEVINST dn = 0;
    if (CM_Locate_DevNodeW(&dn, const_cast<wchar_t*>(instanceId.c_str()), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) {
        wprintf(L"  locate=FAIL\n"); return false;
    }
    wprintf(L"  locate=OK enumerator=%ls\n", EnumeratorOf(dn).c_str());
    DEVINST hub = dn, composite = dn;
    bool foundHub = false;
    for (int i = 0; i < 16; ++i) {
        std::wstring svc;
        if (ServiceOf(hub, svc)) {
            std::wstring up = svc;
            std::transform(up.begin(), up.end(), up.begin(), [](wchar_t c) {
                return static_cast<wchar_t>(toupper(static_cast<unsigned char>(c))); });
            if (up.rfind(L"USBHUB", 0) == 0) { foundHub = true; break; }
        }
        composite = hub;
        DEVINST parent = 0;
        if (CM_Get_Parent(&parent, hub, 0) != CR_SUCCESS) break;
        hub = parent;
    }
    if (!foundHub) { wprintf(L"  hub=NOT-FOUND\n"); return false; }
    ULONG port = 0;
    if (!AddressOf(composite, port) || port == 0) { wprintf(L"  port=UNRESOLVED\n"); return false; }
    WCHAR hubId[MAX_DEVICE_ID_LEN]{};
    if (CM_Get_Device_IDW(hub, hubId, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) { wprintf(L"  hubid=FAIL\n"); return false; }
    static const GUID kUsbHubIface = { 0xf18a0e88, 0xc30c, 0x11d0, { 0x88, 0x15, 0x00, 0xa0, 0xc9, 0x06, 0xbe, 0xd8 } };
    ULONG listLen = 0;
    if (CM_Get_Device_Interface_List_SizeW(&listLen, const_cast<LPGUID>(&kUsbHubIface), hubId,
            CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS || listLen == 0) {
        wprintf(L"  iface-size=FAIL\n"); return false;
    }
    std::wstring buffer(listLen, L'\0');
    if (CM_Get_Device_Interface_ListW(const_cast<LPGUID>(&kUsbHubIface), hubId,
            buffer.data(), listLen, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS) {
        wprintf(L"  iface=FAIL\n"); return false;
    }
    std::wstring hubPath = buffer.c_str();
    if (hubPath.empty()) { wprintf(L"  iface-path=EMPTY\n"); return false; }
    HANDLE h = CreateFileW(hubPath.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        wprintf(L"  hub-open=FAIL err=%lu\n", GetLastError());
        return false;
    }
    wprintf(L"  hub=%ls port=%lu ifacePath=OK handle=OK\n", hubId, port);
    if (!executeIoctl) {
        CloseHandle(h);
        wprintf(L"  ioctl=SKIPPED(dry-run)\n");
        return true;
    }
    UsbCyclePortParams params{ port, 0 };
    DWORD returned = 0;
    BOOL ok = DeviceIoControl(h, kIoctlUsbHubCyclePort, &params, sizeof(params), &params, sizeof(params), &returned, nullptr);
    DWORD err = GetLastError();
    CloseHandle(h);
    wprintf(L"  ioctl=ok:%d error=%lu status=%lu\n", ok ? 1 : 0, err, params.StatusReturned);
    return ok && params.StatusReturned == 0;
}

// 枚举所有 USB 设备接口（GUID_DEVINTERFACE_USB_DEVICE）得到 instanceId 列表。
static std::vector<std::wstring> EnumerateUsbInstances() {
    std::vector<std::wstring> out;
    static const GUID kUsbDevIface = { 0xa5dcbf10, 0x6530, 0x11d2, { 0x90, 0x1f, 0x00, 0xc0, 0x4f, 0xb9, 0x51, 0xed } };
    HDEVINFO set = SetupDiGetClassDevsW(&kUsbDevIface, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return out;
    SP_DEVICE_INTERFACE_DATA ifc{ sizeof(ifc) };
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &kUsbDevIface, i, &ifc); ++i) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifc, nullptr, 0, &need, nullptr);
        if (need == 0 || need > 64 * 1024) continue;
        std::vector<BYTE> buf(need);
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifc, detail, need, nullptr, nullptr)) continue;
        std::wstring symlink(detail->DevicePath);
        // symlink -> instance id（DEVPKEY_Device_InstanceId，prop 10）
        {
            DEVPROPTYPE type = 0;
            WCHAR inst[MAX_DEVICE_ID_LEN]{};
            if (SetupDiGetDeviceInterfacePropertyW(set, &ifc, &DEVPKEY_Device_InstanceId,
                    &type, reinterpret_cast<PBYTE>(inst), sizeof(inst), nullptr, 0) && type == DEVPROP_TYPE_STRING) {
                std::wstring id(inst);
                if (!id.empty()) out.push_back(id);
            }
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    return out;
}

// 枚举所有 HID 设备接口（GUID_DEVINTERFACE_HID），用于验证 HID enumerator
// 上溯路径（无 USB parent 时的 HID child → USBHUB 寻径，同构 0910-18 的
// HID\VID_0B05&PID_1B4C&MI_05&IG_00）。
static std::vector<std::wstring> EnumerateHidInstances() {
    std::vector<std::wstring> out;
    static const GUID kHidIface = { 0x4d1e55b2, 0xf16f, 0x11cf, { 0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };
    HDEVINFO set = SetupDiGetClassDevsW(&kHidIface, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return out;
    SP_DEVICE_INTERFACE_DATA ifc{ sizeof(ifc) };
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &kHidIface, i, &ifc); ++i) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifc, nullptr, 0, &need, nullptr);
        if (need == 0 || need > 64 * 1024) continue;
        std::vector<BYTE> buf(need);
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifc, detail, need, nullptr, nullptr)) continue;
        DEVPROPTYPE type = 0;
        WCHAR inst[MAX_DEVICE_ID_LEN]{};
        if (SetupDiGetDeviceInterfacePropertyW(set, &ifc, &DEVPKEY_Device_InstanceId,
                &type, reinterpret_cast<PBYTE>(inst), sizeof(inst), nullptr, 0) && type == DEVPROP_TYPE_STRING) {
            std::wstring id(inst);
            // 只保留 HID enumerator 实例（接口 instance 形如 HID\VID_...）。
            if (id.rfind(L"HID\\", 0) == 0) out.push_back(id);
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    return out;
}

int wmain(int argc, wchar_t** argv) {
    bool executeIoctl = false;
    std::wstring onlyInstance;
    bool hidMode = false;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--ioctl") == 0) executeIoctl = true;
        else if (wcscmp(argv[i], L"--hid") == 0) hidMode = true;
        else if (wcsncmp(argv[i], L"--instance=", 11) == 0) onlyInstance = argv[i] + 11;
        else if (wcscmp(argv[i], L"--instance") == 0 && i + 1 < argc) onlyInstance = argv[++i];
    }
    if (hidMode) {
        std::vector<std::wstring> hids = EnumerateHidInstances();
        wprintf(L"hid-instances=%zu\n", hids.size());
        if (!onlyInstance.empty())
            hids.clear(), hids.push_back(onlyInstance);
        for (const auto& id : hids)
            CyclePortHub(id, executeIoctl);
        wprintf(L"done\n");
        return 0;
    }
    std::vector<std::wstring> instances = EnumerateUsbInstances();
    wprintf(L"usb-instances=%zu\n", instances.size());
    if (!onlyInstance.empty()) {
        CyclePortHub(onlyInstance, executeIoctl);
        return 0;
    }
    for (const auto& id : instances)
        CyclePortHub(id, executeIoctl);
    wprintf(L"done\n");
    return 0;
}