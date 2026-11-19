    test("route-terminal-retained-after-preflight",[]{
        auto c=reset(false);eventOwner(c);engineReply=[](const json&){return result("refused");};run(c);
        const auto first=c->snapshot.at("dualRoutes");const auto count=engineCalls;targetAvailable=false;run(c,2);
        check(engineCalls==count,"diagnostic-created-extra-engine");
        check(c->snapshot["eventId"].is_null()&&c->snapshot["lastResult"].is_null(),"old-event-id-reused-for-new-attempt");
        const auto rows=c->snapshot.at("dualRoutes");
        for(std::size_t i=0;i<2;++i){
            check(rows[i]["result"].is_null()&&rows[i]["eventId"].is_null(),"old-terminal-became-current-result");
            check(rows[i]["terminalEvidence"].is_object(),"route-terminal-evidence-not-retained");
            check(rows[i]["terminalEvidence"]["eventId"]==first[i]["eventId"],"terminal-event-lineage-lost");
            check(rows[i]["terminalEvidence"]["route"]==rows[i]["route"],"cross-route-terminal-borrowed");
            check(rows[i]["terminalEvidenceHistorical"]==true&&rows[i]["terminalEvidence"]["actionAuthority"]==false,"historical-terminal-granted-authority");
        }
    });
#if !defined(YMCC_OBSERVATION_OLD_COUNTEREXAMPLE) && !defined(YMCC_ROUTE_TERMINAL_FIRST_ONLY)
    test("route-terminal-unknown-is-not-synthetic-completion",[]{
        auto c=reset(false);eventOwner(c);targetAvailable=false;run(c);
        check(engineCalls==0,"unavailable-target-entered-engine");
        for(const auto& row:c->snapshot["dualRoutes"]){check(row["terminalEvidence"].is_null()&&row["result"].is_null(),"invented-terminal");check(row["lastResultState"]=="no-current-result","unknown-state-lost");}
    });
    test("route-terminal-uncertain-consumption-no-refund",[]{
        auto c=reset(false);eventOwner(c);privateAvailable=false;engineReply=[](const json&){return result("blocked",true);};run(c);
        const auto count=engineCalls;const auto prior=c->snapshot["routeTerminals"];run(c,2);
        check(engineCalls==count,"diagnostic-refunded-consumed-route");check(c->snapshot["routeTerminals"]==prior,"terminal-overwritten-with-no-result");
    });
    test("route-terminal-same-event-keeps-independent-route-reasons",[]{
        auto c=reset(false);eventOwner(c);engineReply=[](const json& req){auto value=result("refused");value["reason"]=req["recoveryRoute"]=="legacy-dynamic"?"fixed-profile-hash":"native-compatibility-contract";return value;};run(c);
        const auto rows=c->snapshot["dualRoutes"];check(rows.size()==2&&engineCalls==2,"route-dispatch-changed");
        check(rows[0]["terminalEvidence"]["reason"]=="fixed-profile-hash"&&rows[1]["terminalEvidence"]["reason"]=="native-compatibility-contract","route-reasons-merged");
        check(rows[0]["terminalEvidence"]["summaryPersisted"]==true&&rows[1]["terminalEvidence"]["summaryPersisted"]==true,"saved-summary-reference-missing");
    });
#endif
