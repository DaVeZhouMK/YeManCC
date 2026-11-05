// Source-bound fan declarations. No device construction, transport open or runtime admission on lookup.
using System;
using System.Collections.Generic;
namespace YeManFanBackend.Capabilities;
public enum PinnedFanTransportKind { WmiBankedEc, SuperIo16, WmiUmaEc16 }
public sealed class PinnedFanDescriptor
{
    internal PinnedFanDescriptor(string modelClass,string deviceId,string file,string sha,PinnedFanTransportKind transport,
        ushort control,ushort duty,byte on,byte maximum,ushort statusPort,ushort dataPort,ushort? temperature,string[] manufacturers,string[] products,
        string sourceCommit="f2e58468d9917189d4be7ccfc242385ce2c3035b",
        string sourceRepository="Valkirie/HandheldCompanion",bool manufacturerContains=false)
    { ModelClass=modelClass; DeviceId=deviceId; SourceFile=file; SourceSha256=sha; Transport=transport;
      ControlRegister=control; DutyRegister=duty; ControlOn=on; Maximum=maximum; StatusPort=statusPort; DataPort=dataPort;
      SourceCommit=sourceCommit; SourceRepository=sourceRepository; ManufacturerContains=manufacturerContains;
      TemperatureRegister=temperature; Manufacturers=Array.AsReadOnly(manufacturers); Products=Array.AsReadOnly(products); }
    public string ModelClass {get;} public string DeviceId {get;} public string SourceFile {get;} public string SourceSha256 {get;}
    public string SourceCommit {get;} public string SourceRepository {get;} public bool ManufacturerContains {get;}
    public PinnedFanTransportKind Transport {get;} public ushort ControlRegister {get;} public ushort DutyRegister {get;}
    public byte ControlOn {get;} public byte ControlOff => 0; public byte Minimum => 0; public byte Maximum {get;}
    public ushort StatusPort {get;} public ushort DataPort {get;} public ushort? TemperatureRegister {get;}
    public IReadOnlyList<string> Manufacturers {get;} public IReadOnlyList<string> Products {get;}
    // Existing YMCC Host policy; the nine HC source classes inherit this value.
    // This is not a Super X temperature-register or silicon-limit declaration.
    public uint ThermalTripTemperatureC => 95;
    public bool PhysicalAvailabilityProven => false;
}
public static partial class FanCapabilityCatalog
{
    private static readonly PinnedFanDescriptor[] pinnedSourceRows = {
        new("HandheldCompanion.Devices.AYANEO3","ymcc.device.AYANEO3","HandheldCompanion/Devices/AYANEO/AYANEO3.cs","bee53cd4d730013900da5c542c751b018f006ee09b886c55926f0ac7c17ee88b",PinnedFanTransportKind.SuperIo16,0x44a,0x44b,0x1,100,0x4e,0x4f,null,new[]{"AYADEVICE","AYANEO","AYA"},new[]{"AYANEO 3","AB07"}),
        new("HandheldCompanion.Devices.AYANEOAIR2","ymcc.device.AYANEOAIR2","HandheldCompanion/Devices/AYANEO/AYANEOAIR2.cs","0bde655963a66375a30d17191a6f08a23d1ec39f7b0732c3aa28cf321a65a67c",PinnedFanTransportKind.SuperIo16,0x2f1,0x1809,0x1,184,0x4e,0x4f,null,new[]{"AYADEVICE","AYANEO","AYA"},new[]{"AIR 2","AIR2","AYANEO AIR 2","AB10"}),
        new("HandheldCompanion.Devices.AYANEOAIRPlusNxx","ymcc.device.AYANEOAIRPlusNxx","HandheldCompanion/Devices/AYANEO/AYANEOAIRPlusNxx.cs","5b9f3d00c3d70a7c38ed3c72418dfd47c8b37f9c7ac25a96cfdb5dedf5f6dd07",PinnedFanTransportKind.SuperIo16,0xd1c8,0x1802,0xa5,255,0x4e,0x4f,null,new[]{"AYADEVICE","AYANEO","AYA"},new[]{"AB05N","AB05NXX","AIR Plus Nxx"}),
        new("HandheldCompanion.Devices.AYANEOAIRPro1S","ymcc.device.AYANEOAIRPro1S","HandheldCompanion/Devices/AYANEO/AYANEOAIRPro1S.cs","43a7f1ef360a8ff9ce362974e530ea4b61361118cc93d9de6167c51c3574e652",PinnedFanTransportKind.SuperIo16,0x44a,0x44b,0x1,100,0x4e,0x4f,null,new[]{"AYADEVICE","AYANEO","AYA"},new[]{"AIR Pro 1S","AYANEO AIR Pro 1S"}),
        new("HandheldCompanion.Devices.AYANEOFlip11","ymcc.device.AYANEOFlip11","HandheldCompanion/Devices/AYANEO/AYANEOFlip11.cs","8ebe1ca0c53c740ccb82bc19beac83aef91636b32e67998fd28309787c104d36",PinnedFanTransportKind.SuperIo16,0x44a,0x44b,0x1,100,0x4e,0x4f,null,new[]{"AYADEVICE","AYANEO","AYA"},new[]{"FLIP 11","AYANEO FLIP 11"}),
        new("HandheldCompanion.Devices.AYANEONEXT2","ymcc.device.AYANEONEXT2","HandheldCompanion/Devices/AYANEO/AYANEONEXT2.cs","b47a90a52912eebd2568e920da7c1d0a264a286bb06657f9e274feb7f5da91c2",PinnedFanTransportKind.SuperIo16,0x44a,0x44b,0x1,100,0x4e,0x4f,null,new[]{"AYADEVICE","AYANEO","AYA"},new[]{"NEXT II","NEXT 2","AYANEO NEXT II","AYANEO NEXT 2","AB09"}),
        new("HandheldCompanion.Devices.OneXPlayer3","ymcc.device.OneXPlayer3","HandheldCompanion/Devices/OneXPlayer/OneXPlayer3.cs","e3a6275e34db52fa60c7b5a421655af24d8580611fe9ebb875986817891809dc",PinnedFanTransportKind.WmiBankedEc,0x44a,0x44b,0x1,184,0x4e,0x4f,0x470,new[]{"ONE-NETBOOK TECHNOLOGY CO., LTD.","ONE-NETBOOK"},new[]{"ONEXPLAYER 3","ONEXPLAYER-3"}),
        new("HandheldCompanion.Devices.OneXPlayerX2","ymcc.device.OneXPlayerX2","HandheldCompanion/Devices/OneXPlayer/OneXPlayerX2.cs","41cf648a9d9247c2e1a5da29bed748f0aed0a8407202a690b0b866ae2d5c369b",PinnedFanTransportKind.WmiBankedEc,0x44a,0x44b,0x1,184,0x4e,0x4f,0x470,new[]{"ONE-NETBOOK TECHNOLOGY CO., LTD.","ONE-NETBOOK"},new[]{"ONEXPLAYER X2"}),
        new("HandheldCompanion.Devices.OneXPlayerX2MiniPro","ymcc.device.OneXPlayerX2MiniPro","HandheldCompanion/Devices/OneXPlayer/OneXPlayerX2MiniPro.cs","7f06350daeb1d1c7157ae21f1dc231637394008256b3fc4c22c747109e2c7b67",PinnedFanTransportKind.SuperIo16,0x44a,0x44b,0x1,255,0x4e,0x4f,0x470,new[]{"ONE-NETBOOK TECHNOLOGY CO., LTD.","ONE-NETBOOK"},new[]{"ONEXPLAYER X2Mini PRO"}),
        new("YeManCC.SourceDevices.OneXPlayerSuperX","ymcc.device.OneXPlayerSuperX","capabilities/upstream/xmax-6d728795/SuperXFan.source-facts.json","2d1a55bafb6a1cd906758d92cb1a05877a38a70143ee9fdb7c6e2b89b091c027",PinnedFanTransportKind.WmiUmaEc16,0x44a,0x44b,0x1,255,0,0,null,new[]{"ONE-NETBOOK"},new[]{"ONEXPLAYER SUPER X"},"6d728795511c2f9f683d1757e72481236de9b23a","the-reengineers/XmaX",true),
    };
    public static IReadOnlyList<PinnedFanDescriptor> PinnedSourceDeclarations {get;} = Array.AsReadOnly(pinnedSourceRows);
    public static PinnedFanDescriptor? FindPinnedFanSource(string? modelClass)
    { foreach(var row in pinnedSourceRows) if(string.Equals(row.ModelClass,modelClass,StringComparison.Ordinal))return row; return null; }
    public static PinnedFanDescriptor? MatchPinnedFanSource(string? manufacturer,string? product)
    {
        if(manufacturer is null||product is null)return null;
        var normalizedManufacturer=manufacturer.ToUpperInvariant();
        foreach(var row in pinnedSourceRows){bool matched=false;foreach(var value in row.Manufacturers)if(row.ManufacturerContains?manufacturer.Contains(value,StringComparison.Ordinal):value==normalizedManufacturer){matched=true;break;}
            if(!matched)continue;foreach(var value in row.Products)if(value==product)return row;}
        return null;
    }
    private static PinnedSourceFanSession? pinnedSourceOwner;
    public static PinnedSourceFanSession AcquirePinnedFanSource(PinnedFanDescriptor descriptor,string contextToken,
        Func<bool> ordinaryPermit,Func<IPinnedFanTransport> openTransport)
    {
        ArgumentNullException.ThrowIfNull(descriptor);ArgumentNullException.ThrowIfNull(ordinaryPermit);ArgumentNullException.ThrowIfNull(openTransport);
        if(!ReferenceEquals(FindPinnedFanSource(descriptor.ModelClass),descriptor))throw new InvalidOperationException("PINNED_FAN_SOURCE_NOT_REGISTERED");
        if(contextToken is null||contextToken.Length!=64)throw new ArgumentException("PINNED_FAN_CONTEXT_TOKEN_REQUIRED",nameof(contextToken));
        foreach(var c in contextToken)if(!Uri.IsHexDigit(c))throw new ArgumentException("PINNED_FAN_CONTEXT_TOKEN_REQUIRED",nameof(contextToken));
        lock(Gate){if(owner is not null||pinnedSourceOwner is not null||legacyDispatchInProgress)throw new InvalidOperationException("FAN_CAPABILITY_BUSY");
            return pinnedSourceOwner=new PinnedSourceFanSession(descriptor,++ownershipGeneration,contextToken,ordinaryPermit,openTransport);}
    }
    internal static bool OwnsPinnedSource(PinnedSourceFanSession value)
    {lock(Gate)return ReferenceEquals(pinnedSourceOwner,value)&&value.Generation==ownershipGeneration;}
    internal static void ReleasePinnedSource(PinnedSourceFanSession value)
    {lock(Gate){if(!ReferenceEquals(pinnedSourceOwner,value)||value.Generation!=ownershipGeneration)throw new InvalidOperationException("PINNED_FAN_OWNER_STALE");pinnedSourceOwner=null;}}
}
