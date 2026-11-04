 // Wrong persisted scalar types must not replay a deferred external write.
 const auto receiptBaseline=json{{"pending",true},{"account",42u},{"path",W2U(path)},{"group","77"},{"baseline",145},{"desired",137},{"phase","queued"},{"liveAvailable",false},{"livePid",0u},{"error",""},{"via","deferred"}};
 int damagedCases=0,damagedFailures=0;
 for(const auto* field:{"pending","account","path","group","baseline","desired","phase","liveAvailable","livePid","error","via"}){
  for(const auto& wrong:std::vector<json>{nullptr,json::array(),json::object(),true,42.5,"bad"}){
   if(receiptBaseline[field].is_string()&&wrong.is_string())continue;if(receiptBaseline[field].is_boolean()&&wrong.is_boolean())continue;
   reset();receipts=receiptBaseline;receipts[field]=wrong;const auto original=receipts;json observed;bool threw=false;
   try{observed=steamDeckMouseGet();sdmApplyLocked();}catch(...){threw=true;}
   ++damagedCases;if(threw||observed.value("pending",true)||observed.value("appliedLive",true)||receipts!=original||writes||liveMutations)++damagedFailures;
  }
 }
 std::cout<<"RECEIPT_BEFORE_ASSERT cases="<<damagedCases<<" failures="<<damagedFailures<<"\n";
 expect(damagedFailures==0,"malformed deferred mouse receipts remain inspectable, inactive and byte-preserved without replay");
 reset();receipts=receiptBaseline;receipts["pending"]="bad";liveMode="ok";steamDeckMouseSet({{"percent",137}});sdmApplyLocked();const auto repaired=steamDeckMouseGet();
 expect(repaired.value("ok",false)&&repaired.value("appliedLive",false)&&receipts.value("desired",0)==137&&receipts["pending"].is_boolean(),"explicit valid mouse edit heals a corrupt native receipt with actual readback");
 std::cout<<"STEAMDECK_MOUSE_RECEIPT_STORM cases="<<damagedCases<<" failures="<<damagedFailures<<"\n";
