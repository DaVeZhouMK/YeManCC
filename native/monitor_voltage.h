#pragma once
// Exact measured core-rail names only. VIDs/SoC/memory/12V/battery rails must
// never become CPU/GPU core voltage. Operates on existing bounded HWiNFO rows.
#include "monitor_text_fold.h"
#include <cmath>
namespace ymcc::monitorvoltage {
inline bool valid(double volts) { return std::isfinite(volts) && volts >= .05 && volts <= 3.0; }
struct Reading { double volts = -1; int rank = 0; };
struct Readings {
    Reading cpu, gpu;
    static void select(Reading& r, double volts, int rank) {
        if (rank > r.rank || (rank == r.rank && volts > r.volts)) r = {volts, rank};
    }
    void add(const MonitorFoldedText& name, const MonitorFoldedText& unit, double current, bool readable) {
        if (!readable) return;
        const double volts = unit == "mv" ? current / 1000.0 : current;
        if ((unit != "v" && unit != "mv") || !valid(volts)) return;
        if (name == "cpu core voltage (svi2 tfn)" || name == "cpu core voltage (svi3 tfn)" ||
            name == "cpu vddcr_vdd voltage (svi3 tfn)" || name == "cpu vddcr_vdd voltage (svi2 tfn)") select(cpu, volts, 3);
        else if (name == "cpu core voltage" || name == "cpu vcore") select(cpu, volts, 2);
        else if (name == "vcore") select(cpu, volts, 1);
        else if (name == "gpu core voltage" || name == "gpu core voltage (vddcr_gfx)" ||
                 name == "gpu core voltage (vddc)" || name == "gpu asic voltage") select(gpu, volts, 2);
        else if (name == "gpu voltage") select(gpu, volts, 1);
    }
};
} // namespace ymcc::monitorvoltage
