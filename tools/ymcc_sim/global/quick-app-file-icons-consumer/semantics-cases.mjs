import assert from 'node:assert/strict';
import vm from 'node:vm';

// These are assertion goldens, never replacements for the compiled factory.
const GOLDEN_APPS = [
  {name:'此电脑',path:'C:\\Windows\\explorer.exe'},
  {name:'edge浏览器',path:'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe'},
  {name:'任务管理器',path:'C:\\Windows\\System32\\Taskmgr.exe'},
  {name:'关屏下载',path:'C:\\SOFT\\YeMan\\PowerControl\\关闭屏幕切为节能.vbs'},
  {name:'复古键盘',path:'C:\\Windows\\system32\\osk.exe'},
];
const KINDS = ['computer','browser','task','download','keyboard'];
const VARIANTS = ['b','b','a','a','a'];
const plain = value => JSON.parse(JSON.stringify(value));
const scripts = Object.fromEntries(Object.entries({
  defaults:'module.exports.createDefaultQuickApps()',
  identity:'module.exports.isDefaultQuickAppPath(__args[0])',
  kind:'module.exports.quickAppIconKind(__args[0])',
  variant:'module.exports.quickAppIconVariant(__args[0])',
  previews:'module.exports.QUICK_APP_ICON_PREVIEWS',
  freshness:`(() => {
    const a=module.exports.createDefaultQuickApps(), b=module.exports.createDefaultQuickApps();
    const distinct=a!==b && a.apps!==b.apps && a.apps.every((app,i)=>app!==b.apps[i]);
    a.apps[0].name='owned mutation'; a.apps[0].path='Z:/owned/changed.exe'; a.apps.pop();
    return {distinct,untouched:b,after:module.exports.createDefaultQuickApps()};
  })()`,
}).map(([name,code])=>[name,new vm.Script(code,{filename:'owned-semantics-call-'+name+'.js'})]));

export function fixture(compiled, observation) {
  const requests=[];
  const create = require => {
    const module={exports:{}};
    const context=vm.createContext({module,exports:module.exports,require},{codeGeneration:{strings:false,wasm:false}});
    const globals=new vm.Script('[typeof process,typeof fetch,typeof window,typeof document,typeof Buffer,typeof setTimeout]').runInContext(context,{timeout:1000});
    assert.deepEqual(plain(globals),Array(6).fill('undefined'),'SEMANTICS_AMBIENT_GLOBAL_REFUSED');
    return {module,context};
  };
  const defaults=create(name=>{requests.push({from:'quickAppDefaults',name});throw Error('UNKNOWN_DEFAULTS_MODULE_REQUEST:'+name);});
  new vm.Script(compiled.defaults,{filename:'actual-quickAppDefaults.cjs'}).runInContext(defaults.context,{timeout:1000});
  observation.vmRuns++;
  assert.equal(typeof defaults.module.exports.createDefaultQuickApps,'function','ACTUAL_DEFAULTS_EXPORT_REQUIRED');
  const icons=create(name=>{
    requests.push({from:'quickAppIcons',name});
    assert.equal(name,'./quickAppDefaults','UNKNOWN_SEMANTICS_MODULE_REQUEST');
    return defaults.module.exports;
  });
  new vm.Script(compiled.icons,{filename:'actual-quickAppIcons.cjs'}).runInContext(icons.context,{timeout:1000});
  observation.vmRuns++;
  for(const name of ['isDefaultQuickAppPath','quickAppIconKind','quickAppIconVariant'])assert.equal(typeof icons.module.exports[name],'function','ACTUAL_ICONS_EXPORT_REQUIRED');
  assert.deepEqual(requests,[{from:'quickAppIcons',name:'./quickAppDefaults'}],'EXACT_REAL_MODULE_DEPENDENCY_REQUIRED');
  observation.moduleGraphs.push(plain(requests));
  const calls=[];
  const call=(method,...args)=>{
    assert(Object.hasOwn(scripts,method),'UNKNOWN_SEMANTICS_METHOD');
    const target=method==='defaults'||method==='freshness'?defaults:icons;
    target.context.__args=args;
    try {
      const result=plain(scripts[method].runInContext(target.context,{timeout:1000}));
      calls.push({method,args:plain(args),result});
      return result;
    } finally { delete target.context.__args; }
  };
  return {call,calls,requests};
}

const cases = {
  'defaults-five-baseline-and-freshness'(f) {
    assert.deepEqual(f.call('defaults'),{apps:GOLDEN_APPS},'SEMANTICS_REAL_DEFAULT_BASELINE');
    const fresh=f.call('freshness');
    assert.equal(fresh.distinct,true,'SEMANTICS_DEFAULT_FACTORY_MUST_BE_FRESH');
    assert.deepEqual(fresh.untouched,{apps:GOLDEN_APPS});
    assert.deepEqual(fresh.after,{apps:GOLDEN_APPS});
    for(const app of GOLDEN_APPS)assert.equal(f.call('identity',app.path),true);
    assert.equal(f.call('identity','Z:/owned/changed.exe'),false);
  },
  'default-path-case-slashes-and-outer-whitespace'(f) {
    for(const app of f.call('defaults').apps) {
      // The forward-slash probe is deliberately first: the exact slash mutant
      // must fail this assertion after both real modules have initialized.
      const probes=[app.path.replaceAll('\\','/'),app.path.toUpperCase(),app.path.toLowerCase(),
        ' \t'+app.path+'\r\n',app.path.replace(/\\/g,'/\\/'),
        '  '+app.path.toUpperCase().replaceAll('\\','//')+'  '];
      for(const value of probes)assert.equal(f.call('identity',value),true,'SEMANTICS_DEFAULT_PATH_NORMALIZATION');
    }
  },
  'internal-spaces-and-exact-path-boundaries'(f) {
    const apps=f.call('defaults').apps, edge=apps[1].path;
    assert.equal(f.call('identity',edge),true);
    for(const value of [edge.replace('Program Files','ProgramFiles'),edge.replace('Program Files','Program  Files'),
      ...apps.flatMap(a=>[a.path+'.bak',a.path+'/',a.path.replace('C:','D:'),a.path.replace('C:\\','C:\\owned\\..\\')]),
      '', '   ', 'explorer.exe', 'C:/Windows/Explorer.exe.other']) {
      assert.equal(f.call('identity',value),false,'SEMANTICS_NO_EXTRA_PATH_CANONICALIZATION');
    }
  },
  'renamed-defaults-keep-path-kind-and-variant'(f) {
    f.call('defaults').apps.forEach((app,i)=>{
      const renamed={name:'用户改名 '+i,path:app.path};
      assert.equal(f.call('identity',renamed.path),true,'SEMANTICS_RENAME_KEEPS_DEFAULT_PATH');
      assert.equal(f.call('kind',renamed),KINDS[i],'SEMANTICS_RENAME_KEEPS_PATH_KIND');
      assert.equal(f.call('variant',renamed),VARIANTS[i]);
    });
  },
  'custom-paths-with-default-names-stay-custom'(f) {
    f.call('defaults').apps.forEach((app,i)=>{
      const custom={name:app.name,path:'Z:/owned/custom-'+i+'.exe'};
      assert.equal(f.call('identity',custom.path),false,'SEMANTICS_CUSTOM_NAME_CANNOT_IMPERSONATE_DEFAULT');
      // Name-based fallback marks are intentional; they do not grant default identity.
      assert.equal(f.call('kind',custom),KINDS[i]);
      assert.equal(f.call('variant',custom),VARIANTS[i]);
    });
  },
  'computer-browser-select-B'(f) {
    for(const app of [
      {name:'此电脑',path:'Z:/owned/custom.exe'}, {name:' EDGE preview ',path:''},
      {name:'renamed',path:' c:/WINDOWS/EXPLORER.EXE '}, {path:'Z:/owned/MSEDGE.EXE'},
    ])assert.equal(f.call('variant',app),'b','SEMANTICS_COMPUTER_BROWSER_VARIANT_B');
  },
  'other-kinds-select-A'(f) {
    for(const app of [
      {name:'任务管理器'},{name:'关屏下载'},{name:'复古键盘'},{name:'普通工具'},
      {path:'Z:/owned/TASKMGR.EXE'},{path:'Z:/owned/关闭屏幕.vbs'},{path:'Z:/owned/OSK.EXE'},
    ])assert.equal(f.call('variant',app),'a','SEMANTICS_OTHER_VARIANT_A');
  },
  'fallback-kind-rules-and-precedence'(f) {
    const probes=[
      [{path:'C:/WINDOWS/EXPLORER.EXE'},'computer'],[{name:'owned EDGE copy'},'browser'],
      [{path:'Z:/owned/msedge.exe'},'browser'],[{path:'Z:/owned/taskmgr.exe'},'task'],
      [{name:'关屏'},'download'],[{path:'Z:/owned/关闭屏幕-copy.vbs'},'download'],
      [{path:'Z:/owned/osk.exe'},'keyboard'],[{path:'Z:/owned/custom.exe'},'generic'],
      [{name:'此电脑 edge 任务管理器',path:'Z:/owned/osk.exe'},'computer'],
      [{name:'任务管理器',path:'Z:/owned/msedge.exe'},'browser'],
      [{name:'复古键盘',path:'Z:/owned/taskmgr.exe'},'task'],
      [{name:'关屏',path:'Z:/owned/osk.exe'},'download'],
    ];
    for(const [app,expected] of probes)assert.equal(f.call('kind',app),expected,'SEMANTICS_FIXED_KIND_RULES');
  },
  'unknown-inputs-use-generic-fallback'(f) {
    for(const app of [{},{name:null,path:null},{name:false,path:0},{name:123,path:456},
      {name:' \t',path:' '},{path:'msedge.exe'},{path:'Z:/owned/Explorer.exe'},{path:'Z:/owned/osk.exe.bak'}]) {
      assert.equal(f.call('kind',app),'generic','SEMANTICS_UNKNOWN_KIND_FALLBACK');
      assert.equal(f.call('variant',app),'a');
    }
    for(const value of [null,false,0,123,{},[]])assert.equal(f.call('identity',value),false);
  },
  'preview-table-fixed-intent'(f) {
    assert.deepEqual(f.call('previews'),[
      {kind:'computer',label:'此电脑',appName:'此电脑',variants:['a','b']},
      {kind:'browser',label:'Edge 浏览器',appName:'edge浏览器',variants:['a','b']},
      {kind:'task',label:'任务管理器',appName:'任务管理器',variants:['a','b']},
      {kind:'download',label:'关屏下载',appName:'关屏下载',variants:['a','b']},
      {kind:'keyboard',label:'复古键盘',appName:'复古键盘',variants:['a','b']},
      {kind:'generic',label:'读取失败时的备用图标',appName:'备用图标',variants:['a','b']},
    ],'SEMANTICS_FIXED_PREVIEW_TABLE');
  },
};

export function runSemanticsCases(compiled,names,observation) {
  assert(Array.isArray(names)&&names.every(name=>Object.hasOwn(cases,name)),'UNKNOWN_CASE');
  const results=[];
  for(const name of names) {
    let f, startupSucceeded=false;
    try {
      f=fixture(compiled,observation); startupSucceeded=true;
      cases[name](f);
      results.push({name,status:'PASS',startupSucceeded,calls:f.calls,moduleRequests:f.requests});
    } catch(error) {
      results.push({name,status:'FAIL',startupSucceeded,assertionName:error.name,error:error.message,
        calls:f?.calls??[],moduleRequests:f?.requests??[]});
    }
  }
  return results;
}
