using System.Reflection;
using System.Text;
using System.Text.Json;
using YeManBatteryHost;

// Transport fixture only. All controller state is the existing BatteryControllerBridge Rig.
internal sealed class MemoryDuplexPipe(byte[] request,bool cancelRead=false) : IDisposable
{
    int position;
    readonly MemoryStream response=new();
    public bool IsConnected=>true;
    public byte[] Response=>response.ToArray();
    public ValueTask<int> ReadAsync(byte[] target,CancellationToken token)
    {
        token.ThrowIfCancellationRequested();
        if(cancelRead)throw new OperationCanceledException("MEMORY_TRANSPORT_READ_CANCELLED");
        if(position>=request.Length)return ValueTask.FromResult(0);
        target[0]=request[position++];return ValueTask.FromResult(1);
    }
    public ValueTask WriteAsync(byte[] bytes,CancellationToken token)
    {token.ThrowIfCancellationRequested();response.Write(bytes);return ValueTask.CompletedTask;}
    public Task FlushAsync(CancellationToken token){token.ThrowIfCancellationRequested();return Task.CompletedTask;}
    public void Dispose()=>response.Dispose();
}
internal static class BatteryNativeEndpoint
{
    static readonly MethodInfo Execute=typeof(BatteryControllerBridge).GetMethod("Execute",BindingFlags.NonPublic|BindingFlags.Static)!;
    static readonly FieldInfo RigField=typeof(BatteryControllerBridge).GetField("Rig",BindingFlags.NonPublic|BindingFlags.Static)!;
    static readonly JsonSerializerOptions JsonOptions=new(JsonSerializerDefaults.Web);
    static object Fixture(string command,JsonElement args)=>Execute.Invoke(null,[command,args])!;
    static int BatteryFactoryCalls;
    static HostDomainRouter Domains=NewDomains();
    // Only the existing memory Rig can be created. No display controller or production Host entry is run.
    static HostDomainRouter NewDomains()
    {
        BatteryFactoryCalls=0;
        return new(()=>{BatteryFactoryCalls++;return ((Rig)RigField.GetValue(null)!).Controller;},
            _=>throw new IOException("UNREVIEWED_SECONDARY_DISPLAY_DOMAIN"),()=>true);
    }
    static void Shutdown()
    {
        Domains.Shutdown();
        // A never-created router domain has no controller to return; clean up only the fixture's unused Rig.
        if(BatteryFactoryCalls==0)((Rig)RigField.GetValue(null)!).Controller.Shutdown();
    }
    static JsonElement Empty=>JsonSerializer.SerializeToElement(new{});
    static async Task<int> Main()
    {
        while(await Console.In.ReadLineAsync() is {} line)
        {
            if(line.Length>32768)return 2;
            int id=-1;
            try
            {
                using var doc=JsonDocument.Parse(line);var request=doc.RootElement;id=request.GetProperty("id").GetInt32();var command=request.GetProperty("command").GetString()!;
                if(command=="pipe.exchange")
                {
                    var response=await ProductionMemoryPipeEndpoint.Exchange(Domains,Encoding.UTF8.GetBytes(request.GetProperty("raw").GetString()!),request.TryGetProperty("cancelRead",out var cancel)&&cancel.GetBoolean());
                    Console.WriteLine(JsonSerializer.Serialize(new{id,ok=true,raw=Encoding.UTF8.GetString(response),stats=Fixture("fixture.stats",Empty),batteryFactoryCalls=BatteryFactoryCalls},JsonOptions));
                }
                else if(command=="fixture.shutdown")
                {
                    Shutdown();Console.WriteLine(JsonSerializer.Serialize(new{id,ok=true,reply=Fixture("fixture.stats",Empty)},JsonOptions));return 0;
                }
                else if(command is "fixture.stats" or "fixture.reset" or "fixture.set" or "fixture.suspend" or "fixture.resume" or "fixture.tick")
                {
                    var args=request.TryGetProperty("args",out var value)?value:Empty;
                    object reply;
                    if(command=="fixture.reset") { Domains.Shutdown();reply=Fixture(command,args);Domains=NewDomains(); }
                    else if(command=="fixture.suspend") { Domains.Suspend();reply=Fixture("fixture.stats",Empty); }
                    else if(command=="fixture.resume") { Domains.Resume();reply=Fixture("fixture.stats",Empty); }
                    else if(command=="fixture.tick") { Domains.Tick();reply=Fixture("fixture.stats",Empty); }
                    else reply=Fixture(command,args);
                    Console.WriteLine(JsonSerializer.Serialize(new{id,ok=true,reply},JsonOptions));
                }
                else throw new IOException("UNREVIEWED_MEMORY_ENDPOINT_COMMAND");
            }
            catch(Exception error)
            {
                if(error is TargetInvocationException {InnerException:{} actual})error=actual;
                Console.WriteLine(JsonSerializer.Serialize(new{id,ok=false,error=error.Message},JsonOptions));
            }
        }
        Shutdown();return 0;
    }
}
