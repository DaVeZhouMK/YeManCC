using System.Buffers.Binary;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;
using HIDMaestro;

internal static partial class Program
{
 private const string Approved="8FE7BDB21EC497591F527B7B87C4BC7B41B45C35A06CC211A7A0DCE48838ED2C";
 private const BindingFlags Flags=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance;
 private static readonly List<object> Cases=new();private static int failures;
 private static void Check(string n,bool ok){Cases.Add(new{name=n,ok});if(!ok)failures++;}
 private static void Set(object o,string name,object? v){var f=o.GetType().GetField(name,Flags)??throw new Exception("field:"+name);f.SetValue(o,v);}
 private static object? Field(object o,string n)=>o.GetType().GetField(n,Flags)!.GetValue(o);
 private delegate bool ReadFrame(Span<byte> report,out int length);
 private static byte[] Packet(uint n,bool touch=true){var b=new byte[64];Fill(b,n,touch);return b;}
 private static void Fill(byte[] b,uint n,bool touch=true){Array.Clear(b);b[0]=1;b[2]=9;b[3]=64;BinaryPrimitives.WriteUInt32LittleEndian(b.AsSpan(4),n);if(touch){BinaryPrimitives.WriteUInt64LittleEndian(b.AsSpan(8),(1UL<<19)|(1UL<<20));BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(16),unchecked((short)(n&0x7fff)));BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(18),unchecked((short)-(int)(n&0x7fff)));BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(20),unchecked((short)(~n&0x7fff)));BinaryPrimitives.WriteInt16LittleEndian(b.AsSpan(22),unchecked((short)-(int)(~n&0x7fff)));}}
 private static bool Valid(byte[] bytes){if(bytes.Length!=64)return false;var n=BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(4));return bytes.AsSpan().SequenceEqual(Packet(n));}
 private static int Main(string[] args)
 {
  #if ROG_RECORDED_REPLAY
  if(args.Length!=2)return 2;
#else
  if(args.Length!=1)return 2;
#endif
  nint mem=0;bool freed=false;var meta=new Dictionary<string,object?>();
  try{
   var assembly=typeof(HMController).Assembly;var dll=assembly.Location;var hash=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(dll)));if(hash!=Approved)throw new Exception("sdk-hash-mismatch");
   if(assembly.ManifestModule.ModuleVersionId!=Guid.Parse("86aed9a3-c024-4596-b02c-650082ffb852"))throw new Exception("mvid-mismatch");
   if(new FileInfo(dll).Length!=63076352)throw new Exception("sdk-size-mismatch");
   meta["sdkSha256"]=hash;meta["sdkMvid"]=assembly.ManifestModule.ModuleVersionId;meta["sdkBytes"]=63076352;meta["contract"]="HIDMaestro-2.0.0-shared-input-v4";
   var shared=assembly.GetType("HIDMaestro.Internal.SharedMemoryIO",true)!;
   Check("shared-input-contract-is-v4-with362byte-layout",(int)shared.GetField("SDK_PROTOCOL",BindingFlags.Public|BindingFlags.Static)!.GetRawConstantValue()! ==4 && (int)shared.GetField("SHARED_INPUT_SIZE",BindingFlags.Public|BindingFlags.Static)!.GetRawConstantValue()! ==362);
   // Load only the embedded data model; HMContext prewarms services and is not a fixture.
   var resource=assembly.GetManifestResourceNames().Single(n=>n.EndsWith("steam-deck-composite.json",StringComparison.Ordinal));
   using var stream=assembly.GetManifestResourceStream(resource)!;using var bytes=new MemoryStream();stream.CopyTo(bytes);
   var innerType=assembly.GetType("HIDMaestro.Internal.ControllerProfile",true)!;
   var optionsType=assembly.GetType("HIDMaestro.Internal.HMLayoutJsonOptions",true)!;
   var options=(JsonSerializerOptions)optionsType.GetField("Default",BindingFlags.Public|BindingFlags.Static)!.GetValue(null)!;
   var inner=JsonSerializer.Deserialize(bytes.ToArray(),innerType,options)!;
   var profile=(HMProfile)typeof(HMProfile).GetConstructor(BindingFlags.NonPublic|BindingFlags.Instance,null,new[]{innerType},null)!.Invoke(new[]{inner});
   var builder=inner.GetType().GetMethod("GetOrBuildReportBuilder",Flags)!.Invoke(inner,null)!;
   int size=(int)builder.GetType().GetProperty("InputReportByteSize")!.GetValue(builder)!;byte reportId=(byte)builder.GetType().GetProperty("InputReportId")!.GetValue(builder)!;
   Check("actual-composite-builder-is64bytes-without-report-id",size==64&&reportId==0);
   mem=Marshal.AllocHGlobal(362);Marshal.Copy(new byte[362],0,mem,362);
   // Constructor bypass: no mappings, kernel events, sockets, devices or SDK threads.
   var controller=(HMController)RuntimeHelpers.GetUninitializedObject(typeof(HMController));
   Set(controller,"_submitLock",new object());Set(controller,"<Profile>k__BackingField",profile);Set(controller,"_reportBuilder",builder);Set(controller,"_rawReportBuffer",new byte[64]);Set(controller,"_inputView",mem);
   var devType=assembly.GetType("HIDMaestro.Internal.Usbip.UsbipEmulatedDevice",true)!;var dev=RuntimeHelpers.GetUninitializedObject(devType);Set(dev,"_inputView",mem);Set(dev,"_builder",builder);
   var readInto=(ReadFrame)devType.GetMethod("TryReadInputFrame",Flags)!.CreateDelegate(typeof(ReadFrame),dev);
   // 2.0 writes into the caller's span. Any owned arrays here belong to this
   // fixture; do not attribute the removed 1.12 allocation contract to the SDK.
   bool read(out byte[] report){var buffer=new byte[80];bool ok=readInto(buffer,out int length);report=ok?buffer.AsSpan(0,length).ToArray():Array.Empty<byte>();return ok;}
   meta["actualReadSignature"]="bool TryReadInputFrame(Span<byte>, out int)";
   Check("uninitialized-bindings-have-no-input-events",(nint)Field(controller,"_inputEvent")! ==0&&(nint)Field(controller,"_companionInputEvent")! ==0);
   Check("uninitialized-backend-has-no-connection-or-thread",Field(dev,"_connection") is null&&Field(dev,"_inputThread") is null);
   Check("initial-shared-state-does-not-generate-a-frame",!read(out _));
   var a=Packet(1);controller.SubmitRawReport(a);Array.Clear(a);Check("actual-raw-submit-copies-before-return",read(out var r)&&Valid(r)&&BinaryPrimitives.ReadUInt32LittleEndian(r.AsSpan(4))==1);
   Check("same-shared-sequence-is-not-duplicated",!read(out _));
   var old=r;controller.SubmitRawReport(Packet(2));Check("v200-next-submit-does-not-mutate-caller-owned-copy",Valid(old)&&BinaryPrimitives.ReadUInt32LittleEndian(old.AsSpan(4))==1);
   Check("v200-separate-caller-read-buffers-remain-independent",read(out r)&&Valid(r)&&!ReferenceEquals(r,old));
   // Do not consume first publish; latest-slot coalescing must retain touch, not insert neutral.
   controller.SubmitRawReport(Packet(3));controller.SubmitRawReport(Packet(4));Check("latest-slot-coalescing-keeps-current-touch",read(out r)&&Valid(r)&&BinaryPrimitives.ReadUInt32LittleEndian(r.AsSpan(4))==4);
   controller.SubmitRawReport(Packet(5));uint stable=(uint)Marshal.ReadInt32(mem);Marshal.WriteInt32(mem,0,unchecked((int)(stable|1)));Check("odd-seqlock-is-rejected-not-converted-to-neutral",!read(out _));Marshal.WriteInt32(mem,0,unchecked((int)stable));Check("same-publish-recovers-after-stable-sequence",read(out r)&&Valid(r));
   Marshal.WriteInt32(mem,0,123);Marshal.WriteInt32(mem,278,64);Marshal.Copy(Packet(7),0,mem+282,64);Marshal.WriteInt32(mem,0,124);Check("actual-extended-slot-consumes64byte-frame",read(out r)&&Valid(r)&&BinaryPrimitives.ReadUInt32LittleEndian(r.AsSpan(4))==7);
   controller.SubmitRawReport(Packet(8));Check("raw-submit-clears-stale-extended-length",Marshal.ReadInt32(mem,278)==0);Check("raw-after-extended-uses-main-slot",read(out r)&&Valid(r)&&BinaryPrimitives.ReadUInt32LittleEndian(r.AsSpan(4))==8);
   Set(controller,"_inputSeqNo",uint.MaxValue-1);Set(dev,"_lastSharedSeqNo",uint.MaxValue-1);Marshal.WriteInt32(mem,0,-2);controller.SubmitRawReport(Packet(uint.MaxValue));Check("shared-sequence-wrap-to-zero-is-readable",read(out r)&&Valid(r)&&unchecked((uint)Marshal.ReadInt32(mem))==0);controller.SubmitRawReport(Packet(0));Check("packet-counter-wrap-remains-exact",read(out r)&&Valid(r)&&BinaryPrimitives.ReadUInt32LittleEndian(r.AsSpan(4))==0);
   var reused=new byte[64];int bad=0;for(uint n=1;n<=20000;n++){Fill(reused,n);controller.SubmitRawReport(reused);Array.Clear(reused);if(!read(out r)||!Valid(r))bad++;}Check("20000-real-SDK-write-read-frames-stay-consistent",bad==0);meta["serialFrames"]=20000;meta["serialBadFrames"]=bad;
   // A finite rendezvous exercises concurrent copies; no polling worker or
   // production pump is started. Both sides finish before the block is freed.
   int readerBad=0,readerSuccess=0;uint last=0;using var rendezvous=new Barrier(2);
   var writer=Task.Run(()=>{for(uint n=30000;n<34096;n++){rendezvous.SignalAndWait();Fill(reused,n);controller.SubmitRawReport(reused);Array.Clear(reused);rendezvous.SignalAndWait();}});
   for(int k=0;k<4096;k++){rendezvous.SignalAndWait();if(read(out var frame)){if(!Valid(frame))readerBad++;last=BinaryPrimitives.ReadUInt32LittleEndian(frame.AsSpan(4));readerSuccess++;}rendezvous.SignalAndWait();}
   writer.GetAwaiter().GetResult();if(read(out var finalFrame)){if(!Valid(finalFrame))readerBad++;last=BinaryPrimitives.ReadUInt32LittleEndian(finalFrame.AsSpan(4));readerSuccess++;}
   meta["concurrentWrites"]=4096;meta["concurrentReads"]=readerSuccess;meta["concurrentBadFrames"]=readerBad;meta["lastConcurrentPacket"]=last;
   Check("actual-seqlock-concurrent-copy-does-not-return-torn-frames",readerSuccess>0&&readerBad==0);Check("final-published-active-frame-is-not-lost",last==34095);
   controller.SubmitRawReport(Packet(80000,false));Check("explicit-release-is-the-only-neutral-in-final-step",read(out r)&&r.AsSpan().SequenceEqual(Packet(80000,false)));
#if ROG_RECORDED_REPLAY
   meta["recordedHostReplay"]=RunRecordedHostReplay(controller,profile,read,args[1],Path.Combine(Path.GetDirectoryName(args[0])!,"recorded-host-output.jsonl"));
#endif
   GC.KeepAlive(controller);GC.KeepAlive(dev);
  }catch(Exception e){failures++;meta["error"]=e.ToString();}
  finally{if(mem!=0){Marshal.FreeHGlobal(mem);freed=true;}}
  Check("private-unmanaged-block-released",freed);meta["ok"]=failures==0;meta["checks"]=Cases.Count;meta["cases"]=Cases;meta["actualDriverOrDeviceCalls"]=0;meta["actualSocketsCreated"]=0;meta["actualInputEvents"]=0;meta["sdkContextConstructorCalls"]=0;meta["sdkControllerConstructorCalls"]=0;meta["sdkBackendConstructorCalls"]=0;meta["realSteamConsumerObserved"]=false;meta["pollingWorkersCreated"]=0;meta["scope"]="Actual locked 2.0.0 SDK raw write/shared-input-v4 decode into caller spans against private362B block; finite BCL rendezvous only, no controller/backend constructors, live HID, socket, InputPumpLoop or interrupt replies";File.WriteAllText(args[0],JsonSerializer.Serialize(meta,new JsonSerializerOptions{WriteIndented=true}));return failures==0?0:3;
 }
}
