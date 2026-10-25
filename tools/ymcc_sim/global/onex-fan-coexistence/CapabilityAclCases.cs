using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using System.Text.Json;
using Mono.Cecil;
using Mono.Cecil.Cil;
using AclPorts=ConsumerFixture.CapabilityAclPorts;

static partial class Harness
{
    [DllImport("advapi32.dll",EntryPoint="SetFileSecurityW",CharSet=CharSet.Unicode,SetLastError=true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    static extern bool RestoreOwnedFixtureDaclNative(string path,uint information,byte[] descriptor);
    static void RestoreOwnedFixtureDacl(string path,FileSystemSecurity original,string localRoot)
    {
        Check(Path.GetFullPath(path).StartsWith(Path.GetFullPath(localRoot)+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase),"DACL restoration remains inside the exact newly created fixture root");
        Check((File.GetAttributes(path)&FileAttributes.ReparsePoint)==0,"Fixture DACL restoration refuses reparse points");
        // Set only this owned fixture's DACL. Named managed setters may read
        // READ_CONTROL first, which this deliberate failure fixture denies.
        Check(RestoreOwnedFixtureDaclNative(path,4,original.GetSecurityDescriptorBinaryForm()),"System DACL-only restoration of a new fixture failed: "+Marshal.GetLastWin32Error());
    }
    static readonly string[] AclFixtureKinds={"private","trusted-system-admins","directory-read","directory-write","file-read","file-write","file-permissions","inherited-directory","missing-file","directory-as-token","missing-directory","anonymous-deny","directory-acl-unreadable","file-acl-unreadable"};
    sealed record AclFixture(string LocalAppData,string Directory,string Token,string Kind,DirectorySecurity? RestoreDirectory=null,FileSecurity? RestoreFile=null)
    {
        public bool AclIoFailure=>Kind.EndsWith("-acl-unreadable",StringComparison.Ordinal);
        public bool MustReject=>Kind is not("private" or "trusted-system-admins" or "anonymous-deny");
    }
    static AclFixture CreateCapabilityAclFixture(string root,string kind)
    {
        Check(!System.IO.Directory.Exists(root),"ACL fixtures create a new owned Build subtree only");
        var owner=WindowsIdentity.GetCurrent().User!;
        var local=Path.Combine(root,"local");var parent=Path.Combine(local,"YeManCC");var directory=Path.Combine(parent,"fan-host");
        System.IO.Directory.CreateDirectory(local);
        var trusted=new[]{owner,new SecurityIdentifier(WellKnownSidType.LocalSystemSid,null),new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid,null)};
        DirectorySecurity DirectoryAcl(bool broad=false)
        {
            var acl=new DirectorySecurity();acl.SetOwner(owner);acl.SetAccessRuleProtection(true,false);
            foreach(var sid in trusted.Take(kind=="trusted-system-admins"?3:1))acl.AddAccessRule(new FileSystemAccessRule(sid,FileSystemRights.FullControl,InheritanceFlags.ContainerInherit|InheritanceFlags.ObjectInherit,PropagationFlags.None,AccessControlType.Allow));
            if(broad)acl.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier(WellKnownSidType.WorldSid,null),FileSystemRights.ReadData,InheritanceFlags.ContainerInherit|InheritanceFlags.ObjectInherit,PropagationFlags.None,AccessControlType.Allow));
            return acl;
        }
        new DirectoryInfo(parent).Create(DirectoryAcl(kind=="inherited-directory"));
        if(kind=="missing-directory")return new(local,directory,Path.Combine(directory,"YeManFanHost.session"),kind);
        if(kind=="inherited-directory")System.IO.Directory.CreateDirectory(directory);
        else
        {
            var acl=DirectoryAcl();
            if(kind is "directory-read" or "directory-write")acl.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier(WellKnownSidType.WorldSid,null),kind=="directory-read"?FileSystemRights.ReadData:FileSystemRights.WriteData,AccessControlType.Allow));
            if(kind=="anonymous-deny")acl.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier(WellKnownSidType.AnonymousSid,null),FileSystemRights.ReadData,AccessControlType.Deny));
            new DirectoryInfo(directory).Create(acl);
        }
        var token=Path.Combine(directory,"YeManFanHost.session");
        if(kind=="directory-as-token"){System.IO.Directory.CreateDirectory(token);return new(local,directory,token,kind);}
        if(kind=="missing-file")return new(local,directory,token,kind);
        var fileAcl=new FileSecurity();fileAcl.SetOwner(owner);fileAcl.SetAccessRuleProtection(true,false);
        foreach(var sid in trusted.Take(kind=="trusted-system-admins"?3:1))fileAcl.AddAccessRule(new FileSystemAccessRule(sid,FileSystemRights.FullControl,AccessControlType.Allow));
        if(kind is "file-read" or "file-write" or "file-permissions")fileAcl.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier(WellKnownSidType.WorldSid,null),kind=="file-read"?FileSystemRights.ReadData:kind=="file-write"?FileSystemRights.WriteData:FileSystemRights.ChangePermissions,AccessControlType.Allow));
        using(var stream=new FileInfo(token).Create(FileMode.CreateNew,FileSystemRights.FullControl,FileShare.None,4096,FileOptions.None,fileAcl))stream.Write(Encoding.UTF8.GetBytes("INERT-NOT-A-SESSION-CREDENTIAL"));
        var actualDir=new DirectoryInfo(directory).GetAccessControl();var actualFile=new FileInfo(token).GetAccessControl();
        Check(Equals(actualDir.GetOwner(typeof(SecurityIdentifier)),owner)&&Equals(actualFile.GetOwner(typeof(SecurityIdentifier)),owner),"Created fixture ACLs belong to the actual current Windows identity");
        if(kind=="inherited-directory")Check(actualDir.GetAccessRules(true,true,typeof(SecurityIdentifier)).Cast<FileSystemAccessRule>().Any(r=>r.IsInherited&&r.IdentityReference.Equals(new SecurityIdentifier(WellKnownSidType.WorldSid,null))),"Inherited untrusted Windows ACE is actually present");
        if(kind=="directory-acl-unreadable")
        {
            var denied=DirectoryAcl();denied.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier("S-1-3-4"),FileSystemRights.ReadPermissions,AccessControlType.Deny));
            new DirectoryInfo(directory).SetAccessControl(denied);return new(local,directory,token,kind,RestoreDirectory:actualDir);
        }
        if(kind=="file-acl-unreadable")
        {
            var denied=new FileSecurity();denied.SetAccessRuleProtection(true,false);denied.AddAccessRule(new FileSystemAccessRule(owner,FileSystemRights.FullControl,AccessControlType.Allow));denied.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier("S-1-3-4"),FileSystemRights.ReadPermissions,AccessControlType.Deny));
            new FileInfo(token).SetAccessControl(denied);return new(local,directory,token,kind,RestoreFile:actualFile);
        }
        return new(local,directory,token,kind);
    }
    static object CapabilityAclBackend(string? token,string directory)
    {
        var backend=NewFactoryBackend();var options=Get(backend,"options")!;Set(options,"SessionTokenPath",token);
        Set(options,"HcRuntimeRoot",Path.Combine(directory,"intentionally-missing-runtime"));return backend;
    }
    static void CheckCapabilityAclBoundary(object backend,AclFixture fixture,bool factory)
    {
        if(factory)
        {
            var diagnostics=new List<string>();Set(backend,"Diagnostic",(Action<string,object?>)((name,data)=>{if(name=="hc.initialization.failure")diagnostics.Add(JsonSerializer.Serialize(data));}));
            var failure=CaptureSta(()=>Invoke(backend,"LoadAndHandshakeCore"));
            Check(failure is not null,"Factory fixture must stop before any assembly or native bootstrap");
            var expected=fixture.MustReject?"会话令牌 ACL":"部署不完整";
            Check(fixture.AclIoFailure?failure!.ToString().Contains(nameof(UnauthorizedAccessException),StringComparison.Ordinal):failure!.ToString().Contains(expected,StringComparison.Ordinal),"Actual ACL boundary must determine the factory failure: "+fixture.Kind+"; got "+failure);
            Check(diagnostics.Count==1&&diagnostics[0].Contains("validation",StringComparison.Ordinal),"Actual factory preserves the validation-stage diagnostic");
            Check(Get(backend,"assemblyDirectory")is null&&Get(backend,"assembly")is null&&Get(backend,"IsLoaded")is false&&Get(backend,"CanWrite")is false,"Actual factory rejects before acquiring assembly/native/device ownership");
        }
        else if(fixture.MustReject)
        {
            var failure=CaptureSta(()=>Invoke(backend,"ValidateFormalSessionCapabilityAcl"));
            Check(failure is not null&&(fixture.AclIoFailure?failure is UnauthorizedAccessException:failure.ToString().Contains("会话令牌 ACL",StringComparison.Ordinal)),"Actual formal wrapper must reject "+fixture.Kind);
        }
        else Invoke(backend,"ValidateFormalSessionCapabilityAcl");
    }
    static int RunCapabilityAclCases(string[] args)
    {
        var report=Path.GetFullPath(args[3]);var root=Path.Combine(Path.GetDirectoryName(report)!,"capability-acl-cases");
        Check(new DirectoryInfo(Path.GetDirectoryName(report)!).FullName.Split(Path.DirectorySeparatorChar).Any(segment=>segment.Equals("Build",StringComparison.OrdinalIgnoreCase)),"Real ACL fixtures remain inside an explicitly owned Build evidence subtree");
        Check(!System.IO.Directory.Exists(root),"Capability ACL case directory must be fresh");System.IO.Directory.CreateDirectory(root);var index=0;var restoredAclCount=0;
        void Case(string id,string kind,Action<AclFixture,object> action)=>Run(id,()=>
        {
            var f=CreateCapabilityAclFixture(Path.Combine(root,(index++).ToString()),kind);AclPorts.LocalAppData=f.LocalAppData;var backend=CapabilityAclBackend(f.Token,f.Directory);
            try{action(f,backend);}finally
            {
                // Restore only the newly created, explicitly owned fixture ACL.
                // No existing user/AppData/installed ACL is ever changed.
                if(f.RestoreDirectory is not null)
                {
                    RestoreOwnedFixtureDacl(f.Directory,f.RestoreDirectory,f.LocalAppData);
                    Check(new DirectoryInfo(f.Directory).GetAccessControl().GetSecurityDescriptorSddlForm(AccessControlSections.Access)==f.RestoreDirectory.GetSecurityDescriptorSddlForm(AccessControlSections.Access),"The newly owned directory DACL is actually restored");restoredAclCount++;
                }
                if(f.RestoreFile is not null)
                {
                    RestoreOwnedFixtureDacl(f.Token,f.RestoreFile,f.LocalAppData);
                    Check(new FileInfo(f.Token).GetAccessControl().GetSecurityDescriptorSddlForm(AccessControlSections.Access)==f.RestoreFile.GetSecurityDescriptorSddlForm(AccessControlSections.Access),"The newly owned file DACL is actually restored");restoredAclCount++;
                }
                ((IDisposable)backend).Dispose();AclPorts.LocalAppData="";
            }
        });
        foreach(var factory in new[]{false,true})foreach(var kind in AclFixtureKinds)Case("capability-acl-"+(factory?"factory":"direct")+":"+kind,kind,(f,b)=>CheckCapabilityAclBoundary(b,f,factory));
        foreach(var value in new string?[]{null,"","  "})Case("capability-acl-diagnostic-blank:"+(value is null?"null":value.Length.ToString()),"private",(f,b)=>{Set(Get(b,"options")!,"SessionTokenPath",value);Invoke(b,"ValidateFormalSessionCapabilityAcl");});
        foreach(var kind in new[]{"external","prefix-lookalike"})Case("capability-acl-diagnostic-scope:"+kind,"private",(f,b)=>
        {
            var path=kind=="external"?Path.Combine(Path.GetDirectoryName(f.LocalAppData)!,"diagnostic.session"):Path.Combine(f.LocalAppData,"YeManCC","fan-host-other","diagnostic.session");
            Check(!File.Exists(path),"Diagnostic scope control is an actually absent external file");Set(Get(b,"options")!,"SessionTokenPath",path);Invoke(b,"ValidateFormalSessionCapabilityAcl");
        });
        foreach(var kind in new[]{"case","normalized"})Case("capability-acl-formal-path:"+kind,"file-read",(f,b)=>
        {
            Set(Get(b,"options")!,"SessionTokenPath",kind=="case"?f.Token.ToUpperInvariant():Path.Combine(f.Directory,"..","fan-host","YeManFanHost.session"));CheckCapabilityAclBoundary(b,f,false);
        });
        var failed=Cases.Any(c=>JsonSerializer.Serialize(c).Contains("\"status\":\"FAIL\""));
        File.WriteAllText(report,JsonSerializer.Serialize(new{schemaVersion=1,status=failed?"FAIL":"PASS",cases=Cases,checks=Checks,candidateHostSha256=Hash(args[0]),nativeAttempts=NativeAttempts,hostMainStarted=false,hardwareIo=false,
            actualFormalCapabilityAclWrapperExecuted=true,actualWindowsDirectoryAndFileAclReads=true,windowsAclFixturesCreatedOnlyInsideBuild=true,existingUserOrInstalledAclChanged=false,actualInstalledAppDataTouched=false,
            onlyLocalAppDataFolderGetterRedirected=true,inertFileContentsNotSessionCredentials=true,restoredFixtureAcls=restoredAclCount,onlyNewFixtureRestorationUsesSystemDaclApi=true},new JsonSerializerOptions{WriteIndented=true}));return failed?2:0;
    }
    static int InstrumentCapabilityAclFolder(string[] args)
    {
        var source=Path.GetFullPath(args[0]);var target=Path.GetFullPath(args[1]);using var image=AssemblyDefinition.ReadAssembly(source);var type=image.MainModule.GetType("YeManFanHost.RealHcBackend");var method=type.Methods.Single(m=>m.Name=="ValidateFormalSessionCapabilityAcl");
        var getter=method.Body.Instructions.Single(i=>i.Operand is MethodReference call&&call.DeclaringType.FullName==typeof(Environment).FullName&&call.Name==nameof(Environment.GetFolderPath));
        Check(((MethodReference)getter.Operand).Parameters.Count==1,"Original formal wrapper has exactly one SpecialFolder I/O getter");
        getter.Operand=image.MainModule.ImportReference(typeof(AclPorts).GetMethod(nameof(AclPorts.GetFolderPath))!);System.IO.Directory.CreateDirectory(Path.GetDirectoryName(target)!);image.Write(target);
        var changed=FactoryChangedMethods(source,target);Check(changed.Length==1&&changed[0]==method.FullName,"Only the actual wrapper's one folder getter operand changes");
        using var original=AssemblyDefinition.ReadAssembly(source);using var modified=AssemblyDefinition.ReadAssembly(target);var before=original.MainModule.GetType(type.FullName).Methods.Single(m=>m.Name==method.Name);var after=modified.MainModule.GetType(type.FullName).Methods.Single(m=>m.Name==method.Name);
        after.Body.Instructions.Single(i=>i.Operand is MethodReference call&&call.DeclaringType.FullName==typeof(AclPorts).FullName).Operand=before.Body.Instructions.Single(i=>i.Operand is MethodReference call&&call.DeclaringType.FullName==typeof(Environment).FullName&&call.Name==nameof(Environment.GetFolderPath)).Operand;
        Check(FactoryMethodCanon(before)==FactoryMethodCanon(after),"Restoring the getter restores all original wrapper instructions, locals and exception regions");
        File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(source),instrumentedSha256=Hash(target),changedMethods=changed,onlyOneIoGetterOperandChanged=true,actualWindowsAclConsumersPreserved=true,hardwareIo=false}));return 0;
    }
    static int MutateCapabilityAclGuard(string[] args,string kind)
    {
        var source=Path.GetFullPath(args[0]);var target=Path.GetFullPath(args[1]);using var image=AssemblyDefinition.ReadAssembly(source);var method=image.MainModule.GetType("YeManFanHost.RealHcBackend").Methods.Single(m=>m.Name=="ValidateFormalSessionCapabilityAcl");var il=method.Body.GetILProcessor();
        if(kind=="return"){method.Body.Instructions.Clear();method.Body.Variables.Clear();method.Body.ExceptionHandlers.Clear();il.Append(il.Create(OpCodes.Ret));}
        else if(kind is "directory" or "file")
        {
            var calls=method.Body.Instructions.Where(i=>i.Operand is MethodReference call&&call.Name=="IsPrivateCapabilityAcl").ToArray();Check(calls.Length==2,"Actual wrapper retains exactly the directory and file predicate calls");
            var call=calls[kind=="directory"?0:1];call.OpCode=OpCodes.Pop;call.Operand=null;il.InsertAfter(call,il.Create(OpCodes.Ldc_I4_1));
        }
        else if(kind=="scope")
        {
            var call=method.Body.Instructions.Single(i=>i.Operand is MethodReference m&&m.DeclaringType.FullName==typeof(string).FullName&&m.Name==nameof(string.Equals)&&m.Parameters.Count==2);
            call.OpCode=OpCodes.Pop;call.Operand=null;var cursor=call;foreach(var instruction in new[]{il.Create(OpCodes.Pop),il.Create(OpCodes.Pop),il.Create(OpCodes.Ldc_I4_1)}){il.InsertAfter(cursor,instruction);cursor=instruction;}
        }
        else throw new InvalidOperationException("Unknown capability ACL fault");
        WidenStaBranches(method);System.IO.Directory.CreateDirectory(Path.GetDirectoryName(target)!);image.Write(target);var changed=FactoryChangedMethods(source,target);Check(changed.Length==1&&changed[0]==method.FullName,"ACL fault changes exactly the one original wrapper method");
        File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(source),faultSha256=Hash(target),changedMethods=changed,mutation=kind,hardwareIo=false}));return 0;
    }
}
