import fs from 'node:fs';import assert from 'node:assert/strict';import {createHash} from 'node:crypto';
const hash=t=>createHash('sha256').update(t).digest('hex');
export const orderedHelper=`    // Fixture-only verification endpoint. Preserve each read and output index.
    // Stop assigning work after failure, drain both started workers, then rethrow
    // the earliest input failure with its original exception type and stack.
    // A directly owned second thread avoids fresh-process TPL startup. Join it
    // before returning or rethrowing; both callbacks retain the same claim gate.
    private static object[] ManagedOrdered2<T>(IReadOnlyList<T> rows, Func<T, object> verify)
    {
        var results = new object[rows.Count];
        var gate = new object();
        var next = 0;
        var failedIndex = int.MaxValue;
        System.Runtime.ExceptionServices.ExceptionDispatchInfo? failure = null;
        void Worker()
        {
            while (true)
            {
                int index;
                lock (gate)
                {
                    if (failure is not null || next == rows.Count) return;
                    index = next++;
                }
                try { results[index] = verify(rows[index]); }
                catch (Exception error)
                {
                    lock (gate)
                    {
                        if (index < failedIndex)
                        {
                            failedIndex = index;
                            failure = System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(error);
                        }
                    }
                    return;
                }
            }
        }
        var secondWorker = new System.Threading.Thread(Worker);
        secondWorker.Start();
        try { Worker(); }
        finally { secondWorker.Join(); }
        failure?.Throw();
        return results;
    }
`;
export function optimizeManagedHashes(source){
 const eol=source.slice(source.indexOf('    private static string ManagedHash('),source.indexOf('    private static async Task<int> Main(')).includes('\r\n')?'\r\n':'\n',l=t=>t.replaceAll('\n',eol),edits=[];let text=source;
 function change(name,before,after){before=l(before);after=l(after);assert.equal(text.split(before).length,2,'Exactly one original anchor '+name);text=text.replace(before,after);edits.push({name,before,after});}
 change('same-share-mode-buffer64k','        using var stream = File.OpenRead(file);','        using var stream = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.Read, 65536, FileOptions.SequentialScan);');
 change('bounded-ordered-helper','    private static object[] CheckManagedBindings',orderedHelper+'\n    private static object[] CheckManagedBindings');
 change('original-source-and-frozen-rows',`        var checks = new List<object>();
        foreach (var row in manifest.GetProperty(property).EnumerateArray())
        {`,`        return ManagedOrdered2(manifest.GetProperty(property).EnumerateArray().ToArray(), row =>
        {`);
 change('source-row-receipt-order',`            checks.Add(new { path = file, sha256 = actual, matched = true });
        }
        return checks.ToArray();`,`            return (object)new { path = file, sha256 = actual, matched = true };
        });`);
 change('loaded-assembly-snapshot',`        return AppDomain.CurrentDomain.GetAssemblies().Where(a => !a.IsDynamic)
            .Select(a => new { assembly = a.GetName().Name, path = a.Location })
            .Where(a => a.path.Length > 0).Select(a =>`,`        return ManagedOrdered2(AppDomain.CurrentDomain.GetAssemblies().Where(a => !a.IsDynamic)
            .Select(a => new { assembly = a.GetName().Name, path = a.Location })
            .Where(a => a.path.Length > 0).ToArray(), a =>`);
 change('loaded-assembly-receipt-order',`                return (object)new { a.assembly, a.path, sha256 = hash, ownAssembly };
            }).ToArray();`,`                return (object)new { a.assembly, a.path, sha256 = hash, ownAssembly };
            });`);
 change('runtime-artifact-rows',`            var outputChecks = new List<object>();
            foreach (var row in build.RootElement.GetProperty("runtimeArtifacts").EnumerateArray())
            {`,`            var outputChecks = ManagedOrdered2(build.RootElement.GetProperty("runtimeArtifacts").EnumerateArray().ToArray(), row =>
            {`);
 change('runtime-artifact-receipt-order',`                outputChecks.Add(new { path = file, sha256 = actual, matched = true });
            }
            startupBindings = new`,`                return (object)new { path = file, sha256 = actual, matched = true };
            });
            startupBindings = new`);
 function restore(candidate){for(const edit of [...edits].reverse()){assert.equal(candidate.split(edit.after).length,2,'Inverse '+edit.name);candidate=candidate.replace(edit.after,edit.before);}assert.equal(hash(candidate),hash(source),'Original bytes must restore');return candidate;}
 assert.equal(restore(text),source);
 return {text,restore,originalSha256:hash(source),optimizedSha256:hash(text),edits};
}
