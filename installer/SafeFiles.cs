using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;

namespace YeMan.Setup {
 internal sealed class UnsafeInstallPathException : IOException {
  internal UnsafeInstallPathException(string path):base("路径包含链接或重解析点："+path){}
 }
 public static class SafeFiles {
  public static string IoPath(string p) { return p.StartsWith(@"\\?\") ? p : @"\\?\" + Path.GetFullPath(p); }
  public static void Copy(string from,string to,bool overwrite) { File.Copy(IoPath(from),IoPath(to),overwrite); }
  public static void CopyDurable(string from,string to) {
   using(var input=new FileStream(IoPath(from),FileMode.Open,FileAccess.Read,FileShare.Read))
   using(var output=new FileStream(IoPath(to),FileMode.CreateNew,FileAccess.Write,FileShare.None)){input.CopyTo(output);output.Flush(true);}
  }
  public static string CopyDurableHash(string from,string to) {
   using(var input=new FileStream(IoPath(from),FileMode.Open,FileAccess.Read,FileShare.Read))
   using(var output=new FileStream(IoPath(to),FileMode.CreateNew,FileAccess.Write,FileShare.None))
   using(var hash=SHA256.Create()) {
    var buffer=new byte[81920];int count;while((count=input.Read(buffer,0,buffer.Length))!=0){output.Write(buffer,0,count);hash.TransformBlock(buffer,0,count,buffer,0);}
    hash.TransformFinalBlock(buffer,0,0);output.Flush(true);return BitConverter.ToString(hash.Hash).Replace("-", "").ToLowerInvariant();
   }
  }
  public static string Hash(Stream s) { using (var sha = SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(s)).Replace("-", "").ToLowerInvariant(); }
  public static string Hash(string p) {return Sharing(()=>{using(var stream=File.OpenRead(p))return Hash(stream);});}
  internal static void SharingIo(Action action){Sharing<object>(()=>{action();return null;});}
  static T Sharing<T>(Func<T> action) {
   // Security scanners may briefly hold a just-created executable. Retry only
   // sharing/lock violations; access denial, missing files and hash mismatches
   // remain failures. Every successful open keeps the original read sharing.
   var clock=System.Diagnostics.Stopwatch.StartNew();
   for(int attempt=0;;attempt++) {
    try {return action();}
    catch(IOException error) {
     int code=error.HResult&0xffff;
     if((code!=32&&code!=33)||attempt>=11||clock.ElapsedMilliseconds>=2000)throw;
     int delay=Math.Min(200,25*(attempt+1));int left=2000-(int)clock.ElapsedMilliseconds;
     if(left<=0)throw;System.Threading.Thread.Sleep(Math.Min(delay,left));
    }
   }
  }
  public static string Full(string p) {
   if (string.IsNullOrWhiteSpace(p) || !Path.IsPathRooted(p) || p.StartsWith(@"\\") || p.IndexOf('\0') >= 0) throw new IOException("请选择本地磁盘上的完整目录");
   return Path.GetFullPath(p).TrimEnd(Path.DirectorySeparatorChar);
  }
  public static bool Under(string p, string root) { return Path.GetFullPath(p).StartsWith(Path.GetFullPath(root).TrimEnd('\\') + "\\", StringComparison.OrdinalIgnoreCase); }
  public static void NoLinks(string path) {
   string p = Path.GetFullPath(path);
   while (!string.IsNullOrEmpty(p)) {
    try{if((File.GetAttributes(p)&FileAttributes.ReparsePoint)!=0)throw new UnsafeInstallPathException(p);}
    catch(FileNotFoundException){}catch(DirectoryNotFoundException){}
    p = Path.GetDirectoryName(p);
   }
  }
  public static IEnumerable<string> TreeFiles(string root) {
   NoLinks(root);var pending=new Stack<string>();pending.Push(root);
   while(pending.Count>0)foreach(string path in Directory.GetFileSystemEntries(pending.Pop())){NoLinks(path);if(Directory.Exists(path))pending.Push(path);else yield return path;}
  }
  public static string EntryName(string name) {
   string n = name.Replace('\\', '/');
   if (n.Length > 220 || n.StartsWith("/") || n.Contains(":") || n.Contains("\0")) throw new InvalidDataException("压缩包路径无效");
   foreach (string part in n.TrimEnd('/').Split('/')) {
    if (part.Length == 0 || part == "." || part == ".." || part.EndsWith(".") || part.EndsWith(" ") || part.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 ||
        System.Text.RegularExpressions.Regex.IsMatch(part, @"^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)", System.Text.RegularExpressions.RegexOptions.IgnoreCase))
     throw new InvalidDataException("压缩包含不安全路径：" + n);
   }
   return n;
  }
  public static void Extract(string zip, string root, Func<string,bool> allowed, Func<string,bool> skip = null) {
   NoLinks(root); Directory.CreateDirectory(root); long total = 0;
   using (var archive = ZipFile.OpenRead(zip)) {
    if (archive.Entries.Count > 12000) throw new InvalidDataException("压缩包文件数超限");
    var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
    // Validate the entire directory before writing the first file.
    foreach (var e in archive.Entries) {
     string n = EntryName(e.FullName);
     if (!seen.Add(n.TrimEnd('/'))) throw new InvalidDataException("压缩包含重复路径：" + n);
     int attr = (e.ExternalAttributes >> 16) & 0xf000;
     if (attr == 0xa000 || (e.ExternalAttributes & 0x400) != 0) throw new InvalidDataException("压缩包含链接");
     if (!allowed(n)) throw new InvalidDataException("压缩包布局不匹配：" + n);
     total = checked(total + e.Length);
     if (e.Length > 1024L*1024*1024 || total > 3L*1024*1024*1024) throw new InvalidDataException("压缩包展开大小超限");
     if (!Under(Path.Combine(root, n.Replace('/', '\\')), root)) throw new InvalidDataException("路径越界");
    }
    foreach (var e in archive.Entries) {
     if (skip != null && skip(EntryName(e.FullName))) continue;
     string n = EntryName(e.FullName); string p = Path.Combine(root, n.Replace('/', '\\')); NoLinks(p);
     if (n.EndsWith("/")) { Directory.CreateDirectory(p); continue; }
     Directory.CreateDirectory(Path.GetDirectoryName(p));
     using (var input = e.Open()) using (var output = new FileStream(p, FileMode.CreateNew, FileAccess.Write, FileShare.None)) input.CopyTo(output);
     if (new FileInfo(p).Length != e.Length) throw new InvalidDataException("展开长度不匹配");
    }
   }
  }
  public static void Verify(string p, Blob b) {
   if (!File.Exists(p) || new FileInfo(p).Length != b.Length || !string.Equals(Hash(p), b.Sha256, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("资源校验失败：" + b.Path);
  }
 }
 // All overwritten files keep preimages. A durable journal precedes each mutation.
 // Failure rolls back files; installed Microsoft runtimes are intentionally retained.
 public sealed class FileTransaction {
  public sealed class Change { public string Target; public string Backup; public bool Existed; public string OriginalSha256; public string NewSha256; public string Staging; public int? OriginalAttributes; }
  readonly string storage; readonly string journal;readonly string operation;readonly string records;bool batching;
  readonly List<Change> changes = new List<Change>();
  readonly Dictionary<string,string> expected=new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);
  public FileTransaction(string backup,string operationRoot=null) { storage = backup;operation=operationRoot; SafeFiles.NoLinks(backup); Directory.CreateDirectory(backup); journal = Path.Combine(backup, "file-journal.json");records=Path.Combine(backup,"file-journal.d");Json.Save(journal, changes);Directory.CreateDirectory(records);Json.Save(Path.Combine(backup,"file-journal-format.json"),new {Schema=2}); }
  void Record(Change change) {
   // The immutable per-file intent is durable before mutation. Checkpoints do not grant write permission.
   SafeFiles.NoLinks(records);Directory.CreateDirectory(records);Json.Save(Path.Combine(records,changes.Count.ToString("D6")+".json"),change);changes.Add(change);
   if(!batching)Json.Save(journal,changes);
  }
  public string Backup { get { return storage; } }
  public static string StagingPath(string target,string operationRoot){using(var stream=new MemoryStream(Encoding.UTF8.GetBytes(Path.GetFullPath(operationRoot).ToLowerInvariant()+"\0"+Path.GetFullPath(target).ToLowerInvariant())))return Path.Combine(Path.GetDirectoryName(target),".ymcc-"+SafeFiles.Hash(stream).Substring(0,32)+".tmp");}
  public void Put(string source, string target) {
   SafeFiles.NoLinks(source); SafeFiles.NoLinks(target);
   string sourceHash=SafeFiles.Hash(source);expected[Path.GetFullPath(target)]=sourceHash;
   if (File.Exists(target) && sourceHash == SafeFiles.Hash(target)) return;
   Directory.CreateDirectory(Path.GetDirectoryName(target));
   var c = new Change { Target = Path.GetFullPath(target), Existed = File.Exists(target), Backup = Path.Combine(storage, changes.Count.ToString("D6") + ".bak") };
   if (c.Existed) { c.OriginalAttributes=(int)File.GetAttributes(target);SafeFiles.CopyDurable(target,c.Backup);WorkSecurity.Seal(c.Backup);c.OriginalSha256=SafeFiles.Hash(c.Backup); } c.NewSha256=sourceHash;
   c.Staging=operation==null?Path.Combine(Path.GetDirectoryName(target),".ymcc-"+Guid.NewGuid().ToString("N")+".tmp"):StagingPath(target,operation);Record(c);
   // Move in the destination directory avoids cross-volume moves and torn copies.
   string temp = c.Staging;
   try {
    if(SafeFiles.CopyDurableHash(source,temp)!=sourceHash)throw new IOException("安装源在复制期间改变："+source);
    if(File.Exists(target)){if(!c.Existed||SafeFiles.Hash(target)!=c.OriginalSha256)throw new IOException("写入前检测到安装以外的改动，已保留："+target);MakeWritable(target);File.Replace(SafeFiles.IoPath(temp),SafeFiles.IoPath(target),null);}
    else{if(c.Existed)throw new IOException("写入前原文件已消失，已停止覆盖："+target);File.Move(SafeFiles.IoPath(temp),SafeFiles.IoPath(target));}
   }
   finally { if (File.Exists(temp)) File.Delete(temp); }
   if (sourceHash != SafeFiles.Hash(target)) throw new IOException("写入后校验失败：" + target);
  }
  public void Remove(string target) {
   expected[Path.GetFullPath(target)]=null;
   SafeFiles.NoLinks(target); if (!File.Exists(target)) return;
   var c = new Change { Target = target, Existed = true, Backup = Path.Combine(storage, changes.Count.ToString("D6") + ".bak") };
   c.OriginalAttributes=(int)File.GetAttributes(target);SafeFiles.CopyDurable(target,c.Backup);WorkSecurity.Seal(c.Backup);c.OriginalSha256=SafeFiles.Hash(c.Backup);c.Staging=operation==null?Path.Combine(Path.GetDirectoryName(target),".ymcc-"+Guid.NewGuid().ToString("N")+".tmp"):StagingPath(target,operation);Record(c);MakeWritable(target);File.Delete(target);
  }
  public void VerifyApplied() {
   foreach(var e in expected){SafeFiles.NoLinks(e.Key);if(e.Value==null){if(File.Exists(e.Key))throw new IOException("应删除的文件仍然存在："+e.Key);}else if(!File.Exists(e.Key)||SafeFiles.Hash(e.Key)!=e.Value)throw new IOException("最终文件完整性核验失败，可能被占用、隔离或外部修改："+e.Key);}
  }
  public void Tree(string source, string target, bool preserveConfig, Predicate<string> preservePath=null, bool optional=false) {
   SafeFiles.NoLinks(source); SafeFiles.NoLinks(target);
   bool previous=batching;batching=true;try {
   foreach (var p in SafeFiles.TreeFiles(source)) {
    string relative = p.Substring(source.Length).TrimStart('\\'); string to = Path.Combine(target, relative);
    if (File.Exists(to) && ((preservePath!=null&&preservePath(relative)) || (preserveConfig && (relative.EndsWith(".ini", StringComparison.OrdinalIgnoreCase) || relative.EndsWith(".cfg", StringComparison.OrdinalIgnoreCase) || Path.GetFileName(relative).Equals("config_USER.json", StringComparison.OrdinalIgnoreCase) || Path.GetFileName(relative).Equals("app.config.json", StringComparison.OrdinalIgnoreCase))))) continue;
    try{Put(p,to);}
    catch(Exception e){if(!optional||!CanSkipFile(e))throw;throw new InstallTargetBusyException(relative,e);}
   }
   }finally{batching=previous;}
   if(!batching)Json.Save(journal,changes);
  }
  static void MakeWritable(string path){var attributes=File.GetAttributes(path);if((attributes&FileAttributes.ReadOnly)!=0)File.SetAttributes(path,attributes&~FileAttributes.ReadOnly);}
  static bool CanSkipFile(Exception e){int code=e.HResult&0xffff;return e is UnauthorizedAccessException||(e is IOException&&(code==5||code==32||code==33||code==80||code==112||code==183||code==267||code==1175||code==1176||code==1177||code==1224));}
  internal void RestoreTree(string root) {
   RestorePaths(target=>SafeFiles.Under(target,root));
  }
  internal void RestorePaths(Predicate<string> match) {
   // Keep durable intents for crash recovery, but stop verifying files restored to their original state.
   for(int i=changes.Count-1;i>=0;i--)if(match(changes[i].Target))Restore(changes[i]);
   var keys=new List<string>(expected.Keys);foreach(string key in keys)if(match(key))expected.Remove(key);
  }
  static void RestoreAttributes(Change c){if(c.OriginalAttributes.HasValue&&File.GetAttributes(c.Target)!=(FileAttributes)c.OriginalAttributes.Value)File.SetAttributes(c.Target,(FileAttributes)c.OriginalAttributes.Value);}
  static void Restore(Change c) {
   SafeFiles.NoLinks(c.Target);SafeFiles.NoLinks(c.Backup);
   if(c.Existed&&(!File.Exists(c.Backup)||SafeFiles.Hash(c.Backup)!=c.OriginalSha256))throw new IOException("备份校验失败："+c.Target);
   ValidateStaging(c);if(c.Staging!=null&&File.Exists(c.Staging)){SafeFiles.NoLinks(c.Staging);File.Delete(c.Staging);}
   if(File.Exists(c.Target)) {string now=SafeFiles.Hash(c.Target);if(now==c.OriginalSha256){RestoreAttributes(c);return;}if(now!=c.NewSha256)throw new IOException("检测到安装以外的改动，已保留："+c.Target);}
   if(c.Existed) {
    Directory.CreateDirectory(Path.GetDirectoryName(c.Target));
    // Reuse the journaled sibling path. A crash while restoring never truncates the installed file.
    string temp=c.Staging??StagingPath(c.Target,c.Backup);SafeFiles.NoLinks(temp);
    if(File.Exists(temp))File.Delete(temp);
    try {
     SafeFiles.CopyDurable(c.Backup,temp);if(SafeFiles.Hash(temp)!=c.OriginalSha256)throw new IOException("恢复暂存校验失败："+c.Target);
     if(File.Exists(c.Target)){string now=SafeFiles.Hash(c.Target);if(now==c.OriginalSha256){RestoreAttributes(c);return;}if(now!=c.NewSha256)throw new IOException("恢复前检测到安装以外的改动，已保留："+c.Target);MakeWritable(c.Target);File.Replace(SafeFiles.IoPath(temp),SafeFiles.IoPath(c.Target),null);}
     else File.Move(SafeFiles.IoPath(temp),SafeFiles.IoPath(c.Target));
     if(SafeFiles.Hash(c.Target)!=c.OriginalSha256)throw new IOException("恢复后校验失败："+c.Target);RestoreAttributes(c);
    }finally{if(File.Exists(temp))File.Delete(temp);}
   }else if(File.Exists(c.Target))File.Delete(c.Target);
  }
  static void ValidateStaging(Change c){if(c.Staging==null)return;if(!Path.IsPathRooted(c.Staging)||!string.Equals(Path.GetDirectoryName(Path.GetFullPath(c.Staging)),Path.GetDirectoryName(Path.GetFullPath(c.Target)),StringComparison.OrdinalIgnoreCase)||!System.Text.RegularExpressions.Regex.IsMatch(Path.GetFileName(c.Staging),@"^\.ymcc-[0-9a-f]{32}\.tmp$"))throw new IOException("恢复临时路径无效");}
  public static void Recover(string storage,Predicate<string> permittedTarget=null,Action<string[]> beforeRestore=null) {
   SafeFiles.NoLinks(storage);string p=Path.Combine(storage,"file-journal.json");if(!File.Exists(p))return;
   var items=Json.Read<Change[]>(File.ReadAllText(p));
   if(items==null)throw new IOException("恢复记录为空");
   string records=Path.Combine(storage,"file-journal.d");
   string format=Path.Combine(storage,"file-journal-format.json");
   if(File.Exists(format)){SafeFiles.NoLinks(format);var version=Json.Read<Dictionary<string,int>>(File.ReadAllText(format));if(version==null||version.Count!=1||!version.ContainsKey("Schema")||version["Schema"]!=2||!Directory.Exists(records))throw new IOException("恢复日志格式或增量目录缺失");}
   if(Directory.Exists(records)) {
    SafeFiles.NoLinks(records);var rows=new List<Change>();var files=Directory.GetFiles(records,"*.json");Array.Sort(files,StringComparer.Ordinal);
    if(files.Length>12032)throw new IOException("恢复日志条数超限");
    foreach(string file in files){SafeFiles.NoLinks(file);if(Path.GetFileName(file)!=rows.Count.ToString("D6")+".json")throw new IOException("恢复日志缺项或顺序无效");var row=Json.Read<Change>(File.ReadAllText(file));if(row==null)throw new IOException("恢复日志条目为空");rows.Add(row);}
    if(rows.Count>0&&!File.Exists(format))throw new IOException("恢复日志格式标记缺失");
    if(rows.Count<items.Length)throw new IOException("恢复日志缺失已提交条目");
    for(int i=0;i<items.Length;i++)if(Json.Write(items[i])!=Json.Write(rows[i]))throw new IOException("恢复日志与检查点不一致");
    items=rows.ToArray();
   }
   foreach(var c in items)if(c==null||!SafeFiles.Under(c.Backup,storage)||c.Target==null||!Path.IsPathRooted(c.Target)||(permittedTarget!=null&&!permittedTarget(c.Target)))throw new IOException("恢复记录不安全或超出安装范围");
   foreach(var c in items)ValidateStaging(c);
   if(beforeRestore!=null)beforeRestore(Array.ConvertAll(items,c=>c.Target));
   var errors=new List<string>();for(int i=items.Length-1;i>=0;--i)try{Restore(items[i]);}catch(Exception e){errors.Add(items[i].Target+"："+e.Message);}
   Json.Save(Path.Combine(storage,"rollback.json"),new {Complete=errors.Count==0,Errors=errors,RecoveryUtc=DateTime.UtcNow.ToString("o")});
   if(errors.Count>0)throw new IOException("回滚尚未完成；已继续恢复其他文件："+string.Join("；",errors));
  }
  public void Rollback() {
   List<string> errors = new List<string>();
   for (int i=changes.Count-1; i>=0; --i) { var c = changes[i]; try {
    SafeFiles.NoLinks(c.Target);
    Restore(c);
   } catch (Exception e) { errors.Add(c.Target + ": " + e.Message); } }
   Json.Save(Path.Combine(storage, "rollback.json"), new { Complete = errors.Count == 0, Errors = errors });
   if (errors.Count > 0) throw new IOException("回滚尚未完成；备份在 " + storage + "；" + string.Join("；", errors));
  }
 }
}
