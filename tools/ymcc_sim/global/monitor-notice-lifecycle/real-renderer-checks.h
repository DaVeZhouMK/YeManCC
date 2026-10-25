// Added only to the private fixture; the product renderer source is unmodified.
static Json rendererEvaluate(Carrier& carrier,const wchar_t* script){
 struct Evaluation{bool done=false;HRESULT result=E_PENDING;std::wstring json;};
 auto evaluation=std::make_shared<Evaluation>();auto* view=CarrierTestAccess::view(carrier);need(view,"Real product renderer view unavailable");
 need(SUCCEEDED(view->ExecuteScript(script,Callback<ICoreWebView2ExecuteScriptCompletedHandler>([evaluation](HRESULT result,LPCWSTR json)->HRESULT{evaluation->result=result;if(json)evaluation->json=json;evaluation->done=true;return S_OK;}).Get())),"Actual renderer script could not be scheduled");
 need(until([&]{return evaluation->done;},5000)&&SUCCEEDED(evaluation->result),"Actual renderer script deadline exceeded");
 return Json::parse(utf8(evaluation->json));
}
static Json rendererInspect(Carrier& carrier){
 return rendererEvaluate(carrier,LR"JS((()=>{const root=document.getElementById('notice'),node=root?.querySelector('.shortcut-notice'),bar=node?.querySelector('[role="progressbar"]'),rect=node?.getBoundingClientRect();return {children:root?.childElementCount??-1,visible:!!node&&getComputedStyle(root).visibility==='visible',title:node?.querySelector('.shortcut-notice-title')?.textContent??'',result:node?.querySelector('.shortcut-notice-result')?.textContent??'',width:rect?.width??0,height:rect?.height??0,value:bar?.getAttribute('aria-valuenow')??'',state:node?.getAttribute('data-feedback-state')??'',theme:document.documentElement.dataset.theme??''};})())JS");
}
static unsigned long long rendererWallNow(){FILETIME value{};GetSystemTimeAsFileTime(&value);return ((((ULONGLONG)value.dwHighDateTime<<32)|value.dwLowDateTime)-116444736000000000ULL)/10000;}
static void actualRendererChecks(Carrier& carrier,const Json& owner,const std::function<void(const char*,bool)>& check){
 const auto id=owner["id"].get<int>();const auto nonce=owner["nonce"].get<std::string>();
 auto frame=[&](unsigned long long seq,unsigned long long lifetime){const auto now=rendererWallNow();return Json{{"type","monitor-notice"},{"protocol",1},{"hudId",id},{"nonce",nonce},{"seq",seq},{"expiresAt",now+lifetime},{"feedback",{{"schemaVersion",1},{"id","private-real-notice-"+std::to_string(seq)},{"actionId","display.brightnessAdjust"},{"origin","controller-shortcut"},{"state","applied"},{"title","Private brightness"},{"message","Read-only renderer lifecycle fixture"},{"occurredAtUnixMs",now},{"expiresAtUnixMs",now+lifetime},{"powerGeneration",1},{"value",{{"text","50%"},{"number",50},{"unit","%"}}},{"presentation",{{"fullscreen",true},{"leftMonitor",true}}}}},{"appearance",{{"opacity",0.8},{"theme","blue-black"}}},{"occupied",Json::array()}};};
 auto empty=[&]{return until([&]{return rendererInspect(carrier).value("children",-1)==0;},2500);};
 auto rendered=[&]{return until([&]{const auto state=rendererInspect(carrier);return state.value("visible",false)&&state.value("title",std::string{})=="Private brightness"&&state.value("value",std::string{})=="50";},2500);};
 check("real product Vue starts mounted without a notice",rendererInspect(carrier).value("children",-1)==0);
 auto first=frame(1,7000);check("production native frame is admitted by exact current renderer owner",carrier.noticeFrame(id,nonce,first));
 check("real product Vue presents native feedback and brightness range",rendered());
 const auto firstState=rendererInspect(carrier);check("real product notice has actual layout and applied value",firstState.value("width",0.0)>0&&firstState.value("height",0.0)>0&&firstState.value("state",std::string{})=="applied"&&firstState.value("result",std::string{})=="50%");
 auto wrong=frame(2,7000);wrong["nonce"]="incorrect-renderer-owner-0000";wrong["feedback"]["title"]="Wrong owner";wrong["feedback"]["id"]="wrong-owner-frame";
 check("production native frame refuses a different nonce",!carrier.noticeFrame(id,nonce,wrong));
 Json raw={{"event","monitorNotice.frame"},{"data",wrong}};need(SUCCEEDED(CarrierTestAccess::view(carrier)->PostWebMessageAsJson(wide(raw.dump()).c_str())),"Wrong-owner renderer probe send failed");until([]{return false;},150);
 check("real product Vue independently ignores a wrong owner frame",rendererInspect(carrier).value("title",std::string{})=="Private brightness");
 auto stale=first;stale["feedback"]["title"]="Stale frame";stale["feedback"]["id"]="stale-frame";raw["data"]=stale;need(SUCCEEDED(CarrierTestAccess::view(carrier)->PostWebMessageAsJson(wide(raw.dump()).c_str())),"Old-sequence renderer probe send failed");until([]{return false;},150);
 check("real product Vue independently ignores an old sequence",rendererInspect(carrier).value("title",std::string{})=="Private brightness");
 check("production explicit hide accepted for current owner",carrier.noticeHide(id,nonce,1));check("real product Vue clears the hidden notice",empty());
 auto second=frame(2,1400);check("production admits next feedback after explicit hide",carrier.noticeFrame(id,nonce,second));check("real product Vue presents the next feedback",rendered());
 check("real product Vue expires feedback and removes DOM",empty());
 auto third=frame(3,7000);check("production admits feedback after expiry",carrier.noticeFrame(id,nonce,third));check("real product Vue recovers presentation after expiry",rendered());
 const auto disposed=rendererEvaluate(carrier,LR"JS((()=>{window.dispatchEvent(new Event('pagehide'));return {children:document.getElementById('notice').childElementCount};})())JS");
 check("actual product pagehide disposes subscriptions and unmounts Vue",disposed.value("children",-1)==0);
 auto fourth=frame(4,7000);check("native remains alive for post-unmount late-frame probe",carrier.noticeFrame(id,nonce,fourth));until([]{return false;},150);
 check("disposed actual renderer ignores a later native frame",rendererInspect(carrier).value("children",-1)==0);
}
