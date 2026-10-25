export function composeSdkPrivateOwner(cpp,transport,cases){
 const once=(from,to)=>{if(cpp.split(from).length!==2)throw Error('SDK full-owner seam: '+from.slice(0,110));cpp=cpp.replace(from,()=>to);};
 once('static ULONGLONG GetTickCount64(){return fixtureTick;}','static ULONGLONG GetTickCount64(){return fixtureTick=::GetTickCount64();}');
 once('static auto GetTickCount64(){return fakeNow;}','static auto GetTickCount64(){return fakeNow=::GetTickCount64();}');
 once('fakeNow+=delay;++pumps;','::Sleep((std::min)(delay,DWORD{8}));fakeNow=::GetTickCount64();++pumps;');
 const main=cpp.indexOf('int main(int argc,char**argv)'),tail=cpp.indexOf(' unsigned sequence=0;',main);if(main<0||tail<main)throw Error('Retained oracle/main setup boundary');
 cpp=cpp.slice(0,main)+'namespace sdk_owner_transport {\n'+transport+'\n}\n'+cpp.slice(main,tail)+cases;
 return cpp;
}