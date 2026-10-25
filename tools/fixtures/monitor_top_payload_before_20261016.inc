// Frozen complete production publisher before final-object construction.
static std::string nativeMonitorTopPayloadText(const NativeMonitorHw& hw, bool ac, bool hasBattery,
                                               int batteryPercent, double remain, double cpuUsage,
                                               long long topTs, bool hwHealthy, const json& coreUsage = json::array(), long long coreUsageTs = 0,
                                               const ymcc::monitorbattery::Power& battery = {}, long long batteryTs = 0, long long batteryStatusTs = 0,
                                               const ymcc::monitorbattery::RemainingTime& batteryTime = {},
                                               const ymcc::monitorrtss::Limit& fpsLimit = {},
                                               const ymcc::monitorfan::Receipt& fan = {},
                                               const ymcc::monitorfs::Rates& fileSystem = {},
                                               bool cpuUsageValid = true, long long cpuUsageTs = 0) {
    json topJson = {
        {"ts", topTs},
        {"tdpW", std::round(hw.packagePower * 10.0) / 10.0},
        {"freqMhz", static_cast<int>(std::round(hw.freqMhz))},
        {"tempC", static_cast<int>(std::round(hw.tempC))},
        {"ac", ac ? 1 : 0},
        {"hasBattery", hasBattery},
        {"batteryPercent", batteryPercent},
        {"chargeW", std::round(hw.chargeW * 10.0) / 10.0},
        {"remainMin", remain >= 0.0 ? static_cast<int>(std::round(remain)) : -1},
        {"cpuUsage", static_cast<int>(std::round(cpuUsage))},
        {"gpuPowerW", std::round(hw.gpuPowerW * 10.0) / 10.0},
        {"gpuClockMhz", static_cast<int>(std::round(hw.gpuClockMhz))},
        {"thermalThrottleFound", hw.thermalThrottleFound},
        {"thermalThrottleMax", hw.thermalThrottleMax},
        {"virtualMemoryCommittedFound", hw.virtualMemoryCommittedFound},
        {"virtualMemoryCommittedMb", std::round(hw.virtualMemoryCommittedMb * 10.0) / 10.0},
        {"virtualMemoryLoadFound", hw.virtualMemoryLoadFound},
        {"virtualMemoryLoadPct", std::round(hw.virtualMemoryLoadPct * 10.0) / 10.0},
        {"hwDown", !hwHealthy}
    };
    topJson["cpuPowerValid"] = hwHealthy && hw.cpuPowerValid;
    topJson["cpuClockValid"] = hwHealthy && hw.cpuClockValid;
    topJson["cpuTempValid"] = hwHealthy && hw.cpuTempValid;
    topJson["cpuVoltageV"] = hwHealthy && ymcc::monitorvoltage::valid(hw.cpuVoltageV) ? json(hw.cpuVoltageV) : json(nullptr);
    topJson["gpuVoltageV"] = hwHealthy && ymcc::monitorvoltage::valid(hw.gpuVoltageV) ? json(hw.gpuVoltageV) : json(nullptr);
    const auto number=[&](double v){return hwHealthy&&ymcc::monitormemory::amount(v)?json(v):json(nullptr);};
    topJson["gpuPowerW"]=number(hw.gpuPowerW);topJson["gpuClockMhz"]=number(hw.gpuClockMhz);
    topJson["gpuTempC"]=number(hw.gpuTempC);topJson["gpuUsagePct"]=number(hw.gpuLoad);
    topJson["gpuName"]=hwHealthy&&hw.gpuSelectedPresent?json(hw.gpuName.data()):json(nullptr);
    topJson["gpuSelectionPinned"]=hw.gpuPinned;
    topJson["gpuSelectionPresent"]=hwHealthy&&hw.gpuSelectedPresent;
    topJson["physicalMemoryUsedMb"]=number(hw.physicalMemory.used);
    topJson["physicalMemoryAvailableMb"]=number(hw.physicalMemory.available);
    topJson["physicalMemoryTotalMb"]=number(hw.physicalMemory.total);
    topJson["physicalMemoryLoadPct"]=number(hw.physicalMemory.load);
    topJson["gpuMemoryUsedMb"]=number(hw.gpuMemory.used);
    topJson["gpuMemoryAvailableMb"]=number(hw.gpuMemory.available);
    topJson["gpuMemoryTotalMb"]=number(hw.gpuMemory.total);
    topJson["gpuMemoryLoadPct"]=number(hw.gpuMemory.load);
    topJson["gpuMemorySource"]=hwHealthy&&hw.gpuSelectedPresent?hw.gpuMemorySource:"unavailable";
    topJson["networkDownMBps"]=number(hw.networkDownMBps);topJson["networkUpMBps"]=number(hw.networkUpMBps);
    const bool fileFresh=fileSystem.ts>0&&(fileSystem.readMBps>=0||fileSystem.writeMBps>=0);
    topJson["fileReadMBps"]=fileFresh&&std::isfinite(fileSystem.readMBps)&&fileSystem.readMBps>=0?json(fileSystem.readMBps):json(nullptr);
    topJson["fileWriteMBps"]=fileFresh&&std::isfinite(fileSystem.writeMBps)&&fileSystem.writeMBps>=0?json(fileSystem.writeMBps):json(nullptr);
    topJson["fileSystemTs"]=fileFresh?fileSystem.ts:0;topJson["fileSystemSource"]=fileFresh?"windows-filesystem-disk-activity":"unavailable";
    const bool capValid = fpsLimit.valid && fpsLimit.ts > 0 && std::isfinite(fpsLimit.fps) && fpsLimit.fps >= 0 && fpsLimit.fps <= ymcc::monitorrtss::kMaxLimit;
    topJson["fpsLimit"] = capValid ? json(fpsLimit.fps) : json(nullptr);
    topJson["fpsLimitValid"] = capValid;
    topJson["fpsLimitTs"] = capValid ? fpsLimit.ts : 0;
    topJson["fpsLimitSource"] = capValid ? "rtss-global" : "unavailable";
    const bool fanValid = fan.enabled && fan.valid && fan.ts > 0 && std::isfinite(fan.percent) && fan.percent >= 0 && fan.percent <= 100;
    topJson["fanEnabled"] = fanValid;
    topJson["fanSpeedPercent"] = fanValid ? json(fan.percent) : json(nullptr);
    topJson["fanSpeedValid"] = fanValid;
    topJson["fanSpeedTs"] = fanValid ? fan.ts : 0;
    topJson["fanSpeedSource"] = fanValid ? "ymcc-fan-target" : "unavailable";
    topJson["batteryStatusTs"] = hasBattery && batteryPercent >= 0 && batteryPercent <= 100 ? batteryStatusTs : 0;
    const bool batteryValid = hasBattery && batteryPercent >= 0 && batteryPercent <= 100 && battery.valid;
    topJson["batteryPowerValid"] = batteryValid;
    topJson["batteryPowerEstimated"] = batteryValid && battery.estimated;
    topJson["batteryPowerSource"] = batteryValid ? battery.source : "unavailable";
    topJson["batteryPowerTs"] = batteryValid ? batteryTs : 0;
    topJson["batteryPowerWindowMs"] = batteryValid ? battery.windowMs : 0;
    topJson["chargeW"] = batteryValid ? std::round(battery.watts * 10.0) / 10.0 : 0;
    const bool timeValid = batteryValid && !ac && battery.watts < -.05 && batteryTime.valid;
    topJson["remainMin"] = timeValid ? static_cast<int>(std::round(batteryTime.minutes)) : -1;
    topJson["batteryTimeValid"] = timeValid;
    topJson["batteryTimeEstimated"] = timeValid && batteryTime.estimated;
    topJson["batteryTimeTs"] = timeValid ? batteryTs : 0;
    topJson["batteryTimeSource"] = timeValid ? batteryTime.source : "unavailable";
    if (coreUsage.is_array() && !coreUsage.empty() && coreUsageTs > 0) {
        topJson["coreUsage"] = coreUsage; topJson["coreUsageTs"] = coreUsageTs;
    }
    // Explicit Windows measurement proof is independent of HWiNFO. Omitted
    // arguments preserve legacy constructor callers without inventing a time.
    if (cpuUsageTs > 0 || !cpuUsageValid) {
        const bool known = cpuUsageValid && std::isfinite(cpuUsage) && cpuUsage >= 0 && cpuUsage <= 100 && cpuUsageTs > 0;
        topJson["cpuUsageValid"] = known; topJson["cpuUsageTs"] = known ? cpuUsageTs : 0;
    }
    // HWiNFO sensor names may contain ACP/non-UTF8 bytes. A display-only
    // device label must never throw and terminate the sole monitor worker.
    return topJson.dump(-1, ' ', false, json::error_handler_t::replace);
}

static ymcc::monitorgpu::StickySelection g_monitorGpuSelection; // process lifetime, not worker/generation lifetime
