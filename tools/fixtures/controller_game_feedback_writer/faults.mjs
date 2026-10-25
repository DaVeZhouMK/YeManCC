export const SOURCE_FAULTS=Object.freeze([
 {id:'detach-motor-from-admitted-frame',kind:'source',before:'frame->gameMotorReply=sample;frame->gameMotorBinding=sample.generation;',after:'frame->gameMotorBinding=sample.generation; /* private source fault: omit same frame motor attachment */',expected:'COMBINED_ACTUAL_SERIAL_WRITER_APPLIED'},
 {id:'writer-borrows-current-token',kind:'source',before:'return nativePhysicalFeedbackStart(0,500,false,&command.source,&motors,&command);',after:'const auto borrowed=nativePhysicalFeedbackReady();return nativePhysicalFeedbackStart(0,500,false,&borrowed,&motors,&command);',expected:'COMBINED_OLD_SOURCE_CANNOT_WRITE_REPLACEMENT'},
 {id:'skip-final-MSI-authority',kind:'source',before:'(!gameCommand||nativeGameFeedbackCommandCurrent(*gameCommand))&&',after:'true&&',expected:'COMBINED_REVOKE_PREVENTS_MSI_WRITEFILE'},
 {id:'discard-explicit-zero',kind:'header',before:'if(zero){retire();return true;}',after:'if(zero){return false;}',expected:'COMBINED_EXPLICIT_ZERO_IS_NOT_DEFERRED_TO_SOURCE_EXPIRY'},
 {id:'ignore-target-revoke',kind:'source',before:'if(!g_gamepadSerialGameInterested.load(std::memory_order_acquire))return;',after:'if(true)return; /* private source fault: ignore target revocation */',expected:'COMBINED_TARGET_REVOKE_STOPS_BEFORE_EXPIRY'}
]);
