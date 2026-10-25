# Legion motion lease: interface, verification and limits

Owned production file: `native/legion_motion_source.h`. Tests and supporting
files live only in `tools/legion_motion_source_selftest.mjs` and this directory.
The evidence runner only writes fresh children of workspace
`Build/Validation/HC-ModelCapabilities-20261017/LegacyMotion/legion-lease/`.

The C++17 interface is `ymcc::legion_motion::Context { power, rawGeneration }`,
`Source { std::string physicalIdentity, uint64_t captureEpoch, unsigned sensorIndex }`,
`Sample { gyro, accel, capturedAt, context, source }`, and `Lease`:

- `bool publish(const Sample&, Context current, const Source& currentSource, bool ready)`
- `std::optional<Sample> current(Context current, const Source& currentSource, bool ready, uint64_t now,
  uint64_t maxAgeMs) const`
- `void revoke() noexcept`

Both context generations must be nonzero and exactly match. Both publish and current
also require exactly equal physicalIdentity, captureEpoch and sensorIndex. Identity must
be nonempty, epoch nonzero and index <= 1, even if equally invalid on both sides. No
normalization, identity lookup or hardware selection is performed in this module.
Source owns its string; Sample and returned copies own their source identity. Because
copying can allocate, publish/current are not noexcept; revoke remains noexcept. All six double
axes must be finite and capturedAt must be nonzero. Rejected publishes leave
the prior sample intact. Consumption also requires Ready, a non-future tick,
and age <= the caller's maxAgeMs. Subtraction happens only after the future
gate, so uint64_t extremes cannot wrap a future timestamp into freshness.
Zero maxAgeMs accepts only equal nonzero capture/consume ticks. Zero and
repeated axes are valid and need no value-change heuristic.

There are no global objects, threads, I/O, OS calls, controller/focus state,
clock reads, locks, or default age policy in this header. Return values and
stored samples are copies. The caller must serialize every call with its
existing mutex, sample the context at acquisition, and recheck the current
context/Ready at commit and consumption. It must supply ticks from the same
monotonic millisecond domain and use its existing window constant (currently
1500ms); tests also cover zero and other windows.

`current` is a const visibility gate, not a lifecycle transition. The lifecycle
owner calls `revoke` while serialized when capture/power retires. It must never
roll back/reuse generation identities. `revoke` clears the sample immediately;
only a newly accepted publish can restore data. A Ready=false read alone does
not permanently erase the slot. A delayed callback carrying exactly the same
power/raw context after Stop -> Start is rejected when the caller supplies the new
source captureEpoch. The main owner samples the actual selected physical source and
capture epoch under its existing synchronization at publication and consumption.
Invalid current source never exposes cached data. Rejected publish preserves the prior
sample, but a caller can consume it only with its exact still-current valid source.

Run from the mainline repository:

```
node tools/legion_motion_source_selftest.mjs
```

MSVC x64 is discovered with vswhere (`--vcvars` can override). Release uses
/O2 /DNDEBUG /MT. Checked uses /Od /RTC1 /Zi /D_DEBUG
/D_ITERATOR_DEBUG_LEVEL=2 /MTd. Both use C++17, /W4 /WX and /fp:precise.
Harness checks are explicit and remain active under NDEBUG. Actual successful
compilation and execution are required; compilation failures cannot count as
mutation detection. Thirteen faults isolate source identity/epoch/index equality,
empty identity/zero epoch/out-of-range index, power, raw generation, producer Ready,
consumer Ready, expiry, future capture and revoke. Every fault runs
in both configurations and must fail a named semantic witness.

Evidence freezes exact production/test/document bytes before compilation,
records SHA256 for every input and executable, stores commands and complete
compile/run logs, rechecks owned inputs afterward, and includes an output
inventory. The runner refuses to overwrite an existing evidence directory.

To also compile the three actual production helpers, run:

```
node tools/legion_motion_source_selftest.mjs --main-helpers
```

This mode freezes the current full main.cpp and extracts only
`legionMotionRevoke`, `legionMotionPublish`, and `legionMotionSnapshot`
verbatim. It records exact line/UTF-8 byte ranges and SHA256 for each function,
then compiles those bodies with the production lease and the production
`kVirtualOutputSourceWindowMaxAgeMs` header. Missing/ambiguous functions or
changed selected slices fail closed. Unrelated main.cpp changes are recorded
separately and do not invalidate unchanged selected helper/anchor bytes.

The helper harness provides controlled globals for power/raw generation,
Ready, stop, family, power phase, capture epoch, selected identity/index and clock inputs.
legionMotionSourceForSelectedLocked is a controlled complete Source-returning stub;
the actual provider is saved as an unexecuted source anchor. Model route eligibility
inputCaptureLegionControllerMotionRoute is also a controlled stub with its actual
definition saved as a source-only anchor; those model rules are not executed here. The exact publish and
snapshot helpers lock physicalIdentityMx before legionImuMx; lock order is only a
source check, not a live concurrency claim. Tests cover
both the commit and snapshot gates, complete six-axis copies, unchanged and
all-zero samples, late callbacks after power/raw retirement, exact age/future
limits, repeated revoke, and suspend -> resume -> new-context recovery.
Stop -> Start interleavings deliberately keep power/raw unchanged: old capture
epochs must fail both before and after a new stationary sample is accepted.
Epoch zero, wrong external epoch arguments, unchanged context with changed source,
empty identity, invalid sensor and UINT64_MAX -> 1 wrap are covered. Two additional compiled
faults independently bypass the capture epoch comparison or suppress the
revoke epoch update, and must fail named epoch-boundary checks in both builds.
The extended helper harness covers 19 cases, including full-source transitions and
an independently disabled model route.
The helper harness uses std::mutex but does not start concurrent threads.
These are deterministic interleaving tests, not OS scheduling or data-race
proofs. Real acquisition must tag its sample and captured epoch correctly.

Separately labelled source-only checks freeze RawInput acquisition tags,
selected-source/generation rejection, the exact two legacy model classes,
resolved VID/PID/usage gates, the single dwSizeHid motion parse size, six-axis
sample provenance, same-mutex helper declarations, epoch-before-clear order,
and Lenovo-specific Stop/suspend revoke calls. Those source ranges are saved
as text and hashed, but their functions are not compiled/executed by this
harness. Static anchors do not validate actual report lengths, data lifetimes,
the WM_INPUT path, or that an OS power notification reaches these call sites.

These isolated tests do not validate real HID protocol/decoding, actual PBT
notifications, actual sleep/wake, OS sessions, thread races or lock ownership,
complete controller admission, Host output or Steam consumption. They neither
launch the product nor change devices/services or deploy a build. A passing
pure lease test cannot establish that all Lenovo motion is repaired.

The pure fixture has 23 scenarios, including physical identity switch with unchanged
power/raw context, same-hardware capture epoch retirement, sensor 0/1 switch, invalid
source rejection over a populated cache, 64-bit epoch differences and identity copy
ownership. Zero gyro/accel remains a valid stationary sample under either sensor index.
Actual selected-container VID/PID discovery and capture epoch advancement belong to
main and are not proved by pure module API compilation.
