static const json& promotionPart(const json& view,unsigned attempt){return view.at("attempts").at(attempt).at("promotionEvidence");}
static void cleanupPromotionOwned(const fspath::path& stage,const fspath::path& root,const wchar_t* prefix){if(stage.empty()||!fspath::equivalent(stage.parent_path(),root)||stage.filename().wstring().find(prefix)!=0)throw std::runtime_error("fixture cleanup outside exact owner");std::error_code ec;fspath::remove_all(stage,ec);if(ec)throw std::runtime_error("fixture cleanup failed");}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2)return 3;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);const auto input=root/L"native-lifecycle.log";put(input,"{\"event\":\"owned-promotion-observation\"}\n");
 const auto h1=sha256File(input.wstring()),h2=sha256File(input.wstring());const auto c1=productionRunCapture(L"\"C:\\Windows\\System32\\tar.exe\" --version",10000),c2=productionRunCapture(L"\"C:\\Windows\\System32\\tar.exe\" --version",10000);if(h1.size()!=64||h1!=h2||!c1.ran||c1.exitCode||!c2.ran||c2.exitCode)throw std::runtime_error("owned initialization failed");DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
 json snapshots=json::array();snapshotAttemptObserver=[&](const wchar_t* directory){std::ifstream f(fspath::path(directory)/L"snapshot-evidence"/L"log-export-snapshot.json");json value;f>>value;snapshots.push_back(value);};
 const auto normal=createTarArchiveWithRetry((root/L"normal.zip").wstring(),{input.wstring()});const auto normalView=archiveAttemptTimingEvidence(normal);
 check("original-normal-real-tar-result-and-cleanup-unchanged",normal.ok&&normal.cleanupOk&&normal.attempts==1);
 check("original-promotion-call-has-independent-optional-evidence",normalView["attempts"][0].contains("promotionEvidence"));
#ifdef YMCC_ARCHIVE_PROMOTION_OLD_ONLY
 snapshotAttemptObserver={};GetProcessHandleCount(GetCurrentProcess(),&after);check("owned-handles-return",before==after);unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failed},{"handlesBefore",before},{"handlesAfter",after}}.dump(2)<<std::endl;return failed?2:0;
#else
 const auto& p=promotionPart(normalView,0);check("normal-original-promotion-and-following-validator-report-separate-returns",p["promotion"]["observed"]==true&&p["promotion"]["accepted"]==true&&p["promotion"]["win32Error"]==0&&p["postPromotionValidation"]["entered"]==true&&p["postPromotionValidation"]["returned"]==true&&p["postPromotionValidation"]["readableAndNonempty"]==true);
 const auto& pre=snapshots.at(0)["archiveAttemptTiming"]["attempts"][0]["promotionEvidence"];check("pre-tar-snapshot-does-not-borrow-future-promotion",pre["promotion"]["observed"]==false&&pre["promotion"]["accepted"].is_null()&&pre["postPromotionValidation"]["entered"]==false&&pre["postPromotionValidation"]["returned"].is_null());
 HANDLE blocker=INVALID_HANDLE_VALUE;fspath::path blockedStage;bool held=false,released=false;unsigned calls=0;
 tarCompletionObserver=[&](HANDLE output,const wchar_t* directory){if(++calls!=1)return;wchar_t path[32768]{};const auto n=GetFinalPathNameByHandleW(output,path,32768,FILE_NAME_NORMALIZED);if(n&&n<32768){blocker=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);held=blocker!=INVALID_HANDLE_VALUE;blockedStage=directory;}};
 snapshotAttemptObserver=[&](const wchar_t* directory){if(blocker!=INVALID_HANDLE_VALUE){released=CloseHandle(blocker)!=FALSE;if(released)blocker=INVALID_HANDLE_VALUE;}std::ifstream f(fspath::path(directory)/L"snapshot-evidence"/L"log-export-snapshot.json");json value;f>>value;snapshots.push_back(value);};
 const auto retry=createTarArchiveWithRetry((root/L"retry.zip").wstring(),{input.wstring()});tarCompletionObserver={};snapshotAttemptObserver={};if(blocker!=INVALID_HANDLE_VALUE){CloseHandle(blocker);blocker=INVALID_HANDLE_VALUE;}const auto retryView=archiveAttemptTimingEvidence(retry);const auto& r1=promotionPart(retryView,0);const auto& r2=promotionPart(retryView,1);
 check("real-first-sharing-refusal-then-original-second-attempt-success",held&&released&&calls==2&&retry.ok&&retry.attempts==2&&!retry.cleanupOk);
 check("first-promotion-refusal-retains-original-error-before-top-result-overwrite",r1["promotion"]["observed"]==true&&r1["promotion"]["accepted"]==false&&r1["promotion"]["win32Error"]==retry.previousAttemptError&&retry.previousAttemptError==ERROR_SHARING_VIOLATION&&retry.win32Error==0);
 check("refused-promotion-does-not-claim-post-promotion-validation",r1["postPromotionValidation"]["entered"]==false&&r1["postPromotionValidation"]["readableAndNonempty"].is_null());
 check("second-success-does-not-rewrite-first-promotion-error",r2["promotion"]["accepted"]==true&&r2["promotion"]["win32Error"]==0&&r2["postPromotionValidation"]["readableAndNonempty"]==true&&r1["promotion"]["win32Error"]==ERROR_SHARING_VIOLATION);
 check("persistent-sharing-keeps-both-move-results-and-spends-only-one-repack",r1["sharingRetry"]["firstWin32Error"]==ERROR_SHARING_VIOLATION&&!r2.contains("sharingRetry")&&calls==2);
 const auto& secondSnapshot=snapshots.at(2)["archiveAttemptTiming"]["attempts"];check("second-snapshot-keeps-first-return-and-current-unobserved-slot",secondSnapshot[0]["promotionEvidence"]==r1&&secondSnapshot[1]["promotionEvidence"]["promotion"]["observed"]==false);
 cleanupPromotionOwned(blockedStage,root,L"retry.zip.staging-");check("fixture-held-reader-and-exact-failed-staging-returned",released&&!fspath::exists(blockedStage));
 const auto existing=root/L"existing.zip";put(existing,"existing-user-bytes");const auto beforeCollisionTar=tarInvocations.size();const auto collision=createTarArchiveWithRetry(existing.wstring(),{input.wstring()});const auto collisionView=archiveAttemptTimingEvidence(collision);std::ifstream f(existing);std::string kept;std::getline(f,kept);f.close();
 const auto& collisionPromotion=promotionPart(collisionView,0);const auto collisionError=collisionPromotion["promotion"]["win32Error"].get<DWORD>();
 check("existing-final-is-preserved-with-one-tar-and-one-promotion-refusal",!collision.ok&&collision.reason=="promote-failed"&&collision.attempts==1&&collision.cleanupOk&&kept=="existing-user-bytes"&&tarInvocations.size()==beforeCollisionTar+1&&collisionView["attempts"].size()==1&&collisionPromotion["promotion"]["observed"]==true&&collisionPromotion["promotion"]["accepted"]==false);
 check("collision-retains-win32-error-without-validator-or-retry",(collisionError==ERROR_ALREADY_EXISTS||collisionError==ERROR_FILE_EXISTS)&&collision.win32Error==collisionError&&collisionPromotion["postPromotionValidation"]["entered"]==false&&archivePreviousAttemptEvidence(collision).is_null()&&!collisionView["attempts"][0]["completedStepElapsedMs"].contains("retryDelay"));
 unsigned recoveredZipCount=0;for(const auto& entry:fspath::directory_iterator(root))if(entry.path().extension()==L".zip"&&entry.path().filename().wstring().find(L"retry.zip")==0)++recoveredZipCount;
 check("successful-retry-leaves-one-visible-zip",recoveredZipCount==1&&archiveOutputReadableAndNonEmpty((root/L"retry.zip").wstring()));
 bool transientHeld=false,transientReleased=false;unsigned transientMoves=0,transientTarCalls=0;
 tarCompletionObserver=[&](HANDLE output,const wchar_t*){++transientTarCalls;wchar_t path[32768]{};const auto n=GetFinalPathNameByHandleW(output,path,32768,FILE_NAME_NORMALIZED);if(n&&n<32768){blocker=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);transientHeld=blocker!=INVALID_HANDLE_VALUE;}};
 promotionAfterOriginalCall=[&](const wchar_t*,BOOL accepted){if(++transientMoves==1&&!accepted&&blocker!=INVALID_HANDLE_VALUE){transientReleased=CloseHandle(blocker)!=FALSE;if(transientReleased)blocker=INVALID_HANDLE_VALUE;}};
 const auto transientPath=root/L"transient.zip";const auto transient=createTarArchiveWithRetry(transientPath.wstring(),{input.wstring()});
 promotionAfterOriginalCall={};tarCompletionObserver={};if(blocker!=INVALID_HANDLE_VALUE){CloseHandle(blocker);blocker=INVALID_HANDLE_VALUE;}
 const auto transientView=archiveAttemptTimingEvidence(transient);const auto& transientPromotion=promotionPart(transientView,0);
 check("transient-sharing-uses-one-tar-two-moves-and-cleans-staging",transientHeld&&transientReleased&&transient.ok&&transient.cleanupOk&&transient.attempts==1&&transientTarCalls==1&&transientMoves==2&&archiveOutputReadableAndNonEmpty(transientPath.wstring())&&archivePreviousAttemptEvidence(transient).is_null());
 check("same-candidate-first-sharing-and-final-verified-move-stay-distinct",transientPromotion["sharingRetry"]["firstWin32Error"]==ERROR_SHARING_VIOLATION&&transientPromotion["sharingRetry"]["waitElapsedMs"].is_number_unsigned()&&transientPromotion["promotion"]["accepted"]==true&&transientPromotion["postPromotionValidation"]["readableAndNonempty"]==true&&!transientView["attempts"][0]["completedStepElapsedMs"].contains("retryDelay"));
 const auto latePath=root/L"late.zip";
 bool lateReleased=false;
 unsigned lateMoves=0,lateDenials=0,lateTarCalls=0;
 std::wstring lateCandidate;
 std::string lateCandidateHash;
 tarCompletionObserver=[&](HANDLE output,const wchar_t*){
     if(++lateTarCalls!=1)return;
     wchar_t path[32768]{};const auto n=GetFinalPathNameByHandleW(output,path,32768,FILE_NAME_NORMALIZED);
     if(!n||n>=32768)return;
     lateCandidate.assign(path,n);
     blocker=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 };
 promotionAfterOriginalCall=[&](const wchar_t*,BOOL accepted){
     ++lateMoves;if(!accepted)++lateDenials;
     if(lateMoves!=2||accepted||blocker==INVALID_HANDLE_VALUE)return;
     lateReleased=CloseHandle(blocker)!=FALSE;
     if(lateReleased){blocker=INVALID_HANDLE_VALUE;lateCandidateHash=sha256File(lateCandidate);}
 };
 const auto late=createTarArchiveWithRetry(latePath.wstring(),{input.wstring()});
 promotionAfterOriginalCall={};tarCompletionObserver={};if(blocker!=INVALID_HANDLE_VALUE){CloseHandle(blocker);blocker=INVALID_HANDLE_VALUE;}
 const auto lateView=archiveAttemptTimingEvidence(late);
 const auto lateFinalHash=sha256File(latePath.wstring());
 unsigned lateFormalZips=0,lateResidue=0;
 for(const auto& entry:fspath::directory_iterator(root)){
     const auto name=entry.path().filename().wstring();
     if(name==L"late.zip")++lateFormalZips;
     else if(name.find(L"late.zip")==0)++lateResidue;
 }
 check("second-sharing-refusal-reuses-first-zip-without-repacking",lateReleased&&late.ok&&late.cleanupOk&&late.attempts==1&&lateTarCalls==1&&lateMoves==3&&lateDenials==2&&lateCandidateHash.size()==64&&lateFinalHash==lateCandidateHash&&lateFormalZips==1&&lateResidue==0);
 const auto& lateFirst=lateView.at("attempts").at(0);const auto& persistentFirst=retryView.at("attempts").at(0);
 const auto spentRepackDelay=[](const json& row){
     const auto& evidence=row.at("promotionEvidence");
     if(!evidence.contains("sharingRetry"))return false;
     const auto& sharing=evidence.at("sharingRetry");
     return sharing.value("secondWin32Error",0u)==ERROR_SHARING_VIOLATION&&sharing.contains("secondWaitElapsedMs")&&
         sharing.at("secondWaitElapsedMs").is_number_unsigned()&&!row.at("completedStepElapsedMs").contains("retryDelay");
 };
 check("second-sharing-wait-is-reported-once-even-when-repack-follows",spentRepackDelay(lateFirst)&&spentRepackDelay(persistentFirst));
 bool postHeld=false;unsigned postMoves=0,postTarCalls=0;
 tarCompletionObserver=[&](HANDLE,const wchar_t*){++postTarCalls;};
 promotionAfterOriginalCall=[&](const wchar_t* finalPath,BOOL accepted){if(++postMoves!=1||!accepted)return;blocker=CreateFileW(finalPath,GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);postHeld=blocker!=INVALID_HANDLE_VALUE;};
 const auto postPath=root/L"post-validation.zip";const auto post=createTarArchiveWithRetry(postPath.wstring(),{input.wstring()});promotionAfterOriginalCall={};tarCompletionObserver={};const auto postView=archiveAttemptTimingEvidence(post);const auto& post1=promotionPart(postView,0);
 check("real-accepted-move-followed-by-sharing-denied-read-is-the-terminal-failure",postHeld&&post.attempts==1&&!post.ok&&post.reason=="promoted-archive-unreadable"&&post.win32Error==0);
 check("first-accepted-promotion-and-returned-false-validator-remain-distinct",post1["promotion"]["accepted"]==true&&post1["promotion"]["win32Error"]==0&&post1["postPromotionValidation"]["entered"]==true&&post1["postPromotionValidation"]["returned"]==true&&post1["postPromotionValidation"]["readableAndNonempty"]==false);
 check("accepted-move-skips-repacking-and-does-not-report-a-prior-attempt",postMoves==1&&postTarCalls==1&&postView["attempts"].size()==1&&archivePreviousAttemptEvidence(post).is_null()&&!postView["attempts"][0]["completedStepElapsedMs"].contains("retryDelay"));
 const auto& postChecks=postView["attempts"][0]["outputCheckEvidence"];
 check("persistent-final-sharing-keeps-both-original-open-failures",postChecks.contains("firstFinalSharingCheck")&&postChecks["firstFinalSharingCheck"].value("openWin32Error",0u)==ERROR_SHARING_VIOLATION&&postChecks["final"].value("openWin32Error",0u)==ERROR_SHARING_VIOLATION&&postChecks.contains("finalSharingWaitMs")&&postChecks["finalSharingWaitMs"].is_number_unsigned());
 const bool postReleased=blocker!=INVALID_HANDLE_VALUE&&CloseHandle(blocker)!=FALSE;blocker=INVALID_HANDLE_VALUE;check("fixture-releases-own-final-reader-without-repacking",postReleased&&archiveOutputReadableAndNonEmpty(postPath.wstring()));
 const auto clearedPath=root/L"post-sharing-cleared.zip";bool clearedHeld=false,clearedReleased=false;unsigned clearedOpens=0,clearedMoves=0,clearedTars=0;
 tarCompletionObserver=[&](HANDLE,const wchar_t*){++clearedTars;};
 promotionAfterOriginalCall=[&](const wchar_t* finalPath,BOOL accepted){++clearedMoves;if(!accepted)return;blocker=CreateFileW(finalPath,GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);clearedHeld=blocker!=INVALID_HANDLE_VALUE;};
 promotionAfterOriginalOpen=[&](const wchar_t* path,DWORD error){if(clearedPath!=path)return;++clearedOpens;if(error==ERROR_SHARING_VIOLATION&&blocker!=INVALID_HANDLE_VALUE){clearedReleased=CloseHandle(blocker)!=FALSE;blocker=INVALID_HANDLE_VALUE;}};
 const auto cleared=createTarArchiveWithRetry(clearedPath.wstring(),{input.wstring()});
 promotionAfterOriginalOpen={};promotionAfterOriginalCall={};tarCompletionObserver={};if(blocker!=INVALID_HANDLE_VALUE){CloseHandle(blocker);blocker=INVALID_HANDLE_VALUE;}
 const auto clearedView=archiveAttemptTimingEvidence(cleared);const auto& clearedChecks=clearedView["attempts"][0]["outputCheckEvidence"];
 check("transient-final-sharing-rechecks-same-zip-one-tar-one-move",clearedHeld&&clearedReleased&&clearedOpens==2&&clearedMoves==1&&clearedTars==1&&cleared.ok&&cleared.cleanupOk&&cleared.attempts==1&&archivePreviousAttemptEvidence(cleared).is_null());
 check("recheck-success-keeps-original-error-and-last-check-separate",clearedChecks.contains("firstFinalSharingCheck")&&clearedChecks["firstFinalSharingCheck"].value("openWin32Error",0u)==ERROR_SHARING_VIOLATION&&clearedChecks["final"]["state"]=="stream-opened-nonempty"&&!clearedChecks["final"].contains("openWin32Error")&&clearedChecks.contains("finalSharingWaitMs")&&clearedChecks["finalSharingWaitMs"].is_number_unsigned()&&promotionPart(clearedView,0)["postPromotionValidation"]["readableAndNonempty"]==true&&!normalView["attempts"][0]["outputCheckEvidence"].contains("firstFinalSharingCheck"));
 const auto normalCompression=normalView["attempts"][0]["zipCompressionRequested"];
 check("shared-helper-default-call-keeps-default-compression",normalCompression=="tar-default"&&tarInvocations.front().command.find(L"zip:compression-level=")==std::wstring::npos);
 const auto startZip=tarInvocations.size();failTarChildren=1;
 const auto childRetry=createTarArchiveWithRetry((root/L"child-exit-retry.zip").wstring(),{input.wstring()});
 const auto childRetryView=archiveAttemptTimingEvidence(childRetry);const auto& childAttempts=childRetryView["attempts"];
 check("child-exit-retry-keeps-default-compression-on-both-attempts",childRetry.ok&&childRetry.attempts==2&&tarInvocations.size()==startZip+2&&tarInvocations[startZip].command.find(L"zip:compression-level=")==std::wstring::npos&&tarInvocations[startZip+1].command.find(L"zip:compression-level=")==std::wstring::npos);
 check("default-compression-and-first-error-remain-attributed-to-the-original-attempt",childAttempts[0]["zipCompressionRequested"]=="tar-default"&&childAttempts[1]["zipCompressionRequested"]=="tar-default"&&archivePreviousAttemptEvidence(childRetry)["reason"]=="tar-exit-nonzero"&&childRetry.previousAttemptError==17);
 const ymcc::logexport::SteamLogs noSteam;
 const auto withLevel5=[&](const wchar_t* name){return createTarArchiveWithRetry((root/name).wstring(),{input.wstring()},
     {},{},&noSteam,nullptr,nullptr,nullptr,ymcc::logexport::ZipCompression::level5);};
 const auto level5Start=tarInvocations.size();const auto level5=withLevel5(L"lv5.zip");const auto level5View=archiveAttemptTimingEvidence(level5);
 check("preferred-level-five-writes-readable-archive-on-first-attempt",level5.ok&&level5.cleanupOk&&level5.attempts==1&&tarInvocations.size()==level5Start+1&&archiveOutputReadableAndNonEmpty((root/L"lv5.zip").wstring()));
 check("level-five-option-is-passed-to-real-tar-and-retained-in-first-receipt",tarInvocations.size()>level5Start&&tarInvocations[level5Start].command.find(L"--options zip:hdrcharset=UTF-8,zip:compression-level=5")!=std::wstring::npos&&level5View.at("attempts").at(0).at("zipCompressionRequested")=="level-5");
 const auto level5RetryStart=tarInvocations.size();failTarChildren=1;const auto level5Retry=withLevel5(L"lv5-retry.zip");const auto level5RetryView=archiveAttemptTimingEvidence(level5Retry);
 check("level-five-failure-preserves-two-attempt-budget-and-uses-default-on-retry",level5Retry.ok&&level5Retry.cleanupOk&&level5Retry.attempts==2&&tarInvocations.size()==level5RetryStart+2&&tarInvocations[level5RetryStart].command.find(L"zip:compression-level=5")!=std::wstring::npos&&tarInvocations[level5RetryStart+1].command.find(L"zip:compression-level=")==std::wstring::npos);
 check("level-five-and-default-requests-remain-distinct-from-original-first-error",level5Retry.attempts==2&&level5RetryView.at("attempts").at(0).at("zipCompressionRequested")=="level-5"&&level5RetryView.at("attempts").at(1).at("zipCompressionRequested")=="tar-default"&&archivePreviousAttemptEvidence(level5Retry)["reason"]=="tar-exit-nonzero"&&level5Retry.previousAttemptError==17);
 forceTarLaunchFailure=true;const auto launch=createTarArchiveWithRetry((root/L"launch.zip").wstring(),{input.wstring()});forceTarLaunchFailure=false;const auto launchView=archiveAttemptTimingEvidence(launch);check("earlier-launch-refusal-does-not-invent-promotion-api-return",!launch.ok&&launch.attempts==2&&promotionPart(launchView,0)["promotion"]["observed"]==false&&promotionPart(launchView,1)["promotion"]["accepted"].is_null());
 check("observed-calls-do-not-prove-current-path-or-unique-final-file",p["replacementRequested"]==false&&p["currentPathAbsenceProven"]==false&&p["uniqueFinalZipProven"]==false);
 evidenceTrack=true;evidenceCount=0;evidenceFailAt=1;evidenceFault=false;const auto unavailable=archiveAttemptTimingEvidence(normal);evidenceTrack=false;evidenceFailAt=0;check("optional-first-json-allocation-failure-keeps-original-result",evidenceFault&&unavailable.is_null()&&normal.ok);
 GetProcessHandleCount(GetCurrentProcess(),&after);check("owned-handles-return",before==after);unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failed},{"handlesBefore",before},{"handlesAfter",after},{"normal",normalView},{"retry",retryView},{"transient",transientView},{"late",lateView},{"lateObservations",{{"released",lateReleased},{"moves",lateMoves},{"denials",lateDenials},{"tarCalls",lateTarCalls},{"firstZipSha256",lateCandidateHash},{"finalZipSha256",lateFinalHash},{"formalZips",lateFormalZips},{"residue",lateResidue}}},{"collision",collisionView},{"postValidation",postView},{"postSharingCleared",clearedView},{"level5",level5View},{"level5Retry",level5RetryView},{"launch",launchView},{"snapshot",snapshots.at(0)},{"secondSnapshot",snapshots.at(2)},{"productOrDeviceActions",0},{"realMoveWithFixtureOnlyAfterCallSharingDenial",true}}.dump(2)<<std::endl;return failed?2:0;
#endif
}catch(const std::exception& e){evidenceTrack=false;evidenceFailAt=0;std::cout<<json{{"checksBeforeFixtureFailure",checks},{"fixtureFailure",e.what()}}.dump(2)<<std::endl;std::cerr<<e.what()<<std::endl;return 4;}}
