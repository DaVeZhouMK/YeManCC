"""Drive actual independent test processes through production endpoint cooldown code.
The compiled lifecycle test supplies a Build-only operation entry. No HTTP calls,
installed-data mutexes, game launches or Steam writes are performed.
"""
import argparse, concurrent.futures, hashlib, json, pathlib, subprocess, time
BUILD=(pathlib.Path(__file__).resolve().parents[1]/"../../Build").resolve()
def load(path):return json.loads(path.read_text("utf-8-sig"))
def save(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding="utf-8")
def main():
    parser=argparse.ArgumentParser();parser.add_argument("--root",required=True);parser.add_argument("--test-exe",required=True);args=parser.parse_args()
    root=pathlib.Path(args.root).resolve();exe=pathlib.Path(args.test_exe).resolve()
    assert root.is_relative_to(BUILD) and not root.exists(),"Use a fresh Build-only evidence root"
    assert exe.is_relative_to(BUILD) and exe.is_file();root.mkdir(parents=True)
    endpoint="https://store.steampowered.com:443/api/storesearch";url="https://store.steampowered.com/api/storesearch/?term=Portal%202&l=english&cc=us"
    data=root/"data";cache=data/"cache/network-backoff"/(hashlib.sha256(endpoint.encode()).hexdigest()+".json")
    now=int(time.time()*1000);seed={"schemaVersion":1,"endpoint":endpoint,"observedAt":now-5000,"retryAt":now+120000,"status":429,"identityAuthority":False};save(cache,seed)
    old_success_start=now-1000
    def action(label,operation,**fields):
        file=root/"actions"/(label+".json");request={"dataRoot":str(data),"url":url,"operation":operation,**fields};save(file,request)
        p=subprocess.run([str(exe),"--backoff-fixture-action",str(file)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=20)
        assert p.returncode==0,p.stdout.decode("utf-8",errors="replace")
        return load(pathlib.Path(str(file)+".result.json"))
    rows=[]
    latest=action("new-shorter-fault","write",status=503,delayMs=15000)
    read=action("new-process-read","read");assert read["observedAt"]>=now and read["retryAt"]>=seed["retryAt"]
    rows.append({"name":"new-short-fault-persists-across-processes","passed":True})
    kept=action("earlier-success","clear",requestStartedAt=old_success_start);assert kept and kept["observedAt"]>=now
    rows.append({"name":"late-earlier-success-does-not-clear-new-fault","passed":True})
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        futures=[pool.submit(action,"parallel-"+str(i),"write",status=429 if i%3==0 else 503,delayMs=120000 if i%3==0 else 15000) for i in range(24)]
        results=[f.result() for f in futures]
    merged=action("after-parallel","read")
    assert merged and merged["retryAt"]>=seed["retryAt"] and merged["observedAt"]>=read["observedAt"]
    rows.append({"name":"24-concurrent-process-writes-keep-longest-cooldown","passed":True,"workerProcesses":24})
    kept=action("parallel-old-success","clear",requestStartedAt=old_success_start);assert kept
    rows.append({"name":"earlier-success-after-parallel-writes-keeps-cooldown","passed":True})
    recovered=action("newer-success","clear",requestStartedAt=max(int(time.time()*1000),kept["observedAt"]+1));assert not recovered
    assert not action("recovered-other-process","read")
    rows.append({"name":"newer-success-clears-cooldown-for-another-process","passed":True})
    save(root/"summary.json",{"allPassed":True,"caseCount":len(rows),"cases":rows,"testExeSha256":hashlib.sha256(exe.read_bytes()).hexdigest(),"concurrentWorkerProcesses":24,"finalSharedCooldown":merged,"realNetworkUsed":False,"realSteamFilesModified":False,"productionDataModified":False})
    print("ENDPOINT_CONCURRENCY_OK",len(rows),"cases; 24 concurrent worker processes")
if __name__=="__main__":main()
