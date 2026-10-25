namespace YeManBatteryHost;
// Reviewed CG3EM/MS-1T91 protocol; do not inherit it for older Claw boards.
internal sealed class MsiApProvider : IChargeProvider
{
    readonly IMsiWmi wmi;
    public MsiApProvider(IMsiWmi? wmi=null){this.wmi=wmi??new MsiWmi();}
    public bool CanReadLimit=>true;
    byte[] ReadPacket()
    {
        var packet=MsiPacket.Call(wmi,"Get_AP",new byte[32]);
        if(packet.Length!=32||packet[0]!=1)throw new IOException("MSI Get_AP 充电状态回执无效");
        int raw=packet[5],percent=raw&0x7f;
        if((raw&0x80)==0||(raw!=0x80&&(percent<20||percent>100)))throw new IOException($"MSI Get_AP 充电阈值未确认（0x{raw:X2}）");
        return packet;
    }
    // On BIOS .105 a successful Get_AP returns flag-only 0x80. This proves
    // neither a 0% nor a 100% limit. The reviewed setter can establish a value;
    // keep the exact raw byte for the fence and require independent readback.
    public Reading Read(){var packet=ReadPacket();return new(packet[5]==0x80?null:packet[5]&0x7f,packet[5]);}
    public void Write(int value,Reading before)
    {
        if(value<20||value>100||value%5!=0||before.Raw is null)throw new IOException("MSI 充电设置参数无效");
        // Read immediately before Set_AP: byte 3 also carries a live performance
        // mode. Copy the actual packet; never reconstruct/zero its other fields.
        var packet=ReadPacket();
        if(packet[5]!=before.Raw)throw new IOException("MSI 充电状态已变化，请重新读取");
        var payload=(byte[])packet.Clone();payload[0]=0;payload[5]=(byte)(0x80|value);
        var reply=MsiPacket.Call(wmi,"Set_AP",payload);
        if(reply.Length!=32||reply[0]!=1)throw new IOException("MSI Set_AP 设置结果未确认，请重新读取");
        // The setter returns a bare status. Controller independently reads back
        // the threshold before it can report success or clear the journal.
    }
    public void Dispose()=>wmi.Dispose();
}
