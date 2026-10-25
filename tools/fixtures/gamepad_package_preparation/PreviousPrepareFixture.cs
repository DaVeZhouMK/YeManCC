using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
using P=HidMaestroPackagePreparation;
internal static class PreviousPrepareFixture
{
    static int Main(string[] args)
    {
        if(args.Length!=2)return 2;
        var sdk=AssemblyLoadContext.Default.LoadFromAssemblyPath(Path.GetFullPath(args[0]));
        Directory.CreateDirectory(args[1]);
        var contract=P.ReadSdkMetadata(sdk,Path.Combine(args[1],"driver"));
        var old=new List<P.Driver> {new("oem118.inf","hidmaestro.inf","HIDMaestro","10/13/2026 1.12.0.2302"),
            new("oem119.inf","hidmaestro_xusb.inf","HIDMaestro","10/13/2026 1.12.0.2302")};
        var guard=new P.MaintenanceFacts(true,true,true,false,false);
        var review=Program.InspectCurrentPackage(true,contract,old,guard);
        var decision=Program.DecideForPreparation(new Program.Facts(true,true,false,true,2,true,false,true,true),review,true);
        bool pass=decision=="PREPARE_OFFICIAL";
        Console.WriteLine(JsonSerializer.Serialize(new {status=pass?"PASS":"EXPECTED_PREVIOUS_SDK200_PREPARE_FAILURE",decision,
            expected="PREPARE_OFFICIAL",sdkMethodsInvoked=0,actualPreviousProductionConsumer=true}));
        return pass?0:1;
    }
}
