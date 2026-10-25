using System.Diagnostics;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using YeManBatteryHost;

internal sealed class MemoryStore():Store("fixture-only-never-used")
{
    readonly Dictionary<string,string> data=[];
    public override T? Load<T>(string name) where T:default {if(name.StartsWith("battery-"))throw new Exception("display touched charge journal");return data.TryGetValue(name,out var value)?JsonSerializer.Deserialize<T>(value,Policy.Json):default;}
    public override void Save<T>(string name,T value)=>data[name]=JsonSerializer.Serialize(value,Policy.Json);
    public override void Clear(string name)=>data.Remove(name);
}
internal sealed class MemoryEc
{
    public byte Raw=200;
    public int Opens,Disposals,Writes;
    public string Failure="";
    public readonly List<string> Events=[];
    public IEcAccess Open(bool direct){if(direct)throw new Exception("wrong EC address space");Opens++;return new Session(this);}
    sealed class Session(MemoryEc owner):IEcAccess
    {
        public byte Read(ushort register){if(register!=0x4e)throw new Exception("wrong EC register");owner.Events.Add("read-4e");return owner.Raw;}
        public void Write(ushort register,byte value)
        {
            if(register!=0x4e&&register!=0x4f)throw new Exception("unreviewed EC write");
            if(register==0x4f&&value!=0)throw new Exception("wrong latch byte");
            owner.Events.Add($"write-{register:x2}-{value}");owner.Writes++;
            if(register==0x4e){owner.Raw=value;if(owner.Failure=="write-after")throw new IOException("controlled acknowledgement lost");}
        }
        public void Dispose(){owner.Disposals++;}
    }
}
internal static class Program
{
    [DllImport("kernel32.dll",SetLastError=true)] [return:MarshalAs(UnmanagedType.Bool)] static extern bool GetNamedPipeClientProcessId(Microsoft.Win32.SafeHandles.SafePipeHandle pipe,out uint pid);
    static async Task<int> Main(string[] args)
    {
        int parentId=int.Parse(args[0]);long stamp=long.Parse(args[1]);string board=args[2],eventName=args[3],resultPath=args[4];
        using var parent=Process.GetProcessById(parentId);if(parent.StartTime.ToUniversalTime().ToFileTimeUtc()!=stamp)throw new IOException("fixture parent changed");
        var identity=new Identity("AYANEO",board,"fixture","fixture","fixture",2,80);var ec=new MemoryEc();var store=new MemoryStore();int batteries=0,requests=0,hellos=0;
        var domains=new HostDomainRouter(()=>{batteries++;throw new Exception("display constructed battery domain");},ready=>new(identity,store,ec.Open,ready),()=>!parent.HasExited);
        using var stop=new CancellationTokenSource();using var ready=EventWaitHandle.OpenExisting(eventName);
        var monitor=Task.Run(async()=>{try{while(!stop.IsCancellationRequested){if(parent.HasExited){stop.Cancel();return;}await Task.Delay(250,stop.Token);}}catch(OperationCanceledException){}});
        bool readySent=false;
        try
        {
            while(!stop.IsCancellationRequested)
            {
                await using var pipe=new NamedPipeServerStream($"YMCC.BatteryCharge.{parentId}.{stamp}",PipeDirection.InOut,1,PipeTransmissionMode.Byte,PipeOptions.Asynchronous|PipeOptions.CurrentUserOnly);
                if(!readySent){ready.Set();readySent=true;}
                await pipe.WaitForConnectionAsync(stop.Token);if(!GetNamedPipeClientProcessId(pipe.SafePipeHandle,out var client)||client!=parentId)throw new IOException("fixture client PID mismatch");
                using var reader=new StreamReader(pipe,Encoding.UTF8,false,1024,true);string? line=await reader.ReadLineAsync(stop.Token);if(line is null)continue;
                using var doc=JsonDocument.Parse(line);var root=doc.RootElement;string op=root.GetProperty("op").GetString()??"";object result;
                if(op=="hello"){hellos++;result=RuntimeContract.Identity();}
                else
                {
                    RuntimeContract.Validate(root);requests++;
                    result=op switch
                    {
                        "fixture.status"=>new {ok=true,ec.Raw,ec.Opens,ec.Disposals,ec.Writes,batteries},
                        "fixture.external"=>External(root,ec),
                        "fixture.fault"=>Fault(root,ec),
                        "fixture.suspend"=>Suspend(domains),
                        "fixture.resume"=>Resume(domains),
                        "fixture.stop"=>new {ok=true},
                        _=>domains.Dispatch(root)
                    };
                }
                byte[] response=Encoding.UTF8.GetBytes(JsonSerializer.Serialize(result,Policy.Json)+"\n");await pipe.WriteAsync(response,stop.Token);await pipe.FlushAsync(stop.Token);
                if(op=="fixture.stop")break;
            }
            return 0;
        }
        finally
        {
            stop.Cancel();await monitor;domains.Shutdown();
            File.WriteAllText(resultPath,JsonSerializer.Serialize(new {schemaVersion=1,status=batteries==0&&ec.Opens==ec.Disposals?"PASS":"FAIL",board,requests,hellos,batteries,ec.Raw,ec.Opens,ec.Disposals,ec.Writes,ec.Events,productHostMainExecuted=false,hardwareIo=false},Policy.Json));
        }
    }
    static object External(JsonElement root,MemoryEc ec){ec.Raw=root.GetProperty("raw").GetByte();return new {ok=true};}
    static object Fault(JsonElement root,MemoryEc ec){ec.Failure=root.GetProperty("failure").GetString()??"";return new {ok=true};}
    static object Suspend(HostDomainRouter domains){domains.Suspend();return new {ok=true};}
    static object Resume(HostDomainRouter domains){domains.Resume();return new {ok=true};}
}