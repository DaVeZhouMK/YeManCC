#pragma once
// Fake adapter acceptance is separate from real Host/PnP acceptance.
static bool msiFakeSdFenceProtocolRegression(){
    const auto oldSeam=g_startEntrySeam;const auto oldFence=g_msiSteamDeckReleaseFence;
    unsigned checks=0,failed=0;std::ostringstream report;
    auto check=[&](const char* name,bool ok){++checks;if(!ok)++failed;report<<(ok?"PASS ":"FAIL ")<<name<<"\n";};
    json hello,prepared,released;g_startEntrySeam={};g_startEntrySeam.active=true;
    check("actual fake HELLO is served",ymStartPipeSeamServe("HELLO","OK",&hello));
    g_msiSteamDeckReleaseFence.clear();g_msiSteamDeckReleaseFence.hello(hello);
    check("actual current SD capability permits pin attempt",g_msiSteamDeckReleaseFence.begin(true,"steamdeck","offline-fence-run",9));
    check("actual fake PREPARE passes strict tuple and endpoint pin",ymStartPipeSeamServe("PREPARE_TARGET","OK",&prepared)&&g_msiSteamDeckReleaseFence.acceptPrepare(prepared));
    check("empty proof is still rejected",!g_msiSteamDeckReleaseFence.acceptRelease(json::object()));
    g_startEntrySeam.msiSdProofSucceeded=false;ymStartPipeSeamServe("RELEASE_TARGET","OK",&released);
    check("failed snapshot is still rejected",!g_msiSteamDeckReleaseFence.acceptRelease(released));
    g_startEntrySeam.msiSdProofSucceeded=true;g_startEntrySeam.msiSdReleaseAbsent=false;ymStartPipeSeamServe("RELEASE_TARGET","OK",&released);
    check("still-present target is still rejected",!g_msiSteamDeckReleaseFence.acceptRelease(released));
    g_startEntrySeam.msiSdReleaseAbsent=true;ymStartPipeSeamServe("RELEASE_TARGET","OK",&released);
    check("synthetic exact-owned absence is admitted",g_msiSteamDeckReleaseFence.acceptRelease(released));
    released["msiSteamDeckReleaseEvidence"]["epoch"]=10;
    check("foreign tuple remains rejected",!g_msiSteamDeckReleaseFence.acceptRelease(released));
    g_startEntrySeam.msiSdFenceSupported=false;ymStartPipeSeamServe("HELLO","OK",&hello);g_msiSteamDeckReleaseFence.hello(hello);
    check("old Host capability remains rejected",!g_msiSteamDeckReleaseFence.begin(true,"steamdeck","offline-fence-run",9));
    g_startEntrySeam=oldSeam;g_msiSteamDeckReleaseFence=oldFence;
    report<<"checks="<<checks<<" failed="<<failed<<" realDeviceCalls=0 realHostProcesses=0\n";
    std::ofstream out(fspath::path(g_yemanccDataDir)/L"msi-fake-sd-fence-protocol.txt");out<<report.str();return failed==0&&out.good();
}
