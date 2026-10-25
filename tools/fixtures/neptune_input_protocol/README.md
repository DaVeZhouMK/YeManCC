# Neptune logical decoder offline proof

Scope: software protocol implementation only; **unconsumed**. No controller, transport,
OEM query, capability flag, reader or physical input consumer is enabled by this work.

Run from the YeManCC source directory:

    node tools/neptune_input_protocol_selftest.mjs

Requires local MSVC C++17 and a .NET SDK (8+); restore uses an empty NuGet source list.
The runner creates a fresh direct child of
G:/YeManCC-Work/Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/NeptuneProtocol.
Use --recovery <ProtocolRecovery> or --vcvars <vcvars64.bat> if needed.

Authoritative inputs are the three **actual shipped DLL byte copies** and their
shipped-binaries.json metadata, ILSpy11 text already recovered in ProtocolRecovery/decompiled,
and 40 Git-blob/size/SHA-verified primary files at official
Valkirie/HandheldCompanion commit **06c0b9544db2b1f39abf9cd3796225ccfb096103**
(acquisition-historical.json). The a250 sources are not used. The runner snapshots all
historical and decompiled source and relevant recovery receipts, rehashes the actual DLL
copies without loading them, and records the exact inputs compiled into each oracle.
The existing decompile logs are kept as evidence of the static recovery, not a claim
that source matches every instruction of every DLL. Runtime oracle agreement covers
NCInput layout and all Neptune input buttons/axes; transport equivalence is not claimed.

Two isolated C# builds compile only exact pure structure/state/enum/utility source from
(1) official old source and (2) shipped ILSpy source, plus Oracle.cs. No audited DLL is a
reference; no HidDevice, SteamController or NeptuneController is compiled or instantiated.
Oracle.cs uses Marshal.SizeOf/OffsetOf and BinaryReader/reflection field assignment, then
runs the actual source NeptuneControllerInputState mappings. Its complete enum names,
numeric values, field layouts and per-packet values must agree between origins.
The official Util/StructUtils.cs is pure utility code (only its dictionary comparison is
needed by state equality); its pinned-memory helpers are not used by this oracle.

The expected Pack=1 layout is **60 bytes**. In particular rpad_x is offset **20/0x14**;
the historical comment saying 0x13 is wrong. The decoder explicitly reads LE uint32 and
signed LE int16 without a packed C++ alias. seq, all 28 canonical enum buttons, all 22
raw signed axes, pad/stick touch and press, pressures, triggers, quaternion, and raw
gyro pitch/yaw/roll and accel x/y/z are preserved. No dps/g conversion, wrapper axis
permutation or calibration is applied.

The gate is logical bytes 01 00 09. _a3 and the tail past byte 59 are opaque; neither
is treated as a guessed packet-length field. SC input type 01, connect 03, battery 04
and every other non-09 type are refused. Stationary zero axes, including button-only
input, are valid. Each call requires capacity AND actualBytes. Short actual input is
refused even with a larger, previously populated backing buffer. Rejections return an
empty Input. The default BufferKind is Unverified and cannot parse anything.

The fixed official HC wrapper attempts buffer capacities 65 then 64 and HidDevice
requests that capacity, but ReadLoop copies only the returned len into a reused buffer
without publishing len. This is a potential stale-tail hazard in the old wrapper, not
evidence of an observed short read. The new pure parser permits **already independently identified logical** data whenever
actualBytes is at least 60 and does not exceed accessible capacity. It reads only the
complete 60-byte NCInput core, with every suffix byte opaque. Neither exact source
OnInputReceived imposes a per-packet size whitelist. Lengths 61..63, 66 and larger
therefore pass the same logical-core checks. Known wrapper capacities do not constrain
actualBytes and are not proof of actual native hid_read return lengths, firmware
reports, HID descriptor lengths, native hidapi.dll stripping, Windows RawInput framing
or device binding. No prefix inference is performed. Synthetic prepended-byte rejection
checks cannot establish a Windows raw-frame contract.

raw-logical-packets.json contains hand-authored logical bytes and manual pins. The runner
adds every bit of bytes 8..15 separately (including reserved bits), independent signed
wire-word probes, deterministic random packets, and opaque-tail variants. Each expected
C++ value comes from both exact C# source builds; manual pins guard against mutual oracle
mistakes. Release and Checked compile the **real native header**, with NDEBUG-safe checks.
Finite semantic mutations cover wrong offset/sign/endian/header, short acceptance,
left/right pad touch/press, rear ordering, gyro ordering, sequence endian, reserved bits and an invented length whitelist.
Every mutation must compile and fail semantic checks in both configurations.

Evidence summary.json and REPORT.md identify source hashes, toolchains, actual build
inputs, oracle/layout agreement and mutation results. Physical input, Windows/native
framing, reader lifecycle and the complete input consumer remain unaccepted.

The corpus repeats each core with logical lengths 60, 61, 62, 63, 64, 65, 66, 80
and 129; the C++ harness also checks each length 60..80 against the identical core.
These are pure parser contracts, not assertions about actual transport frame sizes.
