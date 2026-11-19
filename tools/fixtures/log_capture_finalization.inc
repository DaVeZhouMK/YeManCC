static json captureFinalizationRows=json::array();
static std::wstring captureSelfPath() {
    wchar_t path[32768]{};const auto n=GetModuleFileNameW(nullptr,path,32768);
    if(!n||n>=32768)throw std::runtime_error("owned self path unavailable");return std::wstring(path,n);
}
static bool captureWitnessReturned() {
    if(!captureRootWitness)return false;
    const auto waited=WaitForSingleObject(captureRootWitness,2000);const bool closed=CloseHandle(captureRootWitness)!=FALSE;
    captureRootWitness=nullptr;return waited==WAIT_OBJECT_0&&closed;
}
int wmain(int argc,wchar_t** argv){try{
    if(argc==2&&std::wstring(argv[1])==L"--capture-child-exit7"){std::cout<<"owned-normal-output"<<std::flush;return 7;}
    if(argc==2&&std::wstring(argv[1])==L"--capture-child-hold"){Sleep(2500);return 9;}
    if(argc!=2)return 3;
    const auto normal=L"\""+captureSelfPath()+L"\" --capture-child-exit7";
    const auto held=L"\""+captureSelfPath()+L"\" --capture-child-hold";
    const auto finish=[&](){unsigned failures=0;for(const auto& q:checks)failures+=!q["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failures},{"finalizations",captureFinalizationRows},{"realPowercfgCalls",0},{"actualArchiveCalls",0},{"defaultArchiveBusinessExecuted",false}}.dump(2)<<std::endl;return failures?2:0;};
    const auto record=[&](const char* label,const RunOut& r){captureFinalizationRows.push_back({{"case",label},{"ran",r.ran},{"exitCode",r.exitCode},{"completion",r.completion},{"win32Error",r.win32Error},{"observation",ymcc::sleepexport::captureFinalizationEvidence(r.captureFinalization)}});};
#ifdef YMCC_CAPTURE_FINALIZATION_OLD_ONLY
    const auto a=productionRunCapture(normal,5000);
#else
    ymcc::sleepexport::CaptureLoopTiming phases;
    const auto a=productionRunCapture(normal,5000,{},nullptr,nullptr,nullptr,&phases);
#endif
    record("normal-child7",a);
    check("original same-EXE child exit and output are preserved",a.ran&&a.exitCode==7&&a.completion==std::string("child-exit")&&a.out=="owned-normal-output");
    check("original finalization reports accepted job termination and signalled root",a.captureFinalization.entered&&a.captureFinalization.jobTerminationAccepted&&a.captureFinalization.jobTerminationError==0&&a.captureFinalization.rootWaitResult==WAIT_OBJECT_0&&a.captureFinalization.rootWaitError==0);
    check("original returned normal root has its exact owned process witness returned",captureWitnessReturned());
#ifdef YMCC_CAPTURE_FINALIZATION_OLD_ONLY
    return finish();
#else
    check("normal child completion has no deadline job query",!phases.deadlineJob.observed&&captureAccountingCalls==0);
    const auto started=GetTickCount64();
    const auto b=productionRunCapture(held,100,{},nullptr,nullptr,nullptr,&phases);
    const auto wall=GetTickCount64()-started;
    record("deadline",b);
    captureFinalizationRows.back()["captureLoopTiming"]={{"setupThroughResume",phases.setupThroughResumeElapsedMs},{"captureLoop",phases.loopElapsedMs},{"callWall",wall}};
    const auto& job=phases.deadlineJob;
    captureFinalizationRows.back()["deadlineJobAccounting"]={{"observed",job.observed},{"available",job.available},
        {"totalProcesses",job.totalProcesses},{"activeProcesses",job.activeProcesses},{"elapsedMs",job.elapsedMs},
        {"userTime100ns",job.userTime100ns},{"kernelTime100ns",job.kernelTime100ns},{"win32Error",job.win32Error}};
    check("deadline samples the actual owned job once before termination",job.observed&&job.available&&job.win32Error==0&&
        captureAccountingCalls==1&&captureTerminationsAtAccounting==1&&captureTerminationCalls==2&&
        job.activeProcesses>0&&job.activeProcesses<=job.totalProcesses&&job.userTime100ns>=0&&job.kernelTime100ns>=0);
    check("deadline is within the capture loop, alongside non-overlapping setup and finalization",phases.observed&&phases.loopElapsedMs>=100&&
        phases.setupThroughResumeElapsedMs+phases.loopElapsedMs+job.elapsedMs+b.captureFinalization.jobTerminationElapsedMs+b.captureFinalization.rootWaitElapsedMs<=wall);
    check("deadline wrapper mapping and captured finalization remain separate",b.ran&&b.exitCode==ERROR_TIMEOUT&&b.completion==std::string("deadline")&&b.captureFinalization.entered&&b.captureFinalization.jobTerminationAccepted&&b.captureFinalization.rootWaitResult==WAIT_OBJECT_0);
    check("deadline original job cleanup leaves its exact owned root signalled",captureWitnessReturned());
    check("silent live pipe is observed empty; final drainage does not leak its closure into the loop",
        b.out.empty()&&phases.pipe.emptyPeekObserved&&!phases.pipe.nonemptyPeekObserved&&
        !phases.pipe.emptyReadObserved&&!phases.pipe.bytesReadObserved&&!phases.pipe.firstPeekError&&!phases.pipe.firstReadError);
    unsigned cancelCalls=0;const auto c=productionRunCapture(held,5000,[&]{return ++cancelCalls>=3;});record("cancel-after-resume",c);
    check("original post-resume cancellation keeps wrapper code and finalization",c.ran&&c.exitCode==ERROR_CANCELLED&&c.completion==std::string("canceled")&&c.captureFinalization.entered&&c.captureFinalization.rootWaitResult==WAIT_OBJECT_0);
    check("canceled original root witness returns",captureWitnessReturned());
    captureDenyTermination=true;const auto d=productionRunCapture(held,100);captureDenyTermination=false;record("real-termination-access-refusal",d);
    check("real restricted job refuses termination with original immediate error5",d.ran&&d.exitCode==ERROR_TIMEOUT&&d.captureFinalization.entered&&!d.captureFinalization.jobTerminationAccepted&&d.captureFinalization.jobTerminationError==ERROR_ACCESS_DENIED);
    check("original one-second root wait separately retains WAIT_TIMEOUT",d.captureFinalization.rootWaitResult==WAIT_TIMEOUT&&d.captureFinalization.rootWaitError==0);
    const auto timed=ymcc::sleepexport::captureFinalizationEvidence(d.captureFinalization);
    check("the actual one-second wait is attributed to the root wait, with separate termination timing",timed["rootWaitElapsedMs"].is_number_unsigned()&&timed["rootWaitElapsedMs"].get<std::uint64_t>()>=900&&timed["jobTerminationElapsedMs"].is_number_unsigned());
    check("original job close still settles only this finite owned root",captureWitnessReturned());
    captureDenyFinalWait=true;const auto e=productionRunCapture(normal,5000);captureDenyFinalWait=false;record("real-root-wait-access-refusal",e);
    check("real restricted process wait records WAIT_FAILED and original immediate error5",e.ran&&e.exitCode==7&&e.completion==std::string("child-exit")&&e.captureFinalization.rootWaitResult==WAIT_FAILED&&e.captureFinalization.rootWaitError==ERROR_ACCESS_DENIED);
    check("failed final wait does not prevent separate exact owned root settlement observation",captureWitnessReturned());
    const auto before=productionRunCapture(held,5000,[]{return true;},nullptr,nullptr,nullptr,&phases);record("canceled-before-start",before);
    check("pre-start cancellation clears previous timing and deadline job observations",!phases.observed&&!phases.deadlineJob.observed);
    const auto absent=ymcc::sleepexport::captureFinalizationEvidence(before.captureFinalization);
    check("pre-start cancellation remains unentered with null finalization returns",!before.ran&&before.exitCode==ERROR_CANCELLED&&!before.captureFinalization.entered&&absent["jobTerminationAccepted"].is_null()&&absent["rootWaitResult"].is_null()&&absent.contains("jobTerminationElapsedMs")&&absent.at("jobTerminationElapsedMs").is_null()&&absent.contains("rootWaitElapsedMs")&&absent.at("rootWaitElapsedMs").is_null()&&!captureRootWitness);
    const auto reference=ymcc::sleepexport::captureFinalizationEvidence(d.captureFinalization);bool safe=reference.is_object();unsigned faults=0;
    for(unsigned at=1;at<=96;++at){evidenceTrack=true;evidenceCount=0;evidenceFailAt=at;evidenceFault=false;auto value=ymcc::sleepexport::captureFinalizationEvidence(d.captureFinalization);evidenceTrack=false;evidenceFailAt=0;if(evidenceFault){++faults;safe=safe&&value.is_null();}else safe=safe&&value==reference;}
    check("96 optional JSON fault arms return null or original exact finalization without throw",safe&&faults>0);
    check("five original finalization sites invoke exactly five job terminations and five root waits",captureTerminationCalls==5&&capturePostWaitCalls==5);
    check("deadline without the observer, cancellations and normal exits add no job query",captureAccountingCalls==1);
    return finish();
#endif
}catch(const std::exception& ex){evidenceTrack=false;evidenceFailAt=0;if(captureRootWitness){CloseHandle(captureRootWitness);captureRootWitness=nullptr;}std::cerr<<ex.what()<<std::endl;return 4;}}
