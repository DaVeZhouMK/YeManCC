using System.Buffers.Binary;
using System.Reflection;
using System.Text.Json;
using YeManInputHost.Diagnostics;

internal static class Probe
{
 static readonly List<object> Cases=new();static int Failed;
 static void Check(string name,bool ok){Cases.Add(new{name,ok});if(!ok)Failed++;}
 static byte[] Vector(){var b=new byte[64];b[0]=1;b[2]=9;b[3]=64;b[4]=0x78;b[5]=0x56;b[6]=0x34;b[7]=0x12;b[10]=0x1e;b[16]=0;b[17]=0x80;b[18]=0xff;b[19]=0x7f;b[20]=0xc7;b[21]=0xcf;b[22]=0x39;b[23]=0x30;b[56]=2;b[57]=1;b[58]=0xff;b[59]=0xff;return b;}
 static UsbipTransportObservation Observer(Action<string>? callback=null){var a=typeof(FakeSdk.Controller).Assembly;return new(new(a.ManifestModule.ModuleVersionId,a,typeof(FakeSdk.Controller).FullName!,typeof(FakeSdk.Handle).FullName!,typeof(FakeSdk.Device).FullName!,typeof(FakeSdk.Connection).FullName!),callback);}

 static long[] SignalAllocationWindows=Array.Empty<long>();
 static short? Motion(SteamDeckPadReportObservation r,int axis)=>axis switch
 {
  0=>r.AccelX,
  1=>r.AccelY,
  2=>r.AccelZ,
  3=>r.GyroPitch,
  4=>r.GyroYaw,
  5=>r.GyroRoll,
  6=>r.QuatW,
  7=>r.QuatX,
  8=>r.QuatY,
  _=>r.QuatZ
 };
 static void SignalCoverageChecks()
 {
  var b=Vector();short[] raw={-12345,23456,-32768,32767};
  for(int a=0;a<4;a++)BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(48+a*2),raw[a]);
  var saved=(byte[])b.Clone();var d=SteamDeckPadReportObservation.Decode(b);
  Check("cached-four-stick-raw-offsets-signed-little-endian",d.LeftStickX==-12345&&d.LeftStickY==23456&&d.RightStickX==-32768&&d.RightStickY==32767);
  Check("cached-sticks-independent-of-pad-coordinates",d.LeftX==-32768&&d.LeftY==32767&&d.RightX==-12345&&d.RightY==12345&&d.StickFieldsNonzero==true);
  Check("cached-reference-classes-are-diagnostic-with-explicit-threshold",d.ReferenceStickDeadzone==4096&&d.StickClassBits==(ushort)(1|(2<<3)|(5<<6)|(4<<9)));
  Check("cached-signal-decoder-preserves-entire-buffer",b.AsSpan().SequenceEqual(saved));
  // Full 64-bit word: no low-byte-only mask and no loss above bit 31.
  for(int bit=0;bit<64;bit++)
  {
   b=Vector();b[24]=1;ulong bg=((1UL<<19)|(1UL<<20))&~(1UL<<bit);
   BinaryPrimitives.WriteUInt64LittleEndian(b.AsSpan(8),bg);var before=SteamDeckPadReportObservation.Decode(b);
   BinaryPrimitives.WriteUInt64LittleEndian(b.AsSpan(8),bg|(1UL<<bit));var pulse=SteamDeckPadReportObservation.Decode(b);
   BinaryPrimitives.WriteUInt64LittleEndian(b.AsSpan(8),bg);var release=SteamDeckPadReportObservation.Decode(b);
   Check("cached-button-bit-"+bit+"-pulse-and-release-with-IMU",before.ButtonBits==bg&&pulse.ButtonBits==(bg|(1UL<<bit))&&release.ButtonBits==bg&&pulse.ImuFieldsNonzero==true&&
    pulse.LeftTriggerButton==(bit==1)&&pulse.RightTriggerButton==(bit==0)&&pulse.LeftStickClick==(bit==22)&&pulse.RightStickClick==(bit==26)&&pulse.SteamButton==(bit==13)&&pulse.QuickAccessButton==(bit==50));
  }
  b=Vector();BinaryPrimitives.WriteUInt64LittleEndian(b.AsSpan(8),(1UL<<0)|(1UL<<1)|(1UL<<13)|(1UL<<22)|(1UL<<26)|(1UL<<50));d=SteamDeckPadReportObservation.Decode(b);
  Check("cached-key-digital-trigger-stick-Steam-QAM-bits",d.LeftTriggerButton==true&&d.RightTriggerButton==true&&d.LeftStickClick==true&&d.RightStickClick==true&&d.SteamButton==true&&d.QuickAccessButton==true&&d.LeftTouch==false&&d.LeftTrigger==0&&d.RightTrigger==0);
  BinaryPrimitives.WriteUInt64LittleEndian(b.AsSpan(8),0);var off=SteamDeckPadReportObservation.Decode(b);
  Check("cached-key-digital-bits-release-independently-of-analog-triggers",off.LeftTriggerButton==false&&off.RightTriggerButton==false&&off.LeftStickClick==false&&off.RightStickClick==false&&off.SteamButton==false&&off.QuickAccessButton==false&&d.QuickAccessButton==true);
  short[] values={-32768,-32767,-32766,-4097,-4096,-1,0,1,4096,4097,32766,32767};byte[] classes={5,3,1,1,0,0,0,0,0,2,2,4};
  for(int axis=0;axis<4;axis++)
  {
   bool correct=true;b=Vector();
   for(int n=0;n<values.Length;n++)
   {
    BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(48+axis*2),values[n]);d=SteamDeckPadReportObservation.Decode(b);
    short? actual=axis switch{0=>d.LeftStickX,1=>d.LeftStickY,2=>d.RightStickX,_=>d.RightStickY};
    correct&=actual==values[n]&&d.StickClassBits==(ushort)(classes[n]<<(axis*3));
   }
   Check("cached-stick-axis-"+axis+"-both-deadzone-full-and-invalid-boundaries",correct);
  }
  // All twenty bytes count, including high bytes and quaternion offsets 36..43.
  for(int offset=24;offset<44;offset++)
  {
   b=Vector();b[offset]=0x80;d=SteamDeckPadReportObservation.Decode(b);
   Check("cached-IMU-byte-"+offset+"-covered",d.ImuFieldsNonzero==true&&d.ImuNonzeroMask==(ushort)(1<<((offset-24)/2)));
  }
  for(int axis=0;axis<10;axis++)
  {
   b=Vector();int other=axis==0?1:0;BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(24+other*2),123);
   var before=SteamDeckPadReportObservation.Decode(b);BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(24+axis*2),-32768);var pulse=SteamDeckPadReportObservation.Decode(b);
   BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(24+axis*2),0);var release=SteamDeckPadReportObservation.Decode(b);
   Check("cached-IMU-component-"+axis+"-release-under-another-nonzero",before.ImuNonzeroMask==(1<<other)&&pulse.ImuNonzeroMask==((1<<other)|(1<<axis))&&release.ImuNonzeroMask==(1<<other)&&release.ImuFieldsNonzero==true);
  }
  for(int axis=0;axis<10;axis++)
  {
   b=Vector();int bg=axis==0?1:0;BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(24+bg*2),123);
   short[] samples={short.MinValue,-12345,-1,0,1,12345,short.MaxValue};bool exact=true;SteamDeckPadReportObservation frozen=default;
   foreach(short value in samples)
   {
    BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(24+axis*2),value);var before=(byte[])b.Clone();d=SteamDeckPadReportObservation.Decode(b);
    if(value==short.MinValue)frozen=d;
    for(int a=0;a<10;a++)exact&=Motion(d,a)==(a==axis?value:a==bg?(short)123:(short)0);
    exact&=b.AsSpan().SequenceEqual(before);
   }
   Check("cached-raw-IMU-component-"+axis+"-signed-offset-and-extremes",exact&&Motion(d,axis)==short.MaxValue&&Motion(frozen,axis)==short.MinValue&&d.ImuNonzeroMask==frozen.ImuNonzeroMask);
  }
  b=Vector();short[] distinct={-32768,-23456,-12345,-1,1,12345,23456,32767,-24680,13579};
  string[] rawNames={"AccelX","AccelY","AccelZ","GyroPitch","GyroYaw","GyroRoll","QuatW","QuatX","QuatY","QuatZ"};
  for(int a=0;a<10;a++)BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(24+a*2),distinct[a]);d=SteamDeckPadReportObservation.Decode(b);
  using(var json=JsonDocument.Parse(JsonSerializer.Serialize(d)))
  {
   bool exact=true;for(int a=0;a<10;a++)exact&=Motion(d,a)==distinct[a]&&json.RootElement.GetProperty(rawNames[a]).GetInt16()==distinct[a];
   Check("cached-ten-distinct-raw-IMU-scalars-in-version1-JSON",exact&&d.ImuNonzeroMask==1023);
  }
  foreach(int size in new[]{0,63,65})
  {
   var unknown=SteamDeckPadReportObservation.Decode(new byte[size]);bool nullable=true;for(int a=0;a<10;a++)nullable&=Motion(unknown,a) is null;
   Check("cached-ten-raw-IMU-scalars-unknown-for-length-"+size,nullable);
  }
  for(int header=0;header<4;header++)
  {
   b=Vector();b[header]^=0x7f;var unknown=SteamDeckPadReportObservation.Decode(b);bool nullable=true;for(int a=0;a<10;a++)nullable&=Motion(unknown,a) is null;
   Check("cached-ten-raw-IMU-scalars-unknown-for-header-"+header,nullable);
  }
  b=Vector();b[44]=1;b[48]=1;b[60]=1;d=SteamDeckPadReportObservation.Decode(b);
  Check("cached-IMU-does-not-include-trigger-stick-or-reserved-tail",d.ImuNonzeroMask==0&&d.ImuFieldsNonzero==false);
  foreach(bool right in new[]{false,true})
  {
   b=Vector();b.AsSpan(56,4).Clear();bool correct=true;ushort[] pressures={0,1,32767,32768,65535,0};
   for(int n=0;n<pressures.Length;n++)
   {
    BinaryPrimitives.WriteUInt16LittleEndian(b.AsSpan(right?58:56),pressures[n]);d=SteamDeckPadReportObservation.Decode(b);
    correct&=(right?d.RightPressure:d.LeftPressure)==pressures[n]&&d.PressureNonzeroMask==(pressures[n]==0?0:1<<(right?1:0))&&d.PressureOutOfRangeMask==(pressures[n]>32767?1<<(right?1:0):0);
   }
   Check("cached-pressure-"+(right?"right":"left")+"-zero-admitted-max-and-ushort-extremes",correct);
  }
  b=Vector();BinaryPrimitives.WriteUInt64LittleEndian(b.AsSpan(8),0);d=SteamDeckPadReportObservation.Decode(b);
  Check("cached-inactive-coordinates-and-pressure-residue-all-six-fields",d.InactivePadDataMask==63);
  byte[] residue={62,60,56,48,32,0};int[] offsets={16,18,20,22,56,58};bool clears=true;
  for(int n=0;n<offsets.Length;n++){b.AsSpan(offsets[n],2).Clear();d=SteamDeckPadReportObservation.Decode(b);clears&=d.InactivePadDataMask==residue[n];}
  Check("cached-each-residue-clear-visible-despite-other-residue",clears);
  foreach(int length in new[]{0,63,65})
  {
   d=SteamDeckPadReportObservation.Decode(new byte[length]);Check("cached-added-fields-unknown-for-length-"+length,d.ButtonBits is null&&d.LeftStickX is null&&d.LeftStickY is null&&d.RightStickX is null&&d.RightStickY is null&&d.StickClassBits is null&&d.ImuNonzeroMask is null&&d.PressureNonzeroMask is null&&d.InactivePadDataMask is null&&d.LeftTriggerButton is null&&d.QuickAccessButton is null);
  }
  for(int header=0;header<4;header++){b=Vector();b[header]^=0x7f;d=SteamDeckPadReportObservation.Decode(b);Check("cached-added-fields-unknown-for-header-"+header,d.ButtonBits is null&&d.LeftStickX is null&&d.ImuNonzeroMask is null&&d.StickClassBits is null&&d.PressureOutOfRangeMask is null);}
  b=Vector();BinaryPrimitives.WriteUInt64LittleEndian(b.AsSpan(8),1UL<<50);BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(48),-12345);b[43]=0x80;
  var device=new FakeSdk.Device(b);var ctl=new FakeSdk.Controller(new(device));bool mutated=false;
  var observer=Observer(stage=>{if(stage=="owner-after"&&!mutated){mutated=true;Array.Clear(device.Report);}});var snapshot=observer.Capture(ctl,true);d=snapshot.LastSteamDeckPadReport!.Value;
  Check("cached-production-copy-carries-new-fields-before-source-mutation",mutated&&d.ButtonBits==(1UL<<50)&&d.QuickAccessButton==true&&d.LeftStickX==-12345&&d.ImuNonzeroMask==512&&d.QuatZ==short.MinValue&&d.AccelX==0&&device.Report[0]==0);
  using(var json=JsonDocument.Parse(JsonSerializer.Serialize(d)))Check("cached-added-signals-serialize-as-version1",json.RootElement.GetProperty("Version").GetInt32()==1&&json.RootElement.GetProperty("LeftStickX").GetInt16()==-12345&&json.RootElement.GetProperty("ButtonBits").GetUInt64()==(1UL<<50)&&json.RootElement.GetProperty("ImuNonzeroMask").GetUInt16()==512);
  b=Vector();b[24]=1;b[36]=1;var allocs=new long[3];long checksum=0;
  for(int n=0;n<2000;n++)checksum+=SteamDeckPadReportObservation.Decode(b).PacketCounter??0;
  for(int pass=0;pass<3;pass++)
  {
   long start=GC.GetAllocatedBytesForCurrentThread();
   for(int n=0;n<100000;n++){BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(48),(short)(5000+n%20000));d=SteamDeckPadReportObservation.Decode(b);checksum+=d.LeftStickX??0;checksum+=d.StickClassBits??0;checksum+=d.ImuNonzeroMask??0;checksum+=d.AccelX??0;checksum+=d.QuatW??0;}
   allocs[pass]=GC.GetAllocatedBytesForCurrentThread()-start;
  }
  Check("changing-cached-analog-decode300000-adds-zero-steady-state-heap",allocs[1]==0&&allocs[2]==0&&checksum!=0);SignalAllocationWindows=allocs;
 }

 static int Main(string[] args)
 {
  if(args.Length!=1)return 2;SignalCoverageChecks();var bytes=Vector();var d=SteamDeckPadReportObservation.Decode(bytes);
  Check("known-frame-identity",d.Status=="decoded-cached-steamdeck-report");
  Check("counter-is-little-endian",d.PacketCounter==0x12345678);
  Check("pad-touch-and-click-bits-independent",d.LeftTouch==true&&d.RightTouch==true&&d.LeftClick==true&&d.RightClick==true);
  Check("signed-pad-coordinates",d.LeftX==short.MinValue&&d.LeftY==short.MaxValue&&d.RightX==-12345&&d.RightY==12345);
  Check("pressure-is-unsigned-little-endian",d.LeftPressure==258&&d.RightPressure==ushort.MaxValue);
  Check("other-fields-not-inferred-from-pads",d.OtherDigitalFieldsNonzero==false&&d.StickFieldsNonzero==false&&d.TriggerFieldsNonzero==false&&d.ImuFieldsNonzero==false);
  Check("zero-trigger-fields-known-ushort-zero",d.Version==1&&d.LeftTrigger==0&&d.RightTrigger==0);
  var tb=Vector();BinaryPrimitives.WriteUInt16LittleEndian(tb.AsSpan(44),0x1234);BinaryPrimitives.WriteUInt16LittleEndian(tb.AsSpan(46),0xABCD);
  var saved=(byte[])tb.Clone();var td=SteamDeckPadReportObservation.Decode(tb);
  Check("cached-triggers-unsigned-little-endian",td.LeftTrigger==0x1234&&td.RightTrigger==0xABCD&&td.TriggerFieldsNonzero==true);
  Check("cached-trigger-decode-preserves-bytes",tb.AsSpan().SequenceEqual(saved));
  BinaryPrimitives.WriteUInt16LittleEndian(tb.AsSpan(44),16319);BinaryPrimitives.WriteUInt16LittleEndian(tb.AsSpan(46),16384);var half=SteamDeckPadReportObservation.Decode(tb);
  BinaryPrimitives.WriteUInt16LittleEndian(tb.AsSpan(44),0);BinaryPrimitives.WriteUInt16LittleEndian(tb.AsSpan(46),0);var zero=SteamDeckPadReportObservation.Decode(tb);
  Check("cached-half-then-zero-keeps-both-snapshots",half.LeftTrigger==16319&&half.RightTrigger==16384&&zero.LeftTrigger==0&&zero.RightTrigger==0&&zero.TriggerFieldsNonzero==false);
  BinaryPrimitives.WriteUInt16LittleEndian(tb.AsSpan(44),32767);BinaryPrimitives.WriteUInt16LittleEndian(tb.AsSpan(46),32767);var full=SteamDeckPadReportObservation.Decode(tb);
  Check("cached-full-press-is-raw-not-normalized",full.LeftTrigger==32767&&full.RightTrigger==32767);
  using(var j=JsonDocument.Parse(JsonSerializer.Serialize(full)))Check("cached-trigger-fields-compatible-version1-JSON",j.RootElement.GetProperty("Version").GetInt32()==1&&j.RootElement.GetProperty("LeftTrigger").GetUInt16()==32767&&j.RootElement.GetProperty("RightTrigger").GetUInt16()==32767);
  bytes[8]=0x80;bytes[24]=1;bytes[44]=1;bytes[48]=1;d=SteamDeckPadReportObservation.Decode(bytes);
  Check("other-digital-trigger-stick-IMU-flags-retained",d.OtherDigitalFieldsNonzero==true&&d.ImuFieldsNonzero==true&&d.TriggerFieldsNonzero==true&&d.StickFieldsNonzero==true);
  foreach(var size in new[]{0,4,63,65}){d=SteamDeckPadReportObservation.Decode(new byte[size]);Check("wrong-length-"+size+"-unknown",d.Status=="unsupported-frame-shape"&&d.PacketCounter is null&&d.LeftTouch is null&&d.LeftTrigger is null&&d.RightTrigger is null);}
  for(int i=0;i<4;i++){bytes=Vector();bytes[i]^=0x7f;d=SteamDeckPadReportObservation.Decode(bytes);Check("wrong-header-"+i+"-unknown",d.Status=="unsupported-frame-shape"&&d.LeftX is null&&d.LeftTrigger is null&&d.RightTrigger is null);}
  bytes=Vector();bytes[10]=0;d=SteamDeckPadReportObservation.Decode(bytes);Check("nonzero-position-does-not-invent-touch",d.LeftTouch==false&&d.RightTouch==false&&d.LeftX==short.MinValue);
  for(int i=0;i<1000;i++)SteamDeckPadReportObservation.Decode(bytes);long checksum=0;var allocationWindows=new long[3];
  for(int pass=0;pass<3;pass++){long before=GC.GetAllocatedBytesForCurrentThread();for(int i=0;i<100000;i++)checksum+=SteamDeckPadReportObservation.Decode(bytes).PacketCounter??0;allocationWindows[pass]=GC.GetAllocatedBytesForCurrentThread()-before;}
  long allocated=allocationWindows[2];Check("steady-state-decode-adds-no-per-report-heap-allocation",allocationWindows[1]==0&&allocationWindows[2]==0&&checksum!=0);
  var device=new FakeSdk.Device(Vector());var ctl=new FakeSdk.Controller(new(device));var observer=Observer();var sample=observer.Capture(ctl,true);
  Check("existing-approved-shape-capture-decodes-cache",sample.Status=="observed"&&sample.LastSteamDeckPadReport?.PacketCounter==0x12345678);
  Check("existing-queue-metadata-remains-readable",sample.LastInputReportLength==64&&sample.PrimaryInEndpoint==131&&sample.FrameQueueCount==0);
  bool changed=false;observer=Observer(stage=>{if(stage=="owner-after"&&!changed){changed=true;Array.Clear(device.Report);}});sample=observer.Capture(ctl,true);
  Check("copied-fields-survive-source-change-after-lock",changed&&sample.LastSteamDeckPadReport?.PacketCounter==0x12345678&&device.Report[0]==0);
  device.Report=Vector();using(var entered=new ManualResetEventSlim())using(var release=new ManualResetEventSlim()){
   var task=Task.Run(()=>{lock(device.Lock){entered.Set();release.Wait();}});if(!entered.Wait(3000))throw new Exception("lock setup");
   try{sample=Observer().Capture(ctl,true);Check("busy-lock-does-not-read-report-or-wait",sample.Detail=="lock-busy"&&sample.LastSteamDeckPadReport is null);}finally{release.Set();if(!task.Wait(3000))throw new Exception("lock cleanup");}
  }
  int reads=0;observer=Observer(_=>reads++);sample=observer.Capture(ctl,false);Check("unverified-asset-performs-zero-reflection-or-byte-reads",sample.Status=="disabled"&&reads==0&&sample.LastSteamDeckPadReport is null);
  sample=Observer().Capture(null,true);Check("absent-controller-is-unknown-not-zero",sample.Status=="no-controller"&&sample.LastSteamDeckPadReport is null);
  device.Report=Vector()[..63];sample=Observer().Capture(ctl,true);Check("cache-length63-remains-metadata-only",sample.LastInputReportLength==63&&sample.LastSteamDeckPadReport is null);
  device.Report=Vector();sample=Observer().Capture(ctl,true);d=sample.LastSteamDeckPadReport!.Value;
  Check("no-driver-Steam-or-atomic-delivery-claim",!d.SteamConsumptionProven&&!d.TransferCompletedProven&&!d.AtomicWithHostFrame);
  string json=JsonSerializer.Serialize(d);Check("snapshot-has-no-raw-bytes-or-key-content",!json.Contains("RawBytes")&&!json.Contains("ButtonsMask"));
  File.WriteAllText(args[0],JsonSerializer.Serialize(new{ok=Failed==0,checks=Cases.Count,cases=Cases,decodeAllocatedBytes=allocated,allocationWindows,signalAllocationWindows=SignalAllocationWindows,actualSdkLoaded=false,actualControllersCreated=0,actualDeviceReads=0,fieldFlickerVerified=false},new JsonSerializerOptions{WriteIndented=true}));return Failed==0?0:3;
 }
}
namespace FakeSdk
{
 internal sealed class Controller{public Handle UsbipHandle{get;}internal Controller(Handle h){UsbipHandle=h;}}
 internal sealed class Handle{public Device Device{get;}public int VhciPort{get;}=1;internal Handle(Device d){Device=d;}}
 internal sealed class Connection{private Device _device;internal Connection(Device d){_device=d;}}
 internal sealed class Device
 {
  // Mirror only the current observer's read-only cache contract. No SDK is loaded.
  private readonly struct QueuedFrame { }
  private int _vhciPort=1;
  private readonly object _hidLock=new();private readonly Dictionary<byte,Queue<uint>> _pendingInterruptIn=new();private readonly Queue<QueuedFrame> _frameQueue=new();
  private readonly HIDMaestro.Internal.HidReportBuilder _builder=new();private readonly bool _inputReportIds=true;
  private readonly byte[][] _inputReports;private readonly int[] _inputReportLength;private readonly int[] _inputSlot;
  private readonly byte _primaryInEndpoint=131;private volatile bool _stop;private Connection _connection;private readonly Thread _inputThread=new(()=>{});
  internal object Lock=>_hidLock;
  internal byte[] Report{get=>_inputReports[0];set{_inputReports[0]=value;_inputReportLength[0]=value.Length;}}
  internal Device(byte[] report){_inputReports=new[]{report};_inputReportLength=new[]{report.Length};_inputSlot=Enumerable.Repeat(-1,256).ToArray();_inputSlot[1]=0;_connection=new(this);_stop=false;}
 }
}

namespace HIDMaestro.Internal
{
 internal class HidReportBuilder { public byte InputReportId {get;set;}=1; }
}
