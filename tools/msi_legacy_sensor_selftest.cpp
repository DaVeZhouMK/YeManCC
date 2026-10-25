#define _WIN32_WINNT 0x0A00
#include <initguid.h>
#include "../native/msi_legacy_sensor.h"
#include "../native/msi_motion_capabilities.h"
#include <iostream>
#include <limits>
using namespace ymcc::msi_sensor;
struct ReportStub final : ISensorDataReport {
    struct Field {PROPERTYKEY key{}; VARTYPE type=VT_R8; double number=0;};
    std::vector<Field> fields;
    bool hasTime=true;
    SYSTEMTIME time{2026,10,0,11,0,42,0,0};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** out) override {if(!out)return E_POINTER;*out=nullptr;return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() override {return 1;}
    ULONG STDMETHODCALLTYPE Release() override {return 1;}
    HRESULT STDMETHODCALLTYPE GetTimestamp(SYSTEMTIME* out) override {if(!out)return E_POINTER;if(!hasTime)return E_FAIL;*out=time;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetSensorValue(REFPROPERTYKEY key,PROPVARIANT* value) override {
        if(!value)return E_POINTER;
        for(const auto& f:fields) if(IsEqualGUID(f.key.fmtid,key.fmtid)&&f.key.pid==key.pid) {
            *value=PROPVARIANT{};value->vt=f.type;
            if(f.type==VT_R8)value->dblVal=f.number;
            else if(f.type==VT_R4)value->fltVal=static_cast<float>(f.number);
            else if(f.type==VT_I4)value->lVal=static_cast<LONG>(f.number);
            else if(f.type==VT_BOOL)value->boolVal=VARIANT_TRUE;
            return S_OK;
        }
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    HRESULT STDMETHODCALLTYPE GetSensorValues(IPortableDeviceKeyCollection*,IPortableDeviceValues** out) override {if(out)*out=nullptr;return E_NOTIMPL;}
    void triple(const Layout& layout,std::array<double,3> axes) {fields.clear();for(size_t i=0;i<3;++i)fields.push_back({PROPERTYKEY{layout.format,layout.pids[i]},VT_R8,axes[i]});}
};
int main() {
    unsigned cases=0,failures=0;
    auto check=[&](const char* name,bool ok){++cases;if(!ok)++failures;std::cout<<(ok?"PASS ":"FAIL ")<<name<<"\n";};
    const auto sdk=layouts(Kind::Gyro,false)[0];
    check("SDK gyro format exactly matches installed Microsoft SDK",IsEqualGUID(sdk.format,SENSOR_DATA_TYPE_ANGULAR_VELOCITY_X_DEGREES_PER_SECOND.fmtid));
    check("SDK gyro PID labels are 10/11/12, not CTW custom-format guesses",sdk.pids[0]==SENSOR_DATA_TYPE_ANGULAR_VELOCITY_X_DEGREES_PER_SECOND.pid && sdk.pids[1]==SENSOR_DATA_TYPE_ANGULAR_VELOCITY_Y_DEGREES_PER_SECOND.pid && sdk.pids[2]==SENSOR_DATA_TYPE_ANGULAR_VELOCITY_Z_DEGREES_PER_SECOND.pid);
    check("SDK gyro sensor type matches Microsoft SDK",IsEqualGUID(kGyroType,SENSOR_TYPE_GYROMETER_3D));
    check("SDK accel sensor type matches Microsoft SDK",IsEqualGUID(kAccelType,SENSOR_TYPE_ACCELEROMETER_3D));
    const auto accel=layouts(Kind::Accel,false)[0];
    check("SDK accel fields match Microsoft SDK",IsEqualGUID(accel.format,SENSOR_DATA_TYPE_ACCELERATION_X_G.fmtid)&&accel.pids[0]==SENSOR_DATA_TYPE_ACCELERATION_X_G.pid&&accel.pids[2]==SENSOR_DATA_TYPE_ACCELERATION_Z_G.pid);
    const auto claw=layouts(Kind::Gyro,true)[1];
    check("Claw EX field format is kept separate from standard SDK",!IsEqualGUID(claw.format,sdk.format)&&claw.pids==std::array<DWORD,3>{7,8,9});
    check("Unapproved compatibility does not expose OEM field layouts",layouts(Kind::Gyro,false).size()==1);
    ReportStub report; std::array<double,3> output{9,9,9};
    report.triple(sdk,{1.25,-2.0,0.0});
    check("Read complete real report without applying CTW remap",readTriple(&report,sdk,output)&&output==std::array<double,3>{1.25,-2.0,0.0});
    report.triple(claw,{12,34,56});
    check("OEM and SDK property formats cannot alias",!readTriple(&report,sdk,output));
    check("Claw custom triple preserves raw axis order",readTriple(&report,claw,output)&&output==std::array<double,3>{12,34,56});
    report.fields.pop_back();output={9,9,9};
    check("Missing axis cannot publish partial or previous values",!readTriple(&report,claw,output)&&output==std::array<double,3>{9,9,9});
    report.triple(claw,{0,0,0});
    check("A resting gyro's zero triple is real data, not absence",readTriple(&report,claw,output)&&output==std::array<double,3>{0,0,0});
    report.fields[1].type=VT_BOOL;
    check("Sensor state boolean cannot masquerade as a motion axis",!readTriple(&report,claw,output));
    report.triple(claw,{0,std::numeric_limits<double>::infinity(),0});
    check("Infinite axis rejected",!readTriple(&report,claw,output));
    report.triple(claw,{0,std::numeric_limits<double>::quiet_NaN(),0});
    check("NaN axis rejected",!readTriple(&report,claw,output));
    UINT64 ts=0;check("Actual UTC report timestamp converts to WinRT 100ns epoch",timestamp(&report,ts)&&ts>0);
    report.hasTime=false;check("Missing timestamp is not replaced by receipt clock",!timestamp(&report,ts));
    TimestampGate clock;
    check("Only new timestamps advance sample sequence",clock.admit(100)&&!clock.admit(100)&&!clock.admit(99)&&clock.admit(101)&&!clock.admit(0));
    Descriptor standard;standard.type=kGyroType;
    Descriptor oem;oem.name=L"Physical Gyrometer";
    Descriptor unknown;unknown.name=L"Unrelated sensor";
    check("EX custom gyro has evidence-qualified compatibility rank",candidateRank(oem,Kind::Gyro,true)==2);
    check("EX compat does not silently apply to every device",candidateRank(oem,Kind::Gyro,false)==0);
    check("An unrelated sensor cannot be selected by numeric coincidences",candidateRank(unknown,Kind::Gyro,true)==0);
    check("Known standard type takes precedence over custom compatibility",chooseUnique({oem,standard},Kind::Gyro,true)==1);
    check("Two equally qualified gyros fail closed instead of choosing first",chooseUnique({standard,standard},Kind::Gyro,true)==-1);
    check("Unknown type with canonical declared SDK fields can be admitted",([]{Descriptor d;d.documentedGyroFields=true;return chooseUnique({d},Kind::Gyro,false)==0;})());
    check("Gyro name does not qualify an accelerometer",candidateRank(oem,Kind::Accel,true)==0);
    const auto* ex=motionCapability("HandheldCompanion.Devices.ClawCG3EM","MS-1T91");
    const auto* a2=motionCapability("HandheldCompanion.Devices.ClawA2VM","MS-1T52");
    const auto* a1=motionCapability("HandheldCompanion.Devices.ClawA1M","MS-1T41");
    const auto* bz=motionCapability("HandheldCompanion.Devices.ClawBZ2EM","MS-1T8K");
    check("G3E model and actual board qualify the observed CTW HID layout",ex && ex->clawHidSensorApiLayout && ex->exWorldSpaceDefaultOff);
    check("258V has its OWN current HC-defined legacy field profile, not an EX guess",a2 && a2->clawHidSensorApiLayout);
    check("A1M does not inherit EX's unverified property layout",a1 && !a1->clawHidSensorApiLayout);
    check("AMD BZ2EM does not borrow CTW's Intel-only sensor workaround",bz && !bz->clawHidSensorApiLayout);
    check("Wrong board or unknown model cannot enable a guessed sensor profile",!motionCapability("HandheldCompanion.Devices.ClawCG3EM","MS-7B89") && !motionCapability("unknown","MS-1T91"));
    check("A2VM 7-inch and 8-inch boards share declared HC family policy",motionCapability("HandheldCompanion.Devices.ClawA2VM","MS-1T42")==a2);
    check("G3E physical axes are not falsely marked verified by upstream configuration",ex && ex->matrixEvidence.find("pending")!=std::string_view::npos);
    check("Configured B14C legacy gyro uses current HC X/Z/-Y exactly once",configuredLegacyAxes({1,2,3})==std::array<double,3>{1,3,-2});
    check("Configured B14C legacy accel follows HC rather than conflicting CTW signs",configuredLegacyAxes({4,5,6})==std::array<double,3>{4,6,-5});
    check("A2VM and G3E current resource receipts carry separate source identities",ex && a2 && !legacyRecipeSha256(*ex).empty() && legacyRecipeSha256(*ex)!=legacyRecipeSha256(*a2));
    std::cout<<"cases="<<cases<<" failures="<<failures<<"\n";
    return failures?3:0;
}
