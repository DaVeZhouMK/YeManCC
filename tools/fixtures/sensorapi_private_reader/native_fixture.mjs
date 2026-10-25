// Keep the established typed SDK fixture, isolated from real Windows names.
export function typedNativeFixture(source){
 const once=(from,to)=>{if(source.split(from).length!==2)throw Error('Typed Windows fixture seam: '+from);source=source.replace(from,()=>to);};
 once('using json=nlohmann::json;using DWORD=std::uint32_t;','using json=nlohmann::json;');
 once('static constexpr DWORD WAIT_FAILED=0xffffffff,QS_ALLINPUT=0,MWMO_INPUTAVAILABLE=0,PM_REMOVE=0;','');
 once('static constexpr std::int64_t E_FAIL=-2147467259LL;','');
 const shim='namespace ymcc::sensors {template<class Operation,class Cancel>auto getStaResults(Operation value,std::uint64_t deadline,Cancel cancelled){if(cancelled()||GetTickCount64()>=deadline)throw winrt::hresult_error(-84);return value.GetResults();}}';
 once(shim,'template<class Operation,class Cancel>static auto getStaResults(Operation value,std::uint64_t deadline,Cancel cancelled){if(cancelled()||GetTickCount64()>=deadline)throw winrt::hresult_error(-84);return value.GetResults();}');
 once('int main(int argc,char**argv)','static int fixtureMain(int argc,char**argv)');
 const includes=[...source.matchAll(/^#include[^\r\n]+/gm)].map(m=>m[0]);source=source.replace(/^#include[^\r\n]+/gm,'');
 return '#define _WIN32_WINNT 0x0A00\n#define NOMINMAX\n#include <initguid.h>\n#include "sensorapi_private_reader.h"\n'+includes.join('\n')+'\nnamespace typed_fixture {\n'+source+'\n}\nint main(int argc,char**argv){return typed_fixture::fixtureMain(argc,argv);}\n';
}