// Immutable original consumer bodies only; never compiled into production.

static bool msiClawEnsureM12Remap(ymcc::msi::rearSdPreparation::Receipt* receipt = nullptr) {
    ymcc::msi::rearSdPreparation::Receipt observed;
    auto finish = [&](bool returned) {
        observed.returned = returned;
        if (receipt) *receipt = observed;
        return returned;
    };
    ymccResolveMachineIdentityOnce();
    if (g_machineIdentity.family != YmccFamily::MsiClaw) { observed.outcome = "not-msi-family"; return finish(true); } // 非 Claw 不介入
    if (g_msiM12RemapDone) { observed.outcome = "process-cache-hit"; return finish(true); }
    const GUID& hidGuid = kYeManGuidDeviceInterfaceHid;
    HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) { observed.outcome = "hid-class-enumeration-failed"; return finish(false); }
    observed.outcome = "command-interface-not-found";
    bool sent = false;
    for (DWORD index = 0; !sent; ++index) {
        SP_DEVICE_INTERFACE_DATA iface{}; iface.cbSize = sizeof(iface);
        if (!SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, index, &iface)) { observed.enumerationEndError = GetLastError(); break; }
        ++observed.enumerated;
        DWORD bytes = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &iface, nullptr, 0, &bytes, nullptr);
        if (!bytes) { ++observed.detailFailures; continue; }
        std::vector<BYTE> detailBuffer(bytes);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        SP_DEVINFO_DATA data{}; data.cbSize = sizeof(data);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, bytes, nullptr, &data)) { ++observed.detailFailures; continue; }
        HANDLE handle = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) { ++observed.openFailures; observed.lastOpenError = GetLastError(); continue; }
        HIDD_ATTRIBUTES attrs{}; attrs.Size = sizeof(attrs);
        const bool attrOk = HidD_GetAttributes(handle, &attrs) != FALSE;
        PHIDP_PREPARSED_DATA prep = nullptr;
        HIDP_CAPS caps{};
        const bool prepOk = HidD_GetPreparsedData(handle, &prep) != FALSE;
        const bool capsOk = prepOk && HidP_GetCaps(prep, &caps) == HIDP_STATUS_SUCCESS;
        if (!attrOk) ++observed.attributeFailures;
        if (!capsOk) ++observed.capabilityFailures;
        if (attrOk && attrs.VendorID == kMsiClawVendorId) ++observed.vendorMatches;
        bool match = false;
        if (attrOk && capsOk && attrs.VendorID == kMsiClawVendorId) {
            if (attrs.ProductID == kMsiClawPidXInput)
                match = caps.UsagePage == 0xFFA0 && caps.Usage == 0x0001;
            else if (attrs.ProductID == kMsiClawPidDInput)
                match = caps.UsagePage == 0xFFF0 && caps.Usage == 0x0040;
        }
        if (match) {
            ++observed.commandMatches;
            observed.productId = attrs.ProductID; observed.firmware = attrs.VersionNumber;
            observed.usagePage = caps.UsagePage; observed.usage = caps.Usage;
            const USHORT fw = attrs.VersionNumber;  // HIDD_ATTRIBUTES.VersionNumber（HC Attributes.Version 同源）
            // 表外不写（204 W3 guard hunk，C1/P6，2026-09-23）：固件必须落在 HC
            // ClawA1M.cs:149-166 的 DeviceVersion 表内（IsSupported 同语义——精确匹配）。
            // 表外/不可识别一律拒写（fail-closed）并记一条生命周期日志；否则会向未知
            // 固件写入现代档 payload（0xBB/0x64），行为不可预期。
            const bool knownFirmware = (fw == 0x163 || fw == 0x166 || fw == 0x167 ||
                                        fw == 0x211 || fw == 0x217 || fw == 0x219 ||
                                        fw == 0x308 || fw == 0x411);
            if (!knownFirmware) {
                observed.outcome = "firmware-not-in-table";
                // GP-MSIG3E-2 S3：0x419 等表外固件的背键独立来源**未验证**——HC 用"最近固件"
                // 近似匹配（ClawA1M.cs MinBy(Abs(fw-Firmware))），YMCC 采用更保守的精确表 +
                // fail-closed。此处如实标注未验证状态与最近候选，**不写 EEPROM**（定点核实前不放开）。
                appendNativeLifecycleLog("msi-claw-m12-remap-refused", {
                    {"reason", "firmware-not-in-table"},
                    {"status", "firmware-remap-unverified"},
                    {"firmware", static_cast<int>(fw)},
                    {"nearestFirmware", 0x411},
                    {"table", "hc-claw-a1m-deviceversions"},
                    {"policy", "table-exclusive-no-write"},
                    {"writePolicy", "no-eeprom-write"}
                });
                if (prep) HidD_FreePreparsedData(prep);
                CloseHandle(handle);
                break;
            }
            const bool legacyAdd = (fw == 0x163 || fw == 0x211);
            // SD virtual source consumes DInput button 15/16, not F11/F14.
            // HC ClawA1M.Open/GetM12(false) + HidDeviceExtensions.Write copy at
            // offset ZERO. Use the already table-tested DInput report factory.
            const auto packets = ymcc::msi::rearProtocol::reports(
                fw, ymcc::msi::rearProtocol::Route::DInputButtons);
            observed.outputReportByteLength = caps.OutputReportByteLength;
            observed.wireReportId = packets.m1[0];
            auto writeCmd = [&](const std::array<uint8_t,64>& report, size_t slot) -> bool {
                DWORD written = 0;
                const BOOL accepted = WriteFile(handle, report.data(),
                    static_cast<DWORD>(report.size()), &written, nullptr);
                const DWORD error = accepted != FALSE ? 0 : GetLastError();
                auto& evidence = observed.commands[slot];
                evidence.apiAccepted = accepted != FALSE;
                evidence.written = written; evidence.error = error;
                return accepted != FALSE && written == report.size();
            };
            observed.hardwareWriteIssued = true;
            ++observed.writeSets;
            const bool ok1 = writeCmd(packets.m1, 0);
            Sleep(500);  // unchanged existing one-shot cadence
            const bool ok2 = writeCmd(packets.m2, 1);
            Sleep(500);
            const bool ok3 = writeCmd(packets.sync, 2);
            sent = ok1 && ok2 && ok3;
            observed.m1 = ok1; observed.m2 = ok2; observed.sync = ok3;
            observed.returned = sent;
            observed.outcome = sent ? "write-receipts-complete" : "command-write-incomplete";
            appendNativeLifecycleLog("msi-claw-m12-remap", {
                {"firmware", static_cast<int>(fw)},
                {"legacyAdd", legacyAdd},
                {"m1", ok1}, {"m2", ok2}, {"sync", ok3},
                {"mappingRoute", "dinput-buttons-15-16"},
                {"expectedButtonM1", 15}, {"expectedButtonM2", 16},
                {"framing", "64-byte-report-body-offset-0"},
                {"writeReceipt", ymcc::msi::rearSdPreparation::fields<json>(observed)},
                // S0/S3 同原则：写入回执 ≠ 实际映射已验证（无原配置回读、无 M12/SyncToROM
                // 回读）⇒ 回读状态与验证结论分开记录，避免把写成功当"背键已配好"。
                {"readback", "not-attempted"},
                {"mappingVerified", false},
                {"note", "write-receipt-only; mapping needs observed keys"}
            });
        }
        if (prep) HidD_FreePreparsedData(prep);
        CloseHandle(handle);
    }
    SetupDiDestroyDeviceInfoList(set);
    if (sent) g_msiM12RemapDone = true;
    return finish(sent);
}

static bool msiClawPrepareForVirtualStart(ymcc::msi::rearSdPreparation::Receipt* receipt = nullptr) {
    // A blocked MSI target must not mutate M1/M2/ROM before start admission.
    // This private guard is required even in the normal (non-experiment) build.
    if(!ymcc::msi::preparation::allowed(true,msiClawKnownBoardAuthority(),g_inputHostReleaseQuarantined)) {
        if (receipt) { *receipt = {}; receipt->outcome = "release-quarantine-blocked-before-m12"; }
        return false;
    }
#ifdef YMCC_FAULT_INJECT
    ++g_msiStartPreparationEntriesForTest;
#endif
    return msiClawEnsureM12Remap(receipt); // existing payload, policy and success semantics unchanged
}
