// Frozen production sampler immediately before the source-allocation pass.
static double nativeCpuUsagePct(json* coreUsageOut = nullptr, bool* validOut = nullptr) {
    if (coreUsageOut) *coreUsageOut = json::array();
    if (validOut) *validOut = false;
    struct ProcessorTimes { LARGE_INTEGER idle, kernel, user, reserved[2]; ULONG reserved2; };
    static_assert(sizeof(ProcessorTimes) == 48, "Windows processor performance ABI");
    using Query = LONG (NTAPI*)(ULONG, PVOID, ULONG, PULONG);
    static const Query query = reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation"));
    static ymcc::cpusample::Deltas deltas, aggregateDeltas;
    if (query) {
        ProcessorTimes values[ymcc::cpusample::maxItems]{}; ULONG bytes = 0;
        const LONG status = query(8 /* SystemProcessorPerformanceInformation */, values, sizeof(values), &bytes);
        if (status >= 0 && bytes > 0 && bytes <= sizeof(values) && bytes % sizeof(ProcessorTimes) == 0) {
            std::vector<ymcc::cpusample::Times> counters; counters.reserve(bytes / sizeof(ProcessorTimes));
            bool valid = true;
            for (ULONG i = 0; i < bytes / sizeof(ProcessorTimes); ++i) {
                if (values[i].idle.QuadPart < 0 || values[i].kernel.QuadPart < 0 || values[i].user.QuadPart < 0) { valid = false; break; }
                counters.push_back({static_cast<std::uint64_t>(values[i].idle.QuadPart), static_cast<std::uint64_t>(values[i].kernel.QuadPart), static_cast<std::uint64_t>(values[i].user.QuadPart)});
            }
            if (valid) {
                aggregateDeltas.reset();
                const auto sample = deltas.accept(counters, GetTickCount64());
                if (validOut) *validOut = sample.totalValid;
                if (coreUsageOut) for (const auto& value : sample.cores) coreUsageOut->push_back(value ? json(std::round(*value * 10.0) / 10.0) : json(nullptr));
                return sample.total;
            }
        }
    }
    // API/layout unavailable: keep the existing aggregate fallback, leave core
    // slots unavailable rather than filling them with the aggregate percentage.
    deltas.reset();
    FILETIME idleFt{}, kernelFt{}, userFt{};
    if (!GetSystemTimes(&idleFt, &kernelFt, &userFt)) { aggregateDeltas.reset(); return 0.0; }
    auto ticks = [](const FILETIME& ft) {
        ULARGE_INTEGER v{}; v.LowPart = ft.dwLowDateTime; v.HighPart = ft.dwHighDateTime;
        return static_cast<ULONGLONG>(v.QuadPart);
    };
    // Reuse the same delta validity rules: first frame, counter rollback,
    // overflow, zero interval and long resume gaps are unknown, not 0% load.
    const auto sample = aggregateDeltas.accept({{ticks(idleFt), ticks(kernelFt), ticks(userFt)}}, GetTickCount64());
    if (validOut) *validOut = sample.totalValid;
    return sample.total;
}

static bool nativeBatteryStatus();

static void originalDemandLane(){
        json coreUsage = json::array(); bool cpuUsageValid = false;
        // FPS-only policy demand never needs a CPU snapshot. Keep collection
        // in the existing top-demand lane, with no new reader or timer.
        const double cpuUsage = top ? nativeCpuUsagePct(&coreUsage, &cpuUsageValid) : 0.0;
        const auto fpsLimit = 0;
}
