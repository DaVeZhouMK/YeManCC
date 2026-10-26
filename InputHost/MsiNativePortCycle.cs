using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32;
using Microsoft.Win32.SafeHandles;
using Nefarius.Utilities.DeviceManagement.Extensions;
using Nefarius.Utilities.DeviceManagement.PnP;

// One-shot reuse of the existing HC/Nefarius CyclePort implementation for MSI
// native-only foreground visibility. Never creates a HostSession/HMContext,
// target, pipe, service, driver install, or firmware mode write.
internal static class MsiNativePortCycle
{
    internal const string Command = "--msi-native-cycle";
    internal const string GameXusbCycle = "--game-xusb-cycle";
    private const uint DnStarted = 0x00000008, DnHasProblem = 0x00000400;
    private const uint CycleWaitTimeout = 0x00000102;
    private const ulong MaxInitialCycleBudgetMs = 30000;
    private static bool ExactGpdXusbCycleRoot(string root,string target) =>
        ValidRoot(root) && root.StartsWith(@"USB\VID_045E&PID_028E\",StringComparison.OrdinalIgnoreCase) &&
        root.Equals(target,StringComparison.OrdinalIgnoreCase);
    private static readonly Guid LocationFormat = new("a45c254e-df1c-4efd-8020-67d146a850e0");
    [StructLayout(LayoutKind.Sequential)] private struct PropertyKey { public Guid Format; public uint Id; }
    private static readonly PropertyKey LocationKey = new() { Format = LocationFormat, Id = 37 };
    internal static bool IsKnownMsi(string manufacturer, string board) =>
        manufacturer.Contains("MICRO-STAR", StringComparison.OrdinalIgnoreCase) &&
        new[] { "MS-1T41", "MS-1T42", "MS-1T52", "MS-1T8K", "MS-1T91" }.Any(x => board.Contains(x, StringComparison.OrdinalIgnoreCase));
    internal static bool ValidRoot(string root) => new[]{@"USB\VID_0DB0&PID_1901\",@"USB\VID_0B05&PID_1ABE\",@"USB\VID_0B05&PID_1B4C\",@"USB\VID_045E&PID_028E\"}.Any(prefix=>root.StartsWith(prefix,StringComparison.OrdinalIgnoreCase)) &&
        root.Length is > 22 and < 512 && root.IndexOf('\\',22)<0 && root.All(c => char.IsAsciiLetterOrDigit(c) || c is '\\' or '&' or '_' or '-' or '{' or '}') &&
        !root.Contains("&MI_",StringComparison.OrdinalIgnoreCase) && !root.Contains("VIGEM",StringComparison.OrdinalIgnoreCase) &&
        !root.Contains("HIDMAESTRO",StringComparison.OrdinalIgnoreCase);
    internal static bool TryArguments(string[] args, out string root, out string location, out int parent)
    {
        root=location="";parent=0;
        if(args.Length is not (7 or 9 or 15) || args[0]!=Command || args[2]!="--location" || args[4]!="--parent-pid" || args[6]!="--once" ||
            !ValidRoot(args[1]) || !int.TryParse(args[5],out parent) || parent<=0 || parent==Environment.ProcessId) return false;
        if(args.Length==7 && !args[1].StartsWith(@"USB\VID_0DB0&PID_1901\",StringComparison.OrdinalIgnoreCase))return false;
        if(args.Length==15 && (args[7]!=GameXusbCycle || !ExactGpdXusbCycleRoot(args[1],args[8]) ||
            !TryCycleLifetimeArguments(args,out _,out _,out _)))return false;
        if(args.Length==9 && (args[7] is not ("--game-hid" or "--game-xusb") ||
            (args[7]=="--game-hid"?!ValidGameInstance(args[8]):!ValidXusbInstance(args[8],args[1])) ||
            !args[8].AsSpan(4,17).Equals(args[1].AsSpan(4,17),StringComparison.OrdinalIgnoreCase)))return false;
        location=args[3];
        if(location.Length is < 4 or > 4096 || location.Any(c => c<' ' || c=='"') || !location.Contains("USB",StringComparison.OrdinalIgnoreCase)) return false;
        root=args[1];return true;
    }
    private static bool TryPositiveCanonicalInteger(string value,out ulong expected) =>
        ulong.TryParse(value,System.Globalization.NumberStyles.None,System.Globalization.CultureInfo.InvariantCulture,out expected) &&
        expected>0 && expected.ToString(System.Globalization.CultureInfo.InvariantCulture)==value;
    private static bool TryCycleLifetimeArguments(string[] args,out ulong birth,out ulong deadline,out nuint cancelHandle)
    {
        birth=deadline=0;cancelHandle=0;
        if(args.Length!=15 || args[9]!="--parent-birth" || args[11]!="--deadline-tick" || args[13]!="--cancel-handle" ||
            !TryPositiveCanonicalInteger(args[10],out birth) || !TryPositiveCanonicalInteger(args[12],out deadline) ||
            !TryPositiveCanonicalInteger(args[14],out var handle) || handle>=(ulong)nuint.MaxValue)return false;
        cancelHandle=(nuint)handle;return true;
    }
    private static bool CycleExecutionAllowed(ulong deadline,Func<bool> parentAlive,Func<ulong> tick,Func<uint> cancellation,bool initial=false)
    {
        try{
            if(!parentAlive())return false;
            var now=tick();
            if(now>=deadline || (initial && deadline-now>MaxInitialCycleBudgetMs))return false;
            return cancellation()==CycleWaitTimeout;
        }catch{return false;}
    }
    private static bool ExpectedParentMatches(ulong expected,Func<ulong?> readBirth,Func<bool> alive)
    {
        try{
            if(expected==0 || !alive())return false;
            var observed=readBirth();
            return alive() && observed is ulong actual && actual==expected;
        }catch{return false;}
    }
    // Only facts from calls/predicates already evaluated by gameAuthority. No
    // identifiers, extra PnP reads, persistent logging, or retry are added.
    private sealed class GameAuthorityFacts
    {
        internal const int MaxRecordBytes=2048,MaxParentSteps=16,MaxInterfaceAttempts=2;
        internal const string Prefix="YMCC_MSI_GAME_AUTHORITY_DIAGNOSTIC ";
        private bool emitted;
        public int Schema=>1;
        public string Kind=>"ymcc.gameAuthority.failure";
        public int Result=>8;
        public string Stage { get; private set; }="";
        public bool Xusb { get; private set; }
        public bool? InputsValid { get; set; }
        public uint? LocateResult { get; set; }
        public PropertyObservation? CompatibleSize { get; set; }
        public PropertyObservation? CompatibleData { get; set; }
        public int? CompatibleCount { get; set; }
        public List<ParentObservation> ParentChain { get; }=new(MaxParentSteps);
        public string? ChainEnd { get; set; }
        public bool? RootInChain { get; set; }
        public bool? NoVirtualParent { get; set; }
        public bool? GameUsage { get; set; }
        public bool? ActiveXusb { get; set; }
        [System.Text.Json.Serialization.JsonIgnore(Condition=System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
        public string? MutationPolicy { get; private set; }
        internal void MutationRejected(bool xusb){Begin(xusb);MutationPolicy="rog-hid-game-node-refresh-unvalidated";}
        [System.Text.Json.Serialization.JsonIgnore(Condition=System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
        public string? HidCapabilityState { get; set; }
        [System.Text.Json.Serialization.JsonIgnore(Condition=System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
        public ushort? HidUsagePage { get; set; }
        [System.Text.Json.Serialization.JsonIgnore(Condition=System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
        public ushort? HidUsage { get; set; }
        public List<InterfaceObservation> InterfaceAttempts { get; }=new(MaxInterfaceAttempts);
        internal static GameAuthorityFacts? TryCreate(){try{return new();}catch{return null;}}
        internal void Begin(bool xusb)
        {
            Xusb=xusb;InputsValid=null;LocateResult=null;CompatibleSize=CompatibleData=null;CompatibleCount=null;
            ParentChain.Clear();ChainEnd=null;RootInChain=NoVirtualParent=GameUsage=ActiveXusb=null;InterfaceAttempts.Clear();
            HidCapabilityState=null;HidUsagePage=HidUsage=null;MutationPolicy=null;
        }
        internal void DeviceIdResult(uint result)
        {
            try{if(ParentChain.Count<MaxParentSteps)ParentChain.Add(new(result,null,null));}catch{}
        }
        internal void RootStep(bool root)
        {
            try{var i=ParentChain.Count-1;if(i>=0)ParentChain[i]=ParentChain[i] with {IsSelectedRoot=root};}catch{}
        }
        internal void ParentResult(uint result)
        {
            try{var i=ParentChain.Count-1;if(i>=0)ParentChain[i]=ParentChain[i] with {ParentResult=result};}catch{}
        }
        internal void InterfaceSizeResult(uint result,uint characters)
        {
            try{if(InterfaceAttempts.Count<MaxInterfaceAttempts)InterfaceAttempts.Add(new(result,characters,null));}catch{}
        }
        internal void InterfaceListResult(uint result)
        {
            try{var i=InterfaceAttempts.Count-1;if(i>=0)InterfaceAttempts[i]=InterfaceAttempts[i] with {ListResult=result};}catch{}
        }
        internal void WriteFailure(string stage)
        {
            try{
                if(emitted)return;emitted=true;Stage=stage;
                var record=Prefix+System.Text.Json.JsonSerializer.Serialize(this);
                if(Encoding.UTF8.GetByteCount(record)+2>MaxRecordBytes)return;
                Console.WriteLine(record);
            }catch{} // Diagnostic allocation/serialization/stdout must never turn 8 into 6.
        }
    }
    // VC-LIFECYCLE-20261020-R3: evidence for this execution attempt only.
    // A successful Disable call and a failed Enable call must stay distinct;
    // these facts never prove PnP state, authorize retry, or settle return debt.
    internal sealed class GameRefreshFacts
    {
        private bool emitted;
        public int Schema=>1;
        public string Kind=>"ymcc.gameRefresh.failure";
        public int Result=>6;
        public string Stage { get; set; }="admission";
        public bool Xusb { get; init; }
        public uint? PrepareLocateResult { get; set; }
        public uint? CurrentLocateResult { get; set; }
        public bool? SameNode { get; set; }
        public bool DisableCallEntered { get; set; }
        public uint? DisableResult { get; set; }
        // Elapsed facts use Stopwatch's monotonic clock, in whole milliseconds.
        // Null means the measured call was not entered or timing was unavailable.
        public string TimingClock=>"monotonic";
        public long? DisableCallElapsedMs { get; set; }
        public long? DisableFailureObservationElapsedMs { get; private set; }
        internal static long? MonotonicTimestamp(){try{return Stopwatch.GetTimestamp();}catch{return null;}}
        internal static long? MonotonicElapsedMs(long? started)
        {
            try{return started is long value?Stopwatch.GetElapsedTime(value).Ticks/TimeSpan.TicksPerMillisecond:null;}catch{return null;}
        }
        [System.Text.Json.Serialization.JsonIgnore(Condition=System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
        public DisableFailureObservation? DisableFailureStatus { get; private set; }
        public sealed class DisableFailureObservation
        {
            public string State { get; set; }="unknown";
            public uint? LocateResult { get; set; }
            public bool? SameNode { get; set; }
            public uint? StatusResult { get; set; }
            public uint? Status { get; set; }
            public uint? Problem { get; set; }
            public uint? PostLocateResult { get; set; }
            public bool? PostSameNode { get; set; }
            public long? LocateElapsedMs { get; set; }
            public long? StatusElapsedMs { get; set; }
            public long? PostLocateElapsedMs { get; set; }
        }
        // One read-only snapshot after a real nonzero Disable return. Match the
        // original instance/DEVINST before and after; raw error outputs remain
        // unknown. This cannot identify an actor or prove that no write occurred.
        internal void ObserveDisableFailure(string instance,uint node)
        {
            DisableFailureObservation? observation=null;
            var observationStarted=MonotonicTimestamp();
            try{
                if(DisableFailureStatus is not null)return;
                observation=DisableFailureStatus=new();
                uint current=0,located;var locateStarted=MonotonicTimestamp();
                try{located=Locate(ref current,instance,0);}
                finally{observation.LocateElapsedMs=MonotonicElapsedMs(locateStarted);}
                observation.LocateResult=located;observation.SameNode=located==0?current==node:null;
                if(located!=0){observation.State="unknown-locate-error";return;}
                if(current!=node){observation.State="unknown-node-mismatch";return;}
                uint result,status,problem;var statusStarted=MonotonicTimestamp();
                try{result=DevNodeStatus(out status,out problem,node,0);}
                finally{observation.StatusElapsedMs=MonotonicElapsedMs(statusStarted);}
                observation.StatusResult=result;observation.Status=status;observation.Problem=problem;
                current=0;uint postLocated;var postLocateStarted=MonotonicTimestamp();
                try{postLocated=Locate(ref current,instance,0);}
                finally{observation.PostLocateElapsedMs=MonotonicElapsedMs(postLocateStarted);}
                observation.PostLocateResult=postLocated;observation.PostSameNode=postLocated==0?current==node:null;
                observation.State=postLocated!=0?"unknown-post-locate-error":current!=node?"unknown-post-node-mismatch":
                    result!=0?"unknown-status-error":"observed";
            }catch{if(observation is not null)observation.State="unknown-diagnostic-throw";}
            finally{DisableFailureObservationElapsedMs=MonotonicElapsedMs(observationStarted);}
        }
        public bool EnableCallEntered { get; set; }
        public uint? EnableResult { get; set; }
        public bool CompensationChecked { get; set; }
        public bool? CompensationIdentityMatched { get; set; }
        public uint? CompensationLocateResult { get; set; }
        public bool? CompensationSameNode { get; set; }
        public bool CompensationAttempted { get; set; }
        public uint? CompensationEnableResult { get; set; }
        public string? CompensationExceptionType { get; private set; }
        public int? CompensationExceptionHResult { get; private set; }
        internal void CompensationFailed(Exception error)
        {
            try{CompensationExceptionType=error.GetType().FullName;CompensationExceptionHResult=error.HResult;}catch{}
        }
        public string? ExceptionType { get; private set; }
        public int? ExceptionHResult { get; private set; }
        internal static GameRefreshFacts? TryCreate(bool xusb){try{return new(){Xusb=xusb};}catch{return null;}}
        internal void WriteFailure(Exception? error)
        {
            try{
                if(emitted)return;emitted=true;
                ExceptionType=error?.GetType().FullName;ExceptionHResult=error?.HResult;
                var record=GameAuthorityFacts.Prefix+System.Text.Json.JsonSerializer.Serialize(this);
                if(Encoding.UTF8.GetByteCount(record)+2>GameAuthorityFacts.MaxRecordBytes)return;
                Console.WriteLine(record);
            }catch{} // Evidence failure must preserve the original result and device calls.
        }
    }
    private readonly record struct PropertyObservation(uint Result,uint Type,uint Bytes);
    private readonly record struct ParentObservation(uint DeviceIdResult,bool? IsSelectedRoot,uint? ParentResult);
    private readonly record struct InterfaceObservation(uint SizeResult,uint Characters,uint? ListResult);
    private enum AuthorityPredicate { RootInChain,NoVirtualParent,GameUsage,ActiveXusb }
    private static bool ObservePredicate(GameAuthorityFacts? facts,AuthorityPredicate predicate,bool value)
    {
        if(facts is not null){
            switch(predicate){
                case AuthorityPredicate.RootInChain:facts.RootInChain=value;break;
                case AuthorityPredicate.NoVirtualParent:facts.NoVirtualParent=value;break;
                case AuthorityPredicate.GameUsage:facts.GameUsage=value;break;
                case AuthorityPredicate.ActiveXusb:facts.ActiveXusb=value;break;
            }
        }
        return value;
    }
    // GPD native refresh is launched with an opaque inherited share=0 owner-file
    // handle in the exact HANDLE_LIST. Do not close unknown inherited handles or
    // spawn device writers here: that exclusion lasts through ALL CM calls and
    // return compensation, then the OS releases it during process teardown.
    // Parent/Job termination alone is not a physical-return or input-delivery proof.
    internal static int Run(string[] args)
    {
        if(!TryArguments(args,out var root,out var location,out var parent))return 2;
        if(args.Length==15 && args[7]==GameXusbCycle){
            if(!TryCycleLifetimeArguments(args,out var expectedBirth,out var deadline,out var cancelValue))return 2;
            // Own only the explicitly inherited event. Its scope closes it even
            // when the parent cannot be opened; never close unrelated handles.
            using var cancellation=new SafeWaitHandle(unchecked((nint)cancelValue),ownsHandle:true);
            using var cycleLifetime=ParentProcessLifetime.TryOpen(parent);
            if(cycleLifetime is null)return 3;
            Func<bool> parentAlive=()=>cycleLifetime.IsAlive;
            Func<ulong> tick=()=>checked((ulong)Environment.TickCount64);
            Func<uint> cancelState=()=>WaitForCycleCancellation(cancellation,0);
            bool Allowed()=>CycleExecutionAllowed(deadline,parentAlive,tick,cancelState);
            // Keep the original absolute native R3 deadline. Check its initial
            // budget once; cancellation/deadline remain gates at every boundary.
            if(!CycleExecutionAllowed(deadline,parentAlive,tick,cancelState,initial:true) ||
                !ExpectedParentMatches(expectedBirth,()=>{
                    using var parentProcess=Process.GetProcessById(parent);
                    var birth=parentProcess.StartTime.ToUniversalTime().ToFileTimeUtc();
                    return birth>0?(ulong)birth:null;
                },Allowed))return 3;
            var cycleFacts=GameAuthorityFacts.TryCreate();
            return ExecuteGameXusbCycle(root,args[8],location,Allowed,()=>CurrentBoardAllowed(root),CurrentLocations,
                ()=>CurrentGameHid(args[8],root,true,cycleFacts),id=>{
                    uint node=0;var result=Locate(ref node,id,0);return new CycleNodeLocation(result,node);
                },node=>{
                    var result=DevNodeStatus(out var status,out var problem,node,0);return new CycleNodeStatus(result,status,problem);
                },PrepareDeviceCycle,cycleFacts);
        }
        using var lifetime=ParentProcessLifetime.TryOpen(parent);
        if(lifetime is null)return 3;
        if(args.Length==9){
            var target=args[8];var facts=GameAuthorityFacts.TryCreate();
            var xusb=args[7]=="--game-xusb";
            // Last-line protection when an older native caller still requests
            // the unsafe ROGX HID stop/start. Recognition remains supported.
            if(!GameNodeRefreshMutationAllowed(root,target)){
                facts?.MutationRejected(xusb);facts?.WriteFailure("first");return 8;
            }
            var refreshFacts=GameRefreshFacts.TryCreate(xusb);
            // Only the product with current failure evidence gains a new
            // device-write cleanup attempt. Other OEM routes keep their calls.
            Func<bool>? returnLocationCurrent=root.StartsWith(@"USB\VID_0B05&PID_1B4C",StringComparison.OrdinalIgnoreCase)
                ?()=>CurrentLocations(root).Contains(location,StringComparer.OrdinalIgnoreCase):null;
            var binding=!xusb?new HidAuthorityBinding():null;
            bool Authority()=>CurrentGameHid(target,root,xusb,facts,binding);
            Func<bool>? finalAuthority=binding is null?null:()=>lifetime.IsAlive && Authority() && lifetime.IsAlive;
            return ExecuteVerified(root,location,()=>lifetime.IsAlive,()=>CurrentBoardAllowed(root),CurrentLocations,
                _=>PrepareGameRefresh(target,finalAuthority,refreshFacts,returnLocationCurrent),Authority,facts,refreshFacts);
        }
        return ExecuteVerified(root,location,()=>lifetime.IsAlive,()=>CurrentBoardAllowed(root),CurrentLocations,PrepareDeviceCycle);
    }
    internal static bool GameNodeRefreshMutationAllowed(string root,string instance) =>
        !(root.StartsWith(@"USB\VID_0B05&PID_1B4C",StringComparison.OrdinalIgnoreCase) &&
          instance.StartsWith(@"HID",StringComparison.OrdinalIgnoreCase));
    private static bool HasModel(string value,string[] models)
    {
        static bool Character(char character)=>char.IsAsciiLetterOrDigit(character) || character is '-' or '_';
        foreach(var model in models){
            for(var offset=value.IndexOf(model,StringComparison.OrdinalIgnoreCase);offset>=0;offset=value.IndexOf(model,offset+1,StringComparison.OrdinalIgnoreCase)){
                var end=offset+model.Length;
                if((offset==0 || !Character(value[offset-1])) && (end==value.Length || !Character(value[end])))return true;
            }
        }
        return false;
    }
    internal static bool KnownMachineRoot(string root,string manufacturer,string board)
    {
        if(!ValidRoot(root))return false;
        if(root.StartsWith(@"USB\VID_0DB0&PID_1901\",StringComparison.OrdinalIgnoreCase))return IsKnownMsi(manufacturer,board);
        if(root.StartsWith(@"USB\VID_0B05&",StringComparison.OrdinalIgnoreCase))return manufacturer.Contains("ASUSTEK",StringComparison.OrdinalIgnoreCase) && HasModel(board,["RC71L","RC72LA","RC73XA","RC73YA"]);
        return manufacturer.Contains("GPD",StringComparison.OrdinalIgnoreCase) && HasModel(board,["WIN2","G1618-03","G1618-04","G1618-05","G1617-01","G1617-02","G1619-03","G1619-04","G1619-05"]);
    }
    private static bool CurrentBoardAllowed(string root)
    {
        using var bios=Registry.LocalMachine.OpenSubKey(@"HARDWARE\DESCRIPTION\System\BIOS",false);
        return KnownMachineRoot(root,bios?.GetValue("BaseBoardManufacturer") as string ?? "",bios?.GetValue("BaseBoardProduct") as string ?? "");
    }
    // Actual production execution path. Selftests inject all I/O delegates;
    // no SDK construction/registry/PnP/port operation is performed by them.
    private static int ExecuteVerified(string root,string location,Func<bool> alive,Func<bool> knownBoard,
        Func<string,string[]> locations,Func<string,Action?> prepare,Func<bool>? gameAuthority=null,GameAuthorityFacts? gameFacts=null,GameRefreshFacts? refreshFacts=null)
    {
        if(!ValidRoot(root))return 2;
        try
        {
            if(!alive())return 3;
            if(!knownBoard())return 4;
            if(!locations(root).Contains(location,StringComparer.OrdinalIgnoreCase))return 5;
            if(gameAuthority is not null && !gameAuthority()){gameFacts?.WriteFailure("first");return 8;}
            if(!alive())return 3;
            if(refreshFacts is not null)refreshFacts.Stage="prepare";
            var cycle=prepare(root); // locating/converting the PnP object can be slow
            if(cycle is null)return 7;
            if(refreshFacts is not null)refreshFacts.Stage="revalidate";
            // Revalidate after preparation, directly before the ONE mutation.
            // A same PID is insufficient; alive captures the original handle.
            if(!alive())return 3;
            if(!locations(root).Contains(location,StringComparer.OrdinalIgnoreCase))return 5;
            if(gameAuthority is not null && !gameAuthority()){gameFacts?.WriteFailure("after-prepare");return 8;}
            if(!alive())return 3;
            cycle(); // existing CyclePort; no retry, target, mode write or service
            return 0;
        }
        catch(HidAuthorityChangedException){gameFacts?.WriteFailure("after-prepare");return 8;}
        catch(Exception error){refreshFacts?.WriteFailure(error);return 6;}
    }
    private readonly record struct CycleNodeLocation(uint Result,uint Node);
    private readonly record struct CycleNodeStatus(uint Result,uint Status,uint Problem);
    // Isolated investigation: HC's default USB mechanism uses the SDK's USB hub
    // port IOCTL, not CM Disable/Enable. Void completion is not re-enumeration,
    // input-delivery or single-controller proof. No node-enable debt is created.
    private static int ExecuteGameXusbCycle(string root,string target,string location,Func<bool> alive,Func<bool> knownBoard,
        Func<string,string[]> locations,Func<bool> authority,Func<string,CycleNodeLocation> locate,
        Func<uint,CycleNodeStatus> status,Func<string,Action?> prepare,GameAuthorityFacts? facts=null)
    {
        if(!ExactGpdXusbCycleRoot(root,target))return 2;
        uint? selectedNode=null;
        bool CurrentAuthority(){
            if(selectedNode is uint expected){
                var current=locate(root);
                if(current.Result!=0 || current.Node!=expected)return false;
                // The SDK factory/node lookup may be slow. After them, validate
                // the original handle lifetime, location and active authority.
                if(!alive() || !locations(root).Contains(location,StringComparer.OrdinalIgnoreCase))return false;
                return authority() && alive();
            }
            return authority();
        }
        return ExecuteVerified(root,location,alive,knownBoard,locations,id=>{
            var initial=locate(id);
            if(initial.Result!=0)return null;
            var observed=status(initial.Node);
            if(observed.Result!=0 || (observed.Status&DnStarted)==0 || (observed.Status&DnHasProblem)!=0 || observed.Problem!=0)return null;
            selectedNode=initial.Node;
            return prepare(id); // Production passes the unchanged PrepareDeviceCycle(root).
        },CurrentAuthority,facts);
    }
    // Keep SDK activation in a separate method. Tests never call this factory.
    private static Action? PrepareDeviceCycle(string root)
    {
        var device=PnPDevice.GetDeviceByInstanceId(root,DeviceLocationFlags.Normal);
        if(device is null)return null;
        var usb=device.ToUsbPnPDevice();
        return ()=>usb.CyclePort(); // void API success is NOT external-input proof
    }
    internal static bool ValidGameInstance(string instance)
    {
        if(instance.Length<23)return false;
        var prefix=instance[..21];
        if(!prefix.StartsWith(@"HID\",StringComparison.OrdinalIgnoreCase) || !ValidRoot("USB"+prefix[3..]+@"\X"))return false;
        if(instance.Length<=prefix.Length || instance.Length>=512 || !instance.StartsWith(prefix,StringComparison.OrdinalIgnoreCase) ||
            (instance[prefix.Length]!='&' && instance[prefix.Length]!='\\') ||
            instance.Any(character=>!char.IsAsciiLetterOrDigit(character) && character is not ('\\' or '&' or '_' or '-' or '{' or '}')))return false;
        var separator=instance.IndexOf('\\',4);
        return separator>=prefix.Length && separator+1<instance.Length && instance.IndexOf('\\',separator+1)<0 &&
            !instance.Contains("VIGEM",StringComparison.OrdinalIgnoreCase) && !instance.Contains("HIDMAESTRO",StringComparison.OrdinalIgnoreCase);
    }
    internal static bool GameHidAdmitted(string instance,string root,IReadOnlyList<string> parents,IReadOnlyList<string> compatible) =>
        GameHidAdmitted(instance,root,parents,compatible,null);
    private static bool GameHidAdmitted(string instance,string root,IReadOnlyList<string> parents,IReadOnlyList<string> compatible,GameAuthorityFacts? facts,Func<bool>? currentUsage=null)
    {
        return ValidGameInstance(instance) && ValidRoot(root) && instance.AsSpan(4,17).Equals(root.AsSpan(4,17),StringComparison.OrdinalIgnoreCase) &&
            ObservePredicate(facts,AuthorityPredicate.RootInChain,parents.Any(parent=>parent.Equals(root,StringComparison.OrdinalIgnoreCase))) &&
            ObservePredicate(facts,AuthorityPredicate.NoVirtualParent,!parents.Any(parent=>parent.Contains("VIGEM",StringComparison.OrdinalIgnoreCase) || parent.Contains("HIDMAESTRO",StringComparison.OrdinalIgnoreCase))) &&
            ObservePredicate(facts,AuthorityPredicate.GameUsage,currentUsage is not null?currentUsage():compatible.Any(value=>value.Equals("HID_DEVICE_UP:0001_U:0004",StringComparison.OrdinalIgnoreCase) ||
                value.Equals("HID_DEVICE_UP:0001_U:0005",StringComparison.OrdinalIgnoreCase)));
    }
    internal static bool ValidXusbInstance(string instance,string root)
    {
        var busMatches=instance.StartsWith(@"USB\",StringComparison.OrdinalIgnoreCase) ||
            (root.StartsWith(@"USB\VID_0B05&",StringComparison.OrdinalIgnoreCase) && instance.StartsWith(@"HID\",StringComparison.OrdinalIgnoreCase));
        if(!ValidRoot(root) || instance.Length<23 || instance.Length>=512 ||
            !busMatches || !instance.AsSpan(4,17).Equals(root.AsSpan(4,17),StringComparison.OrdinalIgnoreCase) ||
            (instance[21]!='&' && instance[21]!='\\') ||
            instance.Any(character=>!char.IsAsciiLetterOrDigit(character) && character is not ('\\' or '&' or '_' or '-' or '{' or '}')) ||
            instance.Contains("VIGEM",StringComparison.OrdinalIgnoreCase) || instance.Contains("HIDMAESTRO",StringComparison.OrdinalIgnoreCase))return false;
        var separator=instance.IndexOf('\\',4);
        return separator>=21 && separator+1<instance.Length && instance.IndexOf('\\',separator+1)<0;
    }
    internal static bool GameXusbAdmitted(string instance,string root,IReadOnlyList<string> parents,bool active) =>
        GameXusbAdmitted(instance,root,parents,active,null);
    private static bool GameXusbAdmitted(string instance,string root,IReadOnlyList<string> parents,bool active,GameAuthorityFacts? facts)
    {
        return ObservePredicate(facts,AuthorityPredicate.ActiveXusb,active) && ValidXusbInstance(instance,root) &&
            ObservePredicate(facts,AuthorityPredicate.RootInChain,parents.Any(parent=>parent.Equals(root,StringComparison.OrdinalIgnoreCase))) &&
            ObservePredicate(facts,AuthorityPredicate.NoVirtualParent,!parents.Any(parent=>parent.Contains("HIDMAESTRO",StringComparison.OrdinalIgnoreCase) || parent.Contains("VIGEM",StringComparison.OrdinalIgnoreCase)));
    }
    private static bool ActiveXusbInterface(string instance,GameAuthorityFacts? facts=null)
    {
        var type=new Guid("EC87F1E3-C13B-4100-B5F7-8B84D54260CB");
        for(var attempt=0;attempt<2;attempt++){
            var sizeResult=InterfaceSize(out var characters,ref type,instance,0);facts?.InterfaceSizeResult(sizeResult,characters);
            if(sizeResult!=0 || characters is < 2 or > 65536)return false;
            var buffer=new char[characters];
            var result=Interfaces(ref type,instance,buffer,characters,0);facts?.InterfaceListResult(result);
            if(result==0)return new string(buffer).Split('\0',StringSplitOptions.RemoveEmptyEntries).Length>0;
            if(result!=0x1A)return false;
        }
        return false;
    }
    // A helper-local pin, never persisted or inferred from an IG/MI name.
    private sealed class HidAuthorityBinding { internal uint? Node; internal string? Path; }
    private sealed class HidAuthorityChangedException:Exception { }
    private static bool CurrentGameHid(string instance,string root,bool xusb=false,GameAuthorityFacts? facts=null,HidAuthorityBinding? binding=null)
    {
        facts?.Begin(xusb);
        var invalid=!ValidRoot(root) || (xusb?!ValidXusbInstance(instance,root):!ValidGameInstance(instance));
        if(facts is not null)facts.InputsValid=!invalid;
        if(invalid)return false;
        uint node=0;var locateResult=Locate(ref node,instance,0);if(facts is not null)facts.LocateResult=locateResult;
        if(locateResult!=0)return false;
        string[] compatible=[];
        if(!xusb){
            var key=new PropertyKey{Format=LocationFormat,Id=4};uint type=0,bytes=0;
            var sizeResult=Property(node,ref key,out type,null,ref bytes,0);
            if(facts is not null)facts.CompatibleSize=new(sizeResult,type,bytes);
            if(sizeResult==0x1A && bytes is >= 2 and <= 65536){
                var data=new byte[bytes];
                var dataResult=Property(node,ref key,out type,data,ref bytes,0);
                if(facts is not null)facts.CompatibleData=new(dataResult,type,bytes);
                if(dataResult==0 && type==0x2012 && bytes<=data.Length && bytes%2==0)
                    compatible=Encoding.Unicode.GetString(data,0,(int)bytes).Split('\0',StringSplitOptions.RemoveEmptyEntries);
            }
            if(facts is not null)facts.CompatibleCount=compatible.Length;
        }
        var parents=new List<string>();
        if(facts is not null)facts.ChainEnd="depth-limit";
        var targetNode=node;
        for(var depth=0;depth<16;depth++){
            var identifier=new StringBuilder(512);
            var idResult=DeviceId(node,identifier,identifier.Capacity,0);facts?.DeviceIdResult(idResult);
            if(idResult!=0){if(facts is not null)facts.ChainEnd="device-id-error";return false;}
            parents.Add(identifier.ToString());
            var atRoot=identifier.ToString().Equals(root,StringComparison.OrdinalIgnoreCase);facts?.RootStep(atRoot);
            if(atRoot){if(facts is not null)facts.ChainEnd="selected-root";break;}
            var parentResult=Parent(out node,node,0);facts?.ParentResult(parentResult);
            if(parentResult!=0){if(facts is not null)facts.ChainEnd="parent-error";break;}
        }
        return xusb?GameXusbAdmitted(instance,root,parents,ActiveXusbInterface(instance,facts),facts):
            GameHidAdmitted(instance,root,parents,compatible,facts,()=>CurrentHidGameUsage(instance,root,targetNode,facts,binding));
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct HidAttributes { public uint Size; public ushort VendorId,ProductId,VersionNumber; }
    [StructLayout(LayoutKind.Sequential)]
    private struct HidCaps
    {
        public ushort Usage,UsagePage,InputReportByteLength,OutputReportByteLength,FeatureReportByteLength;
        [MarshalAs(UnmanagedType.ByValArray,SizeConst=17)] public ushort[] Reserved;
        public ushort NumberLinkCollectionNodes,NumberInputButtonCaps,NumberInputValueCaps,NumberInputDataIndices;
        public ushort NumberOutputButtonCaps,NumberOutputValueCaps,NumberOutputDataIndices;
        public ushort NumberFeatureButtonCaps,NumberFeatureValueCaps,NumberFeatureDataIndices;
    }
    private static bool HidInterfaceMatches(string path,string instance)
    {
        var key=new PropertyKey{Format=new Guid("78c34fc8-104a-4aca-9ea4-524d52996e57"),Id=256};
        uint bytes=1024;
        var buffer=new byte[bytes];
        if(InterfaceProperty(path,ref key,out var type,buffer,ref bytes,0)!=0 || type!=0x12 ||
            bytes<2 || bytes>buffer.Length || bytes%2!=0)return false;
        var value=Encoding.Unicode.GetString(buffer,0,(int)bytes);
        return value.EndsWith('\0') && value[..^1].Equals(instance,StringComparison.OrdinalIgnoreCase);
    }
    private static bool ActiveHidInterface(string instance,string path)
    {
        var type=new Guid("4d1e55b2-f16f-11cf-88cb-001111000030");
        if(InterfaceSize(out var characters,ref type,instance,0)!=0 || characters is < 2 or > 65536)return false;
        var buffer=new char[characters];
        return Interfaces(ref type,instance,buffer,characters,0)==0 && buffer[^1]=='\0' &&
            new string(buffer).Split('\0',StringSplitOptions.RemoveEmptyEntries).Contains(path,StringComparer.OrdinalIgnoreCase);
    }
    private static bool CurrentHidGameUsage(string instance,string root,uint targetNode,GameAuthorityFacts? facts,HidAuthorityBinding? binding=null)
    {
        if(!ValidRoot(root) || !ValidGameInstance(instance))return false;
        if(binding?.Node is uint pinned && pinned!=targetNode){
            if(facts is not null)facts.HidCapabilityState="interface-replaced";
            return false;
        }
        if(facts is not null)facts.HidCapabilityState="interface-list-unavailable";
        var hidClass=new Guid("4d1e55b2-f16f-11cf-88cb-001111000030");
        if(InterfaceSize(out var characters,ref hidClass,instance,0)!=0 || characters is < 2 or > 65536)return false;
        var buffer=new char[characters];
        if(Interfaces(ref hidClass,instance,buffer,characters,0)!=0 || buffer[^1]!='\0')return false;
        var paths=new string(buffer).Split('\0',StringSplitOptions.RemoveEmptyEntries);
        if(paths.Length is < 1 or > 16)return false;
        foreach(var path in paths){
            if(binding?.Path is string selected && !selected.Equals(path,StringComparison.OrdinalIgnoreCase)){
                if(facts is not null)facts.HidCapabilityState="interface-replaced";
                continue;
            }
            if(facts is not null)facts.HidCapabilityState="interface-binding-unverified";
            if(!HidInterfaceMatches(path,instance))continue;
            using var handle=OpenHid(path,0,3,IntPtr.Zero,3,0x80,IntPtr.Zero);
            if(handle.IsInvalid){if(facts is not null)facts.HidCapabilityState="interface-open-failed";continue;}
            var attributes=new HidAttributes{Size=(uint)Marshal.SizeOf<HidAttributes>()};
            IntPtr preparsed=IntPtr.Zero;
            try{
                if(facts is not null)facts.HidCapabilityState="hid-capabilities-unavailable";
                if(!HidAttributesRead(handle,ref attributes) || !HidPreparsedRead(handle,out preparsed) ||
                    preparsed==IntPtr.Zero || HidCapabilitiesRead(preparsed,out var caps)!=0x00110000)continue;
                if(facts is not null){facts.HidUsagePage=caps.UsagePage;facts.HidUsage=caps.Usage;facts.HidCapabilityState="non-game-or-wrong-device";}
                var vid=attributes.VendorId.ToString("X4");var pid=attributes.ProductId.ToString("X4");
                if(!root.StartsWith("USB\\VID_"+vid+"&PID_"+pid+"\\",StringComparison.OrdinalIgnoreCase) ||
                    caps.UsagePage!=1 || caps.Usage is not (4 or 5) || caps.InputReportByteLength==0)continue;
                uint current=0;
                if(Locate(ref current,instance,0)!=0 || current!=targetNode || !HidInterfaceMatches(path,instance) || !ActiveHidInterface(instance,path)){
                    if(facts is not null)facts.HidCapabilityState="interface-retired";
                    continue;
                }
                if(binding is not null){binding.Node=targetNode;binding.Path=path;}
                if(facts is not null)facts.HidCapabilityState="current-game-collection";
                return true;
            }
            finally{if(preparsed!=IntPtr.Zero)HidPreparsedFree(preparsed);}
        }
        return false;
    }
    private static Action? PrepareGameRefresh(string instance,Func<bool>? currentAuthority=null,GameRefreshFacts? facts=null,Func<bool>? returnLocationCurrent=null)
    {
        if(facts is not null)facts.Stage="prepare";
        uint node=0;var located=Locate(ref node,instance,0);
        if(facts is not null)facts.PrepareLocateResult=located;
        if(located!=0)return null;
        return ()=>{
            if(!RefreshGameNode(()=>{
                if(facts is not null)facts.Stage="current-locate";
                uint current=0;var result=Locate(ref current,instance,0);
                if(facts is not null){facts.CurrentLocateResult=result;facts.SameNode=result==0?current==node:null;}
                if(result!=0 || current!=node)return false;
                if(facts is not null)facts.Stage="current-authority";
                if(currentAuthority is not null && !currentAuthority())throw new HidAuthorityChangedException();
                return true;
            },flags=>{
                uint result;var disableStarted=GameRefreshFacts.MonotonicTimestamp();
                try{result=Disable(node,flags);}
                finally{if(facts is not null)facts.DisableCallElapsedMs=GameRefreshFacts.MonotonicElapsedMs(disableStarted);}
                if(result!=0 && facts is not null)facts.ObserveDisableFailure(instance,node);
                return result;
            },()=>Enable(node,0),facts,returnLocationCurrent is null?null:()=>{
                // Recovery returns THIS acknowledged disable; an inactive XUSB
                // interface is expected here and cannot be an admission gate.
                uint current=0;var result=Locate(ref current,instance,0);
                if(facts is not null){facts.CompensationLocateResult=result;facts.CompensationSameNode=result==0?current==node:null;}
                return result==0 && current==node && returnLocationCurrent is not null && returnLocationCurrent();
            }))throw new InvalidOperationException("Game interface refresh failed");
        };
    }
    internal static bool RefreshGameNode(Func<bool> current,Func<uint,uint> disable,Func<uint> enable,GameRefreshFacts? facts=null,Func<bool>? compensationCurrent=null)
    {
        if(!current())return false;
        if(facts is not null){facts.Stage="disable";facts.DisableCallEntered=true;}
        var disabled=disable(0x00000004);
        if(facts is not null)facts.DisableResult=disabled;
        if(disabled!=0)return false;
        var enabled=false;
        try{
            if(facts is not null){facts.Stage="enable";facts.EnableCallEntered=true;}
            var result=enable();
            if(facts is not null)facts.EnableResult=result;
            enabled=result==0;
            return enabled;
        }
        finally{
            // R3 execution cleanup, not a second player operation or target
            // retry. A confirmed disable creates a return obligation even if
            // the parent leaves. Exactly one immediate Enable compensation on
            // the SAME revalidated node/location; never disable again. Keep
            // the original false/exception, including when compensation is 0.
            if(!enabled && compensationCurrent is not null){
                try{
                    if(facts is not null)facts.CompensationChecked=true;
                    var currentReturn=compensationCurrent();
                    if(facts is not null)facts.CompensationIdentityMatched=currentReturn;
                    if(currentReturn){
                        if(facts is not null)facts.CompensationAttempted=true;
                        var result=enable();
                        if(facts is not null)facts.CompensationEnableResult=result;
                    }
                }catch(Exception error){facts?.CompensationFailed(error);}
            }
        }
    }
    private static string[] CurrentLocations(string root)
    {
        uint node=0;if(Locate(ref node,root,0)!=0)return [];
        var key=LocationKey;uint type=0,bytes=0;
        if(Property(node,ref key,out type,null,ref bytes,0)!=0x1A || bytes is < 4 or > 65536)return [];
        var data=new byte[bytes];
        if(Property(node,ref key,out type,data,ref bytes,0)!=0 || type!=0x2012)return [];
        return Encoding.Unicode.GetString(data,0,(int)bytes).Split('\0',StringSplitOptions.RemoveEmptyEntries);
    }
    internal static bool SelfTest()
    {
        int count=0,fail=0;void Check(bool value){count++;if(!value)fail++;}
        var root=@"USB\VID_0DB0&PID_1901\00006F64096B22E7";
        Check(ValidRoot(root));Check(!ValidRoot(@"USB\VID_0DB0&PID_1902\X"));Check(!ValidRoot(@"HID\VID_0DB0&PID_1901\X"));
        Check(!ValidRoot(@"USB\VID_0DB0&PID_1901&MI_00\X"));Check(!ValidRoot(root+"\" --bad"));Check(!ValidRoot(root+"\n"));
        foreach(var board in new[]{"MS-1T41","MS-1T42","MS-1T52","MS-1T8K","MS-1T91"})Check(IsKnownMsi("MICRO-STAR INTERNATIONAL CO., LTD.",board));
        Check(IsKnownMsi("Micro-Star International Co., Ltd.","ms-1t91"));
        Check(IsKnownMsi(" MICRO-STAR INTERNATIONAL CO., LTD. "," MS-1T91 "));
        Check(!IsKnownMsi("MSI","MS-1T91"));
        Check(!IsKnownMsi("Micro-Star International Co., Ltd.","UNKNOWN"));
        Check(!IsKnownMsi("ASUSTeK COMPUTER INC.","MS-1T91"));Check(!IsKnownMsi("MICRO-STAR INTERNATIONAL CO., LTD.","UNKNOWN"));
        var pid=(Environment.ProcessId+100).ToString();
        var args=new[]{Command,root,"--location","PCIROOT(0)#USBROOT(0)#USB(4)","--parent-pid",pid,"--once"};
        Check(TryArguments(args,out var parsed,out _,out _) && parsed==root);
        Check(!TryArguments(args.Take(6).ToArray(),out _,out _,out _));
        Check(!TryArguments(new[]{Command,root,"--location","PCIROOT(0)","--parent-pid",pid,"--once"},out _,out _,out _));
        Check(!TryArguments(new[]{Command,root,"--location",args[3],"--parent-pid",Environment.ProcessId.ToString(),"--once"},out _,out _,out _));
        Check(ValidRoot(@"USB\VID_0DB0&PID_1901\X"));
        Check(ValidRoot(root.ToLowerInvariant()));
        Check(!ValidRoot(root+@"\EXTRA"));
        Check(!ValidRoot(@"USB\VID_0DB0&PID_1901&IG_02\X"));
        int prepared=0,cycled=0,locationReads=0,parentReads=0;
        bool parentAlive=true,boardAllowed=true,moved=false,dieInPrepare=false,dieAfterLocation=false;
        bool noDevice=false,throwPrepare=false,throwCycle=false;
        string position=args[3];
        void Reset(){prepared=cycled=locationReads=parentReads=0;parentAlive=boardAllowed=true;
            moved=dieInPrepare=dieAfterLocation=noDevice=throwPrepare=throwCycle=false;}
        bool Alive(){parentReads++;return parentAlive;}
        string[] Locations(string id){locationReads++;if(dieAfterLocation)parentAlive=false;
            return moved?[]:[position.ToLowerInvariant()];}
        Action? Prepare(string id){prepared++;if(throwPrepare)throw new InvalidOperationException("fixture");
            if(dieInPrepare)parentAlive=false;if(noDevice)return null;
            return ()=>{cycled++;if(throwCycle)throw new InvalidOperationException("fixture");};}
        int Execute()=>ExecuteVerified(root,position,Alive,()=>boardAllowed,Locations,Prepare);
        Reset();Check(Execute()==0 && prepared==1 && cycled==1 && locationReads==2);
        Reset();parentAlive=false;Check(Execute()==3 && prepared==0 && cycled==0 && locationReads==0);
        Reset();boardAllowed=false;Check(Execute()==4 && prepared==0 && cycled==0 && locationReads==0);
        Reset();moved=true;Check(Execute()==5 && prepared==0 && cycled==0);
        Reset();dieAfterLocation=true;Check(Execute()==3 && prepared==0 && cycled==0);
        Reset();dieInPrepare=true;Check(Execute()==3 && prepared==1 && cycled==0 && locationReads==1);
        Reset();noDevice=true;Check(Execute()==7 && cycled==0);
        Reset();throwPrepare=true;Check(Execute()==6 && prepared==1 && cycled==0);
        Reset();throwCycle=true;Check(Execute()==6 && prepared==1 && cycled==1);
        Reset();Check(ExecuteVerified(root,position,Alive,()=>true,Locations,id=>{prepared++;moved=true;return ()=>cycled++;})==5 && prepared==1 && cycled==0 && locationReads==2);
        Reset();Check(ExecuteVerified(root,position,Alive,()=>true,id=>{locationReads++;if(locationReads==2)parentAlive=false;return [position];},Prepare)==3 && cycled==0 && prepared==1);
        Reset();Check(ExecuteVerified(@"USB\VID_0DB0&PID_1902\FOREIGN",position,Alive,()=>true,Locations,Prepare)==2 && prepared==0 && parentReads==0);
        var game=@"HID\VID_0DB0&PID_1901&IG_03\8&3121E4E&0&0000";
        Check(ValidGameInstance(game));Check(ValidGameInstance(game.ToLowerInvariant()));
        Check(!ValidGameInstance(@"HID\VID_0DB0&PID_1902&MI_00\GAME"));
        Check(!ValidGameInstance(game+@"\EXTRA"));Check(!ValidGameInstance(game+"\" --bad"));
        Check(TryArguments(args.Concat(new[]{"--game-hid",game}).ToArray(),out _,out _,out _));
        Check(!TryArguments(args.Concat(new[]{"--game-hid",root}).ToArray(),out _,out _,out _));
        Check(!TryArguments(args.Concat(new[]{"--other",game}).ToArray(),out _,out _,out _));
        Check(GameHidAdmitted(game,root,new[]{root},new[]{"HID_DEVICE_UP:0001_U:0005"}));
        Check(GameHidAdmitted(game,root,new[]{root},new[]{"hid_device_up:0001_u:0004"}));
        foreach(var capability in new[]{"HID_DEVICE_UP:0001_U:0006","HID_DEVICE_UP:0001_U:0002","HID_DEVICE_UP:000C_U:0001","HID_DEVICE_UP:0020_U:0001","HID_DEVICE_UP:FF00_U:0001"})
            Check(!GameHidAdmitted(game,root,new[]{root},new[]{capability}));
        Check(!GameHidAdmitted(game,root,new[]{@"USB\VID_0DB0&PID_1901\OTHER"},new[]{"HID_DEVICE_UP:0001_U:0005"}));
        Check(!GameHidAdmitted(game,root,new[]{@"ROOT\HIDMAESTRO\VIRTUAL",root},new[]{"HID_DEVICE_UP:0001_U:0005"}));
        Check(!GameHidAdmitted(game,root,new[]{root},Array.Empty<string>()));
        Reset();Check(ExecuteVerified(root,position,Alive,()=>true,Locations,Prepare,()=>false)==8 && prepared==0 && cycled==0);
        Reset();var proofReads=0;
        Check(ExecuteVerified(root,position,Alive,()=>true,Locations,Prepare,()=>++proofReads==1)==8 && prepared==1 && cycled==0);
        Reset();proofReads=0;
        Check(ExecuteVerified(root,position,Alive,()=>true,Locations,Prepare,()=>{proofReads++;return true;})==0 && prepared==1 && cycled==1 && proofReads==2);
        foreach(var family in new[]{"rog","gpd"}){
            var otherRoot=family=="rog"?@"USB\VID_0B05&PID_1B4C\6&A61A827&0&4":@"USB\VID_045E&PID_028E\7&2EA17D0F&0&1";
            var otherHid=family=="rog"?@"HID\VID_0B05&PID_1B4C&MI_05&IG_00\8&GAME&0&0000":@"HID\VID_045E&PID_028E&IG_02\9&GAME&0&0000";
            var otherXusb=family=="rog"?@"USB\VID_0B05&PID_1B4C&MI_05\6&GAME&0&0005":otherRoot;
            var manufacturer=family=="rog"?"ASUSTEK COMPUTER INC.":"GPD";
            var model=family=="rog"?"RC73XA":"G1618-05";
            Check(ValidRoot(otherRoot));Check(KnownMachineRoot(otherRoot,manufacturer,model));
            Check(!KnownMachineRoot(otherRoot,manufacturer,"UNKNOWN"));Check(!KnownMachineRoot(otherRoot,"OTHER",model));
            Check(!KnownMachineRoot(root,manufacturer,model));
            Check(ValidGameInstance(otherHid));Check(ValidXusbInstance(otherXusb,otherRoot));
            Check(GameHidAdmitted(otherHid,otherRoot,new[]{otherRoot},new[]{"HID_DEVICE_UP:0001_U:0005"}));
            Check(!GameHidAdmitted(game,otherRoot,new[]{otherRoot},new[]{"HID_DEVICE_UP:0001_U:0005"}));
            Check(!GameHidAdmitted(otherHid,otherRoot,new[]{otherRoot},new[]{"HID_DEVICE_UP:0001_U:0006"}));
            Check(GameXusbAdmitted(otherXusb,otherRoot,new[]{otherRoot},true));
            Check(!GameXusbAdmitted(otherXusb,otherRoot,new[]{otherRoot},false));
            Check(!GameXusbAdmitted(otherXusb,otherRoot,new[]{otherRoot+"-OTHER"},true));
            Check(!GameXusbAdmitted(otherXusb,otherRoot,new[]{@"ROOT\HIDMAESTRO\VIRTUAL",otherRoot},true));
            var otherArgs=new[]{Command,otherRoot,"--location",position,"--parent-pid",pid,"--once"};
            Check(!TryArguments(otherArgs,out _,out _,out _));
            Check(TryArguments(otherArgs.Concat(new[]{"--game-hid",otherHid}).ToArray(),out _,out _,out _));
            Check(TryArguments(otherArgs.Concat(new[]{"--game-xusb",otherXusb}).ToArray(),out _,out _,out _));
            Check(!TryArguments(otherArgs.Concat(new[]{"--game-hid",game}).ToArray(),out _,out _,out _));
        }
        Check(!KnownMachineRoot(@"USB\VID_0B05&PID_1B4C\X","ASUSTEK COMPUTER INC.","RC73XA-UNKNOWN"));
        const string fieldRoot=@"USB\VID_0B05&PID_1B4C\6&A61A827&0&4";
        const string fieldXusb=@"HID\VID_0B05&PID_1B4C&MI_05&IG_00\8&2BE06C40&0&0000";
        Check(ValidXusbInstance(fieldXusb,fieldRoot));
        Check(GameXusbAdmitted(fieldXusb,fieldRoot,new[]{fieldRoot},true));
        Check(!GameXusbAdmitted(fieldXusb,fieldRoot,new[]{fieldRoot},false));
        Check(!GameXusbAdmitted(fieldXusb,fieldRoot,new[]{fieldRoot+"-OTHER"},true));
        Check(!GameXusbAdmitted(fieldXusb,fieldRoot,new[]{@"ROOT\HIDMAESTRO\VIRTUAL",fieldRoot},true));
        Check(!ValidXusbInstance(fieldXusb,root));
        Check(!ValidXusbInstance(fieldXusb+@"\EXTRA",fieldRoot));
        Check(!ValidXusbInstance(@"HID\VID_045E&PID_028E&IG_00\GPD",@"USB\VID_045E&PID_028E\GPD"));
        Check(!ValidXusbInstance(@"USB\VID_0B05&PID_1B4C&MI_05\X\EXTRA",@"USB\VID_0B05&PID_1B4C\X"));
        Check(!ValidXusbInstance(@"USB\VID_045E&PID_028E\HIDMAESTRO",@"USB\VID_045E&PID_028E\X"));
        var disableCalls=0;var enableCalls=0;uint disabledFlags=0;
        void ResetNode(){disableCalls=enableCalls=0;disabledFlags=0;}
        uint DisableNode(uint flags){disableCalls++;disabledFlags=flags;return 0;}
        uint EnableNode(){enableCalls++;return 0;}
        ResetNode();Check(RefreshGameNode(()=>true,DisableNode,EnableNode) && disableCalls==1 && enableCalls==1 && disabledFlags==4);
        ResetNode();Check(!RefreshGameNode(()=>false,DisableNode,EnableNode) && disableCalls==0 && enableCalls==0);
        ResetNode();Check(!RefreshGameNode(()=>true,_=>{disableCalls++;return 1;},EnableNode) && disableCalls==1 && enableCalls==0);
        ResetNode();Check(!RefreshGameNode(()=>true,DisableNode,()=>{enableCalls++;return 1;}) && disableCalls==1 && enableCalls==1);
        var originalCases=count;
        const string cycleRoot=@"USB\VID_045E&PID_028E\7&2EA17D0F&0&1";
        const ulong cycleBirth=134058384000000000;
        var cycleBirthText=cycleBirth.ToString(System.Globalization.CultureInfo.InvariantCulture);
        var cycleArgs=new[]{Command,cycleRoot,"--location",position,"--parent-pid",pid,"--once",GameXusbCycle,cycleRoot,"--parent-birth",cycleBirthText,"--deadline-tick","31000","--cancel-handle","123"};
        Check(TryArguments(cycleArgs,out var cycleParsed,out _,out _) && cycleParsed==cycleRoot);
        Check(TryArguments(cycleArgs.Select((value,index)=>index is 1 or 8?value.ToLowerInvariant():value).ToArray(),out _,out _,out _));
        foreach(var pair in new[]{(cycleRoot,cycleRoot+"-OTHER"),(cycleRoot,@"HID\VID_045E&PID_028E&IG_00\GAME"),
            (cycleRoot,@"USB\VID_045E&PID_028E&MI_00\CHILD"),(root,root),(fieldRoot,fieldRoot),
            (@"USB\VID_FFFF&PID_FFFF\OTHER",@"USB\VID_FFFF&PID_FFFF\OTHER")}){
            Check(!TryArguments(new[]{Command,pair.Item1,"--location",position,"--parent-pid",pid,"--once",GameXusbCycle,pair.Item2,"--parent-birth",cycleBirthText,"--deadline-tick","31000","--cancel-handle","123"},out _,out _,out _));
        }
        Check(!TryArguments(cycleArgs.Concat(new[]{"--extra"}).ToArray(),out _,out _,out _));
        Check(!TryArguments(cycleArgs.Select((value,index)=>index==7?"--GAME-XUSB-CYCLE":value).ToArray(),out _,out _,out _));
        Check(!TryArguments(cycleArgs.Select((value,index)=>index==5?Environment.ProcessId.ToString():value).ToArray(),out _,out _,out _));
        Check(!TryArguments(cycleArgs.Select((value,index)=>index==3?"PCIROOT(0)":value).ToArray(),out _,out _,out _));
        // One parameterized consumer run: all external boundaries are delegates;
        // no Run, SDK factory, CM call or registry branch is invoked by SelfTest.
        foreach(var fault in new[]{"none","initial-locate","status-error","not-started","has-problem","disabled","other-problem","status-throw",
            "second-authority","second-authority-throw","parent-in-factory","location-in-factory","node-in-factory","locate-after-factory",
            "parent-in-node-lookup","location-in-node-lookup","factory-null","factory-throw","cycle-throw"}){
            var trace=new List<string>();var cycleCount=0;var factoryCount=0;var locateCount=0;var statusCount=0;var authorityCount=0;
            var parentCurrent=true;var locationCurrent=true;var exactArguments=true;
            bool CycleAlive(){trace.Add("alive");return parentCurrent;}
            bool CycleBoard(){trace.Add("board");return true;}
            string[] CycleLocations(string id){trace.Add("location");exactArguments&=id==cycleRoot;return locationCurrent?[position]:[];}
            bool CycleAuthority(){trace.Add("authority");authorityCount++;
                if(authorityCount==2 && fault=="second-authority-throw")throw new InvalidOperationException("changed authority");
                return authorityCount!=2 || fault!="second-authority";}
            CycleNodeLocation CycleLocate(string id){trace.Add("locate");locateCount++;exactArguments&=id==cycleRoot;
                if(locateCount==1 && fault=="initial-locate")return new(1,0);
                if(locateCount==2 && fault=="parent-in-node-lookup")parentCurrent=false;
                if(locateCount==2 && fault=="location-in-node-lookup")locationCurrent=false;
                return locateCount==2 && fault=="locate-after-factory"?new(1,0):new(0,locateCount==2 && fault=="node-in-factory"?72u:71u);}
            CycleNodeStatus CycleStatus(uint node){trace.Add("status");statusCount++;exactArguments&=node==71;
                return fault switch {
                    "status-error"=>new(1,DnStarted,0),"not-started"=>new(0,0,0),"has-problem"=>new(0,DnStarted|DnHasProblem,0),
                    "disabled"=>new(0,DnStarted,22),"other-problem"=>new(0,DnStarted,1),"status-throw"=>throw new InvalidOperationException("unknown status"),
                    _=>new(0,DnStarted,0)
                };}
            Action? CyclePrepare(string id){trace.Add("factory");factoryCount++;exactArguments&=id==cycleRoot;
                if(fault=="parent-in-factory")parentCurrent=false;if(fault=="location-in-factory")locationCurrent=false;
                if(fault=="factory-null")return null;if(fault=="factory-throw")throw new InvalidOperationException("factory");
                return ()=>{trace.Add("cycle");cycleCount++;if(fault=="cycle-throw")throw new InvalidOperationException("cycle");};}
            var result=ExecuteGameXusbCycle(cycleRoot,cycleRoot,position,CycleAlive,CycleBoard,CycleLocations,CycleAuthority,CycleLocate,CycleStatus,CyclePrepare);
            var expected=fault switch {
                "none"=>0,"parent-in-factory"=>3,"location-in-factory"=>5,
                "second-authority" or "node-in-factory" or "locate-after-factory" or "parent-in-node-lookup" or "location-in-node-lookup"=>8,
                "status-throw" or "second-authority-throw" or "factory-throw" or "cycle-throw"=>6,_=>7
            };
            var initialRefusal=fault is "initial-locate" or "status-error" or "not-started" or "has-problem" or "disabled" or "other-problem" or "status-throw";
            Check(result==expected && exactArguments && cycleCount==(fault is "none" or "cycle-throw"?1:0) &&
                factoryCount==(initialRefusal?0:1) && statusCount==(fault=="initial-locate"?0:1));
            if(fault=="none")Check(trace.SequenceEqual(new[]{"alive","board","location","authority","alive","locate","status","factory",
                "alive","location","locate","alive","location","authority","alive","alive","cycle"}) && locateCount==2 && authorityCount==2);
        }
        var unexpectedCalls=0;
        Check(ExecuteGameXusbCycle(cycleRoot,cycleRoot+"-OTHER",position,()=>{unexpectedCalls++;return true;},()=>{unexpectedCalls++;return true;},
            _=>{unexpectedCalls++;return [position];},()=>{unexpectedCalls++;return true;},_=>{unexpectedCalls++;return new(0,71);},
            _=>{unexpectedCalls++;return new(0,DnStarted,0);},_=>{unexpectedCalls++;return ()=>unexpectedCalls++;})==2 && unexpectedCalls==0);
        // Parent review: all new lifetime inputs are fake functions. SelfTest
        // never queries a real PID, event, clock, device or SDK factory.
        Check(new[]{9,11,13}.All(length=>!TryArguments(cycleArgs.Take(length).ToArray(),out _,out _,out _)));
        var badIntegers=new[]{"","0","01","+1","-1"," 1","1 ","1.0","1e2","１２３","18446744073709551616","1\0","0x1","1\n"};
        Check(new[]{10,12,14}.All(index=>badIntegers.All(invalid=>
            !TryArguments(cycleArgs.Select((value,offset)=>offset==index?invalid:value).ToArray(),out _,out _,out _))));
        var invalidHandle=((ulong)nuint.MaxValue).ToString(System.Globalization.CultureInfo.InvariantCulture);
        var oversizedHandle=((System.Numerics.BigInteger)(ulong)nuint.MaxValue+1).ToString(System.Globalization.CultureInfo.InvariantCulture);
        Check(new[]{invalidHandle,oversizedHandle}.All(invalid=>
            !TryArguments(cycleArgs.Select((value,index)=>index==14?invalid:value).ToArray(),out _,out _,out _)));
        Check(new[]{9,11,13}.All(index=>
            !TryArguments(cycleArgs.Select((value,offset)=>offset==index?"--wrong":value).ToArray(),out _,out _,out _)));
        Check(new[]{"--game-hid","--game-xusb"}.All(variant=>
            !TryArguments(cycleArgs.Select((value,index)=>index==7?variant:value).ToArray(),out _,out _,out _)));
        foreach(var fault in new[]{"match","mismatch","null","read-throw","dead-before","dead-after","alive-throw"}){
            var birthReads=0;var aliveReads=0;var birthParentAlive=true;var trace=new List<string>();
            ulong? ReadBirth(){birthReads++;trace.Add("birth");if(fault=="read-throw")throw new InvalidOperationException("unknown birth");
                if(fault=="dead-after")birthParentAlive=false;return fault=="null"?null:fault=="mismatch"?cycleBirth+1:cycleBirth;}
            bool ParentAlive(){aliveReads++;trace.Add("alive");if(fault=="alive-throw")throw new InvalidOperationException("unknown lifetime");return birthParentAlive && fault!="dead-before";}
            var matched=ExpectedParentMatches(cycleBirth,ReadBirth,ParentAlive);
            var expectedBirthReads=fault is "dead-before" or "alive-throw"?0:1;
            var expectedAliveReads=fault is "dead-before" or "alive-throw" or "read-throw"?1:2;
            Check(matched==(fault=="match") && birthReads==expectedBirthReads && aliveReads==expectedAliveReads &&
                (fault!="match" || trace.SequenceEqual(new[]{"alive","birth","alive"})));
        }
        foreach(var fault in new[]{"match","expired","too-long","cancelled","cancel-failed","cancel-abandoned","parent-dead","parent-throw",
            "birth-mismatch","factory-cancelled","factory-expired","tick-throw","cancel-throw"}){
            ulong clock=1000;var deadline=fault=="too-long"?31001uL:31000uL;
            if(fault=="expired")clock=deadline;
            uint wait=fault switch {"cancelled"=>0,"cancel-failed"=>uint.MaxValue,"cancel-abandoned"=>0x80,_=>CycleWaitTimeout};
            var factoryCalls=0;var cycleCalls=0;var birthReads=0;
            bool GuardParent(){if(fault=="parent-throw")throw new InvalidOperationException("parent");return fault!="parent-dead";}
            ulong GuardTick(){if(fault=="tick-throw")throw new InvalidOperationException("tick");return clock;}
            uint GuardCancel(){if(fault=="cancel-throw")throw new InvalidOperationException("cancel");return wait;}
            bool Allowed()=>CycleExecutionAllowed(deadline,GuardParent,GuardTick,GuardCancel);
            var admitted=CycleExecutionAllowed(deadline,GuardParent,GuardTick,GuardCancel,initial:true) &&
                ExpectedParentMatches(cycleBirth,()=>{birthReads++;return fault=="birth-mismatch"?cycleBirth+1:cycleBirth;},Allowed);
            Action? GuardPrepare(string id){factoryCalls++;if(fault=="factory-cancelled")wait=0;if(fault=="factory-expired")clock=deadline;
                return ()=>cycleCalls++;}
            var result=admitted?ExecuteGameXusbCycle(cycleRoot,cycleRoot,position,Allowed,()=>true,_=>[position],()=>true,
                _=>new(0,71),_=>new(0,DnStarted,0),GuardPrepare):3;
            var reachesFactory=fault is "match" or "factory-cancelled" or "factory-expired";
            Check(result==(fault=="match"?0:3) && cycleCalls==(fault=="match"?1:0) && factoryCalls==(reachesFactory?1:0) &&
                birthReads==(reachesFactory || fault=="birth-mismatch"?1:0));
        }
        Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new{cases=count,originalCases,candidateCases=count-originalCases,failures=fail,realHardwareAccess=false,noTargetCreation=true}));
        return fail==0;
    }
    [DllImport("kernel32.dll",EntryPoint="WaitForSingleObject",SetLastError=true)] private static extern uint WaitForCycleCancellation(SafeWaitHandle handle,uint milliseconds);
    [DllImport("cfgmgr32.dll",EntryPoint="CM_Locate_DevNodeW",CharSet=CharSet.Unicode)] private static extern uint Locate(ref uint node,string instance,uint flags);
    [DllImport("cfgmgr32.dll",EntryPoint="CM_Get_DevNode_PropertyW")] private static extern uint Property(uint node,ref PropertyKey key,out uint type,byte[]? buffer,ref uint bytes,uint flags);
    [DllImport("cfgmgr32.dll",EntryPoint="CM_Get_Parent")] private static extern uint Parent(out uint parent,uint node,uint flags);
    [DllImport("cfgmgr32.dll",EntryPoint="CM_Get_Device_IDW",CharSet=CharSet.Unicode)] private static extern uint DeviceId(uint node,StringBuilder identifier,int length,uint flags);
    [DllImport("cfgmgr32.dll",EntryPoint="CM_Get_DevNode_Status")] private static extern uint DevNodeStatus(out uint status,out uint problem,uint node,uint flags);
    [DllImport("cfgmgr32.dll",EntryPoint="CM_Disable_DevNode")] private static extern uint Disable(uint node,uint flags);
    [DllImport("cfgmgr32.dll",EntryPoint="CM_Enable_DevNode")] private static extern uint Enable(uint node,uint flags);
    [DllImport("cfgmgr32.dll",EntryPoint="CM_Get_Device_Interface_List_SizeW",CharSet=CharSet.Unicode)] private static extern uint InterfaceSize(out uint characters,ref Guid type,string instance,uint flags);
    [DllImport("cfgmgr32.dll",EntryPoint="CM_Get_Device_Interface_ListW",CharSet=CharSet.Unicode)] private static extern uint Interfaces(ref Guid type,string instance,[Out] char[] buffer,uint characters,uint flags);
    [DllImport("cfgmgr32.dll",EntryPoint="CM_Get_Device_Interface_PropertyW",CharSet=CharSet.Unicode)] private static extern uint InterfaceProperty(string path,ref PropertyKey key,out uint type,[Out] byte[] buffer,ref uint bytes,uint flags);
    [DllImport("kernel32.dll",EntryPoint="CreateFileW",CharSet=CharSet.Unicode,SetLastError=true)] private static extern SafeFileHandle OpenHid(string path,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template);
    [DllImport("hid.dll",EntryPoint="HidD_GetAttributes")] [return:MarshalAs(UnmanagedType.U1)] private static extern bool HidAttributesRead(SafeFileHandle handle,ref HidAttributes attributes);
    [DllImport("hid.dll",EntryPoint="HidD_GetPreparsedData")] [return:MarshalAs(UnmanagedType.U1)] private static extern bool HidPreparsedRead(SafeFileHandle handle,out IntPtr preparsed);
    [DllImport("hid.dll",EntryPoint="HidP_GetCaps")] private static extern int HidCapabilitiesRead(IntPtr preparsed,out HidCaps caps);
    [DllImport("hid.dll",EntryPoint="HidD_FreePreparsedData")] [return:MarshalAs(UnmanagedType.U1)] private static extern bool HidPreparsedFree(IntPtr preparsed);
}
