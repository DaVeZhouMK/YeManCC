// The two persistent SDL aliases are still not assignable to HID interfaces
// from historical sdl:// indexes alone. Test the proved stale-PID1901 bug, not
// an invented mapping of those aliases to keyboard/mouse/vendor children.
static bool msiModeIdentityBoundaryRegression() {
    unsigned cases=0,failed=0;std::ostringstream report;
    auto check=[&](const char* name,bool ok){++cases;if(!ok)++failed;report<<(ok?"PASS ":"FAIL ")<<name<<"\n";};
    DIDEVICEINSTANCEW nameFixture{};nameFixture.tszProductName[0]=wchar_t(0x0609);DIDEVCAPS capsFixture{};capsFixture.dwAxes=6;capsFixture.dwButtons=17;
    const auto metadata=msiDinputEnumerationReceipt(nameFixture,0x0DB0,0x1902,L"fixture:msi-main",S_OK,capsFixture,S_OK,true);
    check("exact DirectInput product units survive diagnostic UTF8 conversion",metadata.at("productUtf16")==json::array({0x0609})&&metadata.at("product")=="؉");
    check("catalogue carries interface and actual device capabilities",metadata.at("interfacePath")=="fixture:msi-main"&&metadata.at("buttons")==17&&metadata.at("axes")==6);
    check("diagnostic catalogue never grants hiding or publication",metadata.at("enumerationOnly")==true&&metadata.at("hideAuthorized")==false&&!metadata.contains("buttonsDown"));
    const auto unknown=msiDinputEnumerationReceipt(nameFixture,0x0DB0,0x1902,{},E_FAIL,capsFixture,E_FAIL,false);
    check("unreadable metadata stays unknown not invented zero capabilities",unknown.at("capabilitiesKnown")==false&&!unknown.contains("axes")&&!unknown.contains("buttons"));
    const auto pendingReceipt=msiDinputTakeEnumerationReceipt();
    msiDinputStageEnumerationReceipt(metadata);msiDinputStageEnumerationReceipt(unknown);
    const auto latest=msiDinputTakeEnumerationReceipt();
    check("BUS staging coalesces bounded metadata without writing a log",latest.at("productUtf16")==json::array({0x0609})&&latest.at("capabilitiesKnown")==false&&latest.at("coalescedReceipts")==1);
    check("lifecycle consumes catalogue exactly once",msiDinputTakeEnumerationReceipt().is_null());
    if(!pendingReceipt.is_null())msiDinputStageEnumerationReceipt(pendingReceipt);
    const auto machine=g_machineIdentity;const bool route=g_msiDinputRouteDinputArmed.load(),confirmed=g_msiDinputRouteConfirmed.load(),valid=g_msiDinputSourceValid.load(),enabled=g_msiDinput.enabled.load(),main=g_msiDinput.mainInterfaceMatched.load();
    const auto pid=g_msiDinput.productId.load();const bool admitted=g_msiDinputAdmissionLogged.load();
    const DWORD connected=g_xinputConnectedMask,virtualMask=g_xinputVirtualEndpointMask,sourceMask=g_xinputPhysicalSourceMask;const bool locked=g_xinputPhysicalSourceLocked;
    HidHideMsiMainSourceRecord savedRecord;{std::lock_guard<std::mutex> l(g_msiMainSourceMx);savedRecord=g_msiMainSource;g_msiMainSource={};}
    g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";
    g_msiDinputRouteDinputArmed=true;g_msiDinput.enabled=true;g_msiDinput.mainInterfaceMatched=true;g_msiDinput.productId=0x1902;
    g_xinputConnectedMask=1;g_xinputVirtualEndpointMask=0;g_xinputPhysicalSourceMask=1;g_xinputPhysicalSourceLocked=true;
    for(const char* board:{"MS-1T41","MS-1T42","MS-1T52","MS-1T8K","MS-1T91"}) {
        g_machineIdentity.product=board;g_msiDinputRouteConfirmed=false;g_msiDinputSourceValid=false;
        check("mode2 rejects old locked 1901 slot before 1902 confirmation",!inputHostAdmitSelectedPhysicalSource());
        check("mode2 refusal does not release original slot ownership",g_xinputPhysicalSourceLocked&&g_xinputPhysicalSourceMask==1);
        std::vector<HidHidePhysicalIdentity> out(1);out[0].productId=0x1901;
        check("actual hide selector refuses stale source before SetupAPI",!hidHideGetPhysicalIdentities(out)&&out.empty());
        g_msiDinputRouteConfirmed=true;g_msiDinputSourceValid=true;
        check("valid 1902 source admits despite old XInput slot lock",inputHostAdmitSelectedPhysicalSource());
        // Valid sample without a current identity receipt still must not fall
        // back to old physical HID IDs. This call is also strictly no-I/O.
        check("missing 1902 record refuses old 1901 hide identity",!hidHideGetPhysicalIdentities(out)&&out.empty());
        g_msiDinput.productId=0x1901;
        check("wrong PID cannot sign DInput confirmation",!inputHostAdmitSelectedPhysicalSource());
        g_msiDinput.productId=0x1902;
    }
    for(const auto family:{YmccFamily::AsusRogAlly,YmccFamily::AsusRogAllyX,YmccFamily::AsusRogAllyClassic,YmccFamily::AsusRogAllyXClassic,YmccFamily::Unknown}) {
        g_machineIdentity.family=family;g_machineIdentity.manufacturer="ASUSTeK COMPUTER INC.";g_msiDinputRouteConfirmed=false;g_msiDinputSourceValid=false;
        check("ROG/common retain original locked-slot admission",inputHostAdmitSelectedPhysicalSource());
        check("ROG/common stay outside MSI hide guard",!msiControllerRepairScope());
    }
    g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";g_machineIdentity.product="UNREGISTERED";
    check("unknown MSI retains old slot admission",inputHostAdmitSelectedPhysicalSource()&&!msiControllerRepairScope());
    g_machineIdentity.product="MS-1T91";g_msiDinputRouteDinputArmed=false;
    check("unarmed MSI XInput retains old slot admission",inputHostAdmitSelectedPhysicalSource());
    g_machineIdentity=machine;g_msiDinputRouteDinputArmed=route;g_msiDinputRouteConfirmed=confirmed;g_msiDinputSourceValid=valid;g_msiDinput.enabled=enabled;g_msiDinput.mainInterfaceMatched=main;g_msiDinput.productId=pid;g_msiDinputAdmissionLogged=admitted;
    g_xinputConnectedMask=connected;g_xinputVirtualEndpointMask=virtualMask;g_xinputPhysicalSourceMask=sourceMask;g_xinputPhysicalSourceLocked=locked;
    {std::lock_guard<std::mutex> l(g_msiMainSourceMx);g_msiMainSource=savedRecord;}
    report<<"cases="<<cases<<" failed="<<failed<<"\n";std::ofstream out(fspath::path(g_yemanccDataDir)/L"msi-mode-identity-boundary-native-entry.txt");out<<report.str();return failed==0&&out.good();
}
