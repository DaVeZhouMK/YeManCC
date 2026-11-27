import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';

const reviewedHash=text=>createHash('sha256').update(text.replace(/\r\n/g,'\n')).digest('hex');

export function projectExitReceiver(mainText){
 const text=mainText.replace(/\r\n/g,'\n');
 const definition=anchor=>{
  const start=text.indexOf(anchor),last=text.indexOf('\n}',start);
  assert(start>=0&&start===text.lastIndexOf(anchor)&&last>start,`EXPECTED_UNIQUE_EXIT_RECEIVER_DEFINITION ${anchor}`);
  return text.slice(start,last+2);
 };
 const original=definition('static void ipc_dispatch(ICoreWebView2* sourceView, LPCWSTR raw) {');
 const omissions=[];
 let receiver=original;
 const omit=(label,start,end,expected,replacement)=>{
  const a=original.indexOf(start),b=original.indexOf(end,a+start.length);
  assert(a>=0&&a===original.lastIndexOf(start)&&b>a&&b===original.lastIndexOf(end),`EXIT_RECEIVER_OMISSION_BOUNDARY ${label}`);
  const omitted=original.slice(a,b),sha256=reviewedHash(omitted);
  // A changed omission may intercept app.exit and must be reviewed again.
  assert([expected].flat().includes(sha256),`REVIEW_CHANGED_OMITTED_EXIT_RECEIVER_BRANCH ${label}: ${sha256}`);
  omissions.push({label,sha256});
  receiver=receiver.replace(omitted,()=>replacement);
 };
 omit('unrelated renderer events','        if (req.value("event", std::string{}) == "gyro.telemetry.interest") {',
  '        const auto generation = fromChild ? 0 :','a285835ff5b91d7bd886b0a56408b985414d6a22b80c8ac25471f3480db6fb24','');
 omit('unrelated command preparation','        // ── 异步路径：白名单命令 offload 到 worker',
  '        if (ipc_cmd_async(cmd)) {',[
   'ad7a323a076181cc3deaa7035f2b42b07905a533601e5dce30cbfc64771b35a7',
   // Adds power binding and a deadline for game.suspend, not app.exit.
   'd65d057f69765731d268658476aafc21e0d70c5db2739ff5a954d30b5a316bb9',
  ],'');
 omit('async worker body','            IpcFn fn = it->second;',
  '        // ── 同步路径（原行为）──','7e55ec20ae5169575b0cae3d09ea8b1a65be3bc43428d12022718c1cd8b74b3e',
  '            (void)generation;\n            unexpectedAsyncDispatch = true;\n            return;\n        }\n\n');
 return {text:[definition('static std::string W2U(const wchar_t* w, int len = -1) {'),
  definition('static bool ipc_cmd_async(const std::string& cmd) {'),receiver].join('\n\n')+'\n',omissions};
}

export function projectNoticeRootExit(mainText,{dispatch=false,intent=false}={}){
 assert(!intent||dispatch,'EXIT_INTENT_REQUIRES_ROOT_DISPATCH');
 const entry='    case WM_DESTROY: {',end='appendNativeLifecycleLog("window-destroy"',call='g_monitorNoticeCarrier.shutdown();';
 const begin=mainText.indexOf(entry),last=mainText.indexOf(end,begin),firstCall=mainText.indexOf(call,begin);
 assert(begin>=0&&begin===mainText.lastIndexOf(entry)&&last>begin&&firstCall>begin&&firstCall<last&&firstCall===mainText.lastIndexOf(call),'EXPECTED_ROOT_DESTROY_NOTICE_OWNER');
 const result={call,line:mainText.slice(0,firstCall).split('\n').length};
 if(!dispatch)return result;
 const startProc=mainText.indexOf('static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {');
 const endCase=mainText.indexOf('    case WM_HOTKEY:',last);
 assert(startProc>=0&&startProc<begin&&endCase>last,'REVIEWED_ROOT_WNDPROC_REQUIRED');
 const rawCase=mainText.slice(begin,endCase).replace(/\r?\n$/,'');
 const reviewedSha=reviewedHash(rawCase);
 assert.equal(reviewedSha,'ea58ed3084d61ac0da36830671703cfed7a54d3942fbcbcb4e110f9ff7014673','REVIEW_CHANGED_ROOT_DESTROY_BRANCH_BEFORE_EXECUTION');
 assert.equal(rawCase.split(call).length,2);
 Object.assign(result,{destroy:rawCase+'\n',omittedDestroy:rawCase.replace(call,'(void)g_monitorNoticeCarrier;')+'\n',reviewedSha});
 if(!intent)return result;
 const defs=mainText.match(/^#define (?:WM_APP_EXIT|WM_APP_EXIT_READY) +\(WM_USER \+ [67]\) .*$/gm);
 assert.deepEqual(defs?.map(line=>line.match(/^#define (\w+) +\(WM_USER \+ (\d+)\)/).slice(1)),[['WM_APP_EXIT','6'],['WM_APP_EXIT_READY','7']]);
 const parts=[
  ['WM_APP_EXIT','WM_RECOVERY_WEBVIEW_PING','201d5327dcbc4c86c19ed7d83d13cf3c8bd5208e24b8142c43920d129312faa2'],
  ['WM_APP_EXIT_READY','WM_POWER_RESUME_READY','50b81c491573503db9d35fbdbfa7a08420455fbd70ada6a9642a36f4b9ef31f1'],
  ['WM_CLOSE','kMsiNativeForegroundObserverMessage','532c28049d69bc013f366d5f02326173d61111dd0ebb60357fb499619bca1505'],
 ];
 const cases=parts.map(([name,next,expected])=>{
  const offset=mainText.lastIndexOf(`    case ${name}:`,begin),limit=mainText.indexOf(`    case ${next}:`,offset);
  assert(startProc<offset&&offset<limit&&limit<begin,`PROJECTED_ROOT_EXIT_CASE_RANGE ${name}`);
  const text=mainText.slice(offset,limit).replace(/\r?\n$/,'');
  assert.equal(reviewedHash(text),expected,`REVIEW_CHANGED_ROOT_EXIT_BRANCH_BEFORE_EXECUTION ${name}`);
  return text;
 });
 const ipcMarker='    ipc_on("app.exit",',ipcStart=mainText.indexOf(ipcMarker),ipcEnd=mainText.indexOf('    ipc_on("app.dataDir",',ipcStart);
 assert(ipcStart>=0&&ipcStart===mainText.lastIndexOf(ipcMarker)&&ipcStart<ipcEnd&&ipcEnd<startProc,'EXPECTED_UNIQUE_APP_EXIT_HANDLER');
 const ipc=mainText.slice(ipcStart,ipcEnd),reviewedIpcSha=reviewedHash(ipc);
 assert.equal(reviewedIpcSha,'efb52435ef5d0103a8101c1461c7475a1d3495553fe3fd3c96c9054f124f2ebc','REVIEW_CHANGED_APP_EXIT_HANDLER_BEFORE_EXECUTION');
 const receiver=projectExitReceiver(mainText);
 const registrations=[...mainText.matchAll(/^    g_view->add_WebMessageReceived\(\r?\n        Callback<ICoreWebView2WebMessageReceivedEventHandler>\(\r?\n(        \[\]\(ICoreWebView2\* sender, ICoreWebView2WebMessageReceivedEventArgs\* a\) -> HRESULT \{[\s\S]*?^        \})\)\.Get\(\), nullptr\);/gm)];
 assert.equal(registrations.length,1,'EXPECTED_UNIQUE_MAIN_WEBVIEW_MESSAGE_CALLBACK');
 const [registration,callback]=registrations[0];
 return Object.assign(result,{intentCases:[...defs,...cases].join('\n')+'\n',ipc,reviewedIpcSha,receiver,
  callback:'static const auto mainWebMessageReceived =\n'+callback+';\n',
  callbackOffset:registrations[0].index+registration.indexOf(callback),
  registration,registrationOffset:registrations[0].index});
}
