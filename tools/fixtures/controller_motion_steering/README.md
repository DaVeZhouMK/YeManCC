# Controller Auto steering: fixed-source isolated verification

This task owns only tools/controller_motion_steering_selftest.mjs, this fixture
and fresh evidence under ControllerMotion/LegionRuntime/Steering. No production
file or previous fixture is changed. Run after the main owner confirms stability:

    node tools/controller_motion_steering_selftest.mjs

The independent C# oracle reads AUTO-STEERING-OPEN.json and rechecks the actual
archived MotionManager.cs SHA256 and official fixed-tree Git blob at commit
06c0b9544db2b1f39abf9cd3796225ccfb096103. DetermineSteeringAxis, SwapYawRoll,
SetupMotion's exact Default-plane statements and ProcessMotion's LocalSpace
statements are copied verbatim into a minimal standalone class. Value-only enum,
Profile, ControllerState and GyroState stubs provide the original method types.
No native helper participates in producing expected results; no HC DLL is loaded.

The native proof compiles exact helper/contribution functions and exact per-frame
multiplier/decision, sensor-plane, diagnostic captures/lambda and contribution
call fragments. The surrounding values, provider admission and clock delta are
controlled. Actual sensitivity and clamp dependencies are compiled, but the test
uses a neutral position/LocalSpace profile and values outside the sensitivity
curve interval. Aiming/velocity/trigger policy and all HC curves are not covered.

Cases include Windows, SerialUSBIMU and Controller; roll/yaw/auto/unknown; signed
Y/Z gravity, both Controller Auto orientations, ties and zeros; zero/negative
accelerometer multipliers; and signed gyro multipliers. Manual decision pins
cross-check the oracle. Actual Default gyro/accel diagnostics, post-swap sensor
acceleration and actual LocalSpace short contributions must agree with C#.
The real omitted contribution argument must retain Windows semantics. Profile
state is compared before/after calls, and repeated/interleaved source choices
cannot be retained globally.

A whole onGamepad snapshot/source anchor verifies a single same-frame decision,
shared diagnostic/default-plane/contribution usage, pre-swap scaled inputs and
preserved Windows default argument. Only the copied steering fragments execute;
whole onGamepad, provider processing and actual frame submission are not run.

Release and Checked compile/run the real source fragments. Finite semantic
mutants must compile and fail checks, including always-Yaw, raw rather than
scaled acceleration and a post-swap contribution re-decision. Failed historical
runs are retained. No Win32/HID/OEM/PBT/sleep/Host/Steam action is executed. This
is a steering-axis parity proof, not full HC motion/action/curve compatibility.
