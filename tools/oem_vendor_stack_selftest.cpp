static json msiShieldReadServiceInventory(){json rows=json::array();for(const auto& [name,v]:services)rows.push_back({{"service",W2U(name)},{"startType",v.type},{"serviceState",v.state},{"mutationPolicy","preserve-unverified-dependency"}});return {{"readOnly",true},{"complete",!scmFail},{"services",rows},{"discoveryAuthorizesMutation",false}};}
static json msiShieldReadProtectedService(){auto i=services.find(L"MSI Foundation Service");if(scmFail)return {{"readOnly",true},{"readbackVerified",false},{"existsKnown",false}};if(i==services.end())return {{"readOnly",true},{"readbackVerified",false},{"existsKnown",true},{"exists",false}};return {{"readOnly",true},{"readbackVerified",!i->second.deny&&!i->second.queryFail},{"existsKnown",true},{"exists",true},{"startType",i->second.type},{"serviceState",i->second.state},{"controllerOnlyRoleVerified",false}};}
#ifdef YMCC_VENDOR_R15
static unsigned taskDiscoveryCalls = 0;
static std::wstring deniedTaskFolder;
static OemVendorTaskDiscovery oemVendorTaskDiscover(const std::vector<std::wstring>& names) {
    ++taskDiscoveryCalls;
    return oemVendorTaskDiscoverWithReader(names, [&](const std::wstring& folder, OemVendorTaskFolderFacts& facts) {
        if (!deniedTaskFolder.empty() && oemVendorTaskPathEqual(folder, deniedTaskFolder)) { SetLastError(ERROR_ACCESS_DENIED); return false; }
        for (const auto& [key, value] : tasks) {
            const std::wstring path = key.front() == L'\\' ? key : L"\\" + key;
            const auto split = path.find_last_of(L'\\');
            const auto parent = split == 0 ? L"\\" : path.substr(0, split);
            if (oemVendorTaskPathEqual(parent, folder)) { facts.tasks.push_back(path); continue; }
            const std::wstring prefix = folder == L"\\" ? folder : folder + L"\\";
            if (path.rfind(prefix, 0) != 0) continue;
            const auto next = path.find(L'\\', prefix.size());
            if (next == std::wstring::npos) continue;
            const auto child = path.substr(0, next);
            if (std::find(facts.folders.begin(), facts.folders.end(), child) == facts.folders.end()) facts.folders.push_back(child);
        }
        return true;
    });
}
static bool oemVendorTaskQuery(const std::wstring& name,OemVendorTaskFacts& facts){++taskQueries;facts={};auto port=taskXmlPorts.find(name);if(port!=taskXmlPorts.end()&&((port->second.removed&&port->second.queryDeniedAfterRemove)||(port->second.imported&&port->second.queryDeniedAfterImport))){SetLastError(ERROR_ACCESS_DENIED);return false;}auto it=tasks.find(name);if(it==tasks.end())return true;if(it->second.deny){SetLastError(ERROR_ACCESS_DENIED);return false;}facts={true,it->second.enabled,it->second.running};return true;}
static bool oemVendorTaskSet(const std::wstring& name,bool enabled,bool stop,bool run){auto it=tasks.find(name);if(it==tasks.end()){SetLastError(ERROR_FILE_NOT_FOUND);return false;}auto& t=it->second;++t.setCalls;if(t.setFail||(t.failSetCall&&t.setCalls==t.failSetCall)){SetLastError(ERROR_ACCESS_DENIED);return false;}t.enabled=enabled;++taskMutations;if(stop&&!t.stuck)t.running=false;if(run){if(t.runFail){SetLastError(ERROR_ACCESS_DENIED);return false;}t.running=true;++taskStarts;}return true;}
static bool fixtureTaskJournalFlag(const std::wstring& name,const char* field) {
    const auto doc=json::parse(journal,nullptr,false);
    if(doc.is_discarded()||!doc.is_object()||!doc.contains("tasks")||!doc["tasks"].is_array())return false;
    for(const auto& entry:doc["tasks"])if(entry.is_object()&&entry.value("name","")==W2U(name))
        return entry.contains(field)&&entry[field].is_boolean()&&entry[field].get<bool>();
    return false;
}
static bool oemVendorTaskRemovalReady(const std::wstring& name) {
    ++taskReadyCalls;auto& port=taskXmlPorts[name];
    const bool ok=!port.templateMissing&&!port.readyDenied;
    taskXmlTrace.push_back({{"op","ready"},{"task",W2U(name)},{"ok",ok},{"journalWrites",journalWrites}});
    if(!ok)SetLastError(port.readyDenied?ERROR_ACCESS_DENIED:ERROR_FILE_NOT_FOUND);
    return ok;
}
static bool oemVendorTaskRemove(const std::wstring& name) {
    ++taskRemovalCalls;auto& port=taskXmlPorts[name];const auto found=tasks.find(name);
    taskXmlTrace.push_back({{"op","remove"},{"task",W2U(name)},
        {"removePrepared",fixtureTaskJournalFlag(name,"removePrepared")},{"journalWrites",journalWrites},
        {"runningBefore",found!=tasks.end()&&found->second.running}});
    if(port.removeDenied||port.removeFail){SetLastError(port.removeDenied?ERROR_ACCESS_DENIED:ERROR_GEN_FAILURE);return false;}
    tick+=port.removeDelay;
    if(port.removeStillPresent)return true; // API success alone cannot prove absence.
    if(found!=tasks.end()){tasks.erase(found);++taskMutations;++taskRemovals;}
    port.removed=true;
    if(onTaskRemoved){auto work=std::move(onTaskRemoved);onTaskRemoved={};work();}
    return true;
}
static bool oemVendorTaskImport(const std::wstring& name) {
    ++taskImportCalls;auto& port=taskXmlPorts[name];
    taskXmlTrace.push_back({{"op","import"},{"task",W2U(name)},
        {"removePrepared",fixtureTaskJournalFlag(name,"removePrepared")},
        {"returnPrepared",fixtureTaskJournalFlag(name,"returnPrepared")},{"journalWrites",journalWrites}});
    if(port.templateMissing||port.importDenied||port.importFail){SetLastError(port.templateMissing?ERROR_FILE_NOT_FOUND:port.importDenied?ERROR_ACCESS_DENIED:ERROR_GEN_FAILURE);return false;}
    if(tasks.contains(name)){SetLastError(ERROR_ALREADY_EXISTS);return false;}
    if(port.importStillAbsent)return true;
    tasks.emplace(name,port.importedTask);++taskMutations;++taskImports;port.imported=true;
    return true;
}
static bool oemVendorProcessesStop(const std::vector<std::wstring>& names,unsigned long long epoch){++processCalls;if(processFail)return false;for(const auto& n:names)if(oemVendorStackIntentCurrent(epoch)&&std::find(processes.begin(),processes.end(),n)!=processes.end())stoppedProcesses.push_back(n);return true;}
#endif
static int cases=0,failed=0;
static void check(bool ok,const char* text){++cases;std::cout<<(ok?"PASS ":"FAIL ")<<text<<"\n";if(!ok)++failed;}
static bool state(const char* text){return std::any_of(logs.begin(),logs.end(),[&](const auto& x){return x.value("state","")==text;});}
static void reset(){g_oemVendorMsiTaskShieldPreferenceSeen=false;taskXmlPorts.clear();taskXmlTrace.clear();onTaskRemoved={};taskReadyCalls=taskRemovalCalls=taskRemovals=taskImportCalls=taskImports=0;journalReadFails=journalShortRead=journalDirectory=journalReparse=false;journalAttributeError=0;journalWrites=journalReads=0;g_oemVendorShieldWake = {}; g_powerGeneration = 1; g_powerLifecycle = PowerLifecycle::Ready; wakeInputResumes = 0; taskQueries = 0; services.clear();logs.clear();jobs.clear();journal.clear();tasks.clear();processes.clear();stoppedProcesses.clear();onPersist={};journalExists=false;journalFail=scmFail=queueFail=processFail=false;mutationCount=stops=starts=taskMutations=taskStarts=processCalls=0;g_exitRequested=false;tick=1;g_oemVendorServiceLeases.clear();g_oemVendorStackLoaded=false;g_oemVendorStackJournalValid=true;g_oemVendorStackWanted=false;g_oemVendorShortcutWanted=false;g_oemVendorRecordingWanted=false;g_oemVendorShieldWanted=false;g_oemVendorStackIntentControllerOnly=false;g_oemVendorStackLastOutcomeKnown=false;g_oemVendorStackLastOutcomeOk=false;g_oemVendorStackLastOutcomeEpoch=0;g_oemVendorStackLastOutcome="none";g_oemVendorMsiServiceInventory=json::object();g_oemVendorMsiProtectedServiceObservation=json::object();g_oemVendorStackOpQueued=false;g_oemVendorStackRequested=1;g_oemVendorStackCompleted=0;g_machineIdentity.family=YmccFamily::AsusRogAlly;g_machineIdentity.deviceClass="HandheldCompanion.Devices.ROGAlly";SetLastError(0);
#ifdef YMCC_VENDOR_R15
 g_oemVendorTaskLeases.clear();g_oemVendorTaskPaths.clear();g_oemVendorTaskDiscoveryComplete=false;taskDiscoveryCalls=0;deniedTaskFolder.clear();g_oemVendorStackIntentSeen=false;g_oemVendorStackStartupRecovered=false;g_oemVendorStackIntentFamily=YmccFamily::Unknown;g_oemVendorStackAttempts=0;g_oemVendorStackRetryAt=0;
#endif
}
static void seed(){
 if(g_machineIdentity.family==YmccFamily::MsiClaw)g_machineIdentity.deviceClass="HandheldCompanion.Devices.ClawA1M";
 else if(g_machineIdentity.family==YmccFamily::LenovoLegionGo)g_machineIdentity.deviceClass="HandheldCompanion.Devices.LegionGoTablet";
 else if(g_machineIdentity.family==YmccFamily::ZotacGamingZone)g_machineIdentity.deviceClass="HandheldCompanion.Devices.GamingZone";
 for(const auto& n:oemVendorServiceNamesForFamily(g_machineIdentity.family))services[n]={};}
static void drain(){size_t n=0;while(!jobs.empty()&&++n<32){auto f=std::move(jobs.front());jobs.erase(jobs.begin());f();}check(n<32,"serial queue remains bounded");}

static void stormNeed(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
static json stormServiceDocument(){return {{"schema",2},{"family",static_cast<int>(g_machineIdentity.family)},{"services",json::array({{{"name",W2U(oemVendorServiceNamesForFamily(g_machineIdentity.family).front())},{"originalStartType",SERVICE_AUTO_START},{"wasRunning",true},{"originalState",SERVICE_RUNNING}}})},{"tasks",json::array()}};}
static json stormTaskDocument(int schema=3){return {{"schema",schema},{"family",static_cast<int>(g_machineIdentity.family)},{"services",json::array()},{"tasks",json::array({{{"name",schema==3?R"(\MSI\MSI_Center_M_Server)":"MSI_Center_M_Server"},{"wasEnabled",true},{"wasRunning",true}}})}};}
template<class F>static void stormCase(const std::string& name,F work){try{work();check(true,name.c_str());}catch(const std::exception& error){check(false,(name+" : "+error.what()).c_str());}}
static void stormRestart(){g_oemVendorServiceLeases.clear();g_oemVendorTaskLeases.clear();g_oemVendorStackLoaded=false;g_oemVendorStackJournalValid=true;g_oemVendorStackStartupRecovered=false;}
static int journalStorm(){
 const std::vector<std::string> invalid={"empty","truncated","array","null","oversize","deep","future","unknown-schema","schema-string","schema-float","family-float","family-wrong","services-object","service-type-string","service-type-float","service-type-wrap","state-float","state-missing","wasRunning-number","state-inconsistent","foreign-service","duplicate-service","duplicate-schema","escaped-duplicate","nested-duplicate","schema1-hidden-task-debt","trailing","directory","reparse"};
 for(const auto& kind:invalid)stormCase("journal storm rejects "+kind+" and preserves original",[&]{
  reset();seed();auto doc=stormServiceDocument();std::string bytes;
  if(kind=="empty")bytes="";else if(kind=="truncated")bytes="{\"schema\":";else if(kind=="array")bytes="[]";else if(kind=="null")bytes="null";else if(kind=="oversize"){doc["unknownExtension"]=std::string(131073,'x');bytes=doc.dump();}else if(kind=="deep"){bytes=doc.dump();bytes.insert(1,"\"unknownExtension\":"+std::string(40,'[')+"0"+std::string(40,']')+",");}
  else {
   if(kind=="future")doc["schema"]=5;else if(kind=="unknown-schema")doc["schema"]=0;else if(kind=="schema-string")doc["schema"]="2";else if(kind=="schema-float")doc["schema"]=2.5;else if(kind=="family-float")doc["family"]=static_cast<double>(static_cast<int>(g_machineIdentity.family));else if(kind=="family-wrong")doc["family"]=static_cast<int>(YmccFamily::MsiClaw);else if(kind=="services-object")doc["services"]=json::object();else if(kind=="service-type-string")doc["services"][0]["originalStartType"]="2";else if(kind=="service-type-float")doc["services"][0]["originalStartType"]=2.5;else if(kind=="service-type-wrap")doc["services"][0]["originalStartType"]=4294967298ULL;else if(kind=="state-float")doc["services"][0]["originalState"]=4.5;else if(kind=="state-missing")doc["services"][0].erase("originalState");else if(kind=="wasRunning-number")doc["services"][0]["wasRunning"]=1;else if(kind=="state-inconsistent")doc["services"][0]["originalState"]=SERVICE_STOPPED;else if(kind=="foreign-service")doc["services"][0]["name"]="UnknownService";else if(kind=="duplicate-service")doc["services"].push_back(doc["services"][0]);else if(kind=="schema1-hidden-task-debt"){doc["schema"]=1;doc["tasks"]=json::array({{{"name","unconsumed-debt"},{"wasEnabled",true},{"wasRunning",true}}});}
   bytes=doc.dump();if(kind=="duplicate-schema")bytes.insert(1,"\"schema\":99,");if(kind=="escaped-duplicate")bytes.insert(1,"\"\\u0073chema\":99,");if(kind=="nested-duplicate"){const auto at=bytes.find("\"originalStartType\":");bytes.insert(at,"\"originalStartType\":4,");}if(kind=="trailing")bytes+=" {}";
  }
  journal=bytes;journalExists=true;journalDirectory=kind=="directory";journalReparse=kind=="reparse";
  stormNeed(!oemVendorStackLoadJournal(),"invalid journal became authority");stormNeed(g_oemVendorServiceLeases.empty()&&g_oemVendorTaskLeases.empty(),"invalid journal admitted debt");
  stormNeed(!oemVendorStackRestoreLocked("storm-invalid"),"invalid journal claimed returned");stormNeed(journal==bytes&&journalWrites==0&&mutationCount==0&&taskMutations==0&&starts==0,"invalid original changed or caused mutations");
 });
 for(const std::string kind:{"schema2-nested","task-string","task-number","task-owner","task-duplicate","task-escaped-duplicate","task-limit","task-traversal"})stormCase("journal task storm rejects "+kind,[&]{
  reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();auto doc=stormTaskDocument();
  if(kind=="schema2-nested")doc["schema"]=2;else if(kind=="task-string")doc["tasks"][0]["wasEnabled"]="true";else if(kind=="task-number")doc["tasks"][0]["wasRunning"]=1;else if(kind=="task-owner")doc["tasks"][0]["name"]=R"(\MSI\Other_Task)";else if(kind=="task-traversal")doc["tasks"][0]["name"]=R"(\MSI\..\MSI_Center_M_Server)";else if(kind=="task-duplicate")doc["tasks"].push_back(doc["tasks"][0]);else if(kind=="task-limit")for(int n=0;n<65;++n)doc["tasks"].push_back({{"name","\\MSI"+std::to_string(n)+"\\MSI_Center_M_Server"},{"wasEnabled",true},{"wasRunning",true}});
  auto bytes=doc.dump();if(kind=="task-escaped-duplicate"){const auto at=bytes.find("\"wasEnabled\":");bytes.insert(at,"\"was\\u0045nabled\":false,");}journal=bytes;journalExists=true;
  stormNeed(!oemVendorStackRestoreLocked("storm-task-invalid")&&journal==bytes&&journalWrites==0&&taskMutations==0,"invalid nested task debt consumed");
 });
 stormCase("journal transient read failure can recover on explicit valid action",[]{reset();seed();journal=stormServiceDocument().dump();journalExists=true;journalReadFails=true;const auto bytes=journal;stormNeed(!oemVendorStackLoadJournal(),"failed read became known");journalReadFails=false;stormNeed(oemVendorStackLoadJournal()&&g_oemVendorServiceLeases.size()==1&&journal==bytes,"transient read poisoned process permanently");});
 stormCase("journal transient attributes failure can recover on explicit valid action",[]{reset();seed();journal=stormServiceDocument().dump();journalExists=true;journalAttributeError=ERROR_SHARING_VIOLATION;stormNeed(!oemVendorStackLoadJournal(),"failed attributes became known");journalAttributeError=0;stormNeed(oemVendorStackLoadJournal()&&g_oemVendorServiceLeases.size()==1,"attributes failure poisoned process permanently");});
 stormCase("journal explicit trusted correction recovers without guessing old values",[]{reset();seed();journal="{broken";journalExists=true;stormNeed(!oemVendorStackLoadJournal(),"bad initial journal accepted");journal=stormServiceDocument().dump();stormNeed(oemVendorStackLoadJournal()&&g_oemVendorServiceLeases.size()==1,"valid explicit correction never reloaded");});
 for(const std::string kind:{"changed","deleted","future","family-switch"})stormCase("journal loaded debt holds after external "+kind,[&]{reset();seed();journal=stormServiceDocument().dump();journalExists=true;stormNeed(oemVendorStackLoadJournal(),"seed journal failed");const auto original=journal;const auto family=g_machineIdentity.family;if(kind=="deleted")journalExists=false;else if(kind=="family-switch")g_machineIdentity.family=YmccFamily::MsiClaw;else if(kind=="future"){auto d=json::parse(journal);d["schema"]=77;journal=d.dump();}else journal="{external-edit";const auto external=journal;stormNeed(!oemVendorStackRestoreLocked("storm-external")&&g_oemVendorServiceLeases.size()==1,"external original erased known debt");stormNeed(journal==external&&journalWrites==0&&starts==0&&mutationCount==0,"external record overwritten or used");g_machineIdentity.family=family;journal=original;journalExists=true;stormNeed(oemVendorStackRestoreLocked("storm-explicit-retry")&&g_oemVendorServiceLeases.empty(),"trusted original return cannot retry");});
 stormCase("journal write-ahead atomic failure cannot claim disabled and retry survives restart",[]{reset();seed();journalFail=true;oemVendorStackRequest(true);drain();stormNeed(stops==0&&mutationCount==0&&g_oemVendorServiceLeases.empty()&&!journalExists,"failed write-ahead caused disable");journalFail=false;tick+=30000;oemVendorStackSchedule();drain();stormNeed(!g_oemVendorServiceLeases.empty(),"released write failure did not retry");const auto persisted=journal;stormRestart();stormNeed(oemVendorStackLoadJournal()&&!g_oemVendorServiceLeases.empty()&&journal==persisted,"retry did not survive restart");stormNeed(oemVendorStackRestoreLocked("storm-restart-return"),"restart debt return failed");});
 stormCase("journal clear failure retains known debt until durable retry and reread",[]{reset();seed();oemVendorStackRequest(true);drain();const auto bytes=journal;const auto debt=g_oemVendorServiceLeases.size();journalFail=true;stormNeed(!oemVendorStackRestoreLocked("storm-clear-failed")&&g_oemVendorServiceLeases.size()==debt&&journal==bytes,"failed clear erased debt");journalFail=false;stormRestart();stormNeed(oemVendorStackLoadJournal()&&g_oemVendorServiceLeases.size()==debt,"failed clear lost restart debt");stormNeed(oemVendorStackRestoreLocked("storm-clear-retry")&&g_oemVendorServiceLeases.empty(),"clear retry failed");stormRestart();stormNeed(oemVendorStackLoadJournal()&&g_oemVendorServiceLeases.empty(),"durable clear reread failed");});
 stormCase("journal partial return keeps unavailable service debt through restart",[]{reset();seed();oemVendorStackRequest(true);drain();auto name=services.begin()->first;services[name].deny=true;stormNeed(!oemVendorStackRestoreLocked("storm-partial")&&g_oemVendorServiceLeases.size()==1,"partial restore forgot unavailable debt");stormNeed(json::parse(journal)["services"].size()==1,"partial restore persisted false all-returned");stormRestart();stormNeed(oemVendorStackLoadJournal()&&g_oemVendorServiceLeases.size()==1,"partial restart lost debt");services[name].deny=false;stormNeed(oemVendorStackRestoreLocked("storm-partial-retry"),"partial retry failed");});
 stormCase("journal unknown root and entry fields survive debt update",[]{reset();seed();auto d=stormServiceDocument();d["unknownExtension"]={{"ownerNote","KEEP"}};d["services"][0]["unknownLeaseNote"]="KEEP";journal=d.dump();journalExists=true;stormNeed(oemVendorStackLoadJournal(),"legal unknown denied");stormNeed(oemVendorStackSaveJournal(),"legal unknown save failed");auto after=json::parse(journal);stormNeed(after["unknownExtension"]==d["unknownExtension"]&&after["services"][0]["unknownLeaseNote"]=="KEEP","legal unknown fields erased");});
 stormCase("journal schema1 infers legacy running fact then saves schema2",[]{reset();seed();auto d=stormServiceDocument();d["schema"]=1;d.erase("tasks");d["services"][0].erase("originalState");journal=d.dump();journalExists=true;stormNeed(oemVendorStackLoadJournal()&&g_oemVendorServiceLeases[0].originalState==SERVICE_RUNNING,"legacy schema1 inference lost");stormNeed(oemVendorStackSaveJournal()&&json::parse(journal)["schema"]==2,"legacy migration failed");});
 stormCase("journal schema2 to schema3 nested leases persist exact owners and unknown",[]{reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();auto d=stormTaskDocument(2);d["unknownExtension"]="KEEP";d["tasks"][0]["unknownLeaseNote"]="KEEP";journal=d.dump();journalExists=true;tasks[L"MSI_Center_M_Server"]={false,false};tasks[LR"(\MSI\MSI_Center_M_Updater)"]={};stormNeed(oemVendorStackLoadJournal(),"schema2 seed denied");g_oemVendorTaskLeases.push_back({LR"(\MSI\MSI_Center_M_Updater)",true,true});stormNeed(oemVendorStackSaveJournal(),"nested schema3 save failed");auto after=json::parse(journal);stormNeed(after["schema"]==3&&after["unknownExtension"]=="KEEP"&&after["tasks"][0]["unknownLeaseNote"]=="KEEP","schema migration erased identity extension");stormRestart();stormNeed(oemVendorStackLoadJournal()&&g_oemVendorTaskLeases.size()==2&&g_oemVendorTaskLeases[1].name==LR"(\MSI\MSI_Center_M_Updater)","nested identity changed on restart");});
 stormCase("journal partial nested return preserves schema3 and pending unknown fields",[]{reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();auto d=stormTaskDocument(2);d["schema"]=3;d["tasks"][0]["unknownLeaseNote"]="KEEP";d["tasks"].push_back({{"name",R"(\MSI\MSI_Center_M_Updater)"},{"wasEnabled",true},{"wasRunning",true}});journal=d.dump();journalExists=true;tasks[L"MSI_Center_M_Server"]={false,false,true};tasks[LR"(\MSI\MSI_Center_M_Updater)"]={true,true};stormNeed(!oemVendorStackRestoreLocked("storm-partial-task"),"missing query claimed restored");auto after=json::parse(journal);stormNeed(after["schema"]==3&&after["tasks"].size()==1&&after["tasks"][0]["unknownLeaseNote"]=="KEEP","partial return downgraded schema or erased unknown debt");});
 stormCase("journal external task runtime mixture is unknown and cannot be forced returned",[]{reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();journal=stormTaskDocument().dump();journalExists=true;tasks[LR"(\MSI\MSI_Center_M_Server)"]={true,false};stormNeed(!oemVendorStackRestoreLocked("storm-external-task")&&g_oemVendorTaskLeases.size()==1&&taskStarts==0&&taskMutations==0,"unknown scheduler runtime overwritten or debt cleared");tasks[LR"(\MSI\MSI_Center_M_Server)"]={false,false};stormNeed(oemVendorStackRestoreLocked("storm-task-trusted-target"),"confirmed disabled target cannot return");});
 stormCase("journal write success without matching readback never acknowledges cleared debt",[]{reset();seed();oemVendorStackRequest(true);drain();const auto debt=g_oemVendorServiceLeases.size();onPersist=[]{journal="{foreign-after-write";};stormNeed(!oemVendorStackRestoreLocked("storm-false-ack")&&g_oemVendorServiceLeases.size()==debt,"unverified write acknowledged returned debt");stormNeed(journal=="{foreign-after-write","foreign readback overwritten");});
 stormCase("journal successful write with transient readback error keeps debt then retries",[]{reset();seed();oemVendorStackRequest(true);drain();const auto debt=g_oemVendorServiceLeases.size();onPersist=[]{journalReadFails=true;};const auto ok=oemVendorStackRestoreLocked("storm-unreadable-ack");journalReadFails=false;stormNeed(!ok&&g_oemVendorServiceLeases.size()==debt,"unreadable write result was acknowledged");stormNeed(oemVendorStackRestoreLocked("storm-readback-retry")&&g_oemVendorServiceLeases.empty(),"known attempted bytes could not retry");});

 stormCase("journal partial disabled-running task return survives failure and restart without duplicate Run",[]{reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();auto d=stormTaskDocument();d["tasks"][0]["wasEnabled"]=false;journal=d.dump();journalExists=true;auto& task=tasks[LR"(\MSI\MSI_Center_M_Server)"];task={false,false};task.failSetCall=2;stormNeed(!oemVendorStackRestoreLocked("storm-partial-disabled-running")&&g_oemVendorTaskLeases.size()==1&&task.enabled&&task.running,"partial task failure forgot debt");const auto started=taskStarts;stormRestart();task.failSetCall=0;stormNeed(oemVendorStackRestoreLocked("storm-partial-task-restart")&&!task.enabled&&task.running&&taskStarts==started,"owned partial return cannot retry without duplicate Run");});
 stormCase("journal partial enabled task return survives Run failure and restart",[]{reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();journal=stormTaskDocument().dump();journalExists=true;auto& task=tasks[LR"(\MSI\MSI_Center_M_Server)"];task={false,false};task.runFail=true;stormNeed(!oemVendorStackRestoreLocked("storm-run-fail")&&g_oemVendorTaskLeases.size()==1&&task.enabled&&!task.running,"partial Run failure forgot debt");stormRestart();task.runFail=false;stormNeed(oemVendorStackRestoreLocked("storm-run-retry")&&task.enabled&&task.running,"owned enabled intermediate cannot retry");});
 stormCase("journal stopped service original never acknowledges externally running state",[]{reset();seed();auto d=stormServiceDocument();d["services"][0]["wasRunning"]=false;d["services"][0]["originalState"]=SERVICE_STOPPED;journal=d.dump();journalExists=true;stormNeed(!oemVendorStackRestoreLocked("storm-external-running")&&g_oemVendorServiceLeases.size()==1&&stops==0,"external running service guessed returned");});
 stormCase("journal externally paused original policy is held without guessing runtime ownership",[]{reset();seed();journal=stormServiceDocument().dump();journalExists=true;services[oemVendorServiceNamesForFamily(g_machineIdentity.family).front()].state=SERVICE_PAUSED;stormNeed(!oemVendorStackRestoreLocked("storm-external-paused")&&g_oemVendorServiceLeases.size()==1&&stops==0,"unknown service runtime overwritten");});
 stormCase("journal task originally stopped preserves unknown external running instance",[]{reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();auto d=stormTaskDocument();d["tasks"][0]["wasRunning"]=false;journal=d.dump();journalExists=true;tasks[LR"(\MSI\MSI_Center_M_Server)"]={true,true};stormNeed(!oemVendorStackRestoreLocked("storm-external-task-running")&&g_oemVendorTaskLeases.size()==1&&taskMutations==0,"external running task guessed returned");});
 stormCase("journal service return phase write failure cannot change SCM",[]{reset();seed();journal=stormServiceDocument().dump();journalExists=true;services[oemVendorServiceNamesForFamily(g_machineIdentity.family).front()].type=SERVICE_DISABLED;services[oemVendorServiceNamesForFamily(g_machineIdentity.family).front()].state=SERVICE_STOPPED;journalFail=true;stormNeed(!oemVendorStackRestoreLocked("storm-return-phase-save-failed")&&g_oemVendorServiceLeases.size()==1&&mutationCount==0&&starts==0,"failed return intent changed SCM");journalFail=false;stormNeed(oemVendorStackRestoreLocked("storm-return-phase-retry"),"return phase write recovery failed");});

 stormCase("journal partial service start failure retains prepared return across restart",[]{reset();seed();journal=stormServiceDocument().dump();journalExists=true;auto& service=services[oemVendorServiceNamesForFamily(g_machineIdentity.family).front()];service.type=SERVICE_DISABLED;service.state=SERVICE_STOPPED;service.startFail=true;stormNeed(!oemVendorStackRestoreLocked("storm-partial-service")&&g_oemVendorServiceLeases.size()==1&&service.type==SERVICE_AUTO_START,"partial service return lost obligation");stormRestart();service.startFail=false;stormNeed(oemVendorStackRestoreLocked("storm-service-retry")&&service.state==SERVICE_RUNNING,"prepared partial service cannot recover");});
 for(const bool task:{false,true})stormCase(task?"journal invalid task return phase stays unknown":"journal invalid service return phase stays unknown",[&]{reset();if(task)g_machineIdentity.family=YmccFamily::MsiClaw;seed();auto d=task?stormTaskDocument():stormServiceDocument();d[task?"tasks":"services"][0]["returnPrepared"]="true";journal=d.dump();journalExists=true;const auto bytes=journal;stormNeed(!oemVendorStackRestoreLocked("storm-invalid-return-phase")&&journal==bytes&&journalWrites==0&&mutationCount==0&&taskMutations==0,"invalid return phase authorized mutation");});
 stormCase("journal short read remains unknown then explicit retry loads original",[]{reset();seed();journal=stormServiceDocument().dump();journalExists=true;journalShortRead=true;stormNeed(!oemVendorStackLoadJournal(),"short read became known");journalShortRead=false;stormNeed(oemVendorStackLoadJournal()&&g_oemVendorServiceLeases.size()==1,"short read never recovered");});
 stormCase("journal BOM and legal unknown fields are admitted without mutation",[]{reset();seed();auto d=stormServiceDocument();d["unknownExtension"]=nullptr;journal=std::string("\xef\xbb\xbf")+d.dump();journalExists=true;const auto bytes=journal;stormNeed(oemVendorStackLoadJournal()&&g_oemVendorServiceLeases.size()==1&&journal==bytes&&journalWrites==0,"legal BOM changed receipt or failed read");});
 stormCase("journal failure budget stays bounded and changed explicit intent recovers",[]{reset();seed();journal="{broken";journalExists=true;oemVendorStackRequest(true);drain();for(int n=0;n<5;++n){tick+=30000;oemVendorStackSchedule();drain();}stormNeed(g_oemVendorStackAttempts==kOemVendorMaxAttempts&&state("retry-exhausted")&&mutationCount==0,"bad journal exceeded original retry limit");const auto reads=journalReads;journal=stormServiceDocument().dump();oemVendorStackRequest(true);drain();stormNeed(journalReads==reads&&mutationCount==0,"unchanged exhausted intent revived polling");oemVendorStackRequest(false);drain();stormNeed(g_oemVendorServiceLeases.empty()&&g_oemVendorStackLastOutcomeOk,"changed explicit valid intent failed recovery");});

 stormCase("journal invalidation withdraws stale applied status without losing owners or debt",[]{reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();for(const auto& name:oemVendorTaskNamesForFamily(g_machineIdentity.family))tasks[name]={};oemVendorStackShieldRequest(true);drain();stormNeed(oemVendorStackShieldRuntimeSnapshot()["applied"]==true,"status seed not actually applied");const auto count=g_oemVendorTaskLeases.size();journal="{external-journal";stormNeed(!oemVendorStackLoadJournal(),"external invalidation missed");const auto status=oemVendorStackShieldRuntimeSnapshot();stormNeed(status["applied"]==false&&status["pending"]==true&&status["lastOutcomeKnown"]==false&&g_oemVendorTaskLeases.size()==count&&status["isolationOwnerActive"]==true,"invalid journal retained stale applied claim or lost known owner/debt");});
 std::cout<<"cases="<<cases<<" failures="<<failed<<" hardwareCalls=0 scope=journal-storm RESULT="<<(failed?"FAIL":"PASS")<<"\n";return failed?1:0;
}


#ifdef YMCC_VENDOR_NATIVE_IO
static json nativeIoCases=json::array();
template<class F>static void nativeIoCase(const char* name,F work){
    reset();seed();journal_native::beginCase();bool passed=true;std::string error;
    try{work();stormNeed(journal_native::handles.empty(),"consumer leaked native journal handle");stormNeed(journal_native::stages.empty(),"consumer leaked owned native staging file");stormNeed(!journal_native::aclRestoreFailed,"owned ACL was not restored");}
    catch(const std::exception& failure){passed=false;error=failure.what();}
    json files=json::array();for(const auto& entry:std::filesystem::directory_iterator(journal_native::caseRoot))files.push_back({{"leaf",W2U(entry.path().filename().native())},{"bytes",entry.is_regular_file()?entry.file_size():0}});
    nativeIoCases.push_back({{"name",name},{"pass",passed},{"error",error},{"ownedCase",W2U(journal_native::caseRoot.native())},{"trace",journal_native::events},{"leftoverTrackedStages",journal_native::stages.size()},{"leftoverTrackedHandles",journal_native::handles.size()},{"aclRestoreFailed",journal_native::aclRestoreFailed},{"files",files},{"fakeServiceMutations",mutationCount},{"fakeTaskMutations",taskMutations},{"fakeProcessStops",stoppedProcesses.size()},{"realOemActions",0}});
    check(passed,(std::string(name)+(error.empty()?"":" : "+error)).c_str());
    // Preserve all files. Close only known fixture-owned handles on failure.
    journal_native::closeOwnedHandles();
    journal_native::handles.clear();journal_native::fault.clear();journal_native::afterPublish={};journal_native::active=false;POWER_CONTROL_DIR=L"memory";
}
static std::string nativeIoSeed(){auto doc=stormServiceDocument();doc["nativeIoUnknownNote"]="KEEP";const auto bytes=doc.dump();journal_native::put(bytes);return bytes;}
static void nativeIoLoaded(){stormNeed(oemVendorStackLoadJournal()&&g_oemVendorServiceLeases.size()==1,"actual reader did not admit fake observed service debt");}
static void nativeIoNoOemMutation(){stormNeed(mutationCount==0&&stops==0&&starts==0&&taskMutations==0&&processCalls==0,"IO refusal reached OEM mutation boundary");}
static void nativeIoRetrySave(){stormNeed(oemVendorStackSaveJournal(),"explicit native write retry failed");const auto bytes=journal_native::get();stormNeed(json::parse(bytes)["nativeIoUnknownNote"]=="KEEP","actual writer/readback lost extension");stormRestart();nativeIoLoaded();}
static int journalNativeIo(){
    using namespace journal_native;journal_native::beginRoot();
    nativeIoCase("native journal fresh write flush publish readback and restart",[]{
        stormNeed(oemVendorStackLoadJournal(),"fresh missing journal failed");const auto name=oemVendorServiceNamesForFamily(g_machineIdentity.family).front();const auto& observed=services.at(name);
        g_oemVendorServiceLeases.push_back({name,observed.type,observed.state==SERVICE_RUNNING,observed.state});
        stormNeed(oemVendorStackSaveJournal(),"real writer did not publish fresh journal");
        stormNeed(count("WriteFile",true)>0&&count("FlushFileBuffers",true)>0&&count("MoveFileExW",true)>0&&count("ReadFile",true)>0,"native IO API proof missing");
        stormRestart();nativeIoLoaded();stormNeed(oemVendorStackRestoreLocked("native-io-return"),"actual clear after fake readback failed");stormRestart();stormNeed(oemVendorStackLoadJournal()&&g_oemVendorServiceLeases.empty(),"actual cleared journal failed restart read");
    });
    nativeIoCase("native ACL denies actual reader then explicit retry recovers",[]{
        const auto bytes=nativeIoSeed();{
            Deny denied(target(),FILE_GENERIC_READ);stormNeed(!oemVendorStackLoadJournal(),"ACL-denied journal became known");
            const auto h=open(target().c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(h!=INVALID_HANDLE_VALUE)close(h);stormNeed(h==INVALID_HANDLE_VALUE&&error("CreateFileW",ERROR_ACCESS_DENIED),"actual ACCESS_DENIED was not observed");
        }nativeIoLoaded();stormNeed(get()==bytes,"read denial changed original");nativeIoNoOemMutation();
    });
    nativeIoCase("native exclusive share lock refuses read and release retries",[]{
        const auto bytes=nativeIoSeed();{Hold held(0);stormNeed(!oemVendorStackLoadJournal()&&error("CreateFileW",ERROR_SHARING_VIOLATION),"actual sharing read refusal absent");}nativeIoLoaded();stormNeed(get()==bytes,"share refusal changed original");nativeIoNoOemMutation();
    });
    nativeIoCase("native directory ACL refuses temp create then write retries",[]{
        const auto bytes=nativeIoSeed();nativeIoLoaded();{Deny denied(caseRoot,FILE_ADD_FILE);stormNeed(!oemVendorStackSaveJournal()&&error("CreateFileW",ERROR_ACCESS_DENIED),"native temp creation was not refused");}stormNeed(get()==bytes&&g_oemVendorServiceLeases.size()==1,"temp refusal lost original or debt");nativeIoRetrySave();nativeIoNoOemMutation();
    });
    nativeIoCase("native partial WriteFile is rejected without publishing and retries",[]{
        const auto bytes=nativeIoSeed();nativeIoLoaded();fault="short-write";stormNeed(!oemVendorStackSaveJournal(),"short native staging write claimed saved");fault.clear();
        stormNeed(get()==bytes&&g_oemVendorServiceLeases.size()==1&&count("DeleteFileW",true)>0,"partial staging overwrote original or leaked debt");bool partial=false;for(const auto& e:events)if(e["api"]=="WriteFile"&&e["nativeCalled"]==true&&e["injected"]==true&&e["transferred"]<e["requested"])partial=true;stormNeed(partial,"actual half-written staging bytes were not recorded");nativeIoRetrySave();
    });
    nativeIoCase("native FlushFileBuffers acknowledgement failure retains original and retries",[]{
        const auto bytes=nativeIoSeed();nativeIoLoaded();fault="flush-ack";stormNeed(!oemVendorStackSaveJournal(),"failed flush acknowledgement claimed saved");fault.clear();stormNeed(get()==bytes&&count("FlushFileBuffers",true)>0&&count("MoveFileExW")==0&&count("ReplaceFileW")==0,"flush failure published or skipped native flush");nativeIoRetrySave();
    });
    nativeIoCase("native target share lock rejects Move and Replace then publishes on retry",[]{
        const auto bytes=nativeIoSeed();nativeIoLoaded();{Hold held(FILE_SHARE_READ);stormNeed(!oemVendorStackSaveJournal(),"locked target claimed publication");}
        stormNeed(get()==bytes&&g_oemVendorServiceLeases.size()==1&&count("MoveFileExW")>=6&&count("ReplaceFileW")>0,"native publish refusal or old original proof missing");nativeIoRetrySave();
    });
    nativeIoCase("native ReplaceFile fallback publishes verified bytes",[]{
        nativeIoSeed();nativeIoLoaded();fault="force-replace";stormNeed(oemVendorStackSaveJournal(),"native ReplaceFile fallback failed");fault.clear();stormNeed(count("ReplaceFileW",true)==1&&count("ReadFile",true)>0,"real fallback/readback was not executed");stormRestart();nativeIoLoaded();
    });
    nativeIoCase("native damaged original is preserved without any staging or OEM action",[]{
        const std::string bytes="{broken-owned-journal";put(bytes);stormNeed(!oemVendorStackLoadJournal()&&!oemVendorStackRestoreLocked("native-bad-original"),"bad original admitted return");stormNeed(get()==bytes&&count("ReadFile",true)>0&&count("WriteFile")==0&&journalWrites==0,"bad physical original was changed or reader was fake");nativeIoNoOemMutation();
    });
    nativeIoCase("native clear publish failure retains debt through restart then clears on retry",[]{
        const auto bytes=nativeIoSeed();nativeIoLoaded();{Hold held(FILE_SHARE_READ);stormNeed(!oemVendorStackRestoreLocked("native-clear-locked")&&g_oemVendorServiceLeases.size()==1,"failed native clear erased known debt");}
        stormNeed(get()==bytes&&count("MoveFileExW")>=6&&count("ReplaceFileW")>0,"failed clear altered physical original");stormRestart();nativeIoLoaded();stormNeed(oemVendorStackRestoreLocked("native-clear-retry"),"released native clear failed");stormNeed(json::parse(get())["services"].empty(),"clear was not physically persisted");stormRestart();stormNeed(oemVendorStackLoadJournal()&&g_oemVendorServiceLeases.empty(),"cleared native journal failed restart read");
    });
    nativeIoCase("native published write with actual denied readback holds debt and retries",[]{
        nativeIoSeed();nativeIoLoaded();std::unique_ptr<Deny> denied;afterPublish=[&]{denied=std::make_unique<Deny>(target(),FILE_GENERIC_READ);};stormNeed(!oemVendorStackSaveJournal()&&g_oemVendorServiceLeases.size()==1,"denied native readback acknowledged write");denied.reset();stormNeed(error("CreateFileW",ERROR_ACCESS_DENIED),"actual post-publish read denial absent");nativeIoLoaded();nativeIoRetrySave();
    });
    nativeIoCase("native clear with actual denied readback retains known debt until retry",[]{
        nativeIoSeed();nativeIoLoaded();std::unique_ptr<Deny> denied;afterPublish=[&]{denied=std::make_unique<Deny>(target(),FILE_GENERIC_READ);};stormNeed(!oemVendorStackRestoreLocked("native-clear-readback-denied")&&g_oemVendorServiceLeases.size()==1,"unreadable clear acknowledged debt removal");denied.reset();stormNeed(json::parse(get())["services"].empty(),"actual clear did not publish");stormNeed(oemVendorStackRestoreLocked("native-clear-readback-retry")&&g_oemVendorServiceLeases.empty(),"known attempted clear could not retry");stormRestart();stormNeed(oemVendorStackLoadJournal()&&g_oemVendorServiceLeases.empty(),"clear readback retry failed restart");
    });
    nativeIoCase("native path and unowned temp negative controls refuse before any OS mutation",[]{
        const auto note=caseRoot/L"oem-vendor-stack-lease.json.ymcc.user.tmp";put("KEEP-UNKNOWN",note);const auto before=events.size();
        const auto h=open(L"C:\\Windows\\YMCC-DO-NOT-OPEN",GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);stormNeed(h==INVALID_HANDLE_VALUE,"outside-owned-root open was admitted");stormNeed(!remove(note.c_str())&&get(note)=="KEEP-UNKNOWN","unowned staging leaf was deleted");
        for(size_t i=before;i<events.size();++i)stormNeed(events[i]["nativeCalled"]==false,"negative fence reached native OS API");nativeIoNoOemMutation();
    });
    const auto reportPath=root.parent_path()/L"journal-io-receipt.json";std::ofstream report(reportPath,std::ios::binary);report<<json({{"scope","actual production reader and extracted writer; exact fresh owned local root only"},{"ownedRoot",W2U(root.native())},{"cases",nativeIoCases},{"checks",cases},{"failures",failed},{"realOemActions",0},{"allScmTaskProcessBoundariesFake",true},{"faultClassification",{{"short-write","real partial staging WriteFile; requested length reduced at boundary"},{"flush-ack","real FlushFileBuffers called; successful acknowledgement changed to failure"},{"force-replace","MoveFileExW failures injected; fallback ReplaceFileW is real"},{"ACL/share/publish-lock/readback-denial","actual Win32 file access/share failures"}}}}).dump(2);stormNeed(report.good(),"native IO receipt write failed");
    std::cout<<"cases="<<cases<<" failures="<<failed<<" hardwareCalls=0 scope=journal-io RESULT="<<(failed?"FAIL":"PASS")<<"\n";return failed?1:0;
}
#endif

int main(int argc,char** argv){
 // Recording is a third, temporary owner of the existing reversible lease.
 for (const auto family : {YmccFamily::AsusRogAlly, YmccFamily::AsusRogAllyX, YmccFamily::AsusRogAllyClassic,
        YmccFamily::AsusRogAllyXClassic, YmccFamily::LenovoLegionGo, YmccFamily::MsiClaw, YmccFamily::ZotacGamingZone}) {
  reset();g_machineIdentity.family=family;seed();
  if(family==YmccFamily::AsusRogAllyX)g_machineIdentity.deviceClass="HandheldCompanion.Devices.XboxROGAllyX";
  if(family==YmccFamily::MsiClaw){tasks[L"MSI_Center_M_Server"]={};tasks[L"MSI_Center_M_Updater"]={};}
  oemVendorStackRecordingRequest(true);check(g_oemVendorRecordingWanted&&g_oemVendorStackWanted&&!g_oemVendorShortcutWanted,"recording owns takeover without a saved rule on every supported family");drain();
  check(oemVendorStackRecordingSnapshot()["applied"]==true,"recording preparation uses the actual reversible vendor lease");
  oemVendorStackRequest(true);drain();oemVendorStackRecordingRequest(false);drain();check(g_oemVendorStackWanted&&g_oemVendorShortcutWanted&&!g_oemVendorRecordingWanted,"ending recording does not restore a saved-rule-owned takeover");
  oemVendorStackRequest(false);drain();check(!g_oemVendorStackWanted,"last owner releases temporary takeover");for(const auto& item:services)check(item.second.state==SERVICE_RUNNING&&item.second.type==SERVICE_AUTO_START,"recording restores original service state");
 }
 reset();seed();scmFail=true;oemVendorStackRecordingRequest(true);drain();check(g_oemVendorRecordingWanted&&oemVendorStackRecordingSnapshot()["applied"]==false,"failed vendor preparation retains the recording request for fallback");oemVendorStackRecordingRequest(false);drain();check(!g_oemVendorRecordingWanted,"failed preparation still releases the recording owner");
 reset();g_machineIdentity.family=YmccFamily::Unknown;g_machineIdentity.deviceClass="unknown";oemVendorStackRecordingRequest(true);check(!g_oemVendorRecordingWanted&&oemVendorStackRecordingSnapshot()["applied"]==false,"unsupported vendor takeover is explicit and does not invent success");
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();tasks[L"MSI_Center_M_Server"]={};tasks[L"MSI_Center_M_Updater"]={};oemVendorStackShieldRequest(true);drain();oemVendorStackRecordingRequest(true);drain();oemVendorStackRecordingRequest(false);drain();check(g_oemVendorShieldWanted&&g_oemVendorStackWanted,"ending recording preserves the independent MSI controller-only shield");oemVendorStackRestore("recording-fixture-exit");check(!g_oemVendorRecordingWanted&&!g_oemVendorStackWanted,"exit clears every temporary recording owner");

 if(argc==2&&std::string(argv[1])=="--journal-io"){
#ifdef YMCC_VENDOR_NATIVE_IO
 return journalNativeIo();
#else
 std::cerr<<"native journal IO was not compiled";return 2;
#endif
 }
 if(argc==2&&std::string(argv[1])=="--journal-storm")return journalStorm();
#ifdef YMCC_VENDOR_R15
 {
  const auto names = oemVendorTaskNamesForFamily(YmccFamily::MsiClaw);
  check(oemVendorTaskPathAllowed(L"\\MSI\\MSI_Center_M_Server", names), "nested exact MSI scheduled task admitted");
  check(oemVendorTaskPathAllowed(L"\\MSI\\msi_center_m_updater", names), "nested task leaf matches Windows case-insensitively");
  check(!oemVendorTaskPathAllowed(L"\\MSI\\OtherUpdater", names), "unrelated updater excluded");
  check(!oemVendorTaskPathAllowed(L"\\MSI\\..\\MSI_Center_M_Server", names), "journal traversal rejected");
  check(!oemVendorTaskPathAllowed(L"MSI\\MSI_Center_M_Server", names), "non-absolute nested journal path rejected");
  check(!oemVendorTaskPathAllowed(L"\\MSI\\MSI_Center_M_Server\\", names), "trailing task separator rejected");
  unsigned reads = 0;
  auto scan = oemVendorTaskDiscoverWithReader(names, [&](const auto& path, OemVendorTaskFolderFacts& f) {
   ++reads;
   if (path == L"\\") f.folders = {L"\\MSI"};
   else f.tasks = {L"\\MSI\\MSI_Center_M_Server", L"\\MSI\\MSI_Center_M_Updater", L"\\MSI\\OtherUpdater"};
   return true;
  });
  check(scan.complete && reads == 2 && scan.paths.size() == 2, "actual discovery walks subfolder and returns only both exact targets");
  scan = oemVendorTaskDiscoverWithReader(names, [&](const auto&, OemVendorTaskFolderFacts&) { SetLastError(ERROR_ACCESS_DENIED); return false; });
  check(!scan.complete && scan.error == ERROR_ACCESS_DENIED, "unreadable scheduler is not reported as task absence success");
  reads = 0;
  scan = oemVendorTaskDiscoverWithReader({}, [&](const auto&, OemVendorTaskFolderFacts&) { ++reads; return true; });
  check(scan.complete && reads == 0, "non-MSI empty task policy does no scheduler discovery");
 }
 reset(); g_machineIdentity.family=YmccFamily::MsiClaw; seed();
 tasks[L"\\MSI\\MSI_Center_M_Server"]={}; tasks[L"\\MSI\\Updates\\MSI_Center_M_Updater"]={}; tasks[L"\\MSI\\OtherUpdater"]={};
 oemVendorStackShieldRequest(true); drain();
 check(state("applied") && !tasks.contains(L"\\MSI\\MSI_Center_M_Server") &&
       !tasks.contains(L"\\MSI\\Updates\\MSI_Center_M_Updater") && taskRemovals==2,
       "MSI shield removes both nested exact task definitions through the production lease");
 check(tasks[L"\\MSI\\OtherUpdater"].enabled && tasks[L"\\MSI\\OtherUpdater"].running && taskMutations == 2,
       "nested-task shielding does not mutate another MSI updater");
 check(g_oemVendorTaskLeases.size()==2 && json::parse(journal)["schema"]==4 && std::all_of(g_oemVendorTaskLeases.begin(),g_oemVendorTaskLeases.end(),[](const auto& lease){return lease.removePrepared;}), "exact nested task paths carry durable schema4 removal obligations");
 g_oemVendorTaskLeases.clear(); g_oemVendorStackLoaded=false;
 check(oemVendorStackLoadJournal() && g_oemVendorTaskLeases.size()==2, "nested task leases survive restart for exact-path recovery");
 oemVendorStackShieldRequest(false); drain();
 check(g_oemVendorTaskLeases.empty() && tasks[L"\\MSI\\MSI_Center_M_Server"].enabled && tasks[L"\\MSI\\MSI_Center_M_Server"].running &&
       tasks[L"\\MSI\\Updates\\MSI_Center_M_Updater"].enabled && tasks[L"\\MSI\\Updates\\MSI_Center_M_Updater"].running,
       "OFF restores each nested task to its original enabled and running state");
 reset(); g_machineIdentity.family=YmccFamily::MsiClaw; seed();
 tasks[L"MSI_Center_M_Server"]={}; tasks[L"\\MSI\\MSI_Center_M_Server"]={}; tasks[L"MSI_Center_M_Updater"]={};
 oemVendorStackShieldRequest(true); drain();
 check(state("applied") && taskMutations==3 && g_oemVendorTaskLeases.size()==3, "same allowed leaf in root and subfolder keeps distinct full-path leases");
 reset(); g_machineIdentity.family=YmccFamily::MsiClaw; seed();
 tasks[L"MSI_Center_M_Server"]={}; tasks[L"\\MSI\\MSI_Center_M_Updater"]={}; deniedTaskFolder=L"\\MSI";
 oemVendorStackShieldRequest(true); drain();
 check(!state("applied") && !tasks.contains(L"MSI_Center_M_Server") && tasks.at(L"\\MSI\\MSI_Center_M_Updater").enabled,
       "partial discovery removes known target but never claims both tasks are covered");
 reset(); g_machineIdentity.family=YmccFamily::MsiClaw; seed(); journalExists=true;
 journal=json({{"schema",3},{"family",static_cast<int>(g_machineIdentity.family)},{"services",json::array()},
   {"tasks",json::array({{{"name","\\MSI\\..\\MSI_Center_M_Server"},{"wasEnabled",true},{"wasRunning",true}}})}}).dump();
 oemVendorStackShieldRequest(true); drain();
 check(taskMutations==0 && state("recovery-held"), "schema3 traversal cannot direct scheduler writes");

 // A saved ON request must not restart MSI startup tasks while adopting its
 // exact previous-process task leases. OFF still owes their original state.
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();
 for(const auto& name:oemVendorTaskNamesForFamily(g_machineIdentity.family))tasks[name]={};
 oemVendorStackShieldRequest(true);drain();
 const auto restartJournal=journal;const auto restartTasks=tasks;
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();
 journalExists=true;journal=restartJournal;tasks=restartTasks;
 oemVendorStackShieldRequest(true);drain();
 check(taskStarts==0&&taskMutations==0,"MSI saved ON restart never imports or starts already-removed task leases");
 check(g_oemVendorTaskLeases.size()==2&&json::parse(journal)["tasks"].size()==2,"MSI restart preserves durable task originals until OFF");
 check(json::parse(journal)["schema"]==4,"root-only MSI deletion leases retain schema4 removal authority");
 check(state("applied")&&tasks.empty()&&taskImportCalls==0,"MSI restart confirms current absent task readbacks without importing");
 oemVendorStackShieldRequest(false);drain();
 check(taskStarts==2&&g_oemVendorTaskLeases.empty(),"MSI OFF after retained restart restores both originally-running tasks");
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();
 journalExists=true;journal=restartJournal;tasks=restartTasks;
 for(const auto& name:oemVendorTaskNamesForFamily(g_machineIdentity.family))tasks[name]={};
 oemVendorStackShieldRequest(true);drain();
 check(taskStarts==0&&taskRemovals==2&&tasks.empty(),"MSI retained restart removes externally recreated tasks without first running them");
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();
 journalExists=true;journal=restartJournal;tasks=restartTasks;
 oemVendorStackShieldRequest(false);drain();
 check(taskStarts==2&&g_oemVendorTaskLeases.empty(),"MSI saved OFF startup still restores the original task leases");
#endif

 reset();seed();oemVendorStackRequest(true);drain();check(stops==3,"ROG all three HC services stopped");check(state("applied"),"aggregate service readback success");check(g_oemVendorServiceLeases.size()==3,"durable original service facts");
 oemVendorStackRequest(true);drain();check(starts==0,"unchanged takeover never briefly restarts Armoury Crate");check(stops==3,"unchanged takeover performs no redundant stop");oemVendorStackRestore("exit");check(starts==3&&g_oemVendorServiceLeases.empty(),"normal exit restores originals and clears debt");
 reset();seed();services.begin()->second.stopDelay=3000;oemVendorStackRequest(true);drain();check(state("applied"),"realistic 3-second service stop is not lost at old 2-second deadline");
 reset();seed();services.begin()->second.state=SERVICE_START_PENDING;services.begin()->second.settleAt=1001;oemVendorStackRequest(true);drain();check(stops==3&&state("applied"),"START_PENDING stabilizes before takeover instead of permanent skip");
 reset();seed();services.begin()->second.stopFail=true;oemVendorStackRequest(true);drain();check(!state("applied")&&state("apply-failed"),"failed stop never claims applied");check(g_oemVendorServiceLeases.size()==3,"failed stop retains original fact");services.begin()->second.stopFail=false;tick+=1000;oemVendorStackSchedule();drain();check(state("applied"),"transient failed stop retried on existing refresh");check(starts==0,"retry does not restore/restart already acquired services");
 reset();seed();services.begin()->second.stuck=true;oemVendorStackRequest(true);drain();check(!state("applied"),"stuck service never claims suppression");check(tick<=10051,"service wait remains bounded at ten seconds");
 reset();seed();journalFail=true;oemVendorStackRequest(true);drain();check(mutationCount==0&&stops==0,"write-ahead lease failure prevents all SCM mutations");
 reset();seed();services.begin()->second.deny=true;oemVendorStackRequest(true);drain();check(!state("applied"),"unreadable service forbids aggregate success");
 reset();seed();services.begin()->second.state=SERVICE_STOPPED;oemVendorStackRequest(true);drain();oemVendorStackRestore("exit");check(starts==2,"original stopped service is never newly started");
 reset();seed();services.begin()->second.type=SERVICE_DISABLED;services.begin()->second.state=SERVICE_STOPPED;oemVendorStackRequest(true);drain();check(stops==2&&mutationCount==2,"already-disabled/stopped service receives no mutation and no invented lease");oemVendorStackRestore("exit");check(services.begin()->second.type==SERVICE_DISABLED&&services.begin()->second.state==SERVICE_STOPPED,"pre-existing disabled policy survives restoration");
 reset();seed();services.begin()->second.type=SERVICE_DISABLED;oemVendorStackRequest(true);drain();check(stops==2&&!state("applied"),"disabled-but-running unqualified original remains explicit hold");
 reset();seed();oemVendorStackRequest(true);drain();services.begin()->second.configFail=true;oemVendorStackRestore("exit");check(g_oemVendorServiceLeases.size()==1,"restore failure retains debt");check(json::parse(journal)["services"].size()==1,"restore failure remains durable");services.begin()->second.configFail=false;oemVendorStackRestore("retry");check(g_oemVendorServiceLeases.empty(),"later recovery discharges failed restoration");
 reset();seed();oemVendorStackRequest(true);drain();services.begin()->second.type=SERVICE_DEMAND_START;oemVendorStackRestore("exit");check(services.begin()->second.type==SERVICE_DEMAND_START&&!g_oemVendorServiceLeases.empty(),"third-party start policy is not overwritten");
 reset();seed();oemVendorStackRequest(true);drain();journalFail=true;oemVendorStackRestore("exit");check(g_oemVendorServiceLeases.size()==3,"failed journal commit cannot erase in-memory recovery debt");journalFail=false;oemVendorStackRestore("retry");check(g_oemVendorServiceLeases.empty(),"recovery debt commits on later successful write");
 reset();seed();journalExists=true;journal="{broken";oemVendorStackRequest(true);drain();check(stops==0&&state("recovery-held"),"corrupt journal holds all writes");
 reset();seed();journalExists=true;journal=json({{"schema",1},{"family",static_cast<int>(g_machineIdentity.family)},{"services",json::array({{{"name","UnrelatedService"},{"originalStartType",2},{"wasRunning",true}}})}}).dump();oemVendorStackRequest(true);drain();check(mutationCount==0&&state("recovery-held"),"unbound service in recovery journal rejected");
 reset();seed();queueFail=true;oemVendorStackRequest(true);check(jobs.empty()&&!g_oemVendorStackOpQueued,"queue refusal releases publication ownership");queueFail=false;oemVendorStackSchedule();drain();check(stops==3,"rejected publication can use existing refresh retry");
 reset();seed();oemVendorStackRequest(true);g_exitRequested=true;drain();check(mutationCount==0,"queued job after exit never disables");
 reset();seed();oemVendorStackRequest(true);oemVendorStackRequest(false);drain();check(mutationCount==0,"latest rule clear beats queued takeover");
 reset();seed();oemVendorStackRequest(true);drain();oemVendorStackRequest(false);drain();check(starts==3&&g_oemVendorServiceLeases.empty(),"cleared or disabled executable rules return manufacturer control");
 reset();seed();onPersist=[]{oemVendorStackRequest(false);};oemVendorStackRequest(true);drain();check(stops==0,"rule clear during durable write fences remaining stop operations");
 reset();seed();scmFail=true;oemVendorStackRequest(true);drain();check(!state("applied")&&state("apply-failed"),"SCM access failure remains observable");
 reset();g_machineIdentity.family=YmccFamily::Gpd;oemVendorStackRequest(true);drain();check(stops==0,"no services inferred for generic GPD family");
#ifdef YMCC_VENDOR_R15
 reset();seed();g_machineIdentity.deviceClass="HandheldCompanion.Devices.AsusDevice";oemVendorStackRequest(true);drain();check(stops==0&&mutationCount==0,"generic ASUS fallback cannot acquire ROG manufacturer services");
 reset();seed();services.begin()->second.state=SERVICE_PAUSED;oemVendorStackRequest(true);drain();check(state("applied"),"paused service can be stopped under its durable original-state lease");oemVendorStackRestore("exit");check(services.begin()->second.state==SERVICE_PAUSED,"original paused service is restored to Paused not silently promoted to Running");
 reset();seed();services.begin()->second.stopFail=true;oemVendorStackRequest(true);drain();for(int i=0;i<6;++i){tick+=30000;oemVendorStackSchedule();drain();}check(state("retry-exhausted")&&g_oemVendorStackAttempts==3,"per-intent retry budget exhausts with failure not success");const int oldStops=stops;tick+=30000;oemVendorStackRequest(true);drain();check(stops==oldStops,"unchanged failed intent does not become a permanent poller");
 reset();seed();oemVendorStackRequest(true);drain();auto& s=services.begin()->second;s.type=SERVICE_DEMAND_START;s.stopFail=true;g_oemVendorStackCompleted=0;oemVendorStackSchedule();drain();check(s.type==SERVICE_DEMAND_START,"retry respects externally changed start policy");
 reset();seed();oemVendorStackRequest(true);drain();auto oldJournal=json::parse(journal);oldJournal["schema"]=1;oldJournal.erase("tasks");journal=oldJournal.dump();g_oemVendorServiceLeases.clear();g_oemVendorStackLoaded=false;check(oemVendorStackRestoreLocked("schema1-recovery"),"R13 schema1 service journal remains recoverable");
 for(const auto fam:{YmccFamily::LenovoLegionGo,YmccFamily::MsiClaw,YmccFamily::ZotacGamingZone}){
  reset();g_machineIdentity.family=fam;seed();for(const auto& n:oemVendorTaskNamesForFamily(fam))tasks[n]={};processes=oemVendorProcessNamesForFamily(fam);processes.push_back(L"UnrelatedProcess");oemVendorStackRequest(true);drain();check(state("applied"),"HC manufacturer stack service/task/process paths all admitted");check(stoppedProcesses.size()==oemVendorProcessNamesForFamily(fam).size(),"only exact HC process allowlist stopped in memory boundary");for(const auto& [n,t]:tasks)check(!t.enabled&&!t.running,"MSI scheduled task disabled and terminal");oemVendorStackRequest(false);drain();check(g_oemVendorServiceLeases.empty()&&g_oemVendorTaskLeases.empty(),"family restore commits all service/task debt");for(const auto& [n,t]:tasks)check(t.enabled&&t.running,"MSI original enabled/running task restored");
 }
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();for(const auto& n:oemVendorTaskNamesForFamily(g_machineIdentity.family))tasks[n]={};processes=oemVendorShieldProcessNamesForFamily(g_machineIdentity.family);oemVendorStackShieldRequest(true);drain();check(state("service-preserve-unverified"),"MSI shield does not mutate the protected physical-controller service");check(state("applied"),"MSI controller-only shield applies exact task/process policy");check(mutationCount==0,"MSI shield has no Foundation Service mutation");check(stoppedProcesses.size()==processes.size(),"MSI shield stops only its exact front-end process allowlist");for(const auto& name:oemVendorTaskNamesForFamily(g_machineIdentity.family))check(!tasks.contains(name),"MSI shield removes each exact startup task definition");oemVendorStackShieldRequest(false);drain();check(g_oemVendorTaskLeases.empty(),"MSI shield restore clears task lease");for(const auto& [n,t]:tasks)check(t.enabled&&t.running,"MSI shield restore returns original task state");
reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();tasks[L"MSI_Center_M_Server"]={false,false};oemVendorStackRequest(true);drain();check(taskMutations==0&&g_oemVendorTaskLeases.empty(),"pre-disabled task is not owned or modified");oemVendorStackRestore("exit");check(!tasks.begin()->second.enabled&&taskStarts==0,"pre-disabled task not enabled or started on return");
reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();tasks[L"MSI_Center_M_Server"]={false,true};oemVendorStackShieldRequest(true);drain();oemVendorStackShieldRequest(false);drain();check(!tasks[L"MSI_Center_M_Server"].enabled&&tasks[L"MSI_Center_M_Server"].running,"disabled-but-running MSI task restores both policy and runtime state");
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();tasks[L"MSI_Center_M_Server"]={};journalFail=true;oemVendorStackRequest(true);drain();check(taskMutations==0&&mutationCount==0&&stoppedProcesses.empty(),"task write-ahead failure blocks every mutation");
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();tasks[L"MSI_Center_M_Server"]={};tasks.begin()->second.stuck=true;oemVendorStackRequest(true);drain();check(!state("applied")&&state("task-disable-failed"),"task stop without terminal state cannot claim success");
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();tasks[L"MSI_Center_M_Server"]={};oemVendorStackRequest(true);drain();tasks.begin()->second.setFail=true;oemVendorStackRestore("exit");check(g_oemVendorTaskLeases.size()==1,"failed task restoration remains debt");
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();tasks[L"MSI_Center_M_Server"]={};tasks.begin()->second.deny=true;oemVendorStackRequest(true);drain();check(!state("applied"),"task access denied is not capability absence");
 reset();g_machineIdentity.family=YmccFamily::LenovoLegionGo;seed();processFail=true;oemVendorStackRequest(true);drain();check(!state("applied"),"failed manufacturer-process stop blocks aggregate success");
 reset();seed();check(oemVendorTaskNamesForFamily(g_machineIdentity.family).empty()&&oemVendorProcessNamesForFamily(g_machineIdentity.family).empty(),"no ROG task/process names guessed beyond HC source");
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();journalExists=true;journal=json({{"schema",2},{"family",static_cast<int>(g_machineIdentity.family)},{"services",json::array()},{"tasks",json::array({{{"name","ArbitraryTask"},{"wasEnabled",true},{"wasRunning",true}}})}}).dump();oemVendorStackRequest(true);drain();check(taskMutations==0&&mutationCount==0&&state("recovery-held"),"foreign task journal cannot direct arbitrary TaskScheduler writes");
#endif
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();services[L"MSI Foundation Service"]={};services[L"MSI_Unclassified_Service"]={};tasks[L"MSI_Center_M_Server"]={};oemVendorStackShieldRequest(true);drain();auto observation=oemVendorStackShieldRuntimeSnapshot();check(observation["serviceInventory"]["readOnly"]==true&&state("msi-service-inventory"),"MSI shield exports a read-only service inventory");check(observation["serviceInventory"]["discoveryAuthorizesMutation"]==false,"discovered service names never authorize mutations");check(observation["protectedServiceObservation"]["readbackVerified"]==true,"Foundation state has an actual read-only observation");check(mutationCount==0&&stops==0&&services[L"MSI_Unclassified_Service"].state==SERVICE_RUNNING,"unknown MSI service and Foundation are not stopped");check(observation["protectedServiceObservation"]["controllerOnlyRoleVerified"]==false,"SCM readback is not proof of a controller-only role");check(observation["serviceInventory"]["observedRequestedEpoch"]==observation["requestedEpoch"],"inventory epoch is bound to its shield intent");oemVendorStackRequest(true);drain();oemVendorStackShieldRequest(false);drain();check(oemVendorStackShieldRuntimeSnapshot()["shortcutOwnerActive"]==true&&oemVendorStackShieldRuntimeSnapshot()["isolationOwnerActive"]==true,"disabling shield exposes remaining shortcut owner instead of promising full restoration");
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();services[L"MSI Foundation Service"].queryFail=true;oemVendorStackShieldRequest(true);drain();check(oemVendorStackShieldRuntimeSnapshot()["protectedServiceObservation"]["readbackVerified"]==false,"unreadable preserved service is not reported as verified");
 reset();seed();oemVendorStackRequest(true);drain();check(!state("msi-service-inventory")&&oemVendorStackShieldRuntimeSnapshot()["serviceInventory"].empty(),"ROG legacy takeover does not perform MSI service diagnostics");
 reset();g_machineIdentity.family=YmccFamily::MsiClaw;seed();auto telemetryTargets=oemVendorProcessNamesForFamily(g_machineIdentity.family);check(std::find(telemetryTargets.begin(),telemetryTargets.end(),L"MCMOSDInfo")==telemetryTargets.end()&&std::find(telemetryTargets.begin(),telemetryTargets.end(),L"MSI Center OSD Info")==telemetryTargets.end(),"both telemetry executable names excluded from MSI process targets");auto shieldTelemetryTargets=oemVendorShieldProcessNamesForFamily(g_machineIdentity.family);check(std::find(shieldTelemetryTargets.begin(),shieldTelemetryTargets.end(),L"MSI Center OSD Info")==shieldTelemetryTargets.end(),"explicit shield does not stop an unverified telemetry alias");

#ifdef YMCC_VENDOR_R15
 reset(); g_machineIdentity.family = YmccFamily::MsiClaw; seed();
 for (const auto& name : oemVendorTaskNamesForFamily(g_machineIdentity.family)) tasks[name] = {};
 oemVendorStackShieldRequest(true); drain();
 const auto taskProof = std::find_if(logs.begin(), logs.end(), [](const auto& row) {
     return row.value("state", "") == "task-removed";
 });
 check(taskProof != logs.end() && (*taskProof)["mutationAttempted"] == true &&
       (*taskProof)["readbackVerified"] == true && (*taskProof)["existsReadback"] == false &&
       g_oemVendorTaskLeases.size()==2 && g_oemVendorTaskLeases.front().wasRunning &&
       taskReadyCalls==2 && taskRemovalCalls==2 && std::all_of(taskXmlTrace.begin(),taskXmlTrace.end(),[](const auto& event){
           return event["op"]!="remove" || (event["removePrepared"]==true && event["journalWrites"].template get<unsigned>()>0);
       }) &&
       (*taskProof)["requestedEpoch"] == g_oemVendorStackRequested,
       "task removal proof binds original running lease to absent readback in the same request");
 reset(); g_machineIdentity.family = YmccFamily::MsiClaw; seed();
 for (const auto& name : oemVendorTaskNamesForFamily(g_machineIdentity.family)) tasks[name] = {};
 for (auto& [name, task] : tasks) { task.enabled = false; task.running = false; }
 oemVendorStackShieldRequest(true); drain();
 const auto inactiveProof = std::find_if(logs.begin(), logs.end(), [](const auto& row) {
     return row.value("state", "") == "task-removed";
 });
 check(inactiveProof != logs.end() && (*inactiveProof)["mutationAttempted"] == true &&
       (*inactiveProof)["readbackVerified"] == true && (*inactiveProof)["existsReadback"] == false &&
       g_oemVendorTaskLeases.size()==2 && std::all_of(g_oemVendorTaskLeases.begin(),g_oemVendorTaskLeases.end(),
           [](const auto& lease){return lease.removePrepared&&!lease.wasEnabled&&!lease.wasRunning;}),
       "pre-disabled task definitions are removed while original inactive facts remain truthful");
 const auto appliedProof = std::find_if(logs.begin(), logs.end(), [](const auto& row) {
     return row.value("state", "") == "applied";
 });
 check(appliedProof != logs.end() && (*appliedProof)["requiresReboot"] == false &&
       (*appliedProof)["externalDefaultActionSuppressed"] == "DEVICE_UNVERIFIED",
       "operation completes without reboot while physical OEM action remains unverified");
#endif

#ifdef YMCC_SHIELD_IPC_FIXTURE
 registerShieldIpcFixture();
 const auto prepareShield = [](const json& input) {
     reset(); g_machineIdentity.family = YmccFamily::MsiClaw; seed();
     g_machineIdentity.deviceClass = "HandheldCompanion.Devices.ClawCG3EM";
     for (const auto& name : oemVendorTaskNamesForFamily(g_machineIdentity.family)) tasks[name] = {};
     services[L"MSI Foundation Service"] = {};
     g_shieldIpcSettings = {{"input", input}};
     g_shieldWriteFails = g_shieldWriteConflict = false; g_shieldWrites = 0;
 };
 check(msiControllerShieldRequested(json::object()), "absent shield preference defaults on");
 check(!msiControllerShieldRequested({{"msiControllerShieldEnabled", false}}), "saved false overrides default across upgrades");
 check(msiControllerShieldRequested({{"msiControllerShieldEnabled", true}}), "saved true stays enabled");
 for (const auto& invalid : json::array({nullptr, "false", "true", 0, 1, json::array(), json::object()})) {
     check(!msiControllerShieldRequested({{"msiControllerShieldEnabled", invalid}}),
           "malformed saved preference cannot enable or throw");
 }
 check(!msiControllerShieldRequested(json::array()), "non-object policy input fails closed");
 prepareShield(json::object());
 const auto firstRead = g_shieldIpc["msiControllerShield.get"](json::object());
 check(firstRead["enabled"] == true && firstRead["supported"] == true && firstRead["applied"] == false &&
       jobs.empty() && g_shieldWrites == 0, "first getter reports default intent without applying or persisting it");
 oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input"))); drain();
 const auto defaultApplied = g_shieldIpc["msiControllerShield.get"](json::object());
 check(defaultApplied["enabled"] == true && defaultApplied["applied"] == true &&
       services[L"MSI Foundation Service"].state == SERVICE_RUNNING && mutationCount == 0,
       "default startup applies only the admitted MSI task/process policy and preserves Foundation");
 const auto disabled = g_shieldIpc["msiControllerShield.set"]({{"enabled", false}}); drain();
 check(disabled["ok"] == true && ymSettingsSection("input")["msiControllerShieldEnabled"] == false &&
       ymSettingsSection("input")["revision"] == 1, "manual off is persisted by the real CAS path");
 oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input"))); drain();
 check(g_shieldIpc["msiControllerShield.get"](json::object())["enabled"] == false &&
       !oemVendorStackShieldIsWanted() && g_oemVendorTaskLeases.empty(),
       "background refresh does not turn an explicit off back on");
 const auto savedOff = ymSettingsSection("input"); prepareShield(savedOff);
 oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input"))); drain();
 check(taskMutations == 0 && processCalls == 0 && !oemVendorStackShieldIsWanted(),
       "persisted off survives a new runtime rather than becoming default on");
 const auto enabledAgain = g_shieldIpc["msiControllerShield.set"]({{"enabled", true}}); drain();
 check(enabledAgain["ok"] == true && ymSettingsSection("input")["revision"] == 2 &&
       g_shieldIpc["msiControllerShield.get"](json::object())["applied"] == true,
       "manual re-enable after explicit off still applies");
 for (const auto* device : {"ClawA1M", "ClawA2VM", "ClawBZ2EM", "ClawCG3EM"}) {
     prepareShield(json::object()); g_machineIdentity.deviceClass = std::string("HandheldCompanion.Devices.") + device;
     oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input"))); drain();
     check(oemVendorStackShieldRuntimeSnapshot()["applied"] == true, "default on reaches each admitted Claw model");
 }
 for (const auto family : {YmccFamily::AsusRogAlly, YmccFamily::Gpd, YmccFamily::LenovoLegionGo, YmccFamily::Unknown}) {
     prepareShield(json::object()); g_machineIdentity.family = family; seed();
     oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input"))); drain();
     check(!oemVendorStackShieldIsWanted() && taskMutations == 0 && mutationCount == 0 && processCalls == 0 && taskReadyCalls==0 && taskRemovalCalls==0 && taskImportCalls==0,
           "default preference never grants MSI operations to a different or unknown family");
 }
 prepareShield(json::object()); g_machineIdentity.deviceClass = "HandheldCompanion.Devices.UnverifiedClaw";
 oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input"))); drain();
 check(!oemVendorStackShieldIsWanted() && taskMutations == 0 && processCalls == 0,
       "unknown MSI model remains outside automatic isolation");
 prepareShield(json::object()); g_machineIdentity.family = YmccFamily::Unknown;
 oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input")));
 check(jobs.empty(), "unresolved identity does not queue default isolation");
 g_machineIdentity.family = YmccFamily::MsiClaw;
 oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input"))); drain();
 check(oemVendorStackShieldRuntimeSnapshot()["applied"] == true,
       "later resolved identity applies default on without a settings revision change");
 for (const auto& request : json::array({nullptr, true, "true", 1, json::array(), json::object(),
                                      json{{"enabled", nullptr}}, json{{"enabled", "false"}}, json{{"enabled", 1}}})) {
     prepareShield(json::object());
     const auto result = g_shieldIpc["msiControllerShield.set"](request);
     check(result["ok"] == false && result["enabled"] == true && result["reason"] == "invalid-enabled" &&
           g_shieldWrites == 0 && jobs.empty(), "malformed toggle is rejected without silently disabling the default");
 }
 for (const auto& input : json::array({json::object(), json{{"msiControllerShieldEnabled", false}}, json{{"msiControllerShieldEnabled", true}}})) {
     prepareShield(input); g_shieldWriteFails = true;
     const auto result = g_shieldIpc["msiControllerShield.set"]({{"enabled", false}});
     check(result["ok"] == false && result["enabled"] == msiControllerShieldRequested(input) &&
           ymSettingsSection("input") == input && jobs.empty(),
           "storage failure reports the current saved or default preference and does not apply");
 }
 prepareShield({{"revision", 7}, {"msiControllerShieldEnabled", false}, {"outputTarget", {{"persona", "steamdeck"}}}});
 g_shieldWriteConflict = true;
 const auto conflicted = g_shieldIpc["msiControllerShield.set"]({{"enabled", true}});
 check(conflicted["ok"] == false && conflicted["reason"] == "settings-revision-conflict" &&
       conflicted["enabled"] == false && ymSettingsSection("input")["revision"] == 7 && jobs.empty(),
       "revision conflict preserves saved off and does not apply an uncommitted request");
 g_shieldWriteConflict = false;
 const auto committed = g_shieldIpc["msiControllerShield.set"]({{"enabled", true}});
 check(committed["ok"] == true && ymSettingsSection("input")["revision"] == 8 &&
       ymSettingsSection("input")["outputTarget"]["persona"] == "steamdeck",
       "successful boolean CAS preserves unrelated controller configuration");
 for (const auto& revision : json::array({nullptr, true, "0", -1, 0.5,
          json((std::numeric_limits<long long>::max)()), json((std::numeric_limits<unsigned long long>::max)())})) {
     prepareShield({{"revision", revision}});
     const auto result = g_shieldIpc["msiControllerShield.set"]({{"enabled", false}});
     check(result["ok"] == false && result["enabled"] == true &&
           ymSettingsSection("input")["revision"] == revision && g_shieldWrites == 0 && jobs.empty(),
           "invalid or overflowing revision is rejected without exception or mutation");
 }
 prepareShield({{"revision", (std::numeric_limits<long long>::max)() - 1}});
 check(g_shieldIpc["msiControllerShield.set"]({{"enabled", false}})["ok"] == true &&
       ymSettingsSection("input")["revision"] == (std::numeric_limits<long long>::max)(),
       "highest incrementable native revision does not overflow");
 prepareShield(json::object());
 oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input")));
 g_shieldIpc["msiControllerShield.set"]({{"enabled", false}}); drain();
 check(taskMutations == 0 && processCalls == 0 && !oemVendorStackShieldIsWanted(),
       "manual off wins over a queued default startup before any stop is issued");

 prepareShield(json::object());
 onPersist = [] { g_shieldIpc["msiControllerShield.set"]({{"enabled", false}}); };
 oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input"))); drain();
 check(taskMutations == 0 && stoppedProcesses.empty() && g_oemVendorTaskLeases.empty() &&
       !msiControllerShieldRequested(ymSettingsSection("input")),
       "manual off during lease persistence fences default isolation and clears recovery debt");
 prepareShield(json::object()); processFail = true;
 oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input"))); drain();
 for (int attempt = 0; attempt < 4; ++attempt) { tick += 4000; oemVendorStackSchedule(); drain(); }
 const auto failedDefault = g_shieldIpc["msiControllerShield.get"](json::object());
 check(failedDefault["applied"] == false && failedDefault["lastOutcome"] == "retry-exhausted" && processCalls == 3,
       "default-on stop failure exhausts the bounded retry budget without claiming success");
 const auto completedCalls = processCalls;
 oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input"))); drain();
 g_shieldIpc["msiControllerShield.get"](json::object());
 check(processCalls == completedCalls && jobs.empty(), "unchanged default and status reads do not create endless retries");
 g_shieldIpc["msiControllerShield.set"]({{"enabled", false}}); drain(); processFail = false;
 g_shieldIpc["msiControllerShield.set"]({{"enabled", true}}); drain();
 check(g_shieldIpc["msiControllerShield.get"](json::object())["applied"] == true,
       "explicit off-on permits a fresh successful attempt after a failed default startup");
 prepareShield(json::object());
 oemVendorStackShieldRequest(true); drain(); oemVendorStackRequest(true); drain();
 g_shieldIpc["msiControllerShield.set"]({{"enabled", false}}); drain();
 const auto otherOwner = g_shieldIpc["msiControllerShield.get"](json::object());
 check(otherOwner["enabled"] == false && otherOwner["applied"] == false &&
       otherOwner["shortcutOwnerActive"] == true && otherOwner["isolationOwnerActive"] == true &&
       g_oemVendorTaskLeases.empty() && taskImports==2 && tasks.size()==2 &&
       tasks.at(L"MSI_Center_M_Server").enabled && tasks.at(L"MSI_Center_M_Server").running &&
       tasks.at(L"MSI_Center_M_Updater").enabled && tasks.at(L"MSI_Center_M_Updater").running,
       "explicit OFF restores both task definitions and original state while shortcut owner stays active");
 // Required delete/import boundaries in the original consumer, with one bounded parameter set.
 const auto taskTargets=oemVendorTaskNamesForFamily(YmccFamily::MsiClaw);
 for(const auto* fault:{"template-missing","ready-denied","query-denied","remove-denied","still-present"}) {
     prepareShield(json::object());
     for(const auto& name:taskTargets) {
         auto& port=taskXmlPorts[name];
         port.templateMissing=std::string(fault)=="template-missing";
         port.readyDenied=std::string(fault)=="ready-denied";
         tasks.at(name).deny=std::string(fault)=="query-denied";
         port.removeDenied=std::string(fault)=="remove-denied";
         port.removeStillPresent=std::string(fault)=="still-present";
     }
     oemVendorStackShieldRequest(true);drain();
     const bool removalReached=std::string(fault)=="remove-denied"||std::string(fault)=="still-present";
     check(oemVendorStackShieldRuntimeSnapshot()["applied"]==false && tasks.size()==2 && taskMutations==0 &&
           taskRemovalCalls==(removalReached?2u:0u) && g_oemVendorTaskLeases.size()==(removalReached?2u:0u) &&
           (removalReached?fixtureTaskJournalFlag(taskTargets.front(),"removePrepared"):journalWrites==0),
           (std::string("MSI delete boundary holds original tasks and refuses applied: ")+fault).c_str());
 }
 prepareShield(json::object());journalFail=true;
 oemVendorStackShieldRequest(true);drain();
 check(tasks.size()==2 && taskRemovalCalls==0 && taskMutations==0 && processCalls==0 && g_oemVendorTaskLeases.empty(),
       "MSI removal journal failure blocks deletion and later process mutations");
 prepareShield(json::object());tasks.clear();
 oemVendorStackShieldRequest(true);drain();oemVendorStackShieldRequest(false);drain();
 check(tasks.empty() && g_oemVendorTaskLeases.empty() && taskRemovalCalls==0 && taskImportCalls==0,
       "MSI tasks absent before shielding never acquire debt or get imported on OFF");
 for(const auto* fault:{"import-failure","template-missing"}) {
     prepareShield(json::object());oemVendorStackShieldRequest(true);drain();
     for(const auto& name:taskTargets) {
         taskXmlPorts[name].importFail=std::string(fault)=="import-failure";
         taskXmlPorts[name].templateMissing=std::string(fault)=="template-missing";
     }
     oemVendorStackShieldRequest(false);drain();
     check(tasks.empty() && taskImportCalls==2 && g_oemVendorTaskLeases.size()==2 &&
           json::parse(journal)["tasks"].size()==2 &&
           std::all_of(taskXmlTrace.begin(),taskXmlTrace.end(),[](const auto& event){
               return event["op"]!="import"||(event["removePrepared"]==true&&event["returnPrepared"]==true);
           }), (std::string("MSI failed import retains both durable prepared return obligations: ")+fault).c_str());
 }
 const auto failedImportJournal=journal;
 prepareShield({{"msiControllerShieldEnabled",false}});tasks.clear();journal=failedImportJournal;journalExists=true;
 oemVendorStackRequest(true);
 oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input")));drain();
 check(g_oemVendorShortcutWanted && !g_oemVendorShieldWanted && g_oemVendorTaskLeases.empty() &&
       taskImports==2 && taskRemovalCalls==0 && tasks.size()==2 &&
       std::all_of(tasks.begin(),tasks.end(),[](const auto& entry){return entry.second.enabled&&entry.second.running;}),
       "saved OFF restart restores failed import debt while shortcut owner remains active");
 oemVendorStackRequest(true);drain();
 check(taskImports==2 && taskRemovalCalls==0 && g_oemVendorTaskLeases.empty(),
       "unchanged shortcut refresh after explicit OFF cannot disable or recreate returned tasks");
 prepareShield(json::object());oemVendorStackShieldRequest(true);drain();
 const auto beforeImportFailure=journal;journalFail=true;
 oemVendorStackShieldRequest(false);drain();
 check(tasks.empty() && taskImportCalls==0 && g_oemVendorTaskLeases.size()==2 && journal==beforeImportFailure,
       "MSI return preparation persistence failure forbids XML import and preserves debt");
 for(const int schema:{2,3}) {
     prepareShield(json::object());tasks.clear();auto legacy=stormTaskDocument(schema);
     legacy["futureTaskMetadata"]="KEEP";legacy["tasks"][0]["futureLeaseMetadata"]="KEEP";
     const auto name=U2W(legacy["tasks"][0]["name"].get<std::string>());tasks[name]={false,false};
     journal=legacy.dump();journalExists=true;
     const bool legacyRead=oemVendorStackLoadJournal()&&g_oemVendorTaskLeases.size()==1&&!g_oemVendorTaskLeases.front().removePrepared;
     oemVendorStackShieldRequest(true);drain();const auto migrated=json::parse(journal);
     oemVendorStackShieldRequest(false);drain();
     check(legacyRead && migrated["schema"]==4 && migrated["tasks"][0]["removePrepared"]==true &&
           migrated["futureTaskMetadata"]=="KEEP" && migrated["tasks"][0]["futureLeaseMetadata"]=="KEEP" &&
           tasks.contains(name) && tasks.at(name).enabled && tasks.at(name).running && taskImports==1 && g_oemVendorTaskLeases.empty(),
           (std::string("MSI legacy schema")+std::to_string(schema)+" migrates removal authority and preserves unknown metadata").c_str());
 }
 for(const int schema:{2,3,4}) {
     prepareShield(json::object());auto invalid=stormTaskDocument(schema);
     invalid["tasks"][0]["removePrepared"]=schema<4?json(true):json("true");
     journal=invalid.dump();journalExists=true;const auto bytes=journal;
     oemVendorStackShieldRequest(true);drain();
     check(state("recovery-held") && taskMutations==0 && taskRemovalCalls==0 && taskImportCalls==0 && journalWrites==0 && journal==bytes,
           (std::string("MSI rejects reserved removePrepared authority with invalid schema/type: ")+std::to_string(schema)).c_str());
 }
 prepareShield(json::object());
 services[L"MSI Foundation Service"].state = SERVICE_STOPPED;
 services[L"MSI Foundation Service"].type = SERVICE_DEMAND_START;
 oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input"))); drain();
 check(services[L"MSI Foundation Service"].state == SERVICE_STOPPED &&
       services[L"MSI Foundation Service"].type == SERVICE_DEMAND_START && mutationCount == 0 && starts == 0,
       "default isolation also preserves a Foundation service the user had already stopped");
 prepareShield(json::object());
 oemVendorStackShieldRequest(true); drain();
 const auto crashJournal = journal;
 prepareShield(json::object()); journal = crashJournal; journalExists = true;
 tasks.clear();
 oemVendorStackShieldRequest(msiControllerShieldRequested(ymSettingsSection("input"))); drain();
 check(oemVendorStackShieldRuntimeSnapshot()["applied"] == true && g_oemVendorTaskLeases.size() == 2,
       "default startup recovers a previous lease and reapplies isolation");
 oemVendorStackRestore("fixture-app-exit");
 check(g_oemVendorTaskLeases.empty() && tasks[L"MSI_Center_M_Server"].enabled && tasks[L"MSI_Center_M_Server"].running,
       "exit still restores the original enabled and running task state after crash recovery");
#endif
#include "msi_controller_shield_wake_cases.h"
 std::cout<<"cases="<<cases<<" failures="<<failed<<" hardwareCalls=0 RESULT="<<(failed?"FAIL":"PASS")<<"\n";return failed?1:0;
}
