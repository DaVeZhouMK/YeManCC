static int readPublishedPresenceOverride=-999;
static std::wstring mockRoot=L"S:\\Steam";
static DWORD mockAccount=42;
static bool accountReadable=true,offlineFound=true;
static unsigned presenceReads=0,liveReads=0,registryReads=0;
static std::function<void()> readSourceHook;
static bool noPublicSnapshot=false,changedEpoch=false;
static std::vector<int> presenceAnswers{1};
static std::wstring sofRegSZ(int,const wchar_t*,const wchar_t*){++registryReads;return mockRoot;}
static bool sofRegDWORD(int,const wchar_t*,const wchar_t* name,DWORD* out){++registryReads;if(std::wstring(name)==L"pid")reconcilePostGetCheckpoint();applyPostSetCheckpoint();*out=std::wstring(name)==L"pid"?reconcileRegistryPid:mockAccount;return std::wstring(name)==L"pid"||accountReadable;}
static bool sofFindLocalConfig(std::wstring* path,std::string* account){*path=L"S:\\Steam\\localconfig.vdf";*account="42";return offlineFound;}
static bool sofFindLocalConfigAt(const std::wstring&,DWORD,std::wstring* p,std::string* a){return sofFindLocalConfig(p,a);}
// Only the process-inventory transport is controlled. The production presence
// function itself is extracted unchanged, including maxAge=0 and unknown guards.
namespace ymcc::processInventory {
enum class Status{Complete,Incomplete};
struct Row{std::wstring name;};
struct Table{Status status=Status::Complete;std::vector<Row> rows;};
static int resolveQuery(){return 0;}
static int currentTime=0;
static std::shared_ptr<Table> read(int,int,unsigned maxAge){
 if(maxAge!=0)throw std::runtime_error("unexpected-cached-presence");
 const auto answer=presenceAnswers.at(std::min<std::size_t>(presenceReads++,presenceAnswers.size()-1));
 if(answer==-3)throw std::runtime_error("controlled-inventory-failure");
 if(answer==-2)return {};
 auto result=std::make_shared<Table>();
 if(answer==-1)result->status=Status::Incomplete;
 if(answer==1)result->rows.push_back({L"steam"});
 return result;
}
}
static std::wstring sgBaseName(const std::wstring& name){return name;}
//__PRODUCTION_PRESENCE__
