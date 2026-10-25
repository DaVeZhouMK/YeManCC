internal static class BatteryOwnedJobEntry
{
    static async Task<int> Main(string[] args)
    {
        if(args.Length!=4)return 10;
        using var job=new YmccGlobalOwnedJob();
        using var child=new System.Diagnostics.Process();
        child.StartInfo=new System.Diagnostics.ProcessStartInfo(args[0]) {UseShellExecute=false,CreateNoWindow=true,WorkingDirectory=args[2],RedirectStandardInput=true,RedirectStandardOutput=true,RedirectStandardError=true};
        child.StartInfo.ArgumentList.Add(args[1]);child.StartInfo.ArgumentList.Add(args[2]);child.StartInfo.ArgumentList.Add(args[3]);
        var startedAt=DateTimeOffset.UtcNow;
        if(!child.Start())return 10;
        job.Assign(child.Handle);
        var stdout=child.StandardOutput.BaseStream.CopyToAsync(Console.OpenStandardOutput());
        var stderr=child.StandardError.BaseStream.CopyToAsync(Console.OpenStandardError());
        Console.WriteLine("BATTERY_OWNED_JOB childPid="+child.Id);
        await child.StandardInput.WriteLineAsync(System.Text.Json.JsonSerializer.Serialize(new {nonce=args[3],jobAssigned=true}));child.StandardInput.Close();
        await child.WaitForExitAsync();await Task.WhenAll(stdout,stderr);
        File.WriteAllText(Path.Combine(args[2],"owned-job-result.json"),System.Text.Json.JsonSerializer.Serialize(new {nonce=args[3],root=Path.GetFullPath(args[2]),status=child.ExitCode==0?"PASS":"FAIL",workerPid=child.Id,workerExitCode=child.ExitCode,natural=true,forcedTermination=false,startedAt,finishedAt=DateTimeOffset.UtcNow,jobAssignedBeforeGate=true}));
        return child.ExitCode;
    }
}
