Extract the real Windows sink, raw callback methods, BUS snapshot, local pair gate,
HC gyro clip and full ProcessMotion/readback/Madgwick body. Load only the pinned
existing CPU GamepadMotion.dll. No sensors, HC managed library, Host or devices.
Invalid reports cannot establish availability, advance accepted observation
counters/clocks, or enter processing. A later valid current report can recover.
Tests cover NaN/infinity, double-to-float overflow, all six axes, current/retired
bindings, valid zero/subnormal/domain boundaries and per-binding rejection counts.
Representability checks do not claim that float-max inputs are physical readings
or that all possible finite magnitudes are stable through downstream arithmetic.
