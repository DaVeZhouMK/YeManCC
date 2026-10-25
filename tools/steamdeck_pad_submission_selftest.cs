using System.Buffers.Binary;
using System.Text.Json;
using YeManInputHost.Diagnostics;
using E=YeManInputHost.Diagnostics.SteamDeckPadSubmissionEvidence;
internal static class Program
{
 private static readonly List<object> Cases=new();private static int bad;private static bool geometryWireStable=true;
 private static void Check(string name,bool ok){Cases.Add(new{name,ok});if(!ok)bad++;}
 private static byte[] Packet(uint n,byte touch=1,byte click=0){var b=new byte[64];b[0]=1;b[2]=9;b[3]=64;BinaryPrimitives.WriteUInt32LittleEndian(b.AsSpan(4),n);BinaryPrimitives.WriteUInt64LittleEndian(b.AsSpan(8),((ulong)touch<<19)|((ulong)click<<17));return b;}
 private static E.ExpectedPad Left=new(true,false,0,0,0);
 private static void Record(E e,byte[] b,bool enabled=true,E.ExpectedPad? left=null,E.ExpectedPad? right=null,ulong sample=1,bool neutral=false){e.Record(enabled,b,left??Left,right??default,9,sample,1000,neutral);}
 private static byte[] GeometryRecord(E e,uint counter,short lx,short ly,short rx=0,short ry=0,byte touch=1,bool enabled=true)
 {
  var b=Packet(counter,touch);BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(16),lx);BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(18),ly);
  BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(20),rx);BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(22),ry);
  var before=(byte[])b.Clone();
  e.Record(enabled,b,new((touch&1)!=0,false,lx,ly,0),new((touch&2)!=0,false,rx,ry,0),counter*2UL,counter*3UL,1000UL+counter);
  geometryWireStable&=b.AsSpan().SequenceEqual(before);
  return b;
 }
 private static void GeometryChecks()
 {
  var e=new E();e.Reset();var empty=e.Capture().RawGeometry!;
  Check("empty-geometry-does-not-invent-coordinate-zero",empty.Left.ActiveReports==0&&empty.Left.MinimumX is null&&empty.Left.LastActive is null&&empty.Right.MaximumY is null);
  GeometryRecord(e,1,30500,-1200,-30500,1200,3);var s=e.Capture();var g=s.RawGeometry!;
  Check("reference-comparison-is-strict-on-both-sides",g.ReferenceXBoundary==30500&&g.Left.InnerSideReferenceReports==0&&g.Right.InnerSideReferenceReports==0);
  GeometryRecord(e,2,30501,-2400,-30501,2400,3);g=e.Capture().RawGeometry!;
  Check("held-touch-crossing-is-recorded-without-new-wire-edge",g.Left.InnerSideReferenceReports==1&&g.Right.InnerSideReferenceReports==1&&g.Left.ReferenceClassChanges==1&&g.Right.ReferenceClassChanges==1&&e.Capture().Edges==1);
  Check("geometry-retains-source-binding-and-packet",g.Left.LastInnerSideReference is {CommandSequence:4,SampleSequence:6,SourceTick:1002,PacketCounter:2,X:30501}&&g.Right.LastInnerSideReference?.X==-30501);
  GeometryRecord(e,3,-32767,32767,32767,-32767,3);g=e.Capture().RawGeometry!;
  Check("opposite-side-protocol-extreme-is-not-inner-reference",g.Left.InnerSideReferenceReports==1&&g.Right.InnerSideReferenceReports==1&&g.Left.OutsidePadDomainReports==0&&g.Right.OutsidePadDomainReports==0);
  Check("active-movement-extents-survive-edge-only-ring",g.Left.MinimumX==-32767&&g.Left.MaximumX==30501&&g.Left.MinimumY==-2400&&g.Left.MaximumY==32767&&g.Right.MinimumX==-30501&&g.Right.MaximumX==32767&&e.Capture().Edges==1);
  var saved=e.Capture();GeometryRecord(e,4,0,0,0,0,0);var released=e.Capture();
  Check("release-retains-historical-active-and-reference-points",released.LastSuccessfulReport?.TouchMask==0&&released.RawGeometry!.Left.LastActive?.PacketCounter==3&&released.RawGeometry.Left.LastInnerSideReference?.PacketCounter==2);
  Check("captured-geometry-is-immutable",saved.RawGeometry!.Left.ActiveReports==3&&saved.RawGeometry.Left.LastActive?.PacketCounter==3);
  GeometryRecord(e,5,31000,10,-31000,-10,3);g=e.Capture().RawGeometry!;
  Check("release-and-recontact-break-reference-transition-continuity",g.Left.ReferenceClassChanges==2&&g.Right.ReferenceClassChanges==2&&e.Capture().TouchChanges==2);
  e.Reset();GeometryRecord(e,1,30000,0);GeometryRecord(e,2,31000,0,enabled:false);GeometryRecord(e,3,31000,0);g=e.Capture().RawGeometry!;
  Check("disabled-recording-neither-counts-nor-invents-crossing",g.Left.ActiveReports==2&&g.Left.InnerSideReferenceReports==1&&g.Left.ReferenceClassChanges==0&&e.Capture().EnabledSpans==2);
  e.Record(false,ReadOnlySpan<byte>.Empty,default,default,0,0,0);g=e.Capture().RawGeometry!;
  Check("disabled-history-is-explicitly-not-current",!g.RecordingEnabled&&!g.LatestReportValid&&g.Left.LastActive?.PacketCounter==3&&g.Left.ActiveReports==2);
  e.Reset();GeometryRecord(e,1,30000,0);e.Record(true,new byte[63],default,default,2,2,2);GeometryRecord(e,3,31000,0);g=e.Capture().RawGeometry!;
  Check("invalid-packet-breaks-geometry-continuity",g.Left.ActiveReports==2&&g.Left.ReferenceClassChanges==0&&e.Capture().InvalidFrames==1);
  e.Reset();GeometryRecord(e,1,30000,0);GeometryRecord(e,10,31000,0);g=e.Capture().RawGeometry!;
  Check("counter-gap-is-not-presented-as-contiguous-reference-crossing",g.Left.ReferenceClassChanges==0&&g.Left.ActiveReports==2&&e.Capture().CounterDiscontinuities==1);
  e.Reset();GeometryRecord(e,uint.MaxValue,30000,0);GeometryRecord(e,0,31000,0);g=e.Capture().RawGeometry!;
  Check("legitimate-packet-wrap-preserves-reference-continuity",g.Left.ReferenceClassChanges==1&&e.Capture().CounterDiscontinuities==0);
  e.Reset();GeometryRecord(e,1,short.MinValue,0,0,short.MinValue,3);g=e.Capture().RawGeometry!;
  Check("signed-short-minimum-is-outside-reviewed-pad-domain",g.Left.OutsidePadDomainReports==1&&g.Right.OutsidePadDomainReports==1&&g.Left.InnerSideReferenceReports==0);
  e.Reset();var p=Packet(0);for(uint i=0;i<4096;i++){short x=(i&1)==0?(short)30000:(short)31000;BinaryPrimitives.WriteUInt32LittleEndian(p.AsSpan(4),i);BinaryPrimitives.WriteInt16LittleEndian(p.AsSpan(16),x);e.Record(true,p,new(true,false,x,0,0),default,i,i,1000+i);}
  s=e.Capture();g=s.RawGeometry!;
  Check("many-coordinate-crossings-have-bounded-points-and-no-touch-ring-growth",g.Left.ActiveReports==4096&&g.Left.InnerSideReferenceReports==2048&&g.Left.ReferenceClassChanges==4095&&g.Left.LastReferenceClassChange?.PacketCounter==4095&&s.Edges==1&&s.Entries.Length==1);
  string json=JsonSerializer.Serialize(s);using var doc=JsonDocument.Parse(json);var geometry=doc.RootElement.GetProperty("RawGeometry");
  Check("geometry-reaches-existing-JSON-serializer",geometry.GetProperty("Left").GetProperty("InnerSideReferenceReports").GetUInt64()==2048&&json.Length<8192);
  Check("geometry-explicitly-does-not-claim-Steam-policy-or-consumption",!geometry.GetProperty("SteamPolicyObserved").GetBoolean()&&!geometry.GetProperty("SteamConsumptionProven").GetBoolean());
  long generation=s.TargetGeneration;e.Reset();g=e.Capture().RawGeometry!;
  Check("target-generation-reset-removes-old-geometry",e.Capture().TargetGeneration==generation+1&&g.Left.ActiveReports==0&&g.Left.LastInnerSideReference is null&&g.Right.FirstActive is null);
  Check("geometry-observer-preserves-all-raw-inputs",geometryWireStable);
 }

 private static void TriggerChecks()
 {
  // Production successful-submit decoder, with unrelated IMU nonzero so the
  // old aggregate boolean cannot retain trigger edges accidentally.
  var e=new E();uint counter=0;bool wireStable=true;
  void Send(ushort lt,ushort rt,bool enabled=true,bool neutral=false)
  {
   var b=Packet(++counter,0);b[24]=1;
   BinaryPrimitives.WriteUInt16LittleEndian(b.AsSpan(44),lt);BinaryPrimitives.WriteUInt16LittleEndian(b.AsSpan(46),rt);
   var before=(byte[])b.Clone();e.Record(enabled,b,default,default,counter*2UL,counter*3UL,9000UL+counter,neutral);
   wireStable&=b.AsSpan().SequenceEqual(before);
  }
  Send(0,0);Send(16319,16319);Send(0,0);for(int n=0;n<4000;n++)Send(0,0);var s=e.Capture();
  Check("single-frame-dual-half-pulse-and-zero-survive-idle-tail",s.Reports==4003&&s.Edges==3&&s.Entries.Length==3&&s.OmittedRows==0&&s.Entries[1].LeftTrigger==16319&&s.Entries[1].RightTrigger==16319&&s.Entries[1].DualTriggerHalfRange&&s.Entries[2].TriggerNonzeroMask==0&&!s.Entries[2].DualTriggerHalfRange);
  Check("trigger-pulse-retains-source-binding",s.Entries.Length>1&&s.Entries[1] is {CommandSequence:4,SampleSequence:6,SourceTick:9002,PacketCounter:2});
  Check("trigger-values-use-existing-version1-JSON",s.Version==1&&JsonSerializer.Serialize(s).Contains("\"LeftTrigger\":16319")&&JsonSerializer.Serialize(s).Contains("\"RightTrigger\":16319"));var frozen=s;
  e.Reset();counter=0;Send(1000,1000);Send(16384,16384);Send(32767,32767);s=e.Capture();
  Check("half-entry-and-full-exit-with-other-fields-nonzero",s.Edges==3&&!s.Entries[0].DualTriggerHalfRange&&s.Entries[1].DualTriggerHalfRange&&!s.Entries[2].DualTriggerHalfRange&&s.Entries[2].LeftTrigger==32767&&s.Entries[2].RightTrigger==32767);
  Send(0,0,neutral:true);s=e.Capture();Check("full-to-neutral-retains-zero-raw-fields",s.Edges==4&&s.Entries[^1].LeftTrigger==0&&s.Entries[^1].RightTrigger==0&&s.Entries[^1].NeutralCommand);
  Check("captured-half-pulse-stays-immutable",frozen.Entries.Length>1&&frozen.Entries[1].LeftTrigger==16319&&frozen.Entries[1].DualTriggerHalfRange);
  e.Reset();counter=0;Send(0,0);Send(1,0);Send(2,0);Send(2,1);Send(0,1);Send(0,0);s=e.Capture();
  Check("per-side-zero-crossings-not-every-analog-value",s.Edges==5&&s.Entries.Select(r=>r.TriggerNonzeroMask).SequenceEqual(new byte[]{0,1,3,2,0}));
  e.Reset();counter=0;Send(0x1234,0xABCD);s=e.Capture();
  Check("successful-submit-decoder-unsigned-little-endian",s.LastSuccessfulReport?.LeftTrigger==0x1234&&s.LastSuccessfulReport?.RightTrigger==0xABCD&&!s.LastSuccessfulReport.Value.DualTriggerHalfRange);
  e.Reset();counter=0;Send(16318,16384);Send(16319,16448);Send(16448,16319);Send(16449,16384);s=e.Capture();
  Check("dual-half-band-boundaries-not-nearby-value-edges",s.Edges==3&&!s.Entries[0].DualTriggerHalfRange&&s.Entries[1].DualTriggerHalfRange&&!s.Entries[^1].DualTriggerHalfRange);
  e.Reset();counter=0;for(int n=0;n<=32767;n++)Send((ushort)n,(ushort)n);s=e.Capture();
  Check("dual-gradient-four-edges-not32768-values",s.Reports==32768&&s.Edges==4&&s.Entries.Length==4&&s.OmittedRows==0&&s.LastSuccessfulReport?.LeftTrigger==32767&&s.LastSuccessfulReport?.RightTrigger==32767);
  Send(0,0);s=e.Capture();Check("gradient-release-is-fifth-edge",s.Edges==5&&s.Entries[^1].TriggerNonzeroMask==0);
  e.Reset();counter=0;for(int n=0;n<=32767;n++)Send((ushort)n,0);s=e.Capture();
  Check("single-trigger-gradient-never-dual-half",s.Edges==2&&s.Entries.All(r=>!r.DualTriggerHalfRange)&&s.LastSuccessfulReport?.LeftTrigger==32767);
  e.Reset();counter=0;Send(16384,16384);Send(0,0,enabled:false);s=e.Capture();
  Check("disabled-does-not-invent-trigger-release",s.Reports==1&&s.Edges==1&&s.LastSuccessfulReport is null&&s.Entries[0].DualTriggerHalfRange);
  Send(0,0);s=e.Capture();Check("reenable-keeps-current-state-and-gap",s.EnabledSpans==2&&s.Edges==2&&s.Entries[^1].TriggerNonzeroMask==0);
  e.Reset();s=e.Capture();Check("target-reset-clears-trigger-history",s.Reports==0&&s.Entries.Length==0&&s.LastSuccessfulReport is null);
  Check("trigger-observer-preserves-submitted-bytes",wireStable);
 }


 private static short Motion(E.Row r,int axis)=>axis switch
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
 private static void SignalCoverageChecks()
 {
  var e=new E();uint counter=0;bool wireStable=true;
  const ulong held=(1UL<<19)|(1UL<<20);
  void Send(ulong buttons=held,short lx=0,short ly=0,short rx=0,short ry=0,
   ushort lp=0,ushort rp=0,ushort imu=1,short motion=1,short px=0,short py=0,short qx=0,short qy=0,
   E.ExpectedPad? expectedLeft=null,E.ExpectedPad? expectedRight=null,bool enabled=true)
  {
   var b=Packet(++counter,0);BinaryPrimitives.WriteUInt64LittleEndian(b.AsSpan(8),buttons);
   short[] axes={lx,ly,rx,ry};for(int a=0;a<4;a++)BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(48+a*2),axes[a]);
   short[] pads={px,py,qx,qy};for(int a=0;a<4;a++)BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(16+a*2),pads[a]);
   for(int a=0;a<10;a++)BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(24+a*2),(imu&(1<<a))!=0?motion:(short)0);
   BinaryPrimitives.WriteUInt16LittleEndian(b.AsSpan(56),lp);BinaryPrimitives.WriteUInt16LittleEndian(b.AsSpan(58),rp);
   var l=expectedLeft??new E.ExpectedPad((buttons&(1UL<<19))!=0,(buttons&(1UL<<17))!=0,px,py,lp);
   var r=expectedRight??new E.ExpectedPad((buttons&(1UL<<20))!=0,(buttons&(1UL<<18))!=0,qx,qy,rp);
   var before=(byte[])b.Clone();e.Record(enabled,b,l,r,counter*2UL,counter*3UL,12000UL+counter);
   wireStable&=b.AsSpan().SequenceEqual(before);
  }
  // Every wire bit, including high bits, is independent of a held touch and IMU.
  for(int bit=0;bit<64;bit++)
  {
   e.Reset();counter=0;ulong key=1UL<<bit,bg=held&~key;Send(buttons:bg);Send(buttons:bg|key);Send(buttons:bg);
   for(int n=0;n<256;n++)Send(buttons:bg);var s=e.Capture();
   Check("button-bit-"+bit+"-single-frame-pulse-and-release-under-held-inputs",s.Reports==259&&s.Edges==3&&s.OmittedRows==0&&
    s.Entries.Length==3&&s.Entries[0].ButtonBits==bg&&s.Entries[1].ButtonBits==(bg|key)&&s.Entries[2].ButtonBits==bg);
  }
  e.Reset();counter=0;Send(buttons:held|(1UL<<7));Send(buttons:held|(1UL<<6));Send();var frozen=e.Capture();
  Check("nonzero-button-to-different-nonzero-button-retains-both",frozen.Edges==3&&frozen.Entries[1].ButtonBits==(held|(1UL<<6)));
  Check("new-signal-edges-retain-source-identities",frozen.Entries.Length>1&&frozen.Entries[1] is {CommandSequence:4,SampleSequence:6,SourceTick:12002,PacketCounter:2});
  Send(lx:-12345,ly:23456,rx:short.MinValue,ry:short.MaxValue);var raw=e.Capture().LastSuccessfulReport!.Value;
  Check("four-stick-offsets-signed-little-endian-not-pad-coordinates",raw.LeftStickX==-12345&&raw.LeftStickY==23456&&raw.RightStickX==short.MinValue&&raw.RightStickY==short.MaxValue&&raw.LeftX==0&&raw.RightX==0);
  Check("captured-digital-pulse-is-immutable",frozen.Entries[^1].LeftStickX==0&&frozen.Entries[^1].ButtonBits==held);
  for(int axis=0;axis<4;axis++)
  {
   void Axis(short value){Send(lx:axis==0?value:(short)0,ly:axis==1?value:(short)0,rx:axis==2?value:(short)0,ry:axis==3?value:(short)0);}
   foreach(short sign in new short[]{-5000,5000})
   {
    e.Reset();counter=0;Axis(0);Axis(sign);Axis(0);for(int n=0;n<128;n++)Axis(0);var s=e.Capture();
    Check("stick-axis-"+axis+"-"+sign+"-pulse-and-return-under-IMU",s.Edges==3&&s.OmittedRows==0&&s.Entries.Length==3&&
     s.Entries[1].StickClassBits==(ushort)((sign<0?1:2)<<(axis*3))&&s.Entries[^1].StickClassBits==0);
   }
   // Explicit expected classes at both deadzone/full limits and invalid -32768.
   short[] values={-32768,-32767,-32766,-4097,-4096,-1,0,1,4096,4097,32766,32767};
   byte[] classes={5,3,1,1,0,0,0,0,0,2,2,4};bool correct=true;e.Reset();counter=0;
   for(int n=0;n<values.Length;n++)
   {
    Axis(values[n]);var r=e.Capture().LastSuccessfulReport!.Value;
    short actual=axis switch{0=>r.LeftStickX,1=>r.LeftStickY,2=>r.RightStickX,_=>r.RightStickY};
    correct&=actual==values[n]&&r.StickClassBits==(ushort)(classes[n]<<(axis*3));
   }
   Axis(0);var bounds=e.Capture();
   Check("stick-axis-"+axis+"-inclusive-deadzone-full-and-invalid-boundaries",correct&&bounds.ReferenceStickDeadzone==4096&&bounds.Edges==7&&bounds.OmittedRows==0&&bounds.Entries[^1].StickClassBits==0);
  }
  e.Reset();counter=0;for(int n=short.MinValue;n<=short.MaxValue;n++)Send(lx:(short)n,ly:(short)n,rx:(short)n,ry:(short)n);
  var gradient=e.Capture();
  Check("full-signed-four-axis-gradient-retains-six-classes-not65536-values",gradient.Reports==65536&&gradient.Edges==6&&gradient.OmittedRows==0&&gradient.Entries.Length==6&&gradient.LastSuccessfulReport?.RightStickY==32767);
  e.Reset();counter=0;for(int n=0;n<4096;n++)Send(lx:(short)((n&1)==0?-4096:4096),ly:(short)((n&1)==0?4096:-4096));
  Check("within-diagnostic-deadzone-sign-jitter-does-not-fill-ring",e.Capture().Edges==1&&e.Capture().OmittedRows==0);
  // A nonzero component (gravity/quaternion, for example) cannot mask another.
  for(int axis=0;axis<10;axis++)
  {
   e.Reset();counter=0;ushort bg=(ushort)(1<<(axis==0?1:0)),pulse=(ushort)(bg|(1<<axis));
   Send(imu:bg);Send(imu:pulse,motion:short.MinValue);Send(imu:bg,motion:short.MaxValue);for(int n=0;n<128;n++)Send(imu:bg);
   var s=e.Capture();Check("imu-component-"+axis+"-pulse-release-under-other-nonzero-components",s.Edges==3&&s.OmittedRows==0&&s.Entries.Length==3&&
    s.Entries.All(r=>r.ImuFieldsNonzero&&r.OtherFieldsNonzero)&&s.Entries[1].ImuNonzeroMask==pulse&&s.Entries[2].ImuNonzeroMask==bg);
  }
  // Each scalar has its own signed 16-bit wire offset. Keep another IMU
  // component nonzero so raw values cannot borrow aggregate edge retention.
  for(int axis=0;axis<10;axis++)
  {
   e.Reset();counter=0;int bg=axis==0?1:0;short[] samples={short.MinValue,-12345,-1,0,1,12345,short.MaxValue};bool exact=true;
   foreach(short value in samples)
   {
    var b=Packet(++counter,3);BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(24+bg*2),123);
    BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(24+axis*2),value);var before=(byte[])b.Clone();
    e.Record(true,b,new(true,false,0,0,0),new(true,false,0,0,0),counter*2UL,counter*3UL,12000UL+counter);
    wireStable&=b.AsSpan().SequenceEqual(before);var r=e.Capture().LastSuccessfulReport!.Value;
    for(int a=0;a<10;a++)exact&=Motion(r,a)==(a==axis?value:a==bg?(short)123:(short)0);
   }
   var s=e.Capture();Check("IMU-raw-component-"+axis+"-signed-offset-extremes-edge-and-latest",exact&&s.Edges==3&&s.Entries.Length==3&&
    Motion(s.Entries[0],axis)==short.MinValue&&Motion(s.Entries[1],axis)==0&&Motion(s.Entries[2],axis)==1&&Motion(s.LastSuccessfulReport!.Value,axis)==short.MaxValue);
  }
  e.Reset();counter=0;Send(imu:1023,motion:-1);var rawFrozen=e.Capture();
  for(int n=0;n<10000;n++)Send(imu:1023,motion:(short)((n&1)==0?short.MinValue:short.MaxValue));
  var rawMoving=e.Capture();bool latestRaw=true,frozenRaw=true;
  for(int a=0;a<10;a++){latestRaw&=Motion(rawMoving.LastSuccessfulReport!.Value,a)==short.MaxValue;frozenRaw&=Motion(rawFrozen.Entries[0],a)==-1;}
  Check("nonzero-IMU-magnitude-and-sign-variation-keeps-one-edge-with-raw-latest",rawMoving.Reports==10001&&rawMoving.Edges==1&&rawMoving.OmittedRows==0&&latestRaw&&frozenRaw);
  string[] rawNames={"AccelX","AccelY","AccelZ","GyroPitch","GyroYaw","GyroRoll","QuatW","QuatX","QuatY","QuatZ"};
  using(var json=JsonDocument.Parse(JsonSerializer.Serialize(rawMoving)))
  {
   bool jsonRaw=true;for(int a=0;a<10;a++)jsonRaw&=json.RootElement.GetProperty("LastSuccessfulReport").GetProperty(rawNames[a]).GetInt16()==short.MaxValue&&json.RootElement.GetProperty("Entries")[0].GetProperty(rawNames[a]).GetInt16()==-1;
   Check("ten-raw-IMU-scalars-export-through-latest-and-edge-JSON",jsonRaw);
  }
  e.Reset();counter=0;Send(buttons:0,imu:0);Send(buttons:0,imu:512);Send(buttons:0,imu:0);var quaternion=e.Capture();
  Check("quaternion-Z-only-included-in-aggregate-and-release",quaternion.Edges==3&&quaternion.Entries[1].ImuNonzeroMask==512&&quaternion.Entries[1].OtherFieldsNonzero&&!quaternion.Entries[^1].OtherFieldsNonzero);
  e.Reset();counter=0;Send(lp:100,rp:200);Send(lp:0,rp:200);Send(lp:100,rp:200);Send(lp:100,rp:0);var pressure=e.Capture();
  Check("per-side-pressure-release-not-hidden-by-other-pressure-touch-or-IMU",pressure.Edges==4&&pressure.Entries.Select(r=>r.PressureNonzeroMask).SequenceEqual(new byte[]{3,2,3,1}));
  e.Reset();counter=0;for(int n=0;n<=ushort.MaxValue;n++)Send(lp:(ushort)n,rp:200);Send(lp:0,rp:200);pressure=e.Capture();
  Check("unsigned-pressure-gradient-and-release-have-only-four-class-edges",pressure.Reports==65537&&pressure.Edges==4&&pressure.OmittedRows==0&&pressure.Entries[2].LeftPressure==32768&&pressure.Entries[2].PressureOutOfRangeMask==1&&pressure.Entries[^1].PressureNonzeroMask==2);
  foreach(bool right in new[]{false,true})
  {
   e.Reset();counter=0;ushort[] values={0,1,32767,32768,65535,32767,0};byte[] masks={0,0,0,1,1,0,0};bool correct=true;
   for(int n=0;n<values.Length;n++){Send(lp:right?(ushort)0:values[n],rp:right?values[n]:(ushort)0);var r=e.Capture().LastSuccessfulReport!.Value;correct&=r.PressureOutOfRangeMask==(masks[n]<<(right?1:0));}
   Check("pressure-"+(right?"right":"left")+"-zero-maximum-admitted-and-ushort-boundaries",correct&&e.Capture().Edges==5);
  }
  e.Reset();counter=0;var el=new E.ExpectedPad(true,false,0,0,300);var er=new E.ExpectedPad(true,false,0,0,400);
  Send(lp:100,rp:200,expectedLeft:el,expectedRight:er);Send(buttons:1UL<<20,lp:100,rp:200,expectedLeft:el,expectedRight:er);
  Send(buttons:1UL<<20,lp:0,rp:200,expectedLeft:el,expectedRight:er);var abnormal=e.Capture();
  Check("unexpected-touch-release-and-later-pressure-clear-survive-persistent-mismatch",abnormal.Edges==3&&abnormal.TouchMismatch==2&&abnormal.PressureMismatch==3&&
   abnormal.Entries.Length==3&&abnormal.Entries[1].Errors==abnormal.Entries[2].Errors&&abnormal.Entries[1].InactivePadDataMask==16&&abnormal.Entries[2].InactivePadDataMask==0);
  e.Reset();counter=0;Send(buttons:0,px:10,py:20,qx:30,qy:40,lp:100,rp:200);
  Send(buttons:0,py:20,qx:30,qy:40,lp:100,rp:200);Send(buttons:0,qx:30,qy:40,lp:100,rp:200);
  Send(buttons:0,qy:40,lp:100,rp:200);Send(buttons:0,lp:100,rp:200);Send(buttons:0,rp:200);Send(buttons:0);
  var residue=e.Capture();Check("each-inactive-pad-residue-clear-survives-other-coordinate-and-pressure-errors",residue.Edges==7&&residue.Entries.Select(r=>r.InactivePadDataMask).SequenceEqual(new byte[]{63,62,60,56,48,32,0}));
  // Prime all finite classes, preserve a button pulse, then move every analog field.
  e.Reset();counter=0;Send(lx:10000,ly:11000,rx:-12000,ry:-13000,imu:1023,lp:1,rp:2);
  Send(buttons:held|(1UL<<7),lx:10000,ly:11000,rx:-12000,ry:-13000,imu:1023,lp:1,rp:2);
  Send(lx:10000,ly:11000,rx:-12000,ry:-13000,imu:1023,lp:1,rp:2);
  for(int n=1;n<=20000;n++)Send(lx:(short)(5000+n),ly:(short)(6000+n),rx:(short)(-5000-n),ry:(short)(-6000-n),
   imu:1023,motion:(short)n,lp:(ushort)n,rp:(ushort)(n+1),px:(short)n,py:(short)-n,qx:(short)-n,qy:(short)n);
  var moving=e.Capture();Check("normal-continuous-motion-and-gradients-preserve-earlier-button-pulse",moving.Reports==20003&&moving.Edges==3&&moving.OmittedRows==0&&moving.Entries.Length==3&&
   moving.Entries[1].ButtonBits==(held|(1UL<<7))&&moving.LastSuccessfulReport?.LeftStickX==25000&&moving.LastSuccessfulReport?.RightPressure==20001&&moving.CoordinateMismatch==0&&moving.PressureMismatch==0);
  using(var json=JsonDocument.Parse(JsonSerializer.Serialize(moving)))
  {
   var last=json.RootElement.GetProperty("LastSuccessfulReport");
   Check("added-raw-and-class-fields-reach-existing-version1-JSON",moving.Version==1&&last.GetProperty("LeftStickX").GetInt16()==25000&&last.GetProperty("ImuNonzeroMask").GetUInt16()==1023&&last.GetProperty("PressureNonzeroMask").GetByte()==3);
  }
  e.Reset();counter=0;Send(lx:5000,imu:1023);Send(enabled:false);var disabled=e.Capture();Send(lx:5000,imu:1023);var resumed=e.Capture();
  Check("disabled-and-reenabled-signal-history-does-not-invent-release",disabled.LastSuccessfulReport is null&&disabled.Edges==1&&resumed.Edges==2&&resumed.EnabledSpans==2&&resumed.CounterDiscontinuities==0);
  e.Record(true,new byte[63],default,default,0,0,0);Send();var afterInvalid=e.Capture();
  Check("invalid-frame-breaks-signal-comparisons-without-synthetic-zero",afterInvalid.InvalidFrames==1&&afterInvalid.Edges==3&&afterInvalid.CounterDiscontinuities==0&&afterInvalid.Entries[^1].StickClassBits==0);
  e.Reset();counter=0;for(int n=0;n<100;n++)Send(buttons:held|((n&1)==0?0:1UL<<63));var bounded=e.Capture();
  Check("digital-signal-ring-stays32-with-explicit-omission-count",bounded.Edges==100&&bounded.Entries.Length==32&&bounded.OmittedRows==68&&bounded.Entries[0].PacketCounter==69);
  e.Reset();var cleared=e.Capture();Check("reset-clears-added-signal-history",cleared.Entries.Length==0&&cleared.LastSuccessfulReport is null);
  Check("all-signal-observations-preserve-report-bytes",wireStable);
 }
 private static void SignalAllocationChecks()
 {
  var e=new E();var b=Packet(0,3);b[24]=1;b[36]=1;
  BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(48),5000);BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(52),-5000);
  BinaryPrimitives.WriteUInt16LittleEndian(b.AsSpan(56),1);
  for(int n=0;n<2000;n++){BinaryPrimitives.WriteUInt32LittleEndian(b.AsSpan(4),(uint)n);e.Record(true,b,new(true,false,0,0,1),new(true,false,0,0,0),(ulong)n,1,1);}
  var allocs=new long[3];uint packet=2000;
  for(int pass=0;pass<3;pass++)
  {
   long start=GC.GetAllocatedBytesForCurrentThread();
   for(int n=0;n<100000;n++)
   {
    short value=(short)(5000+n%20000);ushort pressure=(ushort)(1+n%32767);
    BinaryPrimitives.WriteUInt32LittleEndian(b.AsSpan(4),packet++);
    BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(48),value);BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(52),(short)-value);
    BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(24),value);BinaryPrimitives.WriteUInt16LittleEndian(b.AsSpan(56),pressure);
    e.Record(true,b,new(true,false,0,0,pressure),new(true,false,0,0,0),packet,packet,packet);
   }
   allocs[pass]=GC.GetAllocatedBytesForCurrentThread()-start;
  }
  var s=e.Capture();Check("changing-analog-record300000-adds-zero-steady-state-heap-and-one-edge",allocs[1]==0&&allocs[2]==0&&s.Reports==302000&&s.Edges==1&&s.PressureMismatch==0&&s.LastSuccessfulReport?.LeftStickX==24999&&s.LastSuccessfulReport?.AccelX==24999&&s.LastSuccessfulReport?.QuatW==1);
  SignalAllocationWindows=allocs;
 }
 private static long[] SignalAllocationWindows=Array.Empty<long>();

 private static int Main(string[] args)
 {
  if(args.Length!=1)return 2;var e=new E();e.Reset();var p=Packet(1);var original=(byte[])p.Clone();Record(e,p);var s=e.Capture();
  Check("record-does-not-change-wire-bytes",p.AsSpan().SequenceEqual(original));
  Check("first-success-is-an-edge",s.Reports==1&&s.Edges==1&&s.Entries.Length==1&&s.EnabledSpans==1);
  var eb=new E();var pb=Packet(1);var expectedButtonBits=(1UL<<0)|(1UL<<1)|(1UL<<2)|(1UL<<3)|(1UL<<15)|(1UL<<16);BinaryPrimitives.WriteUInt64LittleEndian(pb.AsSpan(8),expectedButtonBits);Record(eb,pb);var sb=eb.Capture();Check("full-steamdeck-button-bits-are-retained",sb.LastSuccessfulReport?.ButtonBits==expectedButtonBits);
  Check("packet-source-binding-is-preserved",s.Entries[0].CommandSequence==9&&s.Entries[0].SampleSequence==1&&s.Entries[0].SourceTick==1000&&s.Entries[0].PacketCounter==1);
  for(uint i=2;i<=1000;i++){p=Packet(i);BinaryPrimitives.WriteInt16LittleEndian(p.AsSpan(16),(short)i);Record(e,p,left:new(true,false,(short)i,0,0));}
  s=e.Capture();Check("continuous-moving-reports-do-not-allocate-edges",s.Reports==1000&&s.Edges==1&&s.CoordinateMismatch==0&&s.CounterDiscontinuities==0);
  Check("latest-report-remains-available-without-per-MOVE-ring-growth",s.LastSuccessfulReport?.PacketCounter==1000&&s.LastSuccessfulReport?.LeftX==1000);
  Record(e,Packet(1001,0),left:default(E.ExpectedPad),neutral:true);Record(e,Packet(1002));s=e.Capture();
  Check("short-off-on-both-counted-before1Hz-capture",s.TouchChanges==2&&s.Edges==3&&s.Entries[1].TouchMask==0&&s.Entries[2].TouchMask==1);
  Check("neutral-command-boundary-is-explicit",s.Entries[1].NeutralCommand&&!s.Entries[2].NeutralCommand);
  e.Reset();Record(e,Packet(1,0));s=e.Capture();Check("unexpected-wire-release-is-a-touch-mismatch",s.TouchMismatch==1&&(s.Entries[0].Errors&1)!=0);
  e.Reset();Record(e,Packet(1,1,1));s=e.Capture();Check("unexpected-wire-click-is-a-click-mismatch",s.ClickMismatch==1&&(s.Entries[0].Errors&2)!=0);
  e.Reset();p=Packet(1);p[16]=1;Record(e,p);s=e.Capture();Check("coordinate-mismatch-is-detected",s.CoordinateMismatch==1&&(s.Entries[0].Errors&4)!=0);
  e.Reset();p=Packet(1);p[56]=1;Record(e,p);s=e.Capture();Check("pressure-mismatch-is-detected",s.PressureMismatch==1&&(s.Entries[0].Errors&8)!=0);
  e.Reset();Record(e,Packet(1));Record(e,Packet(3));s=e.Capture();Check("packet-gap-is-detected-and-retained",s.CounterDiscontinuities==1&&(s.Entries[^1].Errors&16)!=0);
  e.Reset();Record(e,Packet(uint.MaxValue));Record(e,Packet(0));s=e.Capture();Check("packet-wrap-is-contiguous",s.CounterDiscontinuities==0);
  e.Reset();Record(e,Packet(1));Record(e,Packet(2),enabled:false);Record(e,Packet(3),enabled:false);s=e.Capture();Check("disabled-adds-no-reports-or-edges",s.Reports==1&&s.Edges==1);
  Record(e,Packet(500));s=e.Capture();Check("reenable-does-not-pretend-gap-is-counter-error",s.CounterDiscontinuities==0&&s.EnabledSpans==2&&s.Edges==2);
  e.Reset();Record(e,new byte[63]);Record(e,new byte[64]);s=e.Capture();Check("invalid-frames-are-counted-not-decoded",s.Reports==2&&s.InvalidFrames==2&&s.Edges==0);
  Record(e,Packet(20));s=e.Capture();Check("invalid-frame-breaks-counter-comparison",s.CounterDiscontinuities==0&&s.Edges==1);
  e.Reset();for(uint i=0;i<100;i++){bool active=(i&1)!=0;Record(e,Packet(i,active?(byte)1:(byte)0),left:active?Left:default(E.ExpectedPad));}
  s=e.Capture();Check("ring-memory-and-omission-count-are-bounded",s.Edges==100&&s.Entries.Length==32&&s.OmittedRows==68&&s.Entries[0].PacketCounter==68&&s.TouchChanges==99);
  var frozen=s;Record(e,Packet(100));Check("captured-rows-are-immutable-after-later-record",frozen.Entries[^1].PacketCounter==99&&frozen.Edges==100);
  long generation=s.TargetGeneration;e.Reset();s=e.Capture();Check("target-reset-clears-counters-and-old-export-rows",s.TargetGeneration==generation+1&&s.Reports==0&&s.Entries.Length==0);
  Record(e,Packet(1));p=Packet(2);p[8]=1;Record(e,p);s=e.Capture();Check("other-digital-fields-remain-in-compatible-aggregate",s.Edges==2&&s.Entries[^1].OtherFieldsNonzero);
  Check("snapshot-does-not-claim-Steam-or-USB-completion",!s.SteamConsumptionProven&&!s.TransferCompletedProven);
  GeometryChecks();
  TriggerChecks();
  SignalCoverageChecks();
  SignalAllocationChecks();
  e.Reset();p=Packet(1);for(int i=0;i<2000;i++)Record(e,p);var allocs=new long[3];var timingUs=new double[3];for(int k=0;k<3;k++){long started=System.Diagnostics.Stopwatch.GetTimestamp();long before=GC.GetAllocatedBytesForCurrentThread();for(int i=0;i<100000;i++){BinaryPrimitives.WriteUInt32LittleEndian(p.AsSpan(4),(uint)i);Record(e,p);}allocs[k]=GC.GetAllocatedBytesForCurrentThread()-before;timingUs[k]=System.Diagnostics.Stopwatch.GetElapsedTime(started).TotalMicroseconds;}
  Check("steady-state100000-records-no-per-report-heap",allocs[1]==0&&allocs[2]==0);
  var measured=e.Capture();Check("benchmark-mutates-real-observer-state",measured.Reports==302000&&measured.LastSuccessfulReport?.PacketCounter==99999);
  File.WriteAllText(args[0],JsonSerializer.Serialize(new{ok=bad==0,checks=Cases.Count,cases=Cases,allocationWindows=allocs,signalAllocationWindows=SignalAllocationWindows,timingUsFor100000Reports=timingUs,measuredReports=measured.Reports,actualSdkOrDeviceCalls=0,fieldFlickerVerified=false},new JsonSerializerOptions{WriteIndented=true}));return bad==0?0:3;
 }
}
