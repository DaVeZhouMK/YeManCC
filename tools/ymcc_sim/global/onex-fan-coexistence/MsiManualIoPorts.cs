using System.Reflection;
public static class MsiManualIoPorts
{
    public const string Scope="root\\WMI",Path="MSI_ACPI.InstanceName='ACPI\\PNP0C14\\0_0'";
    public sealed record Io(string Stage,string Scope,string Path,string Method,int Index,int Length,int Minimum,byte[] Data,int Thread);
    public static readonly List<Io> Rows=new();
    public static bool Enabled;public static byte[][] Tables=Array.Empty<byte[]>();public static byte[][] Axes=Array.Empty<byte[]>();
    public static string FailedRead="",FailedWrite="",Outcome="TransportFailed";public static int FailedBlock=1,FailureAfter;public static bool IgnoreWrite;public static int TableReadLength=31;
    public static Action<Io>? OnIo;public static int TimerArms,ContractRejections;
    public static void Reset(){Enabled=true;Rows.Clear();FailedRead=FailedWrite="";Outcome="TransportFailed";FailedBlock=1;FailureAfter=0;IgnoreWrite=false;TableReadLength=31;TimerArms=ContractRejections=0;OnIo=null;Tables=new[]{Enumerable.Range(0,31).Select(i=>(byte)(i+10)).ToArray(),Enumerable.Range(0,31).Select(i=>(byte)(i+50)).ToArray()};Axes=new[]{new byte[]{0,91,92,50,60,70,80,88},new byte[]{0,81,82,40,50,60,70,80}};}
    static void Contract(bool value){if(!value){ContractRejections++;throw new InvalidOperationException("MSI_MANUAL_IO_CONTRACT");}}
    public static void Validate(string scope,string path,string method,int index,int length,int minimum)
    {
        Contract(scope==Scope&&path==Path);
        Contract(method switch {"Get_WMI"=>index==1&&length==32&&minimum==3,"Get_Data"=>index==152&&length==1&&minimum==1,"Get_AP"=>index==1&&length==3&&minimum==1||index==0&&length==6&&minimum==3,"Get_Fan" or "Get_Temperature"=>index is 1 or 2&&length==32&&minimum==8,_=>false});
    }
    static void Mark(string stage,string scope,string path,string method,int index,int length,int minimum,byte[] data){var row=new Io(stage,scope,path,method,index,length,minimum,data.ToArray(),Environment.CurrentManagedThreadId);Rows.Add(row);OriginalConstructorPorts.Mark("msi-manual."+stage+":"+method+":"+index);OnIo?.Invoke(row);}
    public static object Read(string scope,string path,string method,byte index,int length,int minimum)
    {
        Validate(scope,path,method,index,length,minimum);
        byte[] data=method switch {"Get_Fan"=>Tables[index-1].Take(TableReadLength).ToArray(),"Get_Temperature"=>Axes[index-1].ToArray(),"Get_WMI"=>[0,2,3],"Get_Data"=>[MsiLifecycleIoPorts.Full],"Get_AP" when index==1=>[MsiLifecycleIoPorts.Software,0,0],"Get_AP"=>[0,0,2,0,0,0],_=>throw new InvalidOperationException("UNDECLARED_MANUAL_READ")};
        Mark("read",scope,path,method,index,length,minimum,data);var fail=method==FailedRead&&index==FailedBlock&&FailureAfter--<=0;return MsiLifecycleIoPorts.Result(method,index,data,fail?Outcome:"Ok");
    }
    public static object Write(string scope,string path,string method,byte[] data)
    {
        Contract(scope==Scope&&path==Path&&data.Length==32);Contract(method=="Set_Fan"&&data[0]is 1 or 2||method=="Set_Data"&&data[0]is 152 or 212&&data.Skip(2).All(b=>b==0));
        Mark("write",scope,path,method,data[0],data.Length,0,data);if(method==FailedWrite&&data[0]==FailedBlock)return MsiLifecycleIoPorts.Result(method,data[0],[],Outcome);
        if(!IgnoreWrite){if(method=="Set_Fan")Tables[data[0]-1]=data.Skip(1).ToArray();else if(data[0]==152)MsiLifecycleIoPorts.Full=data[1];else MsiLifecycleIoPorts.Software=data[1];}return MsiLifecycleIoPorts.Result(method,data[0],[]);
    }
    public static int PowerReads;
    public static bool Online(){PowerReads++;OriginalConstructorPorts.Mark("msi-manual.power-read-inert");return true;}
    public static bool Arm(Timer timer,int due,int period){if(MsiReplayIoPorts.Enabled)return MsiReplayIoPorts.Arm(timer,due,period);Contract(Enabled&&due>=0&&period==Timeout.Infinite);TimerArms++;OriginalConstructorPorts.Mark("msi-manual.timer-arm-inert");return true;}
}
