using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text.Json;
static class PrimaryController {
@@HC_ENUM@@
 public static bool pumpThreadRunning=false;
 public static ControllerSlotManagementMode slotManagementMode=ControllerSlotManagementMode.Manual;
}
enum AlternateMode { Automatic=0,Manual=7 }
static class AlternateController {public static bool pumpThreadRunning=false;public static AlternateMode slotManagementMode=AlternateMode.Automatic;}
enum IncompleteMode { Automatic=1 }
static class IncompleteController {public static bool pumpThreadRunning=false;public static IncompleteMode slotManagementMode=IncompleteMode.Automatic;}
static class NonEnumController {public static bool pumpThreadRunning=false;public static int slotManagementMode=0;}
static class NoSlotController {public static bool pumpThreadRunning=false;}
sealed record Receipt(string[] Stopped,string? Failure);
sealed class Maintenance {
@@CACHE@@
 public static int Parses,ModeReads,ModeWrites,PumpReads,PumpWrites;
 private Type? selected;
 public static void Reset(){Parses=ModeReads=ModeWrites=PumpReads=PumpWrites=0;@@CLEAR@@}
 private Type? FindType(string name)=>selected;
 private static object? GetStaticMember(Type type,string name){PumpReads++;return type.GetField(name)?.GetValue(null);}
 private static void SetStaticMember(Type type,string name,object value){PumpWrites++;type.GetField(name)!.SetValue(null,value);}
 private static FieldInfo? FindDeclaredStaticFieldMetadata(Type? type,string name)=>type?.GetField(name,BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Static);
 private static object FixtureEnumParse(Type type,string name){Parses++;return Enum.Parse(type,name);}
 private static object? FixtureModeRead(FieldInfo field){ModeReads++;return field.GetValue(null);}
 private static void FixtureModeWrite(FieldInfo field,object value){ModeWrites++;field.SetValue(null,value);}
 public Receipt Run(Type? controller){selected=controller;var stopped=new List<string>();string? failure=null;
  try {
@@ISLAND@@
  }catch(Exception ex){failure=ex.GetType().Name;}
  return new(stopped.ToArray(),failure);
 }
}
static class Program {
 static readonly List<object> Results=new();static int failures;
 static void Check(string name,bool ok){Results.Add(new{name,ok});if(!ok)failures++;}
 static void Main(){
  var m=new Maintenance();Maintenance.Reset();PrimaryController.slotManagementMode=PrimaryController.ControllerSlotManagementMode.Manual;PrimaryController.pumpThreadRunning=false;
  for(int i=0;i<100;i++){var r=m.Run(typeof(PrimaryController));Check("idle-maintenance-"+i,r.Failure is null&&r.Stopped.Length==0);}
  var idleParses=Maintenance.Parses;
  Check("constant-enum-parsed-once",idleParses==1);Check("live-mode-still-read-every-call",Maintenance.ModeReads==100);Check("live-pump-still-read-every-call",Maintenance.PumpReads==100);Check("idle-does-not-write",Maintenance.ModeWrites==0&&Maintenance.PumpWrites==0);
  Maintenance.Reset();
  for(int i=0;i<12;i++){
   PrimaryController.slotManagementMode=PrimaryController.ControllerSlotManagementMode.Automatic;PrimaryController.pumpThreadRunning=true;
   var r=m.Run(typeof(PrimaryController));Check("externally-restarted-loops-"+i,r.Failure is null&&r.Stopped.SequenceEqual(new[]{"sdl-hotplug-pump","slot-management-manual"})&&PrimaryController.slotManagementMode==PrimaryController.ControllerSlotManagementMode.Manual&&!PrimaryController.pumpThreadRunning);
  }
  Check("external-mode-always-reread",Maintenance.ModeReads==12&&Maintenance.ModeWrites==12);Check("external-pump-always-reread",Maintenance.PumpReads==12&&Maintenance.PumpWrites==12);Check("restart-does-not-reparse-constant",Maintenance.Parses==1);
  Maintenance.Reset();AlternateController.slotManagementMode=AlternateMode.Automatic;AlternateController.pumpThreadRunning=true;PrimaryController.slotManagementMode=PrimaryController.ControllerSlotManagementMode.Automatic;
  var first=m.Run(typeof(PrimaryController));var other=m.Run(typeof(AlternateController));
  Check("manual-value-is-type-specific",other.Failure is null&&AlternateController.slotManagementMode==AlternateMode.Manual);Check("old-type-still-correct",first.Failure is null&&PrimaryController.slotManagementMode==PrimaryController.ControllerSlotManagementMode.Manual);Check("one-parse-per-actual-enum-type",Maintenance.Parses==2);
  Maintenance.Reset();Check("missing-controller-unchanged",m.Run(null).Failure is null&&Maintenance.Parses==0);Check("missing-slot-unchanged",m.Run(typeof(NoSlotController)).Failure is null&&Maintenance.Parses==0);Check("nonenum-slot-unchanged",m.Run(typeof(NonEnumController)).Failure is null&&Maintenance.Parses==0);
  Maintenance.Reset();var bad1=m.Run(typeof(IncompleteController));var bad2=m.Run(typeof(IncompleteController));Check("missing-manual-still-fails",bad1.Failure=="ArgumentException"&&bad2.Failure=="ArgumentException");Check("parse-failure-is-not-cached-as-success",Maintenance.Parses==2&&Maintenance.ModeWrites==0);
  Console.WriteLine(JsonSerializer.Serialize(new{ok=failures==0,checks=Results.Count,results=Results,idleConstantParses=idleParses,scope="exact production ControllerManager maintenance island and immutable cache; actual reflection over inert fields, no HC/native/device/Host process or benchmark"}));Environment.ExitCode=failures==0?0:1;
 }
}
