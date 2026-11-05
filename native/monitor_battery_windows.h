#pragma once
// Read-only cached battery IOCTLs, called only by the existing native monitor collector.
#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <batclass.h>
#include <array>
#include <vector>
#include "monitor_battery_energy.h"
#pragma comment(lib,"setupapi.lib")

namespace ymcc::monitorbattery {
class WindowsBattery {
    struct Device { HANDLE handle; ULONG tag; bool absolute; double fullMwh; };
    std::vector<Device> devices_;
    std::uint64_t identity_ = 0, lastProbe_ = 0;
    bool probed_ = false;
    EnergyCounter energy_;
    CombinedBattery lastReading_;
    int sourceAc_ = -1;
    bool charging_ = false, discharging_ = false;
    static constexpr GUID interfaceId_{0x72631e54,0x78a4,0x11d0,{0xbc,0xf7,0x00,0xaa,0x00,0xb7,0xb3,0x2a}};
    static bool query(HANDLE h, DWORD code, void* in, DWORD inBytes, void* out, DWORD outBytes) {
        DWORD returned = 0;
        return DeviceIoControl(h, code, in, inBytes, out, outBytes, &returned, nullptr) && returned >= outBytes;
    }
    void closeHandles() {
        for (auto& device : devices_) CloseHandle(device.handle);
        devices_.clear(); energy_.reset(); lastReading_ = {};
        sourceAc_ = -1; charging_ = discharging_ = false;
    }
    bool discover(std::uint64_t tick) {
        if (probed_ && tick >= lastProbe_ && tick - lastProbe_ < 30000) return false;
        probed_ = true; lastProbe_ = tick;
        auto set = SetupDiGetClassDevsW(&interfaceId_, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (set == INVALID_HANDLE_VALUE) return false;
        bool complete = true;
        for (DWORD i = 0; i <= 16; ++i) {
            SP_DEVICE_INTERFACE_DATA item{sizeof(item)};
            if (!SetupDiEnumDeviceInterfaces(set, nullptr, &interfaceId_, i, &item)) {
                complete = GetLastError() == ERROR_NO_MORE_ITEMS; break;
            }
            if (i == 16) { complete = false; break; }
            DWORD needed = 0;
            SetupDiGetDeviceInterfaceDetailW(set, &item, nullptr, 0, &needed, nullptr);
            if (needed < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || needed > 65536) { complete = false; break; }
            std::vector<BYTE> buffer(needed);
            auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
            detail->cbSize = sizeof(*detail);
            if (!SetupDiGetDeviceInterfaceDetailW(set, &item, detail, needed, nullptr, nullptr)) { complete = false; break; }
            HANDLE h = CreateFileW(detail->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) { complete = false; break; }
            ULONG wait = 0, tag = 0; BATTERY_QUERY_INFORMATION infoQuery{}; BATTERY_INFORMATION info{};
            bool ok = query(h, IOCTL_BATTERY_QUERY_TAG, &wait, sizeof(wait), &tag, sizeof(tag)) && tag;
            if (ok) {
                infoQuery.BatteryTag = tag; infoQuery.InformationLevel = BatteryInformation;
                ok = query(h, IOCTL_BATTERY_QUERY_INFORMATION, &infoQuery, sizeof(infoQuery), &info, sizeof(info));
            }
            if (!ok) { CloseHandle(h); complete = false; break; }
            if (!(info.Capabilities & BATTERY_SYSTEM_BATTERY) || (info.Capabilities & BATTERY_IS_SHORT_TERM)) {
                CloseHandle(h); continue;
            }
            devices_.push_back({h, tag, !(info.Capabilities & BATTERY_CAPACITY_RELATIVE),
                info.FullChargedCapacity == BATTERY_UNKNOWN_CAPACITY ? -1 : static_cast<double>(info.FullChargedCapacity)});
        }
        SetupDiDestroyDeviceInfoList(set);
        if (!complete || devices_.empty()) { closeHandles(); return false; }
        ++identity_; energy_.reset(); return true;
    }
public:
    WindowsBattery() = default;
    WindowsBattery(const WindowsBattery&) = delete;
    WindowsBattery& operator=(const WindowsBattery&) = delete;
    ~WindowsBattery() { closeHandles(); }
    void stop() { closeHandles(); probed_ = false; }
    // Existing discovery cache only; no new Windows query.
    bool hasMultipleSystemBatteries() const { return devices_.size() > 1; }
    // Facts from the last complete system-battery status batch; getters never query Windows.
    // -1 unknown, 0 DC, 1 AC. Failure, a new sample, or stop invalidates the old proof.
    int sourceAc() const { return sourceAc_; }
    bool charging() const { return charging_; }
    bool discharging() const { return discharging_; }
    EnergyReading remaining(int percent) const {
        if (!lastReading_.valid || devices_.empty()) return {};
        double full = 0;
        for (const auto& d : devices_) {
            if (!d.absolute || !std::isfinite(d.fullMwh) || d.fullMwh <= 0 || d.fullMwh > 1000000) { full = -1; break; }
            full += d.fullMwh;
        }
        return remainingEnergy(lastReading_.capacityValid ? lastReading_.capacityMwh : -1, full, percent);
    }
    Power sample(std::uint64_t tick, bool acKnown, bool ac) {
        lastReading_ = {};
        sourceAc_ = -1; charging_ = discharging_ = false;
        if (devices_.empty() && !discover(tick)) return {};
        std::array<BatteryReading, 16> readings{};
        bool online = false, charging = false, discharging = false;
        bool positiveRate = false, negativeRate = false;
        for (std::size_t i = 0; i < devices_.size(); ++i) {
            BATTERY_WAIT_STATUS request{}; request.BatteryTag = devices_[i].tag; request.Timeout = 0;
            BATTERY_STATUS status{};
            if (!query(devices_[i].handle, IOCTL_BATTERY_QUERY_STATUS, &request, sizeof(request), &status, sizeof(status))) {
                closeHandles(); lastProbe_ = tick; probed_ = true; return {};
            }
            readings[i] = {devices_[i].absolute,
                status.Capacity == BATTERY_UNKNOWN_CAPACITY ? -1 : static_cast<double>(status.Capacity),
                status.Rate == BATTERY_UNKNOWN_RATE ? std::numeric_limits<double>::quiet_NaN() : static_cast<double>(status.Rate) / 1000.0,
                (status.PowerState & BATTERY_CHARGING) != 0, (status.PowerState & BATTERY_DISCHARGING) != 0};
            const auto& r = readings[i];
            online = online || (status.PowerState & BATTERY_POWER_ON_LINE) != 0;
            charging = charging || r.charging;
            discharging = discharging || r.discharging;
            // Relative Rate units and rejected/missing rates cannot prove source direction.
            const auto rate = r.absoluteCapacity ? measured(r.watts, "windows-rate") : Power{};
            positiveRate = positiveRate || (rate.valid && rate.watts > 0);
            negativeRate = negativeRate || (rate.valid && rate.watts < 0);
        }
        charging_ = charging; discharging_ = discharging;
        // Publish only after every system battery was read. Online remains AC even
        // while a pack discharges (hybrid supply); no-online charging/mixed facts
        // stay unknown. Estimates, zero and absent rates never establish DC.
        if (online) sourceAc_ = 1;
        else if (!charging && !positiveRate && (discharging || negativeRate)) sourceAc_ = 0;
        if (sourceAc_ != -1) { acKnown = true; ac = sourceAc_ == 1; }
        const auto reading = combine(readings.data(), devices_.size(), acKnown, ac);
        if (!reading.valid) { energy_.reset(); return {}; }
        lastReading_ = reading;
        if (reading.power.valid) { energy_.reset(); return reading.power; }
        return energy_.update({tick, identity_, reading.capacityMwh, reading.capacityValid,
                               acKnown, ac, reading.charging, reading.discharging});
    }
};
} // namespace ymcc::monitorbattery
