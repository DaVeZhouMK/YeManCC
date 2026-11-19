 for (int mode=0;mode<3;++mode) {
   reset();
   const auto& dir=mode==1?SG_MANUAL_DIR:SG_SLEEP_LEASE_DIR;
   seed(dir);procs[100].depth=3;
   if(mode==2)g_sgSleepTarget={100,1234,1,7,true,true};
   remainingSuccessfulResumes=1;
   const bool first=sgCleanupBeforeExit(false);
   const bool kept=files.count(key(dir))!=0;
   const int firstMarks=sessionCleanMarks,depth=procs[100].depth;
   const bool ownerKept=mode!=2||g_sgSleepTarget.markerOwned;
   remainingSuccessfulResumes=-1;
   const bool second=sgCleanupBeforeExit(false);
   record(mode==2?"exit-partial-exact-sleep-return":mode==1?"exit-partial-manual-directory-return":"exit-partial-sleep-directory-return",
    {{"first",false},{"kept",true},{"firstMarks",0},{"depth",2},{"ownerKept",true},{"second",true},{"finalDepth",0},{"finalMarks",1}},
    {{"first",first},{"kept",kept},{"firstMarks",firstMarks},{"depth",depth},{"ownerKept",ownerKept},{"second",second},{"finalDepth",procs[100].depth},{"finalMarks",sessionCleanMarks}},
    !first&&kept&&firstMarks==0&&depth==2&&ownerKept&&second&&procs[100].depth==0&&sessionCleanMarks==1&&!files.count(key(dir)));
 }
 for (bool exact : {false,true}) {
   reset();seed();
   if(exact)g_sgSleepTarget={100,1234,1,7,true,true};
   WakeGameResume::withholdThreadProof=true;
   const bool first=sgCleanupBeforeExit(false);
   const bool kept=files.count(key(SG_SLEEP_LEASE_DIR))!=0;
   const int depth=procs[100].depth,firstMarks=sessionCleanMarks,successes=ntSuccesses;
   WakeGameResume::withholdThreadProof=false;
   const bool second=sgCleanupBeforeExit(false);
   record(exact?"exit-exact-unknown-thread-proof":"exit-directory-unknown-thread-proof",
    {{"first",false},{"kept",true},{"firstMarks",0},{"depth",0},{"second",true},{"replayed",false}},
    {{"first",first},{"kept",kept},{"firstMarks",firstMarks},{"depth",depth},{"second",second},{"replayed",successes!=ntSuccesses}},
    !first&&kept&&firstMarks==0&&depth==0&&second&&successes==ntSuccesses&&sessionCleanMarks==1&&!files.count(key(SG_SLEEP_LEASE_DIR)));
   reset();seed();procs[100].depth=0;
   if(exact)g_sgSleepTarget={100,1234,1,7,true,true};
   const bool clean=sgCleanupBeforeExit(false);
   record(exact?"exit-exact-already-running":"exit-directory-already-running",
    {{"clean",true},{"attempts",0},{"marks",1}},{{"clean",clean},{"attempts",ntAttempts},{"marks",sessionCleanMarks}},
    clean&&ntAttempts==0&&sessionCleanMarks==1&&!files.count(key(SG_SLEEP_LEASE_DIR)));
 }
