static bool observing=true;
static bool gate=false;
static unsigned gateMask=0;
static bool scriptedGate=false;
static unsigned gateCalls=0;
static unsigned conversions=0,deliveryDumps=0,postCalls=0;
static int throwAtPost=-1;
static int failConversionAt=-1;
static unsigned conversionApiCalls=0;
static bool partialConversionFailure=false;
static std::vector<unsigned> failConversionCalls;
static uint64_t sink=0;
static std::function<void(int)> postHook;
static std::vector<int> resolutions;
static std::vector<std::pair<int,std::wstring>> posts;
static std::vector<std::string> scopeEvents,feedbackEvents,diagnosticEvents;
static std::atomic<unsigned long long> g_c17aOwnedEvents{0},g_c17aParityChecks{0},g_c17aParityFailures{0};
static bool tickCostEnabled(){const bool value=scriptedGate ? ((gateMask>>(gateCalls%4))&1u)!=0 : gate;++gateCalls;return value;}
static unsigned long long currentPowerGeneration(){return 17;}
static void nativeShortcutFeedbackFromResult(const std::string& ev,const json&){if(observing)feedbackEvents.push_back(ev);}
static void appendNativeLifecycleLog(const char* ev,const json&){if(observing)diagnosticEvents.emplace_back(ev);}
struct UiCostIpcScope{explicit UiCostIpcScope(const char* ev){if(observing)scopeEvents.emplace_back(ev);}};
[[maybe_unused]] static std::string delivery_dump(const json& j){if(observing)++deliveryDumps;return j.dump();}
[[maybe_unused]] static int observedConvert(UINT cp,DWORD flags,LPCCH bytes,int length,LPWSTR wide,int capacity){
 ++conversionApiCalls;
 if(static_cast<int>(conversionApiCalls)==failConversionAt||std::find(failConversionCalls.begin(),failConversionCalls.end(),conversionApiCalls)!=failConversionCalls.end()){
  SetLastError(ERROR_NO_UNICODE_TRANSLATION);
  if(partialConversionFailure&&wide&&capacity>1){std::fill_n(wide,capacity,L'\0');wide[0]=L'P';return capacity-1;}
  return 0;
 }
 return ::MultiByteToWideChar(cp,flags,bytes,length,wide,capacity);
}
struct ICoreWebView2{
 int id;HRESULT result=S_OK;
 HRESULT PostWebMessageAsJson(const wchar_t* wire){
  ++postCalls;if(static_cast<int>(postCalls)==throwAtPost)throw std::runtime_error("post-boundary");
  const auto n=wcslen(wire);sink+=static_cast<uint64_t>(n)+(n?static_cast<unsigned>(wire[n-1]):0);
  if(observing){posts.emplace_back(id,wire);if(postHook)postHook(id);}
  return result;
 }
};
template<typename T>using ComPtr=std::shared_ptr<T>;
struct ChildWindow{ComPtr<ICoreWebView2> view;};
static ComPtr<ICoreWebView2> g_view;
static std::unordered_map<int,ChildWindow*> g_children;
static std::array<ChildWindow,5> childStorage;
static void reset(int targets,bool logOn,bool deadOnly=false){
 g_view.reset();g_children.clear();for(auto& c:childStorage)c.view.reset();
 if(targets>0)g_view=std::make_shared<ICoreWebView2>(ICoreWebView2{0});
 for(int i=1;i<targets;++i){childStorage[static_cast<size_t>(i-1)].view=std::make_shared<ICoreWebView2>(ICoreWebView2{i});g_children.emplace(i,&childStorage[static_cast<size_t>(i-1)]);}
 if(deadOnly){g_children.emplace(9,&childStorage[4]);g_children.emplace(10,nullptr);}
 observing=true;gate=logOn;scriptedGate=false;gateMask=0;gateCalls=0;conversions=0;deliveryDumps=0;postCalls=0;throwAtPost=-1;failConversionAt=-1;conversionApiCalls=0;partialConversionFailure=false;failConversionCalls.clear();
 postHook=nullptr;resolutions.clear();posts.clear();scopeEvents.clear();feedbackEvents.clear();diagnosticEvents.clear();
 g_c17aOwnedEvents=0;g_c17aParityChecks=0;g_c17aParityFailures=0;
}