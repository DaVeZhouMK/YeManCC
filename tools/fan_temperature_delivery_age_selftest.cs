// Actual RAM-backed monitor -> real backend callbacks -> fake device writer. No HC/device/Host Main.
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Reflection;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;
internal class FakeDevice { public bool IsOpen=>true; public int Writes; public double LastDuty; public bool Throw; public virtual void SetFanDuty(double value){if(Throw)throw new InvalidOperationException("fixture-live-duty-failure");Writes++;LastDuty=value;} }
internal sealed class DerivedDutyDevice:FakeDevice { public override void SetFanDuty(double value)=>base.SetFanDuty(value+1); }
internal sealed class ObjectDutyDevice { public object? Last; public void SetFanDuty(object value)=>Last=value; }
internal sealed class ReturningDutyDevice { public double Last; public int SetFanDuty(double value){Last=value;return 7;} }
internal sealed class FloatDutyDevice { public int Writes; public void SetFanDuty(float value)=>Writes++; }
internal class FakeProfile(double duty=75d) { public double Duty=duty; public int Reads; public bool Throw; public void SetTemperature(float value){} public virtual double GetFanSpeed(){Reads++;if(Throw)throw new InvalidOperationException("fixture-live-speed-failure");return Duty;} }
internal sealed class DerivedSpeedProfile(double duty):FakeProfile(duty) { public override double GetFanSpeed()=>base.GetFanSpeed()+1; }
internal sealed class IntegerSpeedProfile { public int Reads; public int GetFanSpeed(){Reads++;return Reads+20;} }
internal sealed class TextSpeedProfile { public string GetFanSpeed()=>"37"; }
internal sealed class NullSpeedProfile { public object? GetFanSpeed()=>null; }
internal class DoubleTemperatureProfile {
 public readonly List<double> Samples=[];public bool Throw;public int Reads;public Action? BeforeRead;
 public virtual void SetTemperature(double value){if(Throw)throw new InvalidOperationException("fixture-live-temperature-failure");Samples.Add(value);}
 public double GetFanSpeed(){Reads++;BeforeRead?.Invoke();return Samples[^1];}
}
internal sealed class DerivedTemperatureProfile:DoubleTemperatureProfile { public override void SetTemperature(double value)=>base.SetTemperature(value+1); }
internal sealed class FloatTemperatureProfile { public readonly List<float> Samples=[];public void SetTemperature(float value)=>Samples.Add(value); }
internal sealed class ObjectTemperatureProfile { public object? Last;public void SetTemperature(object value)=>Last=value; }
internal sealed class ReturningTemperatureProfile { public double Last;public int SetTemperature(double value){Last=value;return 7;} }
internal static class Checks {
 const BindingFlags F=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static;
 static Assembly host=null!;static readonly List<object> rows=[];static readonly List<string> failures=[];static readonly List<object> observations=[];
 static Type T(string n)=>host.GetType("YeManFanHost."+n,true)!;
 static object? Get(object o,string n)=>o.GetType().GetField(n,F)!.GetValue(o);
 static void Set(object o,string n,object? value)=>o.GetType().GetField(n,F)!.SetValue(o,value);
 static object? Call(object o,string n,params object?[] args) {try {var m=o.GetType().GetMethods(F).Single(x=>x.Name==n&&x.GetParameters().Length==args.Length);return m.Invoke(o,args);}catch(TargetInvocationException e) when(e.InnerException is not null){throw e.InnerException;}}
 static void Check(bool ok,string name){rows.Add(new{name,ok});if(!ok)failures.Add(name);Console.WriteLine((ok?"PASS ":"FAIL ")+name);}
 sealed class Producer:IDisposable {
  internal readonly string Name=@"Local\YMCC-Fan-Delivery-"+Guid.NewGuid().ToString("N");readonly MemoryMappedFile map;readonly MemoryMappedViewAccessor view;
  internal Producer(){map=MemoryMappedFile.CreateNew(Name,2048);view=map.CreateViewAccessor();view.Write(0,0x53695748U);view.Write(4,2U);view.Write(8,1U);Clock();view.Write(20,48U);view.Write(24,392U);view.Write(28,0U);view.Write(32,64U);view.Write(36,316U);view.Write(40,1U);view.Write(44,3000U);var label=new byte[128];Encoding.ASCII.GetBytes("CPU Package").CopyTo(label,0);view.WriteArray(76,label,0,128);view.Write(348,61d);}
  internal void Clock()=>view.Write(12,(ulong)DateTimeOffset.UtcNow.ToUnixTimeSeconds());
  internal void Temperature(double value)=>view.Write(348,value);
  public void Dispose(){view.Dispose();map.Dispose();}
 }
 sealed class Fixture:IDisposable {
  internal readonly Producer Producer=new();internal readonly object Backend,Source,Gate;internal readonly FakeDevice Device=new();internal readonly long OldTick;
  internal Fixture(){var options=T("HostOptions").GetMethod("Parse",F)!.Invoke(null,[Array.Empty<string>()]);Backend=Activator.CreateInstance(T("RealHcBackend"),F,null,[options],null)!;Source=Activator.CreateInstance(T("HWiNFOTemperatureMonitor"),true)!;Gate=Get(Backend,"hardwareGate")!;
   var route=T("RealHcBackend").GetMethod("ResolveRouteForSelfTest",F)!.Invoke(null,["HandheldCompanion.Devices.GPD.GPDWin5"])!;
   if(route is null)throw new InvalidOperationException("expected current registered GPD GenericDuty fixture route");
   Call(Backend,"InstallSelfTestRouteSessionForSelfTest",Device,route,true);Set(Backend,"activeFanProfile",new FakeProfile());Set(Backend,"cpuTemperatureMonitor",Source);OldTick=Environment.TickCount64-10000;Set(Backend,"lastTemperatureTick",OldTick);Set(Backend,"temperatureHealthRevision",7L);Set(Source,"lastMappingName",Producer.Name);
  }
  internal void Healthy()=>Call(Backend,"OnTemperatureMonitorSampled",Source);
  internal void Changed(float? value)=>Call(Backend,"OnTemperatureMonitorChanged",Source,value);
  internal void Callbacks(Action healthy,Action<float?> changed){Set(Source,"healthySampleReceived",healthy);Set(Source,"sampleReceived",changed);}
  internal void Poll(){Producer.Clock();Call(Source,"Poll",new object?[]{null});}
  // Component transition only: mirror the committed Generic profile/duties replacement.
  // This does not call ApplyCurveCore, authorize Open, or represent a real Host engine chain.
  internal void Profile(double duty,bool sameCurveOwner=false){lock(Gate){Set(Backend,"activeFanProfile",new FakeProfile(duty));if(!sameCurveOwner){Set(Backend,"lastTemperatureC",null);Set(Backend,"lastDutyPercent",null);Set(Backend,"activeCurveDuties",Enumerable.Repeat(duty,11).ToArray());}}}

  public void Dispose(){Set(Backend,"activeFanProfile",null);Set(Backend,"activeCurveDuties",null);Set(Backend,"cpuTemperatureMonitor",null);Backend.GetType().GetProperty("IsOpen",F)!.SetValue(Backend,false);((IDisposable)Source).Dispose();Call(Backend,"Dispose");Producer.Dispose();}
 }
 static void Delayed(bool healthyWindow){
  using var f=new Fixture();using var entered=new ManualResetEventSlim();Exception? error=null;int attempts=0;
  f.Callbacks(healthyWindow?()=>{entered.Set();f.Healthy();}:()=>{},v=>{if(!healthyWindow)entered.Set();Interlocked.Increment(ref attempts);f.Changed(v);});
  var worker=new Thread(()=>{try{f.Poll();}catch(Exception e){error=e;}}){IsBackground=true};
  Monitor.Enter(f.Gate);
  try{worker.Start();if(!entered.Wait(2000))throw new InvalidOperationException("callback did not follow a real RAM sample");if(!SpinWait.SpinUntil(()=>(worker.ThreadState&System.Threading.ThreadState.WaitSleepJoin)!=0,2000))throw new InvalidOperationException("callback did not wait at hardwareGate");Thread.Sleep(7250);}
  finally{Monitor.Exit(f.Gate);}
  if(!worker.Join(3000))throw new InvalidOperationException("callback did not settle after gate release");
  var prefix=healthyWindow?"queued healthy":"queued changed";
  Check(error is null,prefix+": actual Poll/callback completes without an exception");
  Check(f.Device.Writes==0,prefix+": sample older than existing 7s budget cannot reach the fake writer");
  Check((long)Get(f.Backend,"temperatureHealthRevision")! ==7L&&(long)Get(f.Backend,"lastTemperatureTick")! ==f.OldTick,prefix+": stale sample must not refresh health at dequeue time");
  Check(Get(f.Source,"lastPublishedTemperature") is null,prefix+": rejected sample cannot consume unchanged-temperature suppression");
  if(healthyWindow){Call(f.Backend,"TemperatureWatchdogTick");Check(Get(f.Backend,"cpuTemperatureMonitor") is null&&Get(f.Backend,"temperatureStaleSignaled") is true,"queued healthy: expired source still reaches the original stale watchdog path");}
  else {f.Poll();Check(f.Device.Writes==1&&attempts==2,"queued changed: next fresh same-value sample is delivered exactly once");}
 }
 static void Fresh(){using var f=new Fixture();f.Callbacks(f.Healthy,f.Changed);f.Poll();long revision=(long)Get(f.Backend,"temperatureHealthRevision")!;Check(f.Device.Writes==1,"fresh source: actual backend callback still reaches one fake duty write");f.Poll();Check(f.Device.Writes==1&&(long)Get(f.Backend,"temperatureHealthRevision")!>revision,"fresh source: same temperature refreshes health without a duplicate write");
  var now=Environment.TickCount64;long newer=now-50;Set(f.Backend,"lastTemperatureTick",newer);f.Source.GetType().GetField("sampleReadTick",F)?.SetValue(f.Source,now-100);f.Healthy();
  Check((long)Get(f.Backend,"lastTemperatureTick")!>=newer,"fresh source: an older accepted sample cannot roll back a newer health tick");}
 static void NewProfile(){
  foreach(var duty in new[]{25d,75d}){
   using var f=new Fixture();f.Callbacks(f.Healthy,f.Changed);f.Poll();var n=f.Device.Writes;
   Check(n==1&&f.Device.LastDuty==75d,"profile "+duty+": initial fresh sample reaches the original profile");
   f.Profile(duty);Check(f.Device.Writes==n,"profile "+duty+": profile transition itself is not counted as a duty callback");
   f.Poll();Check(f.Device.Writes==n+1&&f.Device.LastDuty==duty,"profile "+duty+": same temperature must dispatch the newly committed profile once");
   var target=Call(f.Backend,"MonitorTargetForRead");Check(target is not null&&(double)target.GetType().GetProperty("Percent",F)!.GetValue(target)! == duty,"profile "+duty+": current owner gets the new target receipt");
   var settled=f.Device.Writes;f.Poll();Check(f.Device.Writes==settled,"profile "+duty+": unchanged temperature and profile must not repeat duty writes");
  }
 }
 static void ProfileAfterHealthy(){using var f=new Fixture();f.Callbacks(f.Healthy,f.Changed);f.Poll();bool change=true;
  f.Callbacks(()=>{f.Healthy();if(change){change=false;f.Profile(25d);}},f.Changed);f.Poll();f.Poll();
  Check(f.Device.Writes==2&&f.Device.LastDuty==25d,"profile after healthy: the following regular sample reaches the new profile without a second manual request");var settled=f.Device.Writes;f.Poll();Check(f.Device.Writes==settled,"profile after healthy: settled next sample has no extra duty write");
 }
 static void ProfileDuringChanged(){using var f=new Fixture();f.Callbacks(f.Healthy,f.Changed);f.Poll();f.Producer.Temperature(65d);using var entered=new ManualResetEventSlim();Exception? error=null;
  f.Callbacks(()=>{},v=>{entered.Set();f.Changed(v);});var worker=new Thread(()=>{try{f.Poll();}catch(Exception e){error=e;}}){IsBackground=true};
  Monitor.Enter(f.Gate);try{worker.Start();if(!entered.Wait(2000))throw new InvalidOperationException("changed callback did not reach the controlled gate");f.Profile(25d);}finally{Monitor.Exit(f.Gate);}
  Check(worker.Join(3000)&&error is null,"profile during changed: queued callback completes on the current real gate");
  Check(f.Device.Writes==2&&f.Device.LastDuty==25d,"profile during changed: the callback uses the latest profile");f.Callbacks(f.Healthy,f.Changed);f.Poll();Check(f.Device.Writes==2,"profile during changed: already-delivered current profile is not forced a second time");
 }
 static void ProfileReplaySameOwner(){using var f=new Fixture();f.Callbacks(f.Healthy,f.Changed);f.Poll();var oldTarget=Call(f.Backend,"MonitorTargetForRead")!;long oldAt=DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()-1000;
  oldTarget.GetType().GetField("SampledAtStorage",F)!.SetValue(oldTarget,oldAt);f.Profile(25d,sameCurveOwner:true);
  f.Callbacks(()=>{f.Healthy();Check((long)oldTarget.GetType().GetProperty("SampledAt",F)!.GetValue(oldTarget)! == oldAt,"profile replay same owner: healthy phase must not relabel the old profile target as fresh");},f.Changed);
  f.Poll();var current=Call(f.Backend,"MonitorTargetForRead");Check(f.Device.Writes==2&&f.Device.LastDuty==25d&&current is not null&&(double)current.GetType().GetProperty("Percent",F)!.GetValue(current)! ==25d,"profile replay same owner: new profile and target are delivered at the unchanged temperature");
  var settled=f.Device.Writes;f.Callbacks(f.Healthy,f.Changed);f.Poll();Check(f.Device.Writes==settled,"profile replay same owner: next steady sample has no duplicate duty write");
 }
 static int? SpeedReflectionCount()=>T("RealHcBackend").GetField("FanSpeedReflectionCallsForSelfTest",F)?.GetValue(null) as int?;
 static double Speed(object backend,object profile){var helper=T("RealHcBackend").GetMethod("ReadFanProfileSpeed",F);if(helper is not null)return (double)Call(backend,"ReadFanProfileSpeed",profile)!;var inv=T("RealHcBackend").GetMethods(F).Single(m=>m.Name=="Invoke"&&m.IsStatic&&m.GetParameters().Length==3);try{return Convert.ToDouble(inv.Invoke(null,[profile,"GetFanSpeed",Array.Empty<object?>()]));}catch(TargetInvocationException e)when(e.InnerException is not null){throw e.InnerException;}}
 static void BoundSpeedPipeline(){using var f=new Fixture();var profile=new FakeProfile();Set(f.Backend,"activeFanProfile",profile);f.Callbacks(f.Healthy,f.Changed);var before=SpeedReflectionCount();
  for(var i=0;i<6;i++){profile.Duty=20+i;f.Producer.Temperature(50+i);f.Poll();Check(f.Device.LastDuty==20+i,"bound speed: every changed temperature uses the current profile result "+i);}
  Check(profile.Reads==6&&f.Device.Writes==6,"bound speed: actual HC profile/device callback counts stay six, no cached return value");
  var after=SpeedReflectionCount();if(before.HasValue&&after.HasValue){observations.Add(new{scenario="six-changing-temperature-samples",getFanSpeedReflectionCalls=after-before,profileCalls=profile.Reads,dutyWrites=f.Device.Writes});Check(after-before==1,"bound speed: a live exact-double profile uses reflection only for its first result");}
  var idleWrites=f.Device.Writes;f.Poll();Check(f.Device.Writes==idleWrites&&profile.Reads==6,"bound speed: the unchanged sample still avoids duplicate HC work");
 }
 static void BoundSpeedIdentityAndFallback(){using var f=new Fixture();var a=new FakeProfile(21);var b=new FakeProfile(62);Check(Speed(f.Backend,a)==21,"bound speed identity: initial target");a.Duty=31;Check(Speed(f.Backend,a)==31,"bound speed identity: live changed value");Check(Speed(f.Backend,b)==62&&Speed(f.Backend,a)==31,"bound speed identity: exact instance changes cannot borrow another profile result");var derived=new DerivedSpeedProfile(40);Check(Speed(f.Backend,derived)==41,"bound speed identity: selected virtual override still dispatches");
  var integer=new IntegerSpeedProfile();Check(Speed(f.Backend,integer)==21&&Speed(f.Backend,integer)==22&&integer.Reads==2,"bound speed fallback: int results remain live Convert.ToDouble calls");Check(Speed(f.Backend,new TextSpeedProfile())==37&&Speed(f.Backend,new NullSpeedProfile())==0,"bound speed fallback: string and null retain original conversion");
  Check(Speed(f.Backend,a)==31,"bound speed exception: bind a successful original profile");a.Throw=true;for(var i=0;i<2;i++){var failure=false;try{_=Speed(f.Backend,a);}catch(TargetInvocationException ex){failure=ex.InnerException is InvalidOperationException e&&e.Message=="fixture-live-speed-failure";}Check(failure,"bound speed exception: live invocation wrapper retained "+i);}a.Throw=false;a.Duty=48;Check(Speed(f.Backend,a)==48,"bound speed exception: no cached fault suppresses later current result");
  var owner=T("RealHcBackend").GetField("fanSpeedReaderProfile",F);var reader=T("RealHcBackend").GetField("fanSpeedReader",F);Check(owner is null&&reader is null || owner is not null&&reader is not null&&ReferenceEquals(owner.GetValue(f.Backend),a)&&reader.GetValue(f.Backend) is Delegate,"bound speed ownership: any existing cache belongs to the exact profile");Call(f.Backend,"DetachCpuTemperatureLoop");Check(owner?.GetValue(f.Backend) is null&&reader?.GetValue(f.Backend) is null,"bound speed ownership: detach retains no profile or bound reader");
 }
 static int? TemperatureReflectionCount()=>T("RealHcBackend").GetField("FanTemperatureReflectionCallsForSelfTest",F)?.GetValue(null) is int count?count:null;
 static void Temperature(object backend,object profile,float value){var helper=T("RealHcBackend").GetMethod("SetFanProfileTemperature",F);if(helper is not null){Call(backend,"SetFanProfileTemperature",profile,value);return;}var inv=T("RealHcBackend").GetMethods(F).Single(m=>m.Name=="Invoke"&&m.IsStatic&&m.GetParameters().Length==3);try{inv.Invoke(null,[profile,"SetTemperature",new object?[]{value}]);}catch(TargetInvocationException e)when(e.InnerException is not null){throw e.InnerException;}}
 static void BoundTemperaturePipeline(){using var f=new Fixture();var profile=new DoubleTemperatureProfile();Set(f.Backend,"activeFanProfile",profile);f.Callbacks(f.Healthy,f.Changed);var before=TemperatureReflectionCount();
  for(var i=0;i<6;i++){var temp=50.1+i;f.Producer.Temperature(temp);f.Poll();Check(f.Device.LastDuty==(double)(float)temp&&profile.Samples[^1]==(double)(float)temp,"bound temperature: real callback preserves float-to-double widening and current sample "+i);}
  Check(profile.Samples.Count==6&&profile.Reads==6&&f.Device.Writes==6,"bound temperature: six samples still update smoothing/read current duty/write device six times");
  var after=TemperatureReflectionCount();if(before.HasValue&&after.HasValue){observations.Add(new{scenario="six-changing-temperature-setter-samples",setTemperatureReflectionCalls=after-before,temperatureCalls=profile.Samples.Count,speedReads=profile.Reads,dutyWrites=f.Device.Writes});Check(after-before==1,"bound temperature: eligible setter reflects only its first successful sample");}
  f.Poll();Check(profile.Samples.Count==6&&profile.Reads==6&&f.Device.Writes==6,"bound temperature: unchanged sample still avoids duplicate work");
  var next=new DoubleTemperatureProfile();Set(f.Backend,"activeFanProfile",next);f.Poll();Check(next.Samples.Count==1&&next.Samples[0]==(double)(float)55.1&&f.Device.Writes==7,"bound temperature: replacement profile at unchanged temperature receives its own first sample");
 }
 static void BoundTemperatureIdentityAndFallback(){using var f=new Fixture();var a=new DoubleTemperatureProfile();var b=new DoubleTemperatureProfile();Temperature(f.Backend,a,41.1f);Temperature(f.Backend,a,42.2f);Temperature(f.Backend,b,61.3f);Temperature(f.Backend,a,43.4f);Check(a.Samples.SequenceEqual(new double[]{41.1f,42.2f,43.4f})&&b.Samples.SequenceEqual(new double[]{61.3f}),"bound temperature: exact instance changes and return to previous instance retain sample ownership");
  var derived=new DerivedTemperatureProfile();Temperature(f.Backend,derived,31.1f);Temperature(f.Backend,derived,32.2f);Check(derived.Samples.SequenceEqual(new double[]{(double)31.1f+1,(double)32.2f+1}),"bound temperature: selected virtual override runs for initial and bound calls");
  var floating=new FloatTemperatureProfile();Temperature(f.Backend,floating,21.1f);Temperature(f.Backend,floating,22.2f);Check(floating.Samples.SequenceEqual(new[]{21.1f,22.2f}),"bound temperature fallback: float signature keeps original values");
  var boxed=new ObjectTemperatureProfile();Temperature(f.Backend,boxed,23.3f);Temperature(f.Backend,boxed,24.4f);Check(boxed.Last is float v&&v==24.4f,"bound temperature fallback: object signature still receives boxed Single");
  var returning=new ReturningTemperatureProfile();Temperature(f.Backend,returning,25.5f);Temperature(f.Backend,returning,26.6f);Check(returning.Last==(double)26.6f,"bound temperature fallback: nonvoid double signature preserves ignored return");
  var early=new DoubleTemperatureProfile{Throw=true};for(var i=0;i<2;i++){var failure=false;try{Temperature(f.Backend,early,27.7f);}catch(TargetInvocationException ex){failure=ex.InnerException is InvalidOperationException e&&e.Message=="fixture-live-temperature-failure";}Check(failure,"bound temperature: first-call failures preserve reflection exception wrapper "+i);}early.Throw=false;Temperature(f.Backend,early,28.8f);Check(early.Samples.SequenceEqual(new double[]{28.8f}),"bound temperature: failed first calls never cache a result or fault");
  Temperature(f.Backend,a,44.4f);a.Throw=true;for(var i=0;i<2;i++){var failure=false;try{Temperature(f.Backend,a,45.5f);}catch(TargetInvocationException ex){failure=ex.InnerException is InvalidOperationException e&&e.Message=="fixture-live-temperature-failure";}Check(failure,"bound temperature: bound live failures preserve reflection exception wrapper "+i);}a.Throw=false;Temperature(f.Backend,a,46.6f);Check(a.Samples[^1]==(double)46.6f,"bound temperature: later live call recovers without cached fault");
  var special=new DoubleTemperatureProfile();foreach(var value in new[]{float.NaN,float.PositiveInfinity,float.NegativeInfinity,-1f,101f})Temperature(f.Backend,special,value);Check(special.Samples.Count==5&&double.IsNaN(special.Samples[0])&&double.IsPositiveInfinity(special.Samples[1])&&double.IsNegativeInfinity(special.Samples[2])&&special.Samples[3]==-1&&special.Samples[4]==101,"bound temperature: dispatch does not introduce filtering before HC owns validation");
  var owner=T("RealHcBackend").GetField("fanTemperatureWriterProfile",F);var writer=T("RealHcBackend").GetField("fanTemperatureWriter",F);Check(owner is null&&writer is null||owner is not null&&writer is not null&&ReferenceEquals(owner.GetValue(f.Backend),special)&&writer.GetValue(f.Backend) is Delegate,"bound temperature ownership: any binding belongs to exact current profile");Call(f.Backend,"DetachCpuTemperatureLoop");Check(owner?.GetValue(f.Backend) is null&&writer?.GetValue(f.Backend) is null,"bound temperature ownership: detach releases writer and target");
 }
 static int? DutyReflectionCount()=>T("RealHcBackend").GetField("FanDutyReflectionCallsForSelfTest",F)?.GetValue(null) is int count?count:null;
 static void Duty(object backend,object? device,double value){Set(backend,"device",device);var helper=T("RealHcBackend").GetMethod("WriteFanProfileDuty",F);if(helper is not null){Call(backend,"WriteFanProfileDuty",value);return;}if(device is null)throw new InvalidOperationException("HC device unavailable");var inv=T("RealHcBackend").GetMethods(F).Single(m=>m.Name=="Invoke"&&m.IsStatic&&m.GetParameters().Length==3);try{inv.Invoke(null,[device,"SetFanDuty",new object?[]{value}]);}catch(TargetInvocationException e)when(e.InnerException is not null){throw e.InnerException;}}
 static void BoundDutyPipeline(){using var f=new Fixture();var profile=new DoubleTemperatureProfile();Set(f.Backend,"activeFanProfile",profile);f.Callbacks(f.Healthy,f.Changed);var before=DutyReflectionCount();
  for(var i=0;i<6;i++){var temp=50.1+i;f.Producer.Temperature(temp);f.Poll();Check(f.Device.Writes==i+1&&f.Device.LastDuty==(double)(float)temp,"bound duty: every actual fresh sample reaches current device with exact current result "+i);}
  Check(profile.Samples.Count==6&&profile.Reads==6&&f.Device.Writes==6,"bound duty: temperature/speed/device sequence counts remain six");var after=DutyReflectionCount();if(before.HasValue&&after.HasValue){observations.Add(new{scenario="six-changing-temperature-duty-samples",setFanDutyReflectionCalls=after-before,temperatureCalls=profile.Samples.Count,speedReads=profile.Reads,dutyWrites=f.Device.Writes});Check(after-before==1,"bound duty: eligible device reflects only its first successful dispatch");}
  f.Poll();Check(f.Device.Writes==6,"bound duty: unchanged temperature does not add writes");
  Set(f.Backend,"closeWriteBlocked",1);f.Producer.Temperature(70.1);f.Poll();Check(f.Device.Writes==6&&profile.Samples.Count==6,"bound duty: real close write fence still stops temperature/profile/device work");Set(f.Backend,"closeWriteBlocked",0);f.Producer.Temperature(71.1);f.Poll();Check(f.Device.Writes==7&&f.Device.LastDuty==(double)71.1f,"bound duty: next changed fresh sample still writes when fence is clear");
 }
 static void BoundDutyChangedBoundary(){foreach(var boundary in new[]{"suspend","close","curve-owner"}){using var f=new Fixture();var profile=new DoubleTemperatureProfile();Set(f.Backend,"activeFanProfile",profile);f.Callbacks(f.Healthy,f.Changed);f.Poll();profile.BeforeRead=()=>{if(boundary=="curve-owner")Set(f.Backend,"activeCurveDuties",new[]{99d});else Call(f.Backend,boundary=="close"?"BlockWritesForClose":"BlockWritesForSuspend");};f.Producer.Temperature(72.25);f.Poll();Check(profile.Samples.Count==2&&profile.Reads==2&&f.Device.Writes==1,"bound duty: boundary changing inside live calculation stops a previously bound writer: "+boundary);Check(Call(f.Backend,"MonitorTargetForRead") is null,"bound duty: retired boundary cannot publish current target: "+boundary);}}
 static void BoundDutyIdentityAndFallback(){using var f=new Fixture();var a=new FakeDevice();var b=new FakeDevice();Duty(f.Backend,a,21.25);Duty(f.Backend,a,22.5);Duty(f.Backend,b,61.75);Duty(f.Backend,a,23.125);Check(a.Writes==3&&a.LastDuty==23.125&&b.Writes==1&&b.LastDuty==61.75,"bound duty: devices of the same type and return to previous device never share target writes");
  var derived=new DerivedDutyDevice();Duty(f.Backend,derived,31.25);Duty(f.Backend,derived,32.5);Check(derived.Writes==2&&derived.LastDuty==33.5,"bound duty: selected virtual override executes on both calls");
  var boxed=new ObjectDutyDevice();Duty(f.Backend,boxed,41.25);Duty(f.Backend,boxed,42.5);Check(boxed.Last is double d&&d==42.5,"bound duty fallback: object signature receives boxed Double");var returning=new ReturningDutyDevice();Duty(f.Backend,returning,43.25);Duty(f.Backend,returning,44.5);Check(returning.Last==44.5,"bound duty fallback: nonvoid double signature retains ignored return");
  var narrowed=new FloatDutyDevice();var narrowFailure=false;try{Duty(f.Backend,narrowed,45.25);}catch(ArgumentException){narrowFailure=true;}Check(narrowFailure&&narrowed.Writes==0,"bound duty fallback: original binder still rejects narrowing Double to Single");
  var early=new FakeDevice{Throw=true};for(var i=0;i<2;i++){var failed=false;try{Duty(f.Backend,early,46.25);}catch(TargetInvocationException ex){failed=ex.InnerException is InvalidOperationException e&&e.Message=="fixture-live-duty-failure";}Check(failed,"bound duty: first-call target failure retains wrapper "+i);}early.Throw=false;Duty(f.Backend,early,47.5);Check(early.Writes==1&&early.LastDuty==47.5,"bound duty: first-call failures do not cache fault");
  Duty(f.Backend,a,24.5);a.Throw=true;for(var i=0;i<2;i++){var failed=false;try{Duty(f.Backend,a,25.5);}catch(TargetInvocationException ex){failed=ex.InnerException is InvalidOperationException e&&e.Message=="fixture-live-duty-failure";}Check(failed,"bound duty: bound target failure retains wrapper "+i);}a.Throw=false;Duty(f.Backend,a,26.5);Check(a.LastDuty==26.5,"bound duty: current call recovers from target error");
  var count=a.Writes;var missing=false;try{Duty(f.Backend,null,99);}catch(InvalidOperationException e){missing=e.Message=="HC device unavailable";}Check(missing&&a.Writes==count,"bound duty: null current device cannot write through previous binding");
  Duty(f.Backend,a,27.5);var owner=T("RealHcBackend").GetField("fanDutyWriterDevice",F);var writer=T("RealHcBackend").GetField("fanDutyWriter",F);Check(owner is null&&writer is null||owner is not null&&writer is not null&&ReferenceEquals(owner.GetValue(f.Backend),a)&&writer.GetValue(f.Backend) is Delegate,"bound duty ownership: current exact device owns optional binding");Call(f.Backend,"DetachCpuTemperatureLoop");Check(owner?.GetValue(f.Backend) is null&&writer?.GetValue(f.Backend) is null,"bound duty ownership: detach retains no device or writer");Set(f.Backend,"device",f.Device);
 }
 public static int Main(string[] args){try{var dll=Path.GetFullPath(args[0]);var resolver=new AssemblyDependencyResolver(dll);AssemblyLoadContext.Default.Resolving+=(_,n)=>resolver.ResolveAssemblyToPath(n) is { } p?AssemblyLoadContext.Default.LoadFromAssemblyPath(p):null;host=AssemblyLoadContext.Default.LoadFromAssemblyPath(dll);T("Program").GetProperty("IsSelfTestProcess",F)!.SetValue(null,true);var dutyOnly=Environment.GetEnvironmentVariable("YMCC_FAN_DUTY_BINDING_ONLY")=="1";var temperatureOnly=Environment.GetEnvironmentVariable("YMCC_FAN_TEMPERATURE_BINDING_ONLY")=="1";var bindingOnly=Environment.GetEnvironmentVariable("YMCC_FAN_SPEED_BINDING_ONLY")=="1";var requireProbe=Environment.GetEnvironmentVariable("YMCC_FAN_SPEED_REFLECTION_PROBE")=="1";Check(requireProbe?SpeedReflectionCount().HasValue:!SpeedReflectionCount().HasValue,"reflection witness is available only in explicit scratch-probe mode");if(!bindingOnly&&!temperatureOnly&&!dutyOnly){Delayed(true);Delayed(false);Fresh();NewProfile();ProfileAfterHealthy();ProfileDuringChanged();ProfileReplaySameOwner();}if(!temperatureOnly&&!dutyOnly){BoundSpeedPipeline();BoundSpeedIdentityAndFallback();}if(!dutyOnly){BoundTemperaturePipeline();BoundTemperatureIdentityAndFallback();}BoundDutyPipeline();BoundDutyChangedBoundary();BoundDutyIdentityAndFallback();Check(requireProbe?TemperatureReflectionCount().HasValue:!TemperatureReflectionCount().HasValue,"temperature reflection witness is available only in explicit scratch-probe mode");Check(requireProbe?DutyReflectionCount().HasValue:!DutyReflectionCount().HasValue,"duty reflection witness is available only in explicit scratch-probe mode");Check(!AppDomain.CurrentDomain.GetAssemblies().Any(a=>a.GetName().Name=="HandheldCompanion"),"HC assembly never loaded");var result=new{ok=failures.Count==0,checks=rows.Count,failures,results=rows,observations,reflectionWitnessAvailable=SpeedReflectionCount().HasValue,temperatureReflectionWitnessAvailable=TemperatureReflectionCount().HasValue,bindingOnly,temperatureOnly,dutyOnly,scope="Actual compiled Poll/private RAM/real hardwareGate/backend callbacks/fake device/profile; local test thread delay only; no real Host Main/HC/hardware/OS sleep/CPU benchmark"};var json=JsonSerializer.Serialize(result);File.WriteAllText(args[1],json);Console.WriteLine(json);return failures.Count==0?0:1;}catch(Exception e){Console.Error.WriteLine(e);return 2;}}
}
