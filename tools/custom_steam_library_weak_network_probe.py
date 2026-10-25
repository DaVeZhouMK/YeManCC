"""Exercise the shipping metadata worker with bounded, synthetic endpoint cooldowns.
No games are launched; all writes stay in Mainline/Build. An optional genuine,
still-fresh AppDetails cache tests verified identity reuse during an outage.
"""
import argparse, datetime, hashlib, json, os, pathlib, shutil, subprocess, time
BUILD=(pathlib.Path(__file__).resolve().parents[1]/"../../Build").resolve()
def load(path): return json.loads(path.read_text("utf-8-sig"))
def save(path,value):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding="utf-8")
def network_attempts(value):
    if isinstance(value,list):
        for child in value: yield from network_attempts(child)
    elif isinstance(value,dict):
        if "attempt" in value and "freshSession" in value: yield value
        for child in value.values(): yield from network_attempts(child)
def run(worker,exe,root,label,details=None,shared_data=None):
    data=shared_data or root/label/"data";jobs=root/label/"jobs"
    now=int(time.time()*1000)
    for path in ([] if shared_data else ["/api/appdetails","/api/storesearch","/search/results"]):
        endpoint="https://store.steampowered.com:443"+path
        name=hashlib.sha256(endpoint.encode()).hexdigest()+".json"
        save(data/"cache/network-backoff"/name,{"schemaVersion":1,"endpoint":endpoint,"status":503,"observedAt":now,"retryAt":now+120000,"identityAuthority":False})
    if details:
        record=load(details);at=record.get("fetchedAt",0)
        assert 0<at<=now and now-at<=12*60*60*1000,"Use a genuinely fresh cache; never redate it"
        target=data/"cache/steam-appdetails"/details.name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(details,target)
    env=os.environ.copy();env["YEMAN_STEAM_BIG_PICTURE_DATA_ROOT"]=str(data)
    args=[str(worker),str(exe),str(jobs),"--metadata-only","--steam-comprehensive-identity-only","--fault-provider","playnite-igdb"]
    start=time.monotonic()
    p=subprocess.run(args,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=55)
    elapsed=round(time.monotonic()-start,3);(root/label/"worker.log").write_bytes(p.stdout)
    documents=[load(f) for name in ["manifest.json","rounds-failed.json"] for f in jobs.rglob(name)]
    traces=[a for d in documents for a in network_attempts(d)]
    manifests=[d for d in documents if isinstance(d.get("match"),dict)]
    match=next((d["match"] for d in manifests if d["match"].get("steamVerificationStatus")=="steam-verified"),{})
    row={"case":label,"exit":p.returncode,"seconds":elapsed,"command":args,"syntheticCooldown":True,"dataRoot":str(data),"cooldownSeededThisCase":not bool(shared_data),"expectedIdPassedToWorker":False,"networkAttempts":len(traces),"suppressedRequests":sum(a.get("skippedNetwork",False) for a in traces),"match":match,"failures":[d["failure"] for d in documents if "failure" in d]}
    if details:
        assert p.returncode==0 and match.get("identityStatus")=="verified",row
    else:
        assert p.returncode!=0 and not match and traces and all(a.get("skippedNetwork",False) for a in traces),row
        assert any(f.get("retryWhenNetworkReturns") for f in row["failures"]),row
    save(root/label/"result.json",row);return row
if __name__=="__main__":
    parser=argparse.ArgumentParser();parser.add_argument("--root",required=True);parser.add_argument("--worker",required=True);parser.add_argument("--exe",required=True);parser.add_argument("--details-cache")
    args=parser.parse_args();root=pathlib.Path(args.root).resolve();worker=pathlib.Path(args.worker).resolve();exe=pathlib.Path(args.exe).resolve()
    assert root.is_relative_to(BUILD) and not root.exists(),"Use a fresh isolated Build directory"
    assert worker.is_file() and exe.is_file();root.mkdir(parents=True)
    rows=[run(worker,exe,root,"unverified-during-outage"),run(worker,exe,root,"new-process-during-outage",shared_data=root/"unverified-during-outage/data")]
    if args.details_cache: rows.append(run(worker,exe,root,"verified-cache-during-outage",pathlib.Path(args.details_cache)))
    summary={"allPassed":True,"checkedAt":datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat(),"workerSha256":hashlib.sha256(worker.read_bytes()).hexdigest(),"cases":rows,"realSteamFilesModified":False,"realGamesLaunched":False,"productionCacheModified":False,"scope":"Shipping EXE, real local executable, synthetic scheduling records; metadata HTTP calls suppressed, not a real network fault measurement"}
    save(root/"summary.json",summary);print(json.dumps(summary,ensure_ascii=True,indent=2))
