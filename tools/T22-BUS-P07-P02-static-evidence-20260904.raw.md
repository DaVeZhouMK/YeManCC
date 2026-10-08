# T22-P02 raw static evidence

capturedUtc: 2026-09-04T15:04:57.0326200Z
cwd: G:\YeManCC-Work
runtimeOperation: false

## Native motion, calibration and pair gate
argv: Get-Content -LiteralPath G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\native\main.cpp, lines 5369-5511
exitCode: 0
stdout:
`
5369:    writeLog("INFO", std::string("input-capture start gyro=") + (g_gyroSensor ? "1" : "0") +
5370:        " accel=" + (g_accelSensor ? "1" : "0") + " path=" + W2U(g_inputCapturePath));
5371:}
5372:
5373:static void inputCaptureOnGamepad(bool live, const XINPUT_GAMEPAD& pad) {
5374:    if (!g_inputCaptureReady) return;
5375:    const ULONGLONG now = GetTickCount64();
5376:    double gx = 0, gy = 0, gz = 0, ax = 0, ay = 0, az = 0;
5377:    bool gyro = inputCaptureReadAxis(g_gyroSensor.Get(),
5378:        SENSOR_DATA_TYPE_ANGULAR_VELOCITY_X_DEGREES_PER_SECOND,
5379:        SENSOR_DATA_TYPE_ANGULAR_VELOCITY_Y_DEGREES_PER_SECOND,
5380:        SENSOR_DATA_TYPE_ANGULAR_VELOCITY_Z_DEGREES_PER_SECOND, &gx, &gy, &gz);
5381:    bool accel = inputCaptureReadAxis(g_accelSensor.Get(),
5382:        SENSOR_DATA_TYPE_ACCELERATION_X_G,
5383:        SENSOR_DATA_TYPE_ACCELERATION_Y_G,
5384:        SENSOR_DATA_TYPE_ACCELERATION_Z_G, &ax, &ay, &az);
5385:    if (g_inputCaptureSink) {
5386:        std::lock_guard<std::mutex> lock(g_inputCaptureSink->mx);
5387:        if (g_inputCaptureSink->gyroOk) { gx=g_inputCaptureSink->gx; gy=g_inputCaptureSink->gy; gz=g_inputCaptureSink->gz; gyro=true; }
5388:        if (g_inputCaptureSink->accelOk) { ax=g_inputCaptureSink->ax; ay=g_inputCaptureSink->ay; az=g_inputCaptureSink->az; accel=true; }
5389:    }
5390:    // EXACT_HC Sensors/IMUCalibration.cs thresholdG=2000; IMUGyrometer zeros abs>=threshold.
5391:    auto hcClip = [](double v) { return fabs(v) >= 2000.0 ? 0.0 : v; };
5392:    gx = hcClip(gx); gy = hcClip(gy); gz = hcClip(gz);
5393:    double mgx = gx, mgy = gy, mgz = gz, maxa = ax, may = ay, maz = az;
5394:    inputCaptureApplyRogGyro(gx, gy, gz, &mgx, &mgy, &mgz);
5395:    inputCaptureApplyRogAccel(ax, ay, az, &maxa, &may, &maz);
5396:    double cgx = mgx, cgy = mgy, cgz = mgz;
5397:    float psx = 0, psy = 0, wsx = 0, wsy = 0;
5398:    float ox = 0, oy = 0, oz = 0, conf = 0;
5399:    bool steady = false;
5400:    // T12: the current two independent Windows sensor reads do not yet carry
5401:    // a ProviderCapabilityMatrix pairing proof.  Do not pair their latest
5402:    // values by guesswork: missing proof is SAFE_STOP for GamepadMotion and
5403:    // virtual gyro output. Physical buttons/sticks remain forwarded below.
5404:    const bool pairProven = false;
5405:    if (g_gm && gyro && accel && !pairProven && !g_gmPairUnprovenLogged) {
5406:        g_gmPairUnprovenLogged = true;
5407:        inputCaptureAppend({
5408:            {"t", now}, {"kind", "sample-pair-unproven"},
5409:            {"reason", "provider-capability-matrix-not-bound"},
5410:            {"gyro", true}, {"accel", true}, {"safeZero", true}
5411:        });
5412:    }
5413:    if (g_gm && g_gmProcess && g_gmGetCal && gyro && accel && pairProven) {
5414:        float dt = 0.016f;
5415:        if (g_gmLastTick != 0 && now > g_gmLastTick) dt = (float)(now - g_gmLastTick) / 1000.0f;
5416:        if (dt < 0.001f) dt = 0.001f;
5417:        if (dt > 0.1f) dt = 0.1f;
5418:        g_gmLastTick = now;
5419:        g_gmProcess(g_gm, (float)mgx, (float)mgy, (float)mgz, (float)maxa, (float)may, (float)maz, dt);
5420:        float cx=0, cy=0, cz=0;
5421:        g_gmGetCal(g_gm, &cx, &cy, &cz);
5422:        cgx = cx; cgy = cy; cgz = cz;
5423:        if (g_gmPlayer) g_gmPlayer(g_gm, &psx, &psy, 1.41f);
5424:        if (g_gmWorld) g_gmWorld(g_gm, &wsx, &wsy, 0.125f);
5425:        if (g_gmGetOffset) g_gmGetOffset(g_gm, &ox, &oy, &oz);
5426:        if (g_gmGetConf) conf = g_gmGetConf(g_gm);
5427:        if (g_gmIsSteady) steady = g_gmIsSteady(g_gm);
5428:        if (!g_gmLocked) {
5429:            if (g_gmCalStart == 0) g_gmCalStart = now;
5430:            // T11 safety refinement over HC's UI timeout: only a complete,
5431:            // steady confidence==1 candidate may enter Manual. Five seconds
5432:            // is diagnostic only; it must never turn a zero/invalid offset
5433:            // into a virtual-stick drift source.
5434:            const bool timedOut = now - g_gmCalStart >= 5000;
5435:            const bool offsetFinite = std::isfinite(ox) && std::isfinite(oy) && std::isfinite(oz);
5436:            const int weight = (int)(conf * 10.0f);
5437:            if (conf == 1.0f && steady && offsetFinite && weight > 0 && g_gmGetOffset && g_gmSetOffset && g_gmSetMode) {
5438:                g_gmGetOffset(g_gm, &ox, &oy, &oz);
5439:                g_gmSetOffset(g_gm, ox, oy, oz, weight);
5440:                g_gmSetMode(g_gm, 0);
5441:                g_gmLocked = true;
5442:                inputCaptureAppend({
5443:                    {"t", now}, {"kind", "cal-lock"},
5444:                    {"source", "HC SensorsManager.Calibrate / GamepadMotion Stillness|SensorFusion then Manual"},
5445:                    {"confidence", conf}, {"steady", steady}, {"weight", weight},
5446:                    {"timeoutMs", 5000},
5447:                    {"offset", {{"x", ox}, {"y", oy}, {"z", oz}}}
5448:                });
5449:            } else if (timedOut && !g_gmCalTimeoutLogged) {
5450:                g_gmCalTimeoutLogged = true;
5451:                inputCaptureAppend({
5452:                    {"t", now}, {"kind", "calibrate-timeout"},
5453:                    {"confidence", conf}, {"steady", steady}, {"weight", weight},
5454:                    {"safeZero", true}, {"reason", "manual-lock-requires-confidence-1-finite-offset-positive-weight"}
5455:                });
5456:            }
5457:        }
5458:    }
5459:        const bool moving = live && (pad.wButtons != 0 || pad.bLeftTrigger > 8 || pad.bRightTrigger > 8 ||
5460:        std::abs((int)pad.sThumbLX) > 2000 || std::abs((int)pad.sThumbLY) > 2000 || std::abs((int)pad.sThumbRX) > 2000 || std::abs((int)pad.sThumbRY) > 2000 ||
5461:        (gyro && (fabs(cgx) + fabs(cgy) + fabs(cgz) > 1.5)));
5462:    if (g_gmLocked && !moving && now - g_inputCaptureLastWrite < 200) {
5463:        inputCaptureCopyDesktop(false);
5464:        return;
5465:    }
5466:    if (now - g_inputCaptureLastWrite < 40) return;
5467:    g_inputCaptureLastWrite = now;
5468:    json rec = {
5469:        {"t", now},
5470:        {"kind", "sample"},
5471:        {"connected", live},
5472:        {"slot", (int)g_xinputPrimarySlot},
5473:        {"slots", (int)g_xinputConnectedMask},
5474:        {"buttons", pad.wButtons},
5475:        {"lt", pad.bLeftTrigger},
5476:        {"rt", pad.bRightTrigger},
5477:        {"lx", pad.sThumbLX},
5478:        {"ly", pad.sThumbLY},
5479:        {"rx", pad.sThumbRX},
5480:        {"ry", pad.sThumbRY},
5481:        {"imu", {
5482:            {"gyro", gyro},
5483:            {"accel", accel},
5484:            {"gx", cgx}, {"gy", cgy}, {"gz", cgz},
5485:            {"ax", maxa}, {"ay", may}, {"az", maz},
5486:            {"raw", {{"gx", gx}, {"gy", gy}, {"gz", gz}, {"ax", ax}, {"ay", ay}, {"az", az}}},
5487:            {"matrix", {{"gx", mgx}, {"gy", mgy}, {"gz", mgz}}},
5488:            {"offset", {{"x", ox}, {"y", oy}, {"z", oz}}},
5489:            {"confidence", conf},
5490:            {"steady", steady},
5491:            {"gamepadMotion", (bool)g_gm},
5492:            {"playerSpace", {{"x", psx}, {"y", psy}}},
5493:            {"worldSpace", {{"x", wsx}, {"y", wsy}}}
5494:        }}
5495:    };
5496:    inputCaptureAppend(rec);
5497:    if (g_hwnd && now - g_inputCaptureLastUi >= 80) {
5498:        g_inputCaptureLastUi = now;
5499:        // Capture runs off the UI thread. PostWebMessageAsJson is UI-thread-only,
5500:        // so marshal through the existing serial event lane (same as gamepad IPC).
5501:        gamepadSerialPostEvent("gyro.telemetry", {
5502:            {"schemaVersion", 1},
5503:            {"sequence", (unsigned long long)now},
5504:            {"timestampUtc", ""},
5505:            {"gyro", {{"x", cgx}, {"y", cgy}, {"z", cgz}}},
5506:            {"playerSpace", {{"x", psx}, {"y", psy}}},
5507:            {"worldSpace", {{"x", wsx}, {"y", wsy}}},
5508:            {"accel", {{"x", maxa}, {"y", may}, {"z", maz}}},
5509:            {"output", {{"x", 0.0}, {"y", 0.0}}}
5510:        });
5511:    }
`
stderr:

## Direct IMU assignment to HIDMaestro state
argv: Get-Content -LiteralPath G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\InputHost\Program.cs, lines 116-136
exitCode: 0
stdout:
`
116:        ctx.LoadDefaultProfiles();
117:        try { ctx.InstallDriver(); } catch { }
118:        var profile = ctx.GetProfile(persona) ?? throw new InvalidOperationException("profile-missing:" + persona);
119:        controller = ctx.CreateController(profile);
120:    }
121:
122:    static void Submit(JsonElement root)
123:    {
124:        if (controller is null) throw new InvalidOperationException("not-ready");
125:        var buttons = root.TryGetProperty("buttons", out var b) && b.ValueKind == JsonValueKind.Number ? b.GetInt32() : 0;
126:        var profile = ctx!.GetProfile(persona)!;
127:        var state = new HMGamepadState
128:        {
129:            Buttons = MapButtons(buttons),
130:            Hat = MapHat(buttons),
131:            Axes = HMGamepadStateHelpers.StandardAxes(profile, GetF(root, "lx"), GetF(root, "ly"), GetF(root, "rx"), GetF(root, "ry"), GetF(root, "lt"), GetF(root, "rt")),
132:            GyroDpsX = GetF(root, "gx"),
133:            GyroDpsY = GetF(root, "gy"),
134:            GyroDpsZ = GetF(root, "gz"),
135:        };
136:        controller.SubmitState(in state);
`
stderr:

## Required negative static branches
argv: Select-String -LiteralPath G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\native\main.cpp -Pattern pairProven, confidence, steady, offsetFinite, calibrate-timeout, safeZero
exitCode: 0
stdout:
`
4879:typedef bool (__cdecl *GmIsSteady)(void*); 4890:static GmIsSteady g_gmIsSteady = nullptr; 4927:    g_gmGetConf = (GmGetConf)GetProcAddress(g_gmDll, "GetAutoCalibrationConfidence"); 4928:    g_gmIsSteady = (GmIsSteady)GetProcAddress(g_gmDll, "GetAutoCalibrationIsSteady"); 5283:static void inputHostSubmitPad(const XINPUT_GAMEPAD& pad, double cgx, double cgy, double cgz, float psx, float psy, float wsx, float wsy, bool steady, bool calLocked) { 5399:    bool steady = false; 5404:    const bool pairProven = false; 5405:    if (g_gm && gyro && accel && !pairProven && !g_gmPairUnprovenLogged) { 5410:            {"gyro", true}, {"accel", true}, {"safeZero", true} 5413:    if (g_gm && g_gmProcess && g_gmGetCal && gyro && accel && pairProven) { 5427:        if (g_gmIsSteady) steady = g_gmIsSteady(g_gm); 5431:            // steady confidence==1 candidate may enter Manual. Five seconds 5435:            const bool offsetFinite = std::isfinite(ox) && std::isfinite(oy) && std::isfinite(oz); 5437:            if (conf == 1.0f && steady && offsetFinite && weight > 0 && g_gmGetOffset && g_gmSetOffset && g_gmSetMode) { 5445:                    {"confidence", conf}, {"steady", steady}, {"weight", weight}, 5452:                    {"t", now}, {"kind", "calibrate-timeout"}, 5453:                    {"confidence", conf}, {"steady", steady}, {"weight", weight}, 5454:                    {"safeZero", true}, {"reason", "manual-lock-requires-confidence-1-finite-offset-positive-weight"} 5489:            {"confidence", conf}, 5490:            {"steady", steady}, 5513:    inputHostSubmitPad(pad, cgx, cgy, cgz, psx, psy, wsx, wsy, steady, g_gmLocked);
`
stderr:
