// One event-only foreground gate per Steam document, shared across route
// components. Route teardown must not forget a window blur or page freeze.
export class WallpaperForeground {
  private focused:boolean;
  private frozen=false;
  private nativeCurrent=false;
  private screensaver=false;
  private navigationRoot:Element|null=null;
  private navigationObserver:MutationObserver|null=null;
  private stopped=false;
  private listeners=new Set<(force:boolean)=>void>();
  private cleanups=new Set<()=>void>();
  private view:Window|null;
  constructor(private doc:Document,private readonly onRetired?:()=>void){
    this.view=doc.defaultView;
    this.focused=this.documentFocused();
    doc.addEventListener('visibilitychange',this.visibility);
    doc.addEventListener('freeze',this.suspend);
    doc.addEventListener('resume',this.resume);
    this.view?.addEventListener('blur',this.blur);
    this.view?.addEventListener('focus',this.focus);
    this.view?.addEventListener('pagehide',this.pagehide);
    this.view?.addEventListener('pageshow',this.resume);
  }
  private documentFocused(){try{return this.doc.hasFocus();}catch{return false;}}
  localVeto(){
    if(this.stopped)return 'retired';
    if(this.frozen)return 'document-suspended';
    if(this.screensaver)return 'screensaver';
    if(!this.view||this.view.closed)return 'window-unavailable';
    if(this.doc.visibilityState==='hidden')return 'document-hidden';
    if(!this.focused)return 'window-blurred';
    if(!this.documentFocused())return 'document-unfocused';
    return null;
  }
  localAllowed(){return this.localVeto()===null;}
  get nativeAdmitted(){return this.nativeCurrent;}
  allowed(){return this.nativeCurrent&&this.localAllowed();}
  bindNavigation(route:Element|null){
    const root=route?.closest('#MainNavMenu-Rest');
    if(this.stopped||!root||root.ownerDocument!==this.doc||root===this.navigationRoot)return;
    this.navigationObserver?.disconnect();this.navigationObserver=null;this.navigationRoot=root;
    // Steam mounts either native saver as a direct sibling of the gamepad UI.
    // CEF can retain document focus while that full-screen view covers the route.
    const sync=()=>{
      if(this.stopped||this.navigationRoot!==root)return;
      let covered=false;
      for(let child=root.firstElementChild;child;child=child.nextElementSibling){
        if((child.id==='ScreensaverPopup'||child.id==='ScreensaverBrowser')&&child.hasAttribute('data-react-nav-root')){covered=true;break;}
      }
      if(this.screensaver===covered)return;
      this.screensaver=covered;this.nativeCurrent=false;this.publish();
    };
    const Observer=(this.view as (Window&typeof globalThis)|null)?.MutationObserver;
    if(typeof Observer==='function'){
      this.navigationObserver=new Observer(sync);
      this.navigationObserver.observe(root,{childList:true});
    }
    sync();
  }
  // The authenticated host alone supplies current native Big Picture admission.
  // Local focus can request proof, but cannot grant playback by itself.
  confirmNative(current:boolean){const admitted=current&&!this.screensaver;if(this.stopped||this.nativeCurrent===admitted)return;this.nativeCurrent=admitted;this.publish();}
  // CEF may restore the document without another Window focus edge. A real
  // DOM focus event can confirm recovery; synthetic/background navigation cannot.
  revalidateFocus(event:Event){if(event.isTrusted&&!this.focused&&this.documentFocused()&&this.doc.visibilityState!=='hidden'&&!this.frozen){this.focused=true;this.publish();}}
  private publish(force=false){if(!this.stopped)for(const listener of this.listeners)listener(force);}
  private visibility=()=>{if(this.doc.visibilityState==='hidden')this.nativeCurrent=false;this.publish();};
  private blur=()=>{this.focused=false;this.nativeCurrent=false;this.publish();};
  private focus=()=>{this.focused=true;this.publish();};
  private suspend=()=>{if(this.frozen)return;this.frozen=true;this.nativeCurrent=false;this.publish();};
  private pagehide=(event:PageTransitionEvent)=>{
    try{this.suspend();}finally{
      if(event.isTrusted&&event.persisted===false){
        try{this.stop();}finally{this.onRetired?.();}
      }
    }
  };
  private resume=(event:Event)=>{
    if(event.type==='pageshow'&&!(event as PageTransitionEvent).persisted&&!this.frozen)return;
    this.frozen=false;this.nativeCurrent=false;this.publish(true);
  };
  subscribe(listener:(force:boolean)=>void){if(this.stopped)return()=>{};this.listeners.add(listener);return()=>{this.listeners.delete(listener);};}
  get retired(){return this.stopped;}
  // A terminal document replacement may precede React unmount. Both routes
  // release the same effect exactly once; temporary suspension does neither.
  own(cleanup:(retiring:boolean)=>void){
    if(this.stopped){cleanup(true);return()=>{};}
    const retire=()=>{if(this.cleanups.delete(retire))cleanup(true);};
    this.cleanups.add(retire);
    return()=>{if(this.cleanups.delete(retire))cleanup(false);};
  }
  stop(){
    if(this.stopped)return;this.stopped=true;this.listeners.clear();
    this.navigationObserver?.disconnect();this.navigationObserver=null;this.navigationRoot=null;
    this.doc.removeEventListener('visibilitychange',this.visibility);
    this.doc.removeEventListener('freeze',this.suspend);
    this.doc.removeEventListener('resume',this.resume);
    this.view?.removeEventListener('blur',this.blur);
    this.view?.removeEventListener('focus',this.focus);
    this.view?.removeEventListener('pagehide',this.pagehide);
    this.view?.removeEventListener('pageshow',this.resume);
    let failed=false,firstError:unknown;
    for(const cleanup of this.cleanups){try{cleanup();}catch(error){if(!failed){failed=true;firstError=error;}}}
    if(failed)throw firstError;
  }
}
