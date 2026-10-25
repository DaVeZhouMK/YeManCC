static void unifiedControllerInputCases() {
    namespace library=ymcc::capabilities::controller;
    run("unified/null-source",[]{
        const auto nowSample=inputHostFrameSubmissionSample(nullptr);
        require(!nowSample.live&&!nowSample.routeKnown&&!nowSample.msi,"null source remains unavailable");
    });
    const std::vector<std::string> sources={"xinput","xinput-any-connected","msi-dinput","none","unknown","rog-hid","sdl","XInput","msi-dinput ","xinput-any-connected-other","","virtual-xinput","hc.device.ClawA1M",std::string("xinput\0other",12)};
    for(const auto& source:sources)for(const unsigned mask:{0u,1u,15u})for(const bool live:{false,true}) {
        run("unified/exact-parity/"+source+"/"+std::to_string(mask)+(live?"/live":"/absent"),[&]{
            sample.inputSource=source;sample.connectedMask=mask;sample.live=live;
            const auto before=inputHostFrameSubmissionSampleBeforeUnified(&sample),current=inputHostFrameSubmissionSample(&sample);
            require(before.live==current.live&&before.routeKnown==current.routeKnown&&before.msi==current.msi,"actual adapter retains old live/provider semantics");
            require(before.identity==current.identity&&before.msiRevision==current.msiRevision&&before.msiMode==current.msiMode,"original source identity and MSI mode/revision copied unchanged");
            require(before.window.readAttemptSequence==current.window.readAttemptSequence&&before.window.readStartedTick==current.window.readStartedTick&&before.window.readCompletedTick==current.window.readCompletedTick&&before.window.readPowerGeneration==current.window.readPowerGeneration&&before.window.readTopologyGeneration==current.window.readTopologyGeneration&&before.window.readContextStable==current.window.readContextStable,"entire read-window proof copied unchanged");
            const auto binding=library::bindStandardInput(source,mask);
            require(current.routeKnown==binding.known(),"actual final-frame sample consumes total-library binding");
            const bool xinput=(source=="xinput"||source=="xinput-any-connected")&&mask!=0;
            const bool msi=source=="msi-dinput";
            const std::string_view expected=xinput?"controller.input.xinput.standard":msi?"controller.input.msi-dinput.standard":"";
            require(binding.capabilityId()==expected,"independent exact source-to-total capability assignment");
            require(current.msi==msi,"MSI source never loses its generation/mode fence discriminator");
            if(binding.provider)require(!binding.provider->requiresConnectedMask||mask!=0,"required XInput connected mask is present");
        });
    }
    run("unified/copied-registration-rejected",[]{
        auto copied=library::inputProviders[0];const library::InputBinding invalid{&copied};
        require(!invalid.known()&&invalid.kind()==library::InputKind::None&&invalid.capabilityId().empty(),"matching descriptor copy is not a registered provider");
    });
    run("unified/no-source-cleanup-still-allowed",[]{
        const auto r=submit(nullptr,true,true);
        require(r.accepted&&r.neutral&&pipeWrites==1,"total source binding cannot prevent actual same-target full-neutral return");
    });
}
