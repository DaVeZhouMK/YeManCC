// Include only inside YMCC_STEAM_ORIGINAL_ENGINE_TESTING, in namespace
// ymcc::steamOriginalRecovery after the anonymous namespace/driver helpers.
// The caller owns the real Observer and fixture processes through this call.
// No lease is minted here. Native record/heap values below are helper inputs,
// not a native memory observation or a healthy-reference capture qualification.
static Json offline_process_group_lease_tests_impl(
    const std::shared_ptr<steamgamefacts::ProcessGroupLease>& lease,
    const std::string& directory,const std::string& caseFilter,
    std::function<void()> retireUnusedHelper,bool directed) {
  need(!directed||!caseFilter.empty(),"engine-role-fixture-empty-case-filter");
  const bool exactSelectedOnly=directed&&caseFilter=="engine-role-exact-selected-still-accepted";
  bool helperRetirementInvoked=false;
  need(lease&&lease->processCurrent(),"engine-role-fixture-real-lease-required");
  need(lease->graph().count==2,"engine-role-fixture-requires-two-real-members");
  need(!gOfflineTypedReadFault&&!gOfflineBeforeStateCreate,"engine-role-fixture-requires-unhooked-real-helpers");
  const DWORD selectedPid=lease->selectedGame().pid;
  const DWORD session=lease->graph().members[0].session;
  DWORD launcherPid=0;
  for(std::uint32_t i=0;i<lease->graph().count;++i)
    if(lease->graph().members[i].pid!=selectedPid)launcherPid=lease->graph().members[i].pid;
  const auto topology=lease->inspectNativeRoles(launcherPid,selectedPid);
  const auto exactTopology=exactSelectedOnly?lease->inspectNativeRoles(selectedPid,selectedPid):
      decltype(lease->inspectNativeRoles(selectedPid,selectedPid)){};
  const bool directedSelectedRoot=exactSelectedOnly&&exactTopology&&
      exactTopology->state==ymcc::steamgameroles::State::ExactSelected&&exactTopology->root.pid==selectedPid;
  need(launcherPid&&((topology&&topology->state==ymcc::steamgameroles::State::VerifiedGroupRoles&&
      topology->root.pid==launcherPid)||directedSelectedRoot),
      "engine-role-fixture-requires-real-launcher-selected-edge");
  constexpr U64 account=42;
  const fp::path root=absolute(J(directory))/L"engine-role-helper-cases";
  need(!fp::exists(root),"engine-role-fixture-directory-already-exists");
  need(fp::create_directories(root),"engine-role-fixture-directory-create");
  J cases=J::array();std::set<std::string> names;U64 fixtureBindOpens=0,handleCalls=0,observedPublisherCalls=0;
  const auto test=[&](const std::string& name,const auto& body){
    if(directed&&name!=caseFilter)return; // Before duplicate/count bookkeeping and body.
    need(names.insert(name).second,"engine-role-fixture-duplicate-case-name");
    try{body();cases.push_back({{"name",name},{"passed",true}});}
    catch(const std::exception& error){cases.push_back({{"name",name},{"passed",false},
        {"reason",std::string(error.what()).substr(0,200)}});}
    catch(...){cases.push_back({{"name",name},{"passed",false},{"reason","unknown-cpp-exception"}});}
  };
  const auto refused=[&](const auto& body,const char* expected=""){
    bool stopped=false;std::string reason;
    try{body();}catch(const std::exception& error){stopped=true;reason=error.what();}
    need(stopped,"engine-role-fixture-expected-refusal");
    need(!*expected||reason==expected,"engine-role-fixture-unexpected-refusal-reason");
  };
  const auto makeScope=[&](bool withGroup){
    Scope scope;scope.gameId=lease->source().fullGameId;scope.appid=lease->source().appid;
    const auto identity=[&](const steamgamefacts::ProcessWitness& witness){
      return normalizedIdentity(J{{"pid",witness.pid},{"birth",std::to_string(witness.created)},
          {"path",utf8(witness.image)},{"session",session}});
    };
    scope.identities["steam"]=identity(lease->steam());
    scope.identities["game"]=identity(lease->selectedGame());
    scope.roles[0]=bindLive(scope.identities.at("steam"),PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE);++fixtureBindOpens;
    scope.roles[1]=bindLive(scope.identities.at("game"),PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE);++fixtureBindOpens;
    // Explicit placeholders: no CEF/YMCC process opened or owned() claim made.
    scope.identities["ymcc"]={{"fixtureDiagnosticOnly",true},{"role","not-a-bound-ymcc-action"}};
    scope.identities["cef"]={{"fixtureDiagnosticOnly",true},{"role","not-a-bound-cef-action"}};
    if(withGroup)bindProcessGroup(scope,lease);
    return scope;
  };
  const J ui={{"available",true}};
  const auto recordFor=[&](DWORD primary,DWORD foreground){
    return J{{"available",true},{"matchingTargetRecordCount",1},{"primaryLivenessPid",primary},
        {"foregroundComparisonPid",foreground},{"originalPrimaryMatched",primary==selectedPid}};
  };
  const auto targetFor=[](DWORD primary,DWORD foreground){
    rc::detail::Target target;target.index=0;target.primary=primary;target.foreground=foreground;
    return std::vector<rc::detail::Target>{target};
  };
  const auto prime=[&](Scope& scope,DWORD primary,DWORD foreground){
    const auto record=recordFor(primary,foreground);requireGroupAssociation(ui,record);
    return requireGroupRoleTuple(scope,record,targetFor(primary,foreground));
  };
  const auto sampleFor=[&](Scope& scope){
    Snapshot sample;sample.gameId=scope.gameId;sample.flags=8196;
    // Fixture-only hash inputs. Never dereferenced/read from either process.
    sample.client.base=0x100000;sample.ui.base=0x800000;sample.flagsAddress=0x800020;
    sample.selected.engine=0x200000;sample.selected.user=0x300000;sample.records.records=0x400000;
    sample.target=targetFor(launcherPid,selectedPid).front();
    sample.record=recordFor(launcherPid,selectedPid);
    sample.roleInspection=prime(scope,launcherPid,selectedPid);
    return sample;
  };
  const std::string nonce="00000000-0000-4000-8000-000000000001";
  const auto referenceFor=[&](const Scope& scope,const Snapshot& sample){
    return J{{"schema",referenceSchema(scope)},{"scope",referenceScope(scope)},
        {"captureParent",scope.identities.at("ymcc")},{"accountFingerprint",textHash(std::to_string(account))},
        {"bindingNonce",nonce},{"pins",pins(sample,scope.roles[0].born,account,nonce)},{"healthyFlags",8196},
        {"compatibilityContract","original-running-code-data-v2"},
        {"clientSha256",sample.clientSha256},{"uiSha256",sample.uiSha256}};
  };
  const auto packetFor=[&]{
    tr::Packet packet;packet.account=account;packet.gameId=lease->source().fullGameId;
    packet.gamePid=selectedPid;packet.gameBirth=lease->selectedGame().created;
    packet.steamPid=lease->steam().pid;packet.steamBirth=lease->steam().created;packet.session=session;
    // Match the same normalization used by the engine/driver.
    const auto path=rw::pathKey(lease->selectedGame().image);
    need(path.size()<std::size(packet.gamePath),"engine-role-fixture-game-path-limit");
    std::copy(path.begin(),path.end(),packet.gamePath);
    return packet;
  };

  test("engine-role-bind-real-pins",[&]{
    auto scope=makeScope(true);
    need(scope.groupPending()&&!scope.groupPath()&&scope.processGroup==lease&&scope.roles[1].pid==selectedPid&&
        scope.roles[0].pid==lease->steam().pid&&!scope.nativeRoleTuple,"engine-role-bind-changed-selection");
  });
  for(const char* field:{"game-birth","steam-birth","game-path","steam-path","game-session","steam-session","full-gameid","appid"})
    test(std::string("engine-role-bind-refuses-")+field,[&]{
      auto scope=makeScope(false);const std::string_view value=field;
      if(value=="game-birth")++scope.roles[1].born;
      else if(value=="steam-birth")++scope.roles[0].born;
      else if(value=="game-path")scope.roles[1].path+=L".wrong";
      else if(value=="steam-path")scope.roles[0].path+=L".wrong";
      else if(value=="game-session")scope.roles[1].session^=1u;
      else if(value=="steam-session")scope.roles[0].session^=1u;
      else if(value=="full-gameid")scope.gameId^=1ULL<<32;
      else scope.appid^=1u;
      refused([&]{bindProcessGroup(scope,lease);});
      need(!scope.processGroup,"engine-role-bad-binding-retained");
    });
  test("engine-role-null-lease-json-does-not-mint",[&]{
    auto scope=makeScope(false);
    scope.identities["group"]={{"verified",true},{"memberCount",2},{"primary",launcherPid},{"selected",selectedPid}};
    bindProcessGroup(scope,{});
    need(!scope.processGroup&&!scope.groupPath(),"engine-role-json-minted-lease");
    refused([&]{prime(scope,launcherPid,selectedPid);},"group-role-path-unavailable");
  });
  test("engine-role-reference-requires-qualified-first-tuple",[&]{
    auto scope=makeScope(true);
    refused([&]{referenceScope(scope);},"group-reference-qualified-tuple-missing");
    refused([&]{scopeKey(scope,account);},"group-reference-qualified-tuple-missing");
  });
  const std::array<std::pair<const char*,J>,7> counts{{
      {"zero",0},{"two",2},{"null",nullptr},{"bool",true},{"fraction",1.5},{"negative",-1},{"string","1"}}};
  for(const auto& item:counts)test(std::string("engine-role-typed-count-refuses-")+item.first,[&]{
    auto record=recordFor(launcherPid,selectedPid);record["matchingTargetRecordCount"]=item.second;
    refused([&]{requireGroupAssociation(ui,record);},"group-native-association-unavailable");
  });
  const std::array<std::pair<const char*,J>,5> truth{{
      {"false",false},{"null",nullptr},{"number",1},{"string","true"},{"fraction",1.0}}};
  for(const auto& item:truth)for(const bool changeUi:{true,false})
    test(std::string("engine-role-available-refuses-")+(changeUi?"ui-":"record-")+item.first,[&]{
      auto state=ui,record=recordFor(launcherPid,selectedPid);
      if(changeUi)state["available"]=item.second;else record["available"]=item.second;
      refused([&]{requireGroupAssociation(state,record);},"group-native-association-unavailable");
    });
  test("engine-role-group-keeps-original-exact-false",[&]{
    auto scope=makeScope(true);auto record=recordFor(launcherPid,selectedPid);const auto original=record;
    requireGroupAssociation(ui,record);const auto roles=requireGroupRoleTuple(scope,record,targetFor(launcherPid,selectedPid));
    need(roles.state==ymcc::steamgameroles::State::VerifiedGroupRoles&&
        record==original&&record.at("originalPrimaryMatched")==false,"engine-role-exact-false-washed");
    refused([&]{requireOriginalAssociation(ui,record);},"original-native-association-unavailable");
  });
  test("engine-role-original-null-stays-rejected",[&]{
    auto record=recordFor(selectedPid,selectedPid);record["originalPrimaryMatched"]=nullptr;
    refused([&]{requireOriginalAssociation(ui,record);},"original-native-association-unavailable");
    need(record.at("originalPrimaryMatched").is_null(),"engine-role-exact-null-washed");
  });
  test("engine-role-exact-selected-still-accepted",[&]{
    auto scope=makeScope(true);const auto record=recordFor(selectedPid,selectedPid);
    requireOriginalAssociation(ui,record);
    need(prime(scope,selectedPid,selectedPid).state==ymcc::steamgameroles::State::ExactSelected,
        "engine-role-exact-selected-rejected");
    need(!scope.groupPath()&&!scope.nativeRoleTuple,"engine-role-strict-selected-entered-group-path");
    need(std::string(referenceSchema(scope))=="ymcc.steam-native-original-reference.v1",
        "engine-role-strict-selected-reference-not-v1");
    const auto strictReference=referenceScope(scope);
    need(!strictReference.contains("processGroup")&&!strictReference.contains("nativeRoles"),
        "engine-role-strict-selected-reference-has-group");
    need(scope.nativeRoleChoice&&!scope.groupPending()&&
        scope.nativeRoleChoice->mode.load(std::memory_order_acquire)==NativeRoleMode::StrictSingle&&
        scope.groupReferenceIdentity.is_null(),"engine-role-strict-selected-mode-not-frozen");
    const auto choice=scope.nativeRoleChoice;
    refused([&]{prime(scope,launcherPid,selectedPid);});
    need(scope.nativeRoleChoice==choice&&choice->mode.load(std::memory_order_acquire)==NativeRoleMode::StrictSingle&&
        !scope.groupPath()&&!scope.groupPending()&&!scope.nativeRoleTuple&&scope.groupReferenceIdentity.is_null()&&
        referenceScope(scope)==strictReference,"engine-role-strict-selected-switched-to-group");
    if(exactSelectedOnly&&retireUnusedHelper){
      const auto cancellation=steamgamefacts::RequestCancellation::create();
      need(cancellation&&cancellation->current(),"engine-role-strict-business-cancellation-create");
      const Current originalCurrent=[lease]{return lease->originalProcessesCurrent();};
      const auto action=groupActionCurrent(originalCurrent,lease,cancellation,scope.nativeRoleChoice);
      const auto observation=groupObservationCurrent(originalCurrent,lease,cancellation,scope.nativeRoleChoice);
      need(action()&&observation(),"engine-role-strict-original-guards-not-current");
      retireUnusedHelper();helperRetirementInvoked=true;
      need(!lease->processCurrent()&&lease->originalProcessesCurrent(),
          "engine-role-strict-helper-exit-changed-original-process-current");
      need(action()&&observation(),"engine-role-strict-helper-exit-cancelled-original-guards");
      need(prime(scope,selectedPid,selectedPid).state==ymcc::steamgameroles::State::ExactSelected&&
          scope.nativeRoleChoice==choice&&choice->mode.load(std::memory_order_acquire)==NativeRoleMode::StrictSingle&&
          !scope.groupPath()&&!scope.nativeRoleTuple&&scope.groupReferenceIdentity.is_null()&&
          referenceScope(scope)==strictReference,"engine-role-strict-helper-exit-changed-mode-or-reference");
      cancellation->cancel();
      need(!action()&&!observation()&&lease->originalProcessesCurrent(),
          "engine-role-strict-business-cancel-did-not-revoke-original-guards");
    }
  });
  test("engine-role-native-target-vector-empty-refuses",[&]{
    auto scope=makeScope(true);
    refused([&]{requireGroupRoleTuple(scope,recordFor(launcherPid,selectedPid),{});},"group-native-record-not-unique");
  });
  test("engine-role-native-target-vector-two-refuses",[&]{
    auto scope=makeScope(true);auto targets=targetFor(launcherPid,selectedPid);targets.push_back(targets[0]);
    refused([&]{requireGroupRoleTuple(scope,recordFor(launcherPid,selectedPid),targets);},"group-native-record-not-unique");
  });
  test("engine-role-json-target-tuple-mismatch-refuses",[&]{
    auto scope=makeScope(true);
    refused([&]{requireGroupRoleTuple(scope,recordFor(selectedPid,selectedPid),targetFor(launcherPid,selectedPid));},
        "group-native-role-tuple-changed-since-typed-read");
  });
  test("engine-role-first-native-tuple-freezes",[&]{
    auto scope=makeScope(true);prime(scope,launcherPid,selectedPid);
    prime(scope,launcherPid,selectedPid);
    refused([&]{prime(scope,selectedPid,selectedPid);},"group-native-role-tuple-changed-during-request");
    need(scope.nativeRoleTuple&&scope.nativeRoleTuple->first==launcherPid&&scope.nativeRoleTuple->second==selectedPid,
        "engine-role-frozen-tuple-replaced");
    // The field failure has selected as P and launcher as F. Preserve the
    // opposite qualified shape without collapsing either role into primary.
    auto reverse=makeScope(true);
    const auto roles=prime(reverse,selectedPid,launcherPid);
    need(reverse.groupPath()&&roles.state==ymcc::steamgameroles::State::VerifiedGroupRoles&&
        roles.nativePrimaryExact&&!roles.nativeForegroundExact&&
        std::string(referenceSchema(reverse))=="ymcc.steam-native-original-reference.v2",
        "engine-role-selected-primary-launcher-foreground-not-group");
    refused([&]{prime(reverse,selectedPid,selectedPid);},"group-native-role-tuple-changed-during-request");
    need(reverse.groupPath()&&reverse.nativeRoleTuple&&reverse.nativeRoleTuple->first==selectedPid&&
        reverse.nativeRoleTuple->second==launcherPid,"engine-role-reverse-tuple-mode-changed");
  });
  test("engine-role-root-root-no-direct-selected-refuses",[&]{
    auto scope=makeScope(true);
    // Required r3 F1 gate: topology alone may accept root/root; engine may not.
    refused([&]{prime(scope,launcherPid,launcherPid);},"group-native-selected-role-required");
    need(!scope.nativeRoleTuple,"engine-role-root-root-froze-an-unqualified-tuple");
  });
  test("engine-role-native-state-exact-bits-are-real",[&]{
    auto scope=makeScope(true);const auto sample=sampleFor(scope);const auto state=nativeState(scope,sample);
    need(state.at("originalPrimaryMatched")==false&&state.at("nativePrimaryExact")==false&&
        state.at("nativeForegroundExact")==true&&state.at("gamePid")==selectedPid&&
        state.at("processGroupRoles").at("actionAuthority")==false,"engine-role-native-state-forged-exactness");
  });
  test("engine-role-reference-v2-full-sorted-graph-and-tuple",[&]{
    auto scope=makeScope(true);prime(scope,launcherPid,selectedPid);const auto value=referenceScope(scope);
    need(std::string(referenceSchema(scope))=="ymcc.steam-native-original-reference.v2"&&
        value.at("gameId")==std::to_string(lease->source().fullGameId)&&value.at("appid")==lease->source().appid,
        "engine-role-reference-v2-identity");
    const auto& group=value.at("processGroup");const auto& members=group.at("members");
    need(group.at("selected")==scope.identities.at("game")&&members.is_array()&&members.size()==2,
        "engine-role-reference-graph-shape");
    DWORD previous=0;
    for(const auto& row:members){
      const auto pid=dword(row.at("pid"));const auto* witness=lease->witness(pid);
      const ymcc::steamgameroles::MemberIdentity* node=nullptr;
      for(std::uint32_t i=0;i<lease->graph().count;++i)if(lease->graph().members[i].pid==pid)node=&lease->graph().members[i];
      need(pid>previous&&node&&witness&&row.at("birthFileTime")==std::to_string(node->birth)&&
          row.at("path")==utf8(rw::pathKey(witness->image))&&row.at("session")==node->session&&
          row.at("parentPid")==node->parentPid,"engine-role-reference-graph-lost-fact");
      previous=pid;
    }
    need(value.at("nativeRoles").at("primaryLivenessPid")==launcherPid&&
        value.at("nativeRoles").at("foregroundComparisonPid")==selectedPid,"engine-role-reference-tuple-lost");
  });
  test("engine-role-reference-key-excludes-source-revisions",[&]{
    auto one=makeScope(true),two=makeScope(true);prime(one,launcherPid,selectedPid);prime(two,launcherPid,selectedPid);
    const auto value=referenceScope(one);
    for(const char* field:{"sourceRevision","consoleRevision","requestRevision"})
      need(!value.contains(field)&&!value.at("processGroup").contains(field),"engine-role-reference-leaked-request-revision");
    need(scopeKey(one,account)==scopeKey(two,account),"engine-role-identical-object-key-changed");
    // One immutable lease only: this is shape/determinism, not a fresh-revision mint test.
  });
  test("engine-role-single-reference-keeps-v1",[&]{
    auto scope=makeScope(false);const auto value=referenceScope(scope);
    need(std::string(referenceSchema(scope))=="ymcc.steam-native-original-reference.v1"&&
        !value.contains("processGroup")&&!value.contains("nativeRoles"),"engine-role-single-reference-upgraded");
  });
  test("engine-role-group-seed-import-forbidden-even-fixture-health",[&]{
    auto scope=makeScope(true);auto sample=sampleFor(scope);
    const J oldSeed={{"healthyOriginalObserved",true},{"schema","steam.wuxia.manual-sleep-observation.v1"}};
    refused([&]{importReference(oldSeed,scope,account);},"group-reference-seed-import-forbidden");
    sample.flags=4;
    need(referenceFromSeed(oldSeed,scope,sample,account).is_null(),"engine-role-seed-upgraded-lost-group");
  });
  test("engine-role-reference-fixture-v2-loader-roundtrip",[&]{
    auto scope=makeScope(true);const auto sample=sampleFor(scope);const auto reference=referenceFor(scope,sample);
    const auto data=root/L"v2-loader";const auto path=data/L"references"/utf16(scopeKey(scope,account)+".json");
    durableCreate(path,reference.dump());const auto bytes=readLegacyReferenceBytes(path);
    need(loadCompatibleReference(data,scope,sample,account)==reference&&readLegacyReferenceBytes(path)==bytes&&
        !fp::exists(data/L"consumed"),"engine-role-v2-loader-changed-authority-files");

    // Parameterized continuation boundaries under the existing case ID.
    // All heap/reference values remain synthetic helper inputs, never native health.
    unsigned ordinal=0;
    const auto continuation=[&](const std::function<void(J&,Snapshot&)>& mutate,
        bool accept,bool currentFile=false,bool healthy=false,bool otherAccount=false,bool ambiguous=false){
      auto now=makeScope(true);prime(now,selectedPid,launcherPid);
      auto lost=sample;lost.target=targetFor(selectedPid,launcherPid).front();lost.flags=healthy?8196u:4u;
      auto saved=reference;
      // Historical root/root reference is a fixture value, not a newly minted proof.
      saved["scope"]["nativeRoles"]={{"primaryLivenessPid",launcherPid},{"foregroundComparisonPid",launcherPid}};
      const auto lookup=saved.at("scope");
      const auto priorKey=textHash(J{{"scope",lookup},{"accountFingerprint",textHash(std::to_string(account))}}.dump());
      mutate(saved,lost);
      const auto dir=root/utf16("v2-continuation-"+std::to_string(ordinal++));
      const auto prior=dir/L"references"/utf16(priorKey+".json");
      const auto bytes=saved.dump(2);durableCreate(prior,bytes);
      const auto currentPath=dir/L"references"/utf16(scopeKey(now,account)+".json");
      if(currentFile)durableCreate(currentPath,"null");
      if(ambiguous){
        auto second=reference;
        const auto key=textHash(J{{"scope",second.at("scope")},{"accountFingerprint",textHash(std::to_string(account))}}.dump());
        durableCreate(dir/L"references"/utf16(key+".json"),second.dump());
      }
      const auto lifeBefore=life(now).dump();unsigned reads=0;
      if(accept){
        const auto loaded=loadCompatibleReference(dir,now,lost,account,[&]{++reads;return lost;});
        need(loaded==saved&&loaded.at("scope")==lookup&&now.continuedReferenceScope.has_value(),
            "engine-role-continuation-not-original");
        matchReference(loaded,now,lost,account);
        auto recovered=lost;recovered.flags=8196;matchReference(loaded,now,recovered,account);
      }else if(healthy||otherAccount){
        need(loadCompatibleReference(dir,now,lost,otherAccount?account+1:account,
            [&]{++reads;return lost;}).is_null(),"engine-role-continuation-wrong-admission");
      }else refused([&]{loadCompatibleReference(dir,now,lost,account,[&]{++reads;return lost;});});
      need(reads==0&&readLegacyReferenceBytes(prior)==bytes&&life(now).dump()==lifeBefore&&
          !fp::exists(dir/L"consumed")&&!fp::exists(dir/L"disabled")&&
          (currentFile?readLegacyReferenceBytes(currentPath)=="null":!fp::exists(currentPath)),
          "engine-role-continuation-wrote-or-refunded");
    };
    const std::function<void(J&,Snapshot&)> unchanged=[](J&,Snapshot&){};
    continuation(unchanged,true);
    continuation(unchanged,false,false,true);
    continuation(unchanged,false,false,false,true);
    continuation(unchanged,false,true);
    continuation(unchanged,false,false,false,false,true);
    for(const auto* domain:{"owner","record","uiObject"})
      continuation([&](J& saved,Snapshot&){saved["pins"][domain]=std::string(64,'0');},false);
    for(const auto* role:{"steam","game","cef"})
      continuation([&](J& saved,Snapshot&){saved["scope"][role]["birthFileTime"]="1";},false);
    continuation([](J& saved,Snapshot&){saved["scope"]["gameId"]="15469709074731368449";},false);
    continuation([](J& saved,Snapshot&){saved["scope"]["epoch"]=1;},false);
    continuation([](J& saved,Snapshot&){saved["healthyFlags"]=8196.0;},false);
    continuation([](J& saved,Snapshot&){saved["schema"]="future";},false);
    continuation([](J& saved,Snapshot&){saved["bindingNonce"]="invalid";},false);
    continuation([](J& saved,Snapshot&){saved["scope"]["nativeRoles"]["primaryLivenessPid"]=1;},false);
    continuation([](J& saved,Snapshot&){saved["scope"]["nativeRoles"]["primaryLivenessPid"]=true;},false);
    continuation([](J& saved,Snapshot&){saved["scope"]["nativeRoles"]["primaryLivenessPid"]=1.0;},false);
    continuation([](J& saved,Snapshot&){saved["scope"]["nativeRoles"]["extra"]=1;},false);
    continuation([](J&,Snapshot& lost){lost.gameId^=1ULL<<32;},false);
    continuation([](J&,Snapshot& lost){lost.target.primary=1;},false);
    continuation([](J&,Snapshot& lost){lost.selected.engine+=0x100;},false);
    continuation([](J&,Snapshot& lost){lost.records.records+=0x1000;},false);
    continuation([](J&,Snapshot& lost){lost.flagsAddress+=0x1000;},false);
    need(ordinal==25,"engine-role-continuation-expected-25-boundaries");
  });
  test("engine-role-legacy-v1-not-searched-for-group",[&]{
    auto scope=makeScope(true);auto sample=sampleFor(scope);sample.flags=4;
    auto old=referenceFor(scope,sample);old["schema"]="ymcc.steam-native-original-reference.v1";
    old["scope"]=legacyReferenceScope(scope);
    const auto data=root/L"legacy-only",oldPath=data/L"references"/utf16(legacyReferenceKey(scope,account)+".json");
    durableCreate(oldPath,old.dump());const auto bytes=readLegacyReferenceBytes(oldPath);
    need(loadCompatibleReference(data,scope,sample,account).is_null()&&
        !fp::exists(data/L"references"/utf16(scopeKey(scope,account)+".json"))&&
        readLegacyReferenceBytes(oldPath)==bytes,"engine-role-legacy-health-upgraded-group");
  });
  test("engine-role-v1-at-current-group-key-refuses",[&]{
    auto scope=makeScope(true);const auto sample=sampleFor(scope);auto bad=referenceFor(scope,sample);
    bad["schema"]="ymcc.steam-native-original-reference.v1";
    const auto data=root/L"wrong-current-schema",path=data/L"references"/utf16(scopeKey(scope,account)+".json");
    durableCreate(path,bad.dump());const auto bytes=readLegacyReferenceBytes(path);
    refused([&]{loadCompatibleReference(data,scope,sample,account);},"healthy-reference-scope");
    need(readLegacyReferenceBytes(path)==bytes,"engine-role-invalid-reference-overwritten");
  });
  for(const char* field:{"pid","birthFileTime","path","session","parentPid"})
    test(std::string("engine-role-reference-member-change-refuses-")+field,[&]{
      auto scope=makeScope(true);const auto sample=sampleFor(scope);auto bad=referenceFor(scope,sample);
      auto& row=bad["scope"]["processGroup"]["members"][0];
      if(std::string_view(field)=="path")row[field]="C:/fixture/changed.exe";
      else if(std::string_view(field)=="birthFileTime")row[field]="1";
      else row[field]=0xffffffffu;
      refused([&]{matchReference(bad,scope,sample,account);},"healthy-reference-scope");
    });
  test("engine-role-reference-native-tuple-change-refuses",[&]{
    auto scope=makeScope(true);const auto sample=sampleFor(scope);auto bad=referenceFor(scope,sample);
    bad["scope"]["nativeRoles"]["primaryLivenessPid"]=selectedPid;
    refused([&]{matchReference(bad,scope,sample,account);},"healthy-reference-scope");
  });
  test("engine-role-reference-original-owner-pins-still-required",[&]{
    auto scope=makeScope(true);const auto sample=sampleFor(scope);auto bad=referenceFor(scope,sample);
    bad["pins"]["owner"]=std::string(64,'0');
    refused([&]{matchReference(bad,scope,sample,account);},"saved-healthy-pins-changed");
  });
  test("engine-role-group-single-life-and-consumption-do-not-refund",[&]{
    auto group=makeScope(true),single=makeScope(false),exactGroup=makeScope(true);
    prime(group,launcherPid,selectedPid);prime(exactGroup,selectedPid,selectedPid);
    need(life(group)==life(single)&&life(group)==life(exactGroup),"engine-role-group-added-lifetime-key");
    const auto data=root/L"shared-budget";
    J event={{"recoveryRoute","semi-static"},{"trigger",{{"kind","startup"},{"generation",1},{"id","engine-role-once"}}}};
    consume(event,group,data,false);need(!fp::exists(data/L"consumed"),"engine-role-preflight-spent-budget");
    consume(event,group,data);
    const auto operation=textHash(J{{"life",life(single)},{"operation","startup-mutation-once"}}.dump());
    const auto path=data/L"consumed"/utf16(operation+".json");const auto bytes=readLegacyReferenceBytes(path);
    need(readJson(path).at("life")==life(single)&&readJson(path).at("refunded")==false,"engine-role-original-key-missing");
    refused([&]{consume(event,single,data,false);},"event-already-consumed");
    event["trigger"]["id"]="engine-role-different-tuple";
    refused([&]{consume(event,exactGroup,data);},"event-already-consumed");
    need(readLegacyReferenceBytes(path)==bytes&&!fp::exists(data/L"disabled"),"engine-role-budget-debt-rewritten");
    std::size_t files=0;for(const auto& entry:fp::directory_iterator(data/L"consumed"))if(entry.is_regular_file())++files;
    need(files==2,"engine-role-group-created-extra-budget-right");
  });
  test("engine-role-wire-uses-real-lease-and-canonical-selected-path",[&]{
    auto packet=packetFor();routeDriver::assignGroupRoleWire(packet,lease,launcherPid,selectedPid);
    need(tr::validRoleGroupShape(packet)&&packet.roleGroup.enabled==1&&packet.roleGroup.graph.selectedPid==selectedPid,
        "engine-role-real-wire-refused");
    for(std::uint32_t i=0;i<packet.roleGroup.graph.count;++i)
      if(packet.roleGroup.graph.members[i].pid==selectedPid)
        need(std::wstring(packet.roleGroup.identities[i].path)==std::wstring(packet.gamePath),"engine-role-selected-path-not-exact-copy");
  });
  test("engine-role-wire-case-variant-selected-path-copy",[&]{
    auto packet=packetFor();std::wstring path=packet.gamePath;
    std::transform(path.begin(),path.end(),path.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towupper(c));});
    std::fill(std::begin(packet.gamePath),std::end(packet.gamePath),L'\0');std::copy(path.begin(),path.end(),packet.gamePath);
    routeDriver::assignGroupRoleWire(packet,lease,launcherPid,selectedPid);
    need(tr::validRoleGroupShape(packet),"engine-role-wire-case-normalization-refused");
    for(std::uint32_t i=0;i<packet.roleGroup.graph.count;++i)if(packet.roleGroup.graph.members[i].pid==selectedPid)
      need(std::wstring(packet.roleGroup.identities[i].path)==path,"engine-role-wire-case-spelling-lost");
  });
  for(const char* field:{"birth","path","session"})
    test(std::string("engine-role-wire-refuses-wrong-selected-")+field,[&]{
      auto packet=packetFor();
      if(std::string_view(field)=="birth")++packet.gameBirth;
      else if(std::string_view(field)=="session")packet.session^=1u;
      else packet.gamePath[0]=(std::towlower(packet.gamePath[0])==L'c'?L'D':L'C');
      refused([&]{routeDriver::assignGroupRoleWire(packet,lease,launcherPid,selectedPid);});
    });
  test("engine-role-wire-native-tuple-outside-group-refuses",[&]{
    auto packet=packetFor();
    refused([&]{routeDriver::assignGroupRoleWire(packet,lease,lease->steam().pid,selectedPid);});
  });
  test("engine-role-wire-root-root-no-direct-selected-refuses",[&]{
    auto packet=packetFor();
    // Required r3 F1 wire gate; never assume two ancestor roles grant an action.
    refused([&]{routeDriver::assignGroupRoleWire(packet,lease,launcherPid,launcherPid);});
  });
  test("engine-role-wire-null-lease-json-cannot-mint",[&]{
    auto packet=packetFor();
    const J metadata={{"verified",true},{"enabled",1},{"primary",launcherPid},{"foreground",selectedPid}};
    need(metadata.at("verified")==true,"engine-role-fixture-metadata");
    refused([&]{routeDriver::assignGroupRoleWire(packet,{},launcherPid,selectedPid);},"driver-group-role-lease-revoked");
    need(packet.roleGroup.enabled==0,"engine-role-json-enabled-wire");
  });

  test("engine-role-wire-shape-is-not-complete-event-authority",[&]{
    auto packet=packetFor();routeDriver::assignGroupRoleWire(packet,lease,launcherPid,selectedPid);
    need(tr::validRoleGroupShape(packet)&&!tr::validRoleGroupWire(packet),
        "engine-role-missing-target-events-became-full-wire-authority");
  });
  test("engine-role-empty-original-current-real-handle-refuses",[&]{
    need(!active(),"engine-role-fixture-engine-already-active");
    const auto cancellation=steamgamefacts::RequestCancellation::create();
    need(cancellation&&cancellation->current(),"engine-role-fixture-business-cancellation-create");
    const auto data=root/L"empty-current"/L"data",logs=root/L"empty-current"/L"logs";
    J event={{"dataRoot",utf8(data.wstring())},{"logRoot",utf8(logs.wstring())},
        {"evidenceRoot",utf8((root/L"empty-current"/L"evidence").wstring())},
        {"account64","42"},{"recoveryRoute","semi-static"},{"recoveryAllowed",true},
        {"trigger",{{"kind","startup"},{"generation",1},{"id","engine-role-empty-current"},{"rulesEpoch",1}}}};
    ++handleCalls;
    const auto receipt=handle(event,{},Current{},Current{},0,0,0,{},lease,cancellation);
    observedPublisherCalls+=receipt.at("publisherCalls").get<U64>();
    need(receipt.at("status")=="refused"&&receipt.at("reason")=="group-request-scope-unavailable",
        "engine-role-empty-current-not-rejected-at-actual-group-current-gate");
    need(receipt.at("publisherCalls")==0&&receipt.at("driverEntered")==false&&receipt.at("consumed")==false&&
        !receipt.contains("before")&&!active()&&lease->processCurrent()&&!fp::exists(data/L"consumed"),
        "engine-role-empty-current-reached-native-action");
  });

  // DESTRUCTIVE TO THE CALLER'S ACTUAL LEASE. No later case may use it as live.
  test("engine-role-final-real-cancel-revokes-all-followup-helpers",[&]{
    auto scope=makeScope(true),unbound=makeScope(false);const auto sample=sampleFor(scope);
    const auto reference=referenceFor(scope,sample);auto packet=packetFor();
    need(lease->processCurrent(),"engine-role-fixture-lease-not-live-before-cancel");
    lease->cancel();
    need(!lease->processCurrent()&&!lease->inspectNativeRoles(launcherPid,selectedPid),
        "engine-role-cancel-not-observed");
    refused([&]{bindProcessGroup(unbound,lease);},"group-lease-not-current");
    refused([&]{prime(scope,launcherPid,selectedPid);});
    refused([&]{matchReference(reference,scope,sample,account);},"healthy-reference-group-lease-revoked");
    refused([&]{routeDriver::assignGroupRoleWire(packet,lease,launcherPid,selectedPid);},"driver-group-role-lease-revoked");
    const auto sameLease=lease;lease->cancel();
    need(!sameLease->processCurrent()&&!lease->processCurrent(),"engine-role-cancel-revived-original-lease");
  });
  need(!cases.empty(),"engine-role-fixture-empty-or-unknown-case-filter");
  std::size_t passed=0;for(const auto& row:cases)if(row.at("passed")==true)++passed;
  return {{"schema","ymcc.steam-engine-process-group-helper-cases.v1"},{"tests",cases.size()},{"passed",passed},
      {"success",!cases.empty()&&passed==cases.size()},{"cases",cases},{"fixtureProcessQueryOpens",fixtureBindOpens},
      {"stateDirectory",utf8(root.wstring())},{"selectedCase",directed?J(caseFilter):J(nullptr)},
      {"helperRetirementInvoked",helperRetirementInvoked},{"callerLeaseCancelledAtLastCase",!lease->processCurrent()},
      {"nativeMemoryReads",0},{"nativeSnapshots",0},{"debugAttaches",0},{"queryActors",0},{"publisherCalls",observedPublisherCalls},
      {"handleInvocations",handleCalls},{"processesCreatedByFunction",0},{"fixtureNativePinInputsOnly",true},
      {"fullNativeRecoveryVerified",false},{"fullHandleOwnershipVerified",false},{"countGreaterThanTwoEngineVerified",false}};
}

// Preserve the original two-argument all-cases entry. Explicit filters reject empty input.
Json offline_process_group_lease_tests(
    const std::shared_ptr<steamgamefacts::ProcessGroupLease>& lease,const std::string& directory){
  return offline_process_group_lease_tests_impl(lease,directory,{},std::function<void()>{},false);
}
Json offline_process_group_lease_tests(
    const std::shared_ptr<steamgamefacts::ProcessGroupLease>& lease,const std::string& directory,
    const std::string& caseFilter){
  need(!caseFilter.empty(),"engine-role-fixture-empty-case-filter");
  return offline_process_group_lease_tests_impl(lease,directory,caseFilter,std::function<void()>{},true);
}
Json offline_process_group_lease_tests(
    const std::shared_ptr<steamgamefacts::ProcessGroupLease>& lease,const std::string& directory,
    const std::string& caseFilter,std::function<void()> retireUnusedHelper){
  need(!caseFilter.empty(),"engine-role-fixture-empty-case-filter");
  return offline_process_group_lease_tests_impl(lease,directory,caseFilter,std::move(retireUnusedHelper),true);
}
