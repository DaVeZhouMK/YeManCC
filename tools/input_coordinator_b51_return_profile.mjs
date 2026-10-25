// Exact static source identities and read-only observation anchors. No source/runtime admission.
const freeze=value=>{if(value&&typeof value==='object'){for(const member of Object.values(value))freeze(member);Object.freeze(value);}return value;};
export const B51_RETURN_PROFILE=freeze({
  "schemaVersion": 1,
  "kind": "EXACT_STATIC_B51_RETURN_PROFILE_NOT_EXECUTION_AUTHORITY",
  "fixedBinding": {
    "path": "D:/Codex/2026-10-18/kai-s/work/global-simulator-20261018/phase32/B51-fixed-run-input-bindings.json",
    "sha256": "adda6d41dc82a2a99fdd709d50ddb5b477a05ba7242ee5a64181013938910133",
    "snapshotRoot": "G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC-SimSnapshot-20261021-B51",
    "manifestSHA": "3282961fc2d6473e2c4c86e889326c368cf91b1d78e35b8f41e967fe9f4ffa44",
    "files": 8128,
    "bytes": 501870700
  },
  "producerSHA": "880d6734d536e4c12cea53cb61572bae230aea640096236badc328076d0a1346",
  "expandedSHA": "60ec167ce39be67c5c8a4df74a4e939725ffe7631644c90835715a2aa389793a",
  "recipeCount": 28,
  "recipeSHA": "a0ef873fc5d0985b087a48d5848d8a1477e01d0df331fd1775e724a75e3eb97d",
  "legacyRecipeCount": 21,
  "legacyRecipeSHA": "98889d67229707e320bff765db7d15dae7028a2ffa1883f62f71dcca3c9d96c5",
  "legacyPortNames": [
    "inputHostStartProtectionNow",
    "inputHostTargetReturnRecordPath",
    "inputHostTargetReturnMachineBinding",
    "inputHostTargetReturnCaptureOwner",
    "inputHostTargetReturnVerifyPhysicalPath",
    "inputHostTargetReturnProbePhysicalWriter",
    "observeTdpRuntimeIdentity",
    "inputCaptureAssetsPresent",
    "oemBackMappingRefreshGate",
    "reg_desktop_monitor_hud",
    "msiDinputOpen",
    "msiClawOpenCommandHid",
    "ymccXInputGetState14",
    "ymccXInputGetStateSecret14",
    "ymccXInputSetState14",
    "ymccOpenXInputGetDevicePath",
    "ymccOpenXInputGetUserIndex",
    "ymccOpenXInputAvailable",
    "hidHideServiceRunning",
    "hidHideRun",
    "appendNativeLifecycleLog"
  ],
  "additionalPortNames": [
    "hidHideCliWriterRecordPath",
    "hidHideCliWriterMachineBinding",
    "hidHideCliWriterCapture",
    "hidHideCliWriterProbe",
    "hidHideCliWriterProbeJob",
    "inputHostTargetReturnColdAbsence",
    "inputHostTargetReturnColdHidden"
  ],
  "fixtureSHA": {
    "fixture.cpp": "d1c23e7c652bd6ee20e3ebca8203a25f7d70ffbdef92f2dc3273d18fda4750d1",
    "pipe_cases.inc": "aedf0e692b896e9ccc23839f0df9d25bd7cfda4d1017323159638cfa4be16a99",
    "target_return_cases.inc": "4b6fae563968da232a158cea64338ac7aa87ab51becd0ff55729a30fadbdcd5d",
    "prepare_ack_cases.inc": "3d45b27866a12dec759ffa01d123f8b64376e9ba788ae0245772d923a377c02a",
    "off_lifecycle_cases.inc": "70c6d964e90d328718da955a4163ace6855e29882c6599f58606e92b649e167e",
    "ps_return_cases.inc": "7958fa04f7befd3ba9357c059bf1b8a6ec14ea42c017946e2ff16b3f3fe89122",
    "ps_wake_cases.inc": "e3719ca4d2811fc5179935568d8e5e21e2bdcf8935a22c4435a2ddf8966471b6"
  },
  "hooks": [
    {
      "symbol": "inputHostTargetReturnBeforeCreateLocked",
      "sha256": "c795085349d95a71f0f71b282c95bb140dbd76ebb49d96af1fa4d7effa6a2dab"
    },
    {
      "symbol": "inputHostTargetReturnAcceptPsLocked",
      "sha256": "241d40b3d0b061d6a13eeee5dd03e63858f97eba77d0e5d64e6881b2603626b3"
    },
    {
      "symbol": "inputHostTargetReturnTargetExitedLocked",
      "sha256": "bcc0e1ff6be5a7467c2c240f74d863ecc026fc6a6a35885ca547d5b28c280eaf"
    },
    {
      "symbol": "inputHostTargetReturnBeforeHideLocked",
      "sha256": "c35a3f969c84bc8646a0f3106e2166a88e83d87ca4941e397e2124d7024bcc0e"
    },
    {
      "symbol": "inputHostTargetReturnAfterPhysicalRestoreLocked",
      "sha256": "09797d669f09558aaeb253a31f56a7dc543c6977be9191de4cd8f17707166b51"
    }
  ],
  "legacyNameSequenceSHA": "95df6b96a4fa1bb64af496bb594d890e071b4f652360691efcfcef016af29952",
  "additionalCaseName": "PS-RETURN/ROG/owned-HID-absent/xinput-mask-0/durable-store-blocked-once",
  "additionalCaseActualOrdinal": 93,
  "legacyOccurrences": 100,
  "legacyUniqueNames": 99,
  "actualOccurrences": 101,
  "actualUniqueNames": 100,
  "legacyMsiWitnessName": "OFF-LIFECYCLE/MSI-return-unproven",
  "observationPatches": [
    {
      "label": "Original read-only observation 1",
      "before": "static void check(bool good,const char* name){++checks;const auto owner=cases.empty()?\"bootstrap\":cases.back().value(\"name\",\"\");assertionTrace.push_back({{\"case\",owner},{\"assertion\",name},{\"passed\",good},{\"checkIndex\",checks}});if(!good)failures.push_back({{\"case\",owner},{\"assertion\",name}});}\n",
      "after": "static void check(bool good,const char* name){++checks;const auto owner=cases.empty()?\"bootstrap\":cases.back().value(\"name\",\"\");\n const auto ordinal=cases.size();unsigned occurrence=0;for(const auto& item:cases)if(item.value(\"name\",\"\")==owner)++occurrence;\n if(!cases.empty()){cases.back()[\"sourceOrdinal\"]=ordinal;cases.back()[\"nameOccurrence\"]=occurrence;}\n assertionTrace.push_back({{\"case\",owner},{\"assertion\",name},{\"passed\",good},{\"checkIndex\",checks},{\"sourceOrdinal\",ordinal},{\"nameOccurrence\",occurrence}});\n if(!good)failures.push_back({{\"case\",owner},{\"assertion\",name},{\"sourceOrdinal\",ordinal},{\"nameOccurrence\",occurrence},{\"checkIndex\",checks}});}\n"
    },
    {
      "label": "Original read-only observation 2",
      "before": "int wmain(int argc,wchar_t**argv){try{\n",
      "after": "// Read-only observations at the original contract boundary (including explicit B51 extension) and after the return tail.\n// Same resource fields/lock as the original grouped witness; no new check,\n// seed, Stop, debt assignment, process query or file/device operation.\nstatic json fixtureB51OrderedReturn=json::object();\nstatic json fixtureB51OrderedBoundary(){\n std::lock_guard<std::mutex> work(g_inputHostLifecycleWorkMx);\n return json{{\"afterSourceOrdinal\",cases.size()},{\"lastCaseName\",cases.empty()?\"\":cases.back().value(\"name\",\"\")},{\"checks\",checks},{\"nativeEvents\",fixtureEvents},{\"resources\",json{{\"hostHandle\",g_inputHostProcess!=nullptr},{\"pipeHandle\",g_inputHostServerPipe!=nullptr},{\"pipeConnected\",g_inputHostPipeConnected},{\"prepared\",g_inputHostPrepared},{\"quarantined\",g_inputHostReleaseQuarantined},{\"visibility\",g_hidHideVisibilityJournal.active},{\"childObservers\",fullPipeChildObservers.size()},{\"workQueue\",g_inputHostLifecycleWorkQueue.size()},{\"workRequested\",g_inputHostLifecycleWorkRequested.load()},{\"workInFlight\",g_inputHostLifecycleWorkInFlight.load()},{\"psRetirement\",g_psEndpointOwnership.retirementRequired()},{\"msiSdDebt\",g_msiSteamDeckReleaseDebt.required()},{\"msiModeDebt\",g_msiDinputReturnOwed.load()},{\"msiModeDebtEpisode\",g_msiDinputReturnDebtEpisode.load()},{\"startBlocked\",g_inputHostStartBlocked},{\"spawnCount\",fullPipeSpawnCount},{\"receiptPaths\",fullPipeReceipts.size()}}}};\n}\nint wmain(int argc,wchar_t**argv){try{\n"
    },
    {
      "label": "Original read-only observation 3",
      "before": " else if(mode==L\"full-pipe\"){fullPipeCases();fixtureTargetReturnFullPipeCases();ok=failures.empty();}\n",
      "after": " else if(mode==L\"full-pipe\"){fullPipeCases();fixtureB51OrderedReturn[\"actualOriginalRegionBoundary\"]=fixtureB51OrderedBoundary();fixtureTargetReturnFullPipeCases();fixtureB51OrderedReturn[\"postTargetReturnTail\"]=fixtureB51OrderedBoundary();ok=failures.empty();}\n"
    },
    {
      "label": "Original read-only observation 4",
      "before": " const auto report=json{{\"status\",ok?\"PASS\":\"FAIL\"},{\"checks\",checks},{\"assertionTrace\",assertionTrace},{\"cases\",cases},{\"failures\",failures},{\"expectedFailure\",expectedFailure},{\"forbiddenIo\",forbiddenIo},{\"inertXinputReads\",inertXinputReads},{\"targetReturnObservations\",fixtureTargetReturnObservations},{\"targetReturnCaptureModel\",\"full-pipe: real107 parent+managed-child; native-only: event Host has explicit controlled PID/birth\"},{\"fullTranslationUnit\",true},{\"normalProductMainExecuted\",false},{\"realDeviceIo\",false}};\n",
      "after": " const auto report=json{{\"status\",ok?\"PASS\":\"FAIL\"},{\"checks\",checks},{\"assertionTrace\",assertionTrace},{\"orderedReturn\",fixtureB51OrderedReturn},{\"cases\",cases},{\"failures\",failures},{\"expectedFailure\",expectedFailure},{\"forbiddenIo\",forbiddenIo},{\"inertXinputReads\",inertXinputReads},{\"targetReturnObservations\",fixtureTargetReturnObservations},{\"targetReturnCaptureModel\",\"full-pipe: real107 parent+managed-child; native-only: event Host has explicit controlled PID/birth\"},{\"fullTranslationUnit\",true},{\"normalProductMainExecuted\",false},{\"realDeviceIo\",false}};\n"
    },
    {
      "label": "Original read-only observation 5",
      "before": " }catch(const std::exception& e){try{const auto report=json{{\"status\",\"INCOMPLETE\"},{\"exception\",e.what()},{\"checks\",checks},{\"assertionTrace\",assertionTrace},{\"cases\",cases},{\"failures\",failures},{\"nativeEvents\",fixtureEvents},{\"state\",state()},{\"forbiddenIo\",forbiddenIo},{\"normalProductMainExecuted\",false},{\"realDeviceIo\",false}};std::ofstream(fspath::path(g_yemanccDataDir)/L\"fixture-exception.json\")<<report.dump(2);}catch(...){}std::cerr<<e.what()<<'\\n';return 3;}}\n",
      "after": " }catch(const std::exception& e){try{const auto report=json{{\"status\",\"INCOMPLETE\"},{\"exception\",e.what()},{\"checks\",checks},{\"assertionTrace\",assertionTrace},{\"orderedReturn\",fixtureB51OrderedReturn},{\"cases\",cases},{\"failures\",failures},{\"nativeEvents\",fixtureEvents},{\"state\",state()},{\"forbiddenIo\",forbiddenIo},{\"normalProductMainExecuted\",false},{\"realDeviceIo\",false}};std::ofstream(fspath::path(g_yemanccDataDir)/L\"fixture-exception.json\")<<report.dump(2);}catch(...){}std::cerr<<e.what()<<'\\n';return 3;}}\n"
    }
  ]
});
