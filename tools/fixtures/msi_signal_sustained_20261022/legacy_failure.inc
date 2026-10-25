    // Executed with the saved old production headers and the same actual adapter.
    actualDisabledPump();
    ++g_msiDinput.bindCount;rows.clear();
    for(unsigned i=0;i<65;++i){auto p=neutral;p.bLeftTrigger=(i&1)?127:0;
        msiDinputRecordTriggerEvidence(neutralRaw,neutralRaw,&p,1,"canonical-published",1000+i,1000+i,0,0);}
    check(rows.size()==65,"legacy negative control reached its real cap");
    msiDinputRecordTriggerEvidence(neutralRaw,neutralRaw,&neutral,1,"canonical-published",6000,6000,0,0);
    check(rows.size()==66,"sustained witness missing after actual legacy64 cap");
