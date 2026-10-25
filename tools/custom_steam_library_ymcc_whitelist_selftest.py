"""Bounded offline tests of the production library-owned YMCC whitelist publisher.
Runs exact extracted production I/O and the production header in owned fixture
paths only. No worker, Steam, game, installed file or process scan is executed.
"""
from pathlib import Path
import argparse
import concurrent.futures
import hashlib
import json
import os
import re
import subprocess
import sys
import uuid


def sha(data):
    return hashlib.sha256(data).hexdigest()


def masked(source):
    tokens = re.compile(r'//[^\n]*|/\*.*?\*/|(?:u8|u|U|L)?R"(?P<tag>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=tag)"|"(?:\\.|[^"\\])*"|\x27(?:\\.|[^\x27\\])*\x27', re.S)
    return tokens.sub(lambda m: ''.join('\n' if c == '\n' else ' ' for c in m.group()), source)


def extract(source, pattern, semicolon=False):
    clean = masked(source)
    hits = list(re.finditer(pattern, clean))
    if len(hits) != 1:
        raise ValueError('Missing/ambiguous production definition: ' + pattern)
    start = hits[0].start()
    opening = clean.index('{', hits[0].start())
    end, depth = opening + 1, 1
    while depth and end < len(clean):
        depth += (clean[end] == '{') - (clean[end] == '}')
        end += 1
    if depth:
        raise ValueError('Unterminated production definition')
    if semicolon:
        while clean[end].isspace():
            end += 1
        if clean[end] != ';':
            raise ValueError('Missing production semicolon')
        end += 1
    return source[start:end]


def function(source, name):
    clean = masked(source)
    found = []
    for hit in re.finditer(r'\bstatic\b[^;{}]*?\b' + re.escape(name) + r'\s*\(', clean):
        index, depth = clean.index('(', hit.start()) + 1, 1
        while depth and index < len(clean):
            depth += (clean[index] == '(') - (clean[index] == ')')
            index += 1
        tail = re.match(r'\s*(?:noexcept\s*)?\{', clean[index:])
        if not tail:
            continue
        opening = index + tail.end() - 1
        end, depth = opening + 1, 1
        while depth and end < len(clean):
            depth += (clean[end] == '{') - (clean[end] == '}')
            end += 1
        if depth:
            raise ValueError('Unterminated production function: ' + name)
        found.append(source[hit.start():end])
    if len(found) != 1:
        raise ValueError('Missing/ambiguous production function: ' + name)
    return found[0]


CPP = r'''
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "json.hpp"
#include "custom_steam_library_paths.h"
#include "production-helper.h"
using json=nlohmann::json;
namespace fs=std::filesystem;
namespace member=custom_steam_library::ymcc_whitelist;
static std::wstring ownedMutexName;
static std::wstring faultDestination;
static fs::path manualRewritePath;
static std::string manualRewriteText;
static std::atomic<int> moves{0};
static HANDLE WINAPI ownedCreateMutex(LPSECURITY_ATTRIBUTES a,BOOL own,LPCWSTR) {
    if(ownedMutexName.empty()) throw std::runtime_error("test mutex namespace missing");
    return CreateMutexW(a,own,ownedMutexName.c_str());
}
static BOOL WINAPI ownedMove(LPCWSTR source,LPCWSTR destination,DWORD flags) {
    if(!faultDestination.empty()&&fs::path(destination)==fs::path(faultDestination)) {SetLastError(ERROR_WRITE_FAULT);return FALSE;}
    if(!manualRewritePath.empty()&&fs::path(destination).filename()==L"ymcc-game-whitelist-membership.json") {
        std::ofstream changed(manualRewritePath,std::ios::binary);changed<<manualRewriteText;changed.close();manualRewritePath.clear();
    }
    ++moves;return MoveFileExW(source,destination,flags);
}
#define CreateMutexW ownedCreateMutex
#define MoveFileExW ownedMove
static fs::path gameWhitelistPath(){throw std::runtime_error("installed destination forbidden in selftest");}
@@EXTRACTED@@
#undef CreateMutexW
#undef MoveFileExW
static std::vector<std::string> names;
static void check(bool ok,const std::string& name){if(!ok)throw std::runtime_error(name);names.push_back(name);}
static std::string fileText(const fs::path& p){const auto b=readBinaryFile(p);return {b.begin(),b.end()};}
static void fileText(const fs::path& p,const std::string& text){writeAtomic(p,{text.begin(),text.end()});}
static json row(const std::string& exe,const std::string& status="already-in-steam") {
 return {{"primaryExecutable",exe},{"status",status},{"primarySelectionStatus","ready"},{"primaryRole","main-game"}};
}
static json event(const std::string& account,json items,const std::string& mode="steam-library-add-plan") {
 return {{"accountId",account},{"mode",mode},{"realSteamTarget",true},{"committed",mode!="steam-library-add-plan"},{"items",items}};
}
static member::Update applyEvent(const json& state,json& payload,bool full=true){member::prepareLiveEvent(payload,state,full);return member::applyPrepared(state,payload,normalizeGameRule);}
static json stateWith(json state,const std::string& account,json rows,bool full=true){auto p=event(account,rows);auto next=applyEvent(state,p,full);check(next.accepted,"live authority accepted");return next.membership;}
static std::set<std::string> rules(const json& state){return member::unionRules(state);}
int wmain(int argc,wchar_t** argv){
 try {
  if(argc<3)throw std::runtime_error("fixture directory and owned mutex required");
  const fs::path root=fs::absolute(argv[1]);ownedMutexName=argv[2];
  const auto data=root/L"data", whitelist=root/L"game-whitelist.txt";
  if(argc==5&&std::wstring(argv[3])==L"--account") {
   const auto account=toUtf8(argv[4]);auto payload=event(account,json::array({row("D:\\Games\\"+account+".exe")}));
   publishYmccGameWhitelist(payload,data,true,whitelist);
   if(payload["ymccWhitelistPublication"]["status"]=="warning")throw std::runtime_error("child publication failed");
   std::cout<<payload.dump()<<"\n";return 0;
  }
  fs::create_directories(root);
  const auto empty=member::emptyMembership();
  check(normalizeGameRule("D:\\Games\\WUxia.EXE")=="wuxia","production basename normalization");
  check(!member::validRule("bad#comment")&&!member::validRule("bad*")&&!member::validRule("bad.exe"),"managed rules reject comments/globs/extensions");
  auto state=empty;
  for(const auto& status:std::vector<std::string>{"ready-to-add","ready","failed","needs-primary-confirmation","needs-identity-and-artwork","needs-steam-verification","ready-not-selected","artwork-updated"}){
   auto p=event("filter",json::array({row("D:\\Games\\NotOwned.exe",status)}));auto next=applyEvent(empty,p);
   check(next.accepted&&rules(next.membership).empty(),"reject status "+status);
  }
  for(const auto& role:std::vector<std::string>{"launcher","helper","installer","modifier","mod-manager","crash-handler","updater",""}){
   auto r=row("D:\\Games\\Tool.exe");r["primaryRole"]=role;auto p=event("roles",json::array({r}));auto next=applyEvent(empty,p);
   check(rules(next.membership).empty(),"reject primary role "+role);
  }
  for(const auto& field:std::vector<std::string>{"unrecognizedTool","isLauncher","isTool"}){
   auto r=row("D:\\Games\\Tool.exe");r[field]=true;auto p=event("flags",json::array({r}));check(rules(applyEvent(empty,p).membership).empty(),"reject tool flag "+field);
  }
  auto wrongPrimary=row("D:\\Games\\Choice.exe");wrongPrimary["primarySelectionStatus"]="needs-primary-exe-confirmation";
  auto p=event("primary",json::array({wrongPrimary}));check(rules(applyEvent(empty,p).membership).empty(),"unselected primary excluded");
  auto builtin=row("D:\\Games\\Redist.exe");builtin["nativeSteamAppId"]=228980;
  p=event("tool",json::array({builtin}));check(rules(applyEvent(empty,p).membership).empty(),"builtin Steam tool excluded");
  auto native=row("D:\\Games\\Native.exe","steam-native");native["steamNative"]=true;
  p=event("native",json::array({native}));check(rules(applyEvent(empty,p).membership)==std::set<std::string>{"native"},"owned native Steam semantics");
  for(const auto& path:std::vector<std::string>{"game.exe","D:\\Games\\*.exe","D:\\Games\\bad?.exe","D:\\Games\\bad\n.exe","D:\\Games\\..\\bad.exe"}){
   p=event("path",json::array({row(path)}));check(rules(applyEvent(empty,p).membership).empty(),"unsafe executable path excluded");
  }
  state=stateWith(state,"A",json::array({row("D:\\Games\\Keep.exe"),row("D:\\Games\\Remove.exe")}));
  state=stateWith(state,"B",json::array({row("E:\\Games\\Other.exe"),row("E:\\Games\\Remove.exe")}));
  check(rules(state)==std::set<std::string>{"keep","other","remove"},"account union and basename deduplication");
  auto historical=event("A",json::array({row("D:\\Games\\Keep.exe"),row("D:\\Games\\Remove.exe")}));member::prepareLiveEvent(historical,state,true);
  auto deletion=event("A",json::array({row("D:\\Games\\Remove.exe","ready-to-delete")}),"steam-library-delete-commit");
  auto update=applyEvent(state,deletion,false);state=update.membership;
  check(!state["accounts"]["A"]["entries"].contains("d:/games/remove.exe")&&state["accounts"]["A"]["entries"].size()==1,"delete only matching account executable");
  check(rules(state).contains("remove")&&rules(state).contains("other"),"delete retains same basename other account");
  auto replay=member::applyPrepared(state,historical,normalizeGameRule);
  check(!replay.accepted&&replay.reason=="stale-account-revision"&&replay.membership==state,"historical last result cannot resurrect deletion");
  auto partialNegative=row("D:\\Games\\Keep.exe","ready-to-add");partialNegative["ymccSteamMembership"]="absent";
  p=event("A",json::array({partialNegative}));auto partial=applyEvent(state,p,false);
  check(partial.membership["accounts"]["A"]["entries"].empty(),"partial fresh explicit absence revokes same executable");
  check(partial.membership["accounts"]["B"]==state["accounts"]["B"],"partial absence preserves absent account entries");
  p=event("A",json::array());partial=applyEvent(state,p,false);check(partial.membership==state,"partial missing inventory never clears absent members");
  p=event("A",json::array());auto complete=applyEvent(state,p,true);
  check(complete.membership["accounts"]["A"]["entries"].empty()&&complete.membership["accounts"]["B"]==state["accounts"]["B"],"full empty authority clears only current account");
  auto art=event("A",json::array({row("D:\\Games\\Injected.exe")}),"steam-library-artwork-refresh-commit");member::prepareLiveEvent(art,state,true);
  check(!member::applyPrepared(state,art,normalizeGameRule).accepted,"artwork result cannot add or clear members");
  p=event("A",json::array({row("D:\\Games\\Injected.exe")}));check(!member::applyPrepared(state,p,normalizeGameRule).accepted,"cached unstamped payload has no membership authority");
  p=event("A",json::array({row("D:\\Games\\Injected.exe")}));p["realSteamTarget"]=false;check(!applyEvent(state,p).accepted,"simulation never publishes managed rules");
  json scan={{"games",json::array({json{{"scanComplete",true}}})},{"roots",json::array({json{{"scanComplete",true}}})}};
  check(member::completeInventory(scan,json::array({json::object()})),"complete inventory proven");scan["missingGames"]=json::array({json::object()});
  check(!member::completeInventory(scan,json::array({json::object()})),"missing games prevent whole-account replacement");
  const std::string manual="# user custom rules\r\nManual.EXE\r\nmanual*\r\n";
  auto text=member::renderManagedBlock(manual,{"keep","remove"});
  check(text.starts_with(manual)&&text.find("keep\r\n")!=std::string::npos,"manual bytes and CRLF preserved");
  check(member::renderManagedBlock(text,{})==manual,"empty union removes only managed block");
  const std::string rewritten="manual\nremove\n";
  check(member::renderManagedBlock(rewritten,{"keep"}).starts_with(rewritten),"marker lost preserves all naked historical rules");
  const std::string duplicate="remove\n"+member::renderManagedBlock("",{"remove"});
  check(member::renderManagedBlock(duplicate,{})=="remove\n","manual duplicate of managed rule survives deletion");
  bool refused=false;try{member::renderManagedBlock(std::string(member::beginMarker)+"\nold\n",{});}catch(...){refused=true;}
  check(refused,"incomplete markers fail closed");
  refused=false;try{member::renderManagedBlock(text+text,{});}catch(...){refused=true;}
  check(refused,"duplicate managed blocks fail closed");
  const auto actual=loadJsonDocument(root.parent_path()/L"wuxia-input.json");
  auto wuxia=actual["savedPlanItem"];
  p=event("Wuxia",json::array({wuxia}));check(rules(applyEvent(empty,p,false).membership).empty(),"historical Wuxia item missing primaryRole is not fresh authority");
  wuxia["primaryRole"]=actual["scanGame"]["primaryRole"];
  p=event("Wuxia",json::array({wuxia}));auto wuxiaState=applyEvent(empty,p,false).membership;
  check(rules(wuxiaState)==std::set<std::string>{"wuxia"},"actual Wuxia owned plan with new builder primaryRole publishes wuxia");
  auto absentWuxia=wuxia;absentWuxia["status"]="needs-identity-and-artwork";absentWuxia["ymccSteamMembership"]="absent";
  p=event("Wuxia",json::array({absentWuxia}));
  check(rules(applyEvent(wuxiaState,p,false).membership).empty(),"actual Wuxia explicit absent under partial plan removes old managed rule");
  // Exact production publisher and Win32 atomic writer, owned fixture paths only.
  fs::create_directories(data/L"state");fileText(whitelist,manual);
  fileText(root/L"player-blacklist.txt","do-not-touch-player\n");fileText(root/L"system-blacklist.txt","do-not-touch-system\n");
  p=event("A",json::array({row("D:\\Games\\First.exe")}));publishYmccGameWhitelist(p,data,true,whitelist);
  check(p["ymccWhitelistPublication"]["status"]=="updated"&&fileText(whitelist).starts_with(manual),"real production atomic publication preserves manual");
  moves=0;p=event("A",json::array({row("D:\\Games\\First.exe")}));publishYmccGameWhitelist(p,data,true,whitelist);
  check(p["ymccWhitelistPublication"]["status"]=="unchanged"&&moves==0,"unchanged ownership and managed text perform zero writes");
  const auto statePath=data/L"state"/L"ymcc-game-whitelist-membership.json";
  const auto beforeState=fileText(statePath),beforeRules=fileText(whitelist);
  faultDestination=statePath.wstring();p=event("A",json::array({row("D:\\Games\\Failed.exe")}),"steam-library-add-commit");publishYmccGameWhitelist(p,data,false,whitelist);faultDestination.clear();
  check(p["committed"]==true&&p["ymccWhitelistPublication"]["status"]=="warning"&&fileText(statePath)==beforeState&&fileText(whitelist)==beforeRules,"snapshot failure preserves files and successful Steam result");
  faultDestination=whitelist.wstring();p=event("A",json::array({row("D:\\Games\\First.exe","ready-to-delete")}),"steam-library-delete-commit");publishYmccGameWhitelist(p,data,false,whitelist);faultDestination.clear();
  check(p["committed"]==true&&p["ymccWhitelistPublication"]["status"]=="warning"&&fileText(whitelist)==beforeRules,"rules activation failure does not roll back Steam deletion");
  p=event("A",json::array());publishYmccGameWhitelist(p,data,true,whitelist);
  check(fileText(whitelist)==manual&&p["ymccWhitelistPublication"]["rulesFileChanged"]==true,"next ordinary authority retries stale block after auxiliary failure");
  check(fileText(root/L"player-blacklist.txt")=="do-not-touch-player\n"&&fileText(root/L"system-blacklist.txt")=="do-not-touch-system\n","all success/failure paths leave player and system blacklists unchanged");
  fileText(whitelist,rewritten);p=event("A",json::array({row("D:\\Games\\New.exe")}));publishYmccGameWhitelist(p,data,true,whitelist);
  check(fileText(whitelist).starts_with(rewritten),"production publisher protects manual UI marker-loss rewrite");
  manualRewritePath=whitelist;manualRewriteText="new-concurrent-manual\n";
  p=event("A",json::array({row("D:\\Games\\Changed.exe")}),"steam-library-add-commit");publishYmccGameWhitelist(p,data,false,whitelist);
  check(p["committed"]==true&&p["ymccWhitelistPublication"]["status"]=="warning"&&fileText(whitelist)==manualRewriteText,"destination recheck preserves concurrent manual rewrite and Steam success");
  p=event("A",json::array({row("D:\\Games\\Changed.exe")}));publishYmccGameWhitelist(p,data,true,whitelist);
  check(fileText(whitelist).starts_with(manualRewriteText)&&p["ymccWhitelistPublication"]["status"]=="updated","next existing library event reconciles managed block after manual race");
  std::cout<<json{{"passed",true},{"checks",names.size()},{"names",names},{"productionHelperExecuted",true},{"exactProductionIoExecuted",true},{"steamActions",0},{"gameActions",0},{"processScans",0}}.dump(2)<<"\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<json{{"passed",false},{"failed",e.what()},{"checksPassedBeforeFailure",names.size()}}.dump()<<"\n";return 1;}
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--wuxia-state', type=Path, default=Path(r'D:/YeMan/CustomSteamLibrary/data/state/library-scan.json'))
    parser.add_argument('--wuxia-plan', type=Path, default=Path(r'D:/YeMan/CustomSteamLibrary/data/state/steam-add-plan.json'))
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    out = args.output.resolve()
    if out.exists() and any(out.iterdir()):
        parser.error('use a fresh output directory; preserve earlier failures')
    if out == repo or out.is_relative_to(repo) or repo.is_relative_to(out):
        parser.error('output must be outside the product source tree')
    out.mkdir(parents=True, exist_ok=True)
    lab_path = repo/'native/custom-steam-library/steam_artwork_lab.cpp'
    host_path = repo/'native/custom-steam-library/workspace_host.cpp'
    helper_path = repo/'native/custom-steam-library/custom_steam_library_ymcc_whitelist.h'
    sources = {str(p): p.read_bytes() for p in [lab_path, host_path, helper_path, Path(__file__).resolve(), repo/'native/custom-steam-library/custom_steam_library_exclusions.h', repo/'native/custom-steam-library/custom_steam_library_paths.h', repo/'deps/json/json.hpp']}
    lab = sources[str(lab_path)].decode('utf-8-sig')
    host = sources[str(host_path)].decode('utf-8-sig')
    parts = [function(lab, n) for n in ('toUtf8','toWide','unicodeLower','trimWide','normalizeGameRule')]
    parts += [extract(lab, r'\bclass\s+DataTransactionMutex\s*\{', True), extract(lab, r'\bstruct\s+ScopedStagedFiles\s*\{', True)]
    parts += [function(lab, n) for n in ('writeBytesWin32','writeAtomic','writeJsonAtomic')]
    parts += [extract(lab, r'\bclass\s+SharedBinaryReadFile\s*\{', True)]
    parts += [function(lab, n) for n in ('readBinaryFile','loadJsonDocument','publishYmccGameWhitelist')]
    write_json = lambda p, d: p.write_text(json.dumps(d, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    write_json(out/'source-bindings.json', {'sources': [{'path': name, 'sha256': sha(data), 'bytes': len(data)} for name,data in sources.items()], 'exactExtractions': [{'sha256':sha(p.encode()),'text':p} for p in parts], 'mockedBoundaries': ['test-only mutex namespace', 'destination rename failure injection'], 'actualIo': 'exact Win32 writer/atomic replace and production transaction mutex', 'productionCompileLink': False})
    actual_state = json.loads(args.wuxia_state.read_text(encoding='utf-8-sig'))
    actual_plan = json.loads(args.wuxia_plan.read_text(encoding='utf-8-sig'))
    game = next(r for r in actual_state['games'] if str(r.get('primaryExecutable','')).lower().endswith('\\wuxia.exe'))
    item = next(r for r in actual_plan['items'] if str(r.get('primaryExecutable','')).lower() == game['primaryExecutable'].lower())
    write_json(out/'wuxia-input.json', {'scanGame':game,'savedPlanItem':item,'roots':actual_state.get('roots'), 'missingGamesCount':len(actual_state.get('missingGames',[])), 'stateSource':str(args.wuxia_state),'stateSha256':sha(args.wuxia_state.read_bytes()),'savedPlanSource':str(args.wuxia_plan),'savedPlanSha256':sha(args.wuxia_plan.read_bytes()),'historicalRecordOnly':True,'noLiveGameOrSteamRead':True})
    cpp = CPP.replace('@@EXTRACTED@@','\n'.join(parts))
    # Lifecycle wiring is structural evidence, separately reported from executed helpers.
    build = function(lab,'buildSteamLibraryAddPlan')
    add = function(lab,'commitPreparedSteamAddPlan')
    delete = function(lab,'commitSteamLibraryDeletePlan')
    wrapper = function(lab,'publishYmccGameWhitelist')
    lifecycle = {
        'plan_read_publish_serialized': build.index('DataTransactionMutex membershipTransaction') < build.index('loadLibraryScanState') < build.index('publishYmccGameWhitelist'),
        'builder_copies_primary_role': '{"primaryRole", jsonStringOr(game, "primaryRole")}' in build,
        'fresh_absence_only_after_existing_shortcut_lookup': build.index('existingByExecutable.find') < build.index('item["ymccSteamMembership"] = "absent"') < build.index('auto manifest ='),
        'plan_publication_before_persisting_result': build.index('publishYmccGameWhitelist') < build.rindex('writeJsonAtomic(outputPath, plan)'),
        'add_after_durable_complete_and_status_added': add.index('transaction.complete.json') < add.index('item["status"] = "added-to-steam"') < add.index('publishYmccGameWhitelist') < add.index('publishCommittedSteamResult'),
        'delete_after_durable_completion': delete.index('transaction.complete.json') < delete.index('plan["committed"] = true') < delete.index('publishYmccGameWhitelist') < delete.index('publishCommittedSteamResult'),
        'artwork_never_publishes': 'publishYmccGameWhitelist' not in function(lab,'refreshExistingSteamArtwork'),
        'local_artwork_snapshot_never_publishes': 'publishYmccGameWhitelist' not in function(host,'buildLocalSteamPlanSnapshot'),
        'startup_actual_authoritative_refresh': re.search(r'if \(command == "startup"\)\s*\{[^}]*return workspaceSnapshot\(true, json::object\(\), nullptr, true\);',host,re.S) is not None,
        'open_library_existing_pipeline_refresh': 'startLibraryPipeline(true)' in host and 'workspaceSnapshot(true, json::object(), job)' in function(host,'startLibraryPipeline'),
        'publisher_no_new_scanner_process_timer': re.search(r'\b(?:CreateProcess\w*|ShellExecute\w*|CreateToolhelp32Snapshot|Process32First\w*|SetTimer|Sleep)\s*\(|directory_iterator|scanLibraryRoots|runWorker',wrapper) is None,
        'publisher_reads_only_two_existing_files': wrapper.count('readBinaryFile(')==2 and wrapper.count('loadJsonDocument(')==1,
        'snapshot_before_rules_activation': wrapper.index('if (membershipChanged) writeJsonAtomic') < wrapper.index('writeAtomic(destination'),
        'all_account_rmw_locked': wrapper.index('DataTransactionMutex transaction') < wrapper.index('loadJsonDocument'),
        'destination_rechecked_before_activation': wrapper.index('if (latest != originalBytes)') < wrapper.index('writeAtomic(destination'),
        'blacklists_and_legacy_sync_not_called_by_publisher': not re.search(r'playerBlacklistPath|blacklist|commitGameWhitelistSync|planGameWhitelistSync',wrapper),
    }
    write_json(out/'lifecycle-wiring.json', {'scope':'source wiring only; no full Steam commit runtime executed','checks':lifecycle,'passed':all(lifecycle.values())})
    sdk=Path(r'C:/Program Files (x86)/Windows Kits/10')
    vc=Path(r'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207')
    env=os.environ.copy()
    for key in list(env):
        if key.upper() in ('CL','_CL_','LINK','_LINK_'):
            del env[key]
    env.update(INCLUDE=';'.join(map(str,[vc/'include',sdk/'Include/10.0.26100.0/um',sdk/'Include/10.0.26100.0/shared',sdk/'Include/10.0.26100.0/ucrt'])),LIB=';'.join(map(str,[vc/'lib/x64',sdk/'Lib/10.0.26100.0/um/x64',sdk/'Lib/10.0.26100.0/ucrt/x64'])),VSCMD_SKIP_SENDTELEMETRY='1',PYTHONDONTWRITEBYTECODE='1')
    temp=out/'tmp';temp.mkdir();env['TEMP']=env['TMP']=str(temp)
    variants = [
        ('baseline',None),
        ('accept-ready',('if (status != "added-to-steam" && status != "already-in-steam" && status != "steam-native") continue;', 'if (false) continue;')),
        ('ignore-stale-revision',('if ((*authority)["baseRevision"].get<std::uint64_t>() != previousRevision)', 'if (false)')),
        ('ignore-explicit-absence',('if (mode == "steam-library-add-plan" && text(row, "ymccSteamMembership") == "absent" &&', 'if (false && text(row, "ymccSteamMembership") == "absent" &&')),
    ]
    results=[]
    mutex='Local\\YMCC-Membership-Owned-'+uuid.uuid4().hex
    for name,mutation in variants:
        case=out/name;case.mkdir()
        header=sources[str(helper_path)].decode('utf-8-sig')
        if mutation:
            if header.count(mutation[0])!=1:
                raise ValueError('Nonunique mutation '+name)
            header=header.replace(*mutation,1)
        (case/'production-helper.h').write_text(header,encoding='utf-8')
        source=case/'fixture.cpp';source.write_text(cpp,encoding='utf-8')
        exe=case/'fixture.exe'
        command=[str(vc/'bin/Hostx64/x64/cl.exe'),'/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/W3',str(source),'/I'+str(repo/'deps/json'),'/I'+str(repo/'native/custom-steam-library'),'/Fo'+str(case/'fixture.obj'),'/Fe'+str(exe),'/link','/INCREMENTAL:NO']
        write_json(case/'compile-command.json',{'command':command,'cwd':str(case)})
        built=subprocess.run(command,cwd=case,env=env,capture_output=True,timeout=90)
        (case/'compile.stdout').write_bytes(built.stdout);(case/'compile.stderr').write_bytes(built.stderr)
        entry={'name':name,'compileReturnCode':built.returncode,'passed':False}
        if built.returncode==0:
            fixture_root=case/'owned-fixture'
            # The exact recorded Wuxia input is shared read-only with all variants.
            (case/'wuxia-input.json').write_bytes((out/'wuxia-input.json').read_bytes())
            ran=subprocess.run([str(exe),str(fixture_root),mutex],cwd=case,env=env,capture_output=True,timeout=30)
            (case/'run.stdout').write_bytes(ran.stdout);(case/'run.stderr').write_bytes(ran.stderr)
            entry['returnCode']=ran.returncode
            entry['passed']=ran.returncode==(0 if mutation is None else 1)
            if mutation is None and ran.returncode==0:
                entry['result']=json.loads(ran.stdout)
        results.append(entry)
        if name=='baseline' and not entry['passed']:
            break
    concurrent_result={'executed':False,'passed':False}
    if results[0]['passed']:
        root=out/'parallel-owned-fixture';root.mkdir()
        (root/'game-whitelist.txt').write_text('manual-preserved\n',encoding='utf-8')
        exe=out/'baseline/fixture.exe'
        def publish(account):
            ran=subprocess.run([str(exe),str(root),mutex,'--account',account],env=env,cwd=out,capture_output=True,timeout=45)
            (out/('parallel-'+account+'.stdout')).write_bytes(ran.stdout)
            (out/('parallel-'+account+'.stderr')).write_bytes(ran.stderr)
            return ran.returncode
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
            codes=list(executor.map(publish,['account'+str(i) for i in range(16)]))
        state=json.loads((root/'data/state/ymcc-game-whitelist-membership.json').read_text(encoding='utf-8'))
        text=(root/'game-whitelist.txt').read_text(encoding='utf-8')
        concurrent_result={'executed':True,'processes':16,'exitCodes':codes,'accountsPreserved':len(state['accounts']),'manualPreserved':text.startswith('manual-preserved\n'),'eachManagedRulePresent':all('\naccount'+str(i)+'\n' in text for i in range(16))}
        concurrent_result['passed']=all(code==0 for code in codes) and concurrent_result['accountsPreserved']==16 and concurrent_result['manualPreserved'] and concurrent_result['eachManagedRulePresent']
    drift=[name for name,data in sources.items() if Path(name).read_bytes()!=data]
    passed=len(results)==4 and all(r['passed'] for r in results) and concurrent_result['passed'] and all(lifecycle.values()) and not drift
    report={'schema':'ymcc.library-owned-whitelist-publisher-selftest.v1','passed':passed,'variants':results,'crossProcessAccounts':concurrent_result,'lifecycleWiring':lifecycle,'sourceDrift':drift,'productionHelperExecuted':bool(results[0].get('result')),'productionCompileLink':False,'deployed':False,'liveSteamOrGameOrDeviceActions':0,'installedWhitelistTouched':False,'scope':'exact production helper and extracted publisher/I/O in owned paths; full Steam lifecycle wiring source-only','wuxiaInput':str(out/'wuxia-input.json')}
    write_json(out/'result.json',report)
    print(json.dumps(report,ensure_ascii=False))
    return 0 if passed else 1

if __name__=='__main__':
    raise SystemExit(main())