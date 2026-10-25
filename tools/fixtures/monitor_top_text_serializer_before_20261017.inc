static std::string nativeMonitorTopPayloadText(const NativeMonitorHw& hw, bool ac, bool hasBattery,
                                               int batteryPercent, double remain, double cpuUsage,
                                               long long topTs, bool hwHealthy, const json& coreUsage = json::array(), long long coreUsageTs = 0,
                                               const ymcc::monitorbattery::Power& battery = {}, long long batteryTs = 0, long long batteryStatusTs = 0,
                                               const ymcc::monitorbattery::RemainingTime& batteryTime = {},
                                               const ymcc::monitorrtss::Limit& fpsLimit = {},
                                               const ymcc::monitorfan::Receipt& fan = {},
                                               const ymcc::monitorfs::Rates& fileSystem = {},
                                               bool cpuUsageValid = true, long long cpuUsageTs = 0, json* transferableCoreUsage = nullptr) {
    // Build final fields directly; avoid transient pair arrays and values
    // that the coherent GPU/battery receipts below always replace.
    (void)remain; // Kept in the existing publisher API for legacy callers.
    // Reuse only this thread's fixed output field nodes. Every scalar below is
    // assigned on every call; optional proofs are erased explicitly when absent.
    static thread_local json topJson = json::object();
    // Heterogeneous lookup avoids constructing a temporary long std::string
    // on every existing-key update. Missing optional keys keep normal insertion.
    const auto field = [&](std::string_view key) -> json& {
        const auto found = topJson.find(key);
        return found == topJson.end() ? topJson[key] : *found;
    };
    // Stable text fields reuse their own string object and bounded capacity.
    // Assign every call so source/health transitions cannot retain stale text.
    const auto text = [&](std::string_view key, const char* value) {
        json& target = field(key);
        if (target.is_string()) target.get_ref<json::string_t&>().assign(value);
        else target = value;
    };
    field("ts") = topTs;
    field("tdpW") = std::round(hw.packagePower * 10.0) / 10.0;
    field("freqMhz") = static_cast<int>(std::round(hw.freqMhz));
    field("tempC") = static_cast<int>(std::round(hw.tempC));
    field("ac") = ac ? 1 : 0;
    field("hasBattery") = hasBattery;
    field("batteryPercent") = batteryPercent;
    field("cpuUsage") = static_cast<int>(std::round(cpuUsage));
    field("thermalThrottleFound") = hw.thermalThrottleFound;
    field("thermalThrottleMax") = hw.thermalThrottleMax;
    field("virtualMemoryCommittedFound") = hw.virtualMemoryCommittedFound;
    field("virtualMemoryCommittedMb") = std::round(hw.virtualMemoryCommittedMb * 10.0) / 10.0;
    field("virtualMemoryLoadFound") = hw.virtualMemoryLoadFound;
    field("virtualMemoryLoadPct") = std::round(hw.virtualMemoryLoadPct * 10.0) / 10.0;
    field("hwDown") = !hwHealthy;
    field("cpuPowerValid") = hwHealthy && hw.cpuPowerValid;
    field("cpuClockValid") = hwHealthy && hw.cpuClockValid;
    field("cpuTempValid") = hwHealthy && hw.cpuTempValid;
    field("cpuVoltageV") = hwHealthy && ymcc::monitorvoltage::valid(hw.cpuVoltageV) ? json(hw.cpuVoltageV) : json(nullptr);
    field("gpuVoltageV") = hwHealthy && ymcc::monitorvoltage::valid(hw.gpuVoltageV) ? json(hw.gpuVoltageV) : json(nullptr);
    const auto number=[&](double v){return hwHealthy&&ymcc::monitormemory::amount(v)?json(v):json(nullptr);};
    field("gpuPowerW")=number(hw.gpuPowerW);field("gpuClockMhz")=number(hw.gpuClockMhz);
    field("gpuTempC")=number(hw.gpuTempC);field("gpuUsagePct")=number(hw.gpuLoad);
    if(hwHealthy&&hw.gpuSelectedPresent)text("gpuName",hw.gpuName.data());
    else field("gpuName")=nullptr;
    field("gpuSelectionPinned")=hw.gpuPinned;
    field("gpuSelectionPresent")=hwHealthy&&hw.gpuSelectedPresent;
    field("physicalMemoryUsedMb")=number(hw.physicalMemory.used);
    field("physicalMemoryAvailableMb")=number(hw.physicalMemory.available);
    field("physicalMemoryTotalMb")=number(hw.physicalMemory.total);
    field("physicalMemoryLoadPct")=number(hw.physicalMemory.load);
    field("gpuMemoryUsedMb")=number(hw.gpuMemory.used);
    field("gpuMemoryAvailableMb")=number(hw.gpuMemory.available);
    field("gpuMemoryTotalMb")=number(hw.gpuMemory.total);
    field("gpuMemoryLoadPct")=number(hw.gpuMemory.load);
    text("gpuMemorySource",hwHealthy&&hw.gpuSelectedPresent?hw.gpuMemorySource:"unavailable");
    field("networkDownMBps")=number(hw.networkDownMBps);field("networkUpMBps")=number(hw.networkUpMBps);
    const bool fileFresh=fileSystem.ts>0&&(fileSystem.readMBps>=0||fileSystem.writeMBps>=0);
    field("fileReadMBps")=fileFresh&&std::isfinite(fileSystem.readMBps)&&fileSystem.readMBps>=0?json(fileSystem.readMBps):json(nullptr);
    field("fileWriteMBps")=fileFresh&&std::isfinite(fileSystem.writeMBps)&&fileSystem.writeMBps>=0?json(fileSystem.writeMBps):json(nullptr);
    field("fileSystemTs")=fileFresh?fileSystem.ts:0;text("fileSystemSource",fileFresh?"windows-filesystem-disk-activity":"unavailable");
    const bool capValid = fpsLimit.valid && fpsLimit.ts > 0 && std::isfinite(fpsLimit.fps) && fpsLimit.fps >= 0 && fpsLimit.fps <= ymcc::monitorrtss::kMaxLimit;
    field("fpsLimit") = capValid ? json(fpsLimit.fps) : json(nullptr);
    field("fpsLimitValid") = capValid;
    field("fpsLimitTs") = capValid ? fpsLimit.ts : 0;
    text("fpsLimitSource",capValid ? "rtss-global" : "unavailable");
    const bool fanValid = fan.enabled && fan.valid && fan.ts > 0 && std::isfinite(fan.percent) && fan.percent >= 0 && fan.percent <= 100;
    field("fanEnabled") = fanValid;
    field("fanSpeedPercent") = fanValid ? json(fan.percent) : json(nullptr);
    field("fanSpeedValid") = fanValid;
    field("fanSpeedTs") = fanValid ? fan.ts : 0;
    text("fanSpeedSource",fanValid ? "ymcc-fan-target" : "unavailable");
    field("batteryStatusTs") = hasBattery && batteryPercent >= 0 && batteryPercent <= 100 ? batteryStatusTs : 0;
    const bool batteryValid = hasBattery && batteryPercent >= 0 && batteryPercent <= 100 && battery.valid;
    field("batteryPowerValid") = batteryValid;
    field("batteryPowerEstimated") = batteryValid && battery.estimated;
    text("batteryPowerSource",batteryValid ? battery.source : "unavailable");
    field("batteryPowerTs") = batteryValid ? batteryTs : 0;
    field("batteryPowerWindowMs") = batteryValid ? battery.windowMs : 0;
    field("chargeW") = batteryValid ? std::round(battery.watts * 10.0) / 10.0 : 0;
    const bool timeValid = batteryValid && !ac && battery.watts < -.05 && batteryTime.valid;
    field("remainMin") = timeValid ? static_cast<int>(std::round(batteryTime.minutes)) : -1;
    field("batteryTimeValid") = timeValid;
    field("batteryTimeEstimated") = timeValid && batteryTime.estimated;
    field("batteryTimeTs") = timeValid ? batteryTs : 0;
    text("batteryTimeSource",timeValid ? batteryTime.source : "unavailable");
    if (coreUsage.is_array() && !coreUsage.empty() && coreUsageTs > 0) {
        // Only the collector's explicit ownership handoff may move this array.
        // Legacy/lvalue callers and omitted arrays keep their existing costs.
        if (transferableCoreUsage == &coreUsage)
            field("coreUsage") = std::move(*transferableCoreUsage);
        else field("coreUsage") = coreUsage;
        field("coreUsageTs") = coreUsageTs;
    } else { topJson.erase("coreUsage"); topJson.erase("coreUsageTs"); }
    // Explicit Windows measurement proof is independent of HWiNFO. Omitted
    // arguments preserve legacy constructor callers without inventing a time.
    if (cpuUsageTs > 0 || !cpuUsageValid) {
        const bool known = cpuUsageValid && std::isfinite(cpuUsage) && cpuUsage >= 0 && cpuUsage <= 100 && cpuUsageTs > 0;
        field("cpuUsageValid") = known; field("cpuUsageTs") = known ? cpuUsageTs : 0;
    } else { topJson.erase("cpuUsageValid"); topJson.erase("cpuUsageTs"); }
    // HWiNFO sensor names may contain ACP/non-UTF8 bytes. A display-only
    // device label must never throw and terminate the sole monitor worker.
    return topJson.dump(-1, ' ', false, json::error_handler_t::replace);
}
