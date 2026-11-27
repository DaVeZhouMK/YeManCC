export const radialPowerSource='native/virtual_controller_ui_output_runtime.h';

export function projectFirstOnRadialInput({fixture,radial,replace,sha}){
  const signatures=radial.match(/^static void radialInputIsolationOwnership[^\r\n]+/gm);
  const signature='static void radialInputIsolationOwnership(bool active,const ymcc::sleeppower::SharedPowerV1& power) {';
  if(signatures?.length!==1||signatures[0]!==signature)throw Error('GPD_SETTLED_CURRENT_RADIAL_POWER_DECLARATION_REQUIRED');
  const project=(source,edits)=>{
    const result=edits.reduce((text,[before,after])=>replace(text,before,after,'settled first ON fixture'),source);
    const reversed=[...edits].reverse().reduce((text,[before,after])=>replace(text,after,before,'settled first ON fixture inverse'),result);
    if(reversed!==source)throw Error('GPD_SETTLED_FIXTURE_PROJECTION_NOT_REVERSIBLE');
    return result;
  };
  const projectedFixture=project(fixture,[[
    'CachedRadialOwnerScope(){radialInputIsolationOwnership(true);}',
    'CachedRadialOwnerScope(){ymcc::sleeppower::SharedPowerV1 power; if(!captureSharedPowerLifecycle(power))throw std::logic_error("foreground-neutral-public-power-capture-failed"); radialInputIsolationOwnership(true,power);}'
  ]]);
  return {fixture:projectedFixture,receipt:{symbol:'expired-first-on/fixture-radial-input',
    radialSource:radialPowerSource,radialSourceSha256:sha(radial),fixtureBeforeSha256:sha(fixture),fixtureAfterSha256:sha(projectedFixture),sourceModified:false}};
}

export function projectSettledFirstOnInputs({fixture,pipe,radial,replace,sha}){
  const projectedFixture=projectFirstOnRadialInput({fixture,radial,replace,sha}).fixture;
  const project=(source,edits)=>{
    const result=edits.reduce((text,[before,after])=>replace(text,before,after,'settled first ON fixture'),source);
    const reversed=[...edits].reverse().reduce((text,[before,after])=>replace(text,after,before,'settled first ON fixture inverse'),result);
    if(reversed!==source)throw Error('GPD_SETTLED_FIXTURE_PROJECTION_NOT_REVERSIBLE');
    return result;
  };
  const projectedPipe=project(pipe,[
    ['donor=std::make_shared<writer::Session>(writer::Session::open(nativeRefreshWriterRecordPath(),inputHostTargetReturnMachineBinding()));',
     'donor.reset(new writer::Session(writer::Session::open(nativeRefreshWriterRecordPath(),inputHostTargetReturnMachineBinding())));'],
    ['bool coldStart=true,bool allowCurrentSource=false){','bool coldStart=true,bool allowCurrentSource=false,unsigned priorCompleteBoundary=0){'],
    ['  check(!coldStart&&readsBeforeGuard[0]>0&&g_xinputPhysicalSourceLocked&&g_xinputPhysicalSourceMask==1&&',
     `  const auto scanEpoch=g_xinputBoundaryScanEpoch.load(),topology=g_xinputTopologyEpoch.load();
  const bool boundary=priorCompleteBoundary ? scanEpoch==priorCompleteBoundary&&topology>priorCompleteBoundary : scanEpoch==topology;
  check(!coldStart&&readsBeforeGuard[0]>0&&g_xinputPhysicalSourceLocked&&g_xinputPhysicalSourceMask==1&&`],
    [`   g_xinputBoundaryScanEpoch.load()==g_xinputTopologyEpoch.load(),
   "warm serial source retains only actual current-boundary admission after a fresh controlled physical read");`,
     `   boundary,priorCompleteBoundary ? "settled queued start retains its selected slot after fresh controlled input without calling the pre-BUS scan current" : "warm serial source retains only actual current-boundary admission after a fresh controlled physical read");`],
    [`   fixtureGpdTransferSourceAtEnsure=[&](){initialSourceObserved=true;
    cases.back()["initialSourceDiscovery"]=fullPipeReadPhysicalSourceForElite(false,true);};`,
     `   const auto preBusBoundary=cases.back()["preBusSource"].value("boundaryEpoch",0U);
   require(preBusBoundary&&cases.back()["preBusSource"].value("topologyEpoch",0U)==preBusBoundary, "settled queued start had a current completed scan before BUS admission");
   fixtureGpdTransferSourceAtEnsure=[&,preBusBoundary](){initialSourceObserved=true;
    cases.back()["initialSourceDiscovery"]=fullPipeReadPhysicalSourceForElite(false,true,preBusBoundary);};`],
    [`    inputHostStop("shutdown");
  }
  const auto child=fullPipeChildOutcome(observer,receipt);`,
     `    inputHostStop(settledFirstOn?"app-exit":"shutdown");
  }
  const auto child=fullPipeChildOutcome(observer,receipt);`]
  ]);
  return {fixture:projectedFixture,pipe:projectedPipe,receipt:{symbol:'settled-first-on/fixture-inputs',
    radialSource:radialPowerSource,radialSourceSha256:sha(radial),fixtureBeforeSha256:sha(fixture),fixtureAfterSha256:sha(projectedFixture),
    pipeBeforeSha256:sha(pipe),pipeAfterSha256:sha(projectedPipe),sourceModified:false}};
}
