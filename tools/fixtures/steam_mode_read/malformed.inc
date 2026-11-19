    steamui::ModeObserver malformed;std::atomic<unsigned> badReads{0};

    config.read=[&](const auto&){++badReads;return steamui::ModeJson{{"known","invalid"},{"reason",5}};};config.changed={};

    malformed.initialize(config);malformed.setDemand(steamui::ModeConsumer::Decky,true);malformed.observeSteam(session);

    expect(until([&]{return badReads==3;}),"Malformed reads use only three finite attempts");Sleep(200);

    expect(badReads==3&&malformed.snapshot(session).value("mode",std::string{})=="unknown","Malformed data cannot manufacture a mode or crash the worker");

    malformed.requestRefresh(session);expect(until([&]{return badReads>3;}),"A later explicit recovery renews parked observation");malformed.shutdown();

