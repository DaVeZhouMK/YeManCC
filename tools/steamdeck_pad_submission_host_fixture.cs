using HIDMaestro;
using System.Text.Json;
using System.Buffers.Binary;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using YeManInputHost.Diagnostics;
internal static partial class Program
{
 private static bool RunPadSubmissionOfflineIntegration(string path)
 {
  var cases=new List<object>();bool all=true;void Check(string name,bool ok){cases.Add(new{name,ok});all&=ok;}
  nint mem=Marshal.AllocHGlobal(362);
  try
  {
   Marshal.Copy(new byte[362],0,mem,362);using var ctx=new HMContext();ctx.LoadDefaultProfiles();var profile=ctx.GetProfile("steam-deck-composite")!;
   var flags=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance;
   var inner=typeof(HMProfile).GetFields(flags).Single(f=>f.FieldType.FullName=="HIDMaestro.Internal.ControllerProfile").GetValue(profile)!;
   var builder=inner.GetType().GetMethod("GetOrBuildReportBuilder",flags)!.Invoke(inner,null)!;
   var controller=(HMController)RuntimeHelpers.GetUninitializedObject(typeof(HMController));
   void Set(string n,object? value)=>typeof(HMController).GetField(n,flags)!.SetValue(controller,value);
   Set("<Profile>k__BackingField",profile);Set("_reportBuilder",builder);Set("_rawReportBuffer",new byte[64]);Set("_inputView",mem);
   using var cts=new CancellationTokenSource();Set("_idleCts",cts);var prior=new byte[64];BinaryPrimitives.WriteUInt32LittleEndian(prior.AsSpan(4),100);Set("_extendedReportBuffer",prior);
   _screenPadOwnsReport=false;_screenPadPacketCounter=0;ScreenPadSubmissionEvidence.Reset();
   var seed=new CommandEnvelope("SUBMIT_FRAME","offline",1,"offline",1,1,"target","owner","steamdeck","steam-deck-composite","nonce",1,LockedCoreSha256);
   var left=new ScreenPad(true,false,1234,-2345,0);var input=seed with{ScreenPadsPresent=true,ScreenPadLeft=left,SampleSequence=1,MonotonicTick=1000};
   SubmitSteamDeckWithScreenPads(controller,profile,input,true);var snap=ScreenPadSubmissionEvidence.Capture();var raw=new byte[64];Marshal.Copy(mem+8,raw,0,64);
   Check("raw-ownership-stops-idle-token-before-first-report",_screenPadOwnsReport&&cts.IsCancellationRequested);
   Check("actual-submission-counter-follows-prior-codec-counter",BinaryPrimitives.ReadUInt32LittleEndian(raw.AsSpan(4))==101&&snap.LastSuccessfulReport?.PacketCounter==101);
   Check("production-return-observer-matches-encoded-contact",snap.Reports==1&&snap.TouchMismatch==0&&snap.CoordinateMismatch==0&&snap.PressureMismatch==0&&snap.LastSuccessfulReport?.LeftX==1234);
   Check("observation-does-not-change-actual-SDK-written-payload",raw.AsSpan().SequenceEqual(BuildSteamDeckScreenPadReport(profile,input,101)));
   for(ulong n=2;n<=1000;n++){var pad=new ScreenPad(true,false,(short)n,(short)-(int)n,0);input=seed with{Sequence=n,SampleSequence=n,MonotonicTick=n*8,ScreenPadsPresent=true,ScreenPadLeft=pad};SubmitSteamDeckWithScreenPads(controller,profile,input,true);}
   snap=ScreenPadSubmissionEvidence.Capture();Check("1000-actual-raw-returns-keep-one-touch-edge",snap.Reports==1000&&snap.Edges==1&&snap.CounterDiscontinuities==0&&snap.LastSuccessfulReport?.LeftX==1000);
   SubmitSteamDeckWithScreenPads(controller,profile,seed with{Sequence=1001,SampleSequence=1001},true,isNeutralCommand:true);
   SubmitSteamDeckWithScreenPads(controller,profile,input with{Sequence=1002,SampleSequence=1002},true);
   snap=ScreenPadSubmissionEvidence.Capture();Check("real-submit-short-neutral-and-recontact-are-both-retained",snap.TouchChanges==2&&snap.Entries.Any(r=>r.TouchMask==0&&r.NeutralCommand)&&snap.Entries[^1].TouchMask==1);
   ulong reports=snap.Reports;SubmitSteamDeckWithScreenPads(controller,profile,input with{Sequence=1003},false);snap=ScreenPadSubmissionEvidence.Capture();Marshal.Copy(mem+8,raw,0,64);
   Check("disabled-observation-does-not-suppress-or-alter-SDK-submit",snap.Reports==reports&&snap.LastSuccessfulReport is null&&BinaryPrimitives.ReadUInt32LittleEndian(raw.AsSpan(4))==1103);
   Check("encoder-still-matches-payload-when-evidence-disabled",raw.AsSpan().SequenceEqual(BuildSteamDeckScreenPadReport(profile,input,1103)));
   bool codec=RunSteamDeckScreenPadSelfTest();Check("existing512-codec-idle-handoff-suite-preserved",codec);
  }
  finally{Marshal.FreeHGlobal(mem);}
  File.WriteAllText(path,JsonSerializer.Serialize(new{ok=all,checks=cases.Count,cases,actualControllerConstructorCalls=0,actualDevicesCreated=0,actualSocketsCreated=0,actualInputEvents=0,scope="Frozen fullHost actual submit function + lockedSDK write to private362B block; no target/pipe/driver/socket/Steam startup"},new JsonSerializerOptions{WriteIndented=true}));return all;
 }
}
