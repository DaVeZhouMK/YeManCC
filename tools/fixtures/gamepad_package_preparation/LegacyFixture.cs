using System.Reflection;
using System.Text.Json;
internal static class LegacyReadinessFixture
{
    static int Main()
    {
        var program=typeof(Program);var flags=BindingFlags.Static|BindingFlags.NonPublic;
        string xml="<Drivers><Driver DriverName='oem118.inf'><OriginalName>hidmaestro.inf</OriginalName><ProviderName>HIDMaestro</ProviderName><DriverVersion>08/21/2026 1.4.7.12</DriverVersion></Driver><Driver DriverName='oem119.inf'><OriginalName>hidmaestro_xusb.inf</OriginalName><ProviderName>HIDMaestro</ProviderName><DriverVersion>08/21/2026 1.4.7.12</DriverVersion></Driver></Drivers>";
        var rows=program.GetMethod("ParseDrivers",flags)!.Invoke(null,[xml]);
        bool complete=(bool)program.GetMethod("Complete",flags)!.Invoke(null,[rows])!;
        var factsType=program.GetNestedType("Facts",BindingFlags.NonPublic)!;
        var facts=Activator.CreateInstance(factsType,[true,true,false,true,2,true,false,true,complete]);
        string status=(string)program.GetMethod("Decide",flags)!.Invoke(null,[facts,true])!;
        bool pass=status!="DRIVER_STORE_READY";
        Console.WriteLine(JsonSerializer.Serialize(new{status=pass?"PASS":"EXPECTED_OLD_CODE_FAILURE",observedStatus=status,
            knownFieldFacts="new core + exact old two driver versions + no current catalog; legacy sdkReady=true",complete,
            sdkMethodsInvoked=0,deviceApisInvoked=false}));
        return pass?0:1;
    }
}
