using YeManBatteryHost;
internal sealed class MsiBag : IMsiWmiObject
{
    internal IMsiWmiObject? Child;
    internal bool ThrowData,Disposed;
    internal object? ReturnValue;
    public IMsiWmiObject? Data {get{if(ThrowData)throw new InvalidOperationException("missing Data property");return Child;}set{Child=value;}}
    public byte[]? Bytes {get;set;}
    public void CheckReturn()=>MsiPacket.CheckReturn(ReturnValue);
    public void Dispose()=>Disposed=true;
}
internal sealed class MsiFixture(string mode="direct") : IMsiWmi
{
    internal int Templates,Reads,Writes,ParameterCalls;
    internal byte Raw=0xd0;
    internal bool BrokenTemplate,FailedRead,ThrowAfterWrite,IgnoreWrite;
    internal readonly List<MsiBag> Bags=[];
    MsiBag? expected;
    MsiBag Bag(IMsiWmiObject? child=null,byte[]? bytes=null){var b=new MsiBag{Child=child,Bytes=bytes};Bags.Add(b);return b;}
    public IMsiWmiObject? Parameters(string method)
    {
        ParameterCalls++;
        if(method=="Get_WMI")throw new Exception("Get_WMI must be called without parameter discovery");
        return mode switch
        {
            "throw"=>throw new System.Management.ManagementException("WMI signature unavailable"),
            "null"=>null,"missing-data"=>Bag(),"throw-data"=>ThrowData(),
            _=>expected=Bag(Bag())
        };
    }
    MsiBag ThrowData(){var b=Bag();b.ThrowData=true;return b;}
    public IMsiWmiObject? Invoke(string method,IMsiWmiObject? input)
    {
        if(method=="Get_WMI")
        {
            if(input is not null)throw new Exception("template call must have null input");
            Templates++;return expected=Bag(BrokenTemplate?null:Bag());
        }
        if(!ReferenceEquals(input,expected))throw new Exception("entire WMI template container was not preserved");
        var packet=input?.Data?.Bytes??throw new Exception("missing packet");
        if(packet.Length!=32||packet[0]!=215)throw new Exception("unexpected MSI charge packet");
        if(method=="Get_Data"){Reads++;return Bag(Bag(bytes:[FailedRead?(byte)0:(byte)1,Raw]));}
        if(method!="Set_Data")throw new Exception("unexpected method");
        Writes++;if(!IgnoreWrite)Raw=packet[1];
        if(ThrowAfterWrite)throw new IOException("write acknowledgement lost");
        return Bag();
    }
    public void Dispose(){}
}

internal sealed class MsiApFixture : IMsiWmi
{
    internal readonly byte[] Packet=Enumerable.Range(0,32).Select(i=>(byte)(i+11)).ToArray();
    internal readonly List<string> Methods=[];
    internal readonly List<byte[]> Sent=[];
    internal int Reads,Writes,ReplyLength=32;
    internal byte Flag=1,SetFlag=1;
    internal bool IgnoreWrite,LoseReply,FailReadAfterWrite;
    internal object? ReturnValue=true;
    MsiBag? expected;
    internal MsiApFixture(){Packet[0]=1;Packet[1]=0;Packet[2]=0;Packet[3]=0xc6;Packet[4]=0x80;Packet[5]=0xe4;}
    public IMsiWmiObject? Parameters(string method){if(method=="Get_WMI")throw new Exception("unexpected parameter discovery");return new MsiBag();}
    public IMsiWmiObject? Invoke(string method,IMsiWmiObject? input)
    {
        Methods.Add(method);
        if(method=="Get_WMI"){if(input is not null)throw new Exception("Get_WMI must have null input");return expected=new MsiBag{Child=new MsiBag()};}
        if(!ReferenceEquals(input,expected))throw new Exception("template container changed");
        var request=input!.Data!.Bytes!;
        if(request.Length!=32||request[0]!=0)throw new Exception("wrong AP selector/packet size");
        if(method=="Get_AP")
        {
            Reads++;if(request.Skip(1).Any(v=>v!=0))throw new Exception("AP read request must be zeroed");
            var reply=new byte[ReplyLength];Array.Copy(Packet,reply,Math.Min(32,ReplyLength));
            if(reply.Length>0)reply[0]=FailReadAfterWrite&&Writes>0?(byte)0:Flag;
            return new MsiBag{ReturnValue=ReturnValue,Child=new MsiBag{Bytes=reply}};
        }
        if(method!="Set_AP")throw new Exception("CG3EM must never call legacy Get_Data/Set_Data");
        for(int i=1;i<32;i++)if(i!=5&&request[i]!=Packet[i])throw new Exception("non-charge AP byte changed");
        Sent.Add((byte[])request.Clone());Writes++;
        if(!IgnoreWrite)Packet[5]=request[5];
        if(LoseReply)throw new IOException("Set_AP reply lost");
        var status=new byte[32];status[0]=SetFlag;
        return new MsiBag{ReturnValue=ReturnValue,Child=new MsiBag{Bytes=status}};
    }
    public void Dispose(){}
}
