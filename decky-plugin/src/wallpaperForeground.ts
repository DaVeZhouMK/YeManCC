// One event-only foreground gate per Steam document, shared across route
// components. Route teardown must not forget a window blur or page freeze.
export class WallpaperForeground {
  private focused:boolean;
  private frozen=false;
  private stopped=false;
  private listeners=new Set<(force:boolean)=>void>();
  private view:Window|null;
  constructor(private doc:Document){
    this.view=doc.defaultView;
    this.focused=this.documentFocused();
    doc.addEventListener('visibilitychange',this.visibility);
    doc.addEventListener('freeze',this.suspend);
    doc.addEventListener('resume',this.resume);
    this.view?.addEventListener('blur',this.blur);
    this.view?.addEventListener('focus',this.focus);
    this.view?.addEventListener('pagehide',this.suspend);
    this.view?.addEventListener('pageshow',this.resume);
  }
  private documentFocused(){try{return this.doc.hasFocus();}catch{return false;}}
  allowed(){return !this.stopped&&!this.frozen&&this.focused&&!!this.view&&!this.view.closed&&this.doc.visibilityState!=='hidden'&&this.documentFocused();}
  // CEF may restore the document without another Window focus edge. A real
  // DOM focus event can confirm recovery; synthetic/background navigation cannot.
  revalidateFocus(event:Event){if(event.isTrusted&&!this.focused&&this.documentFocused()&&this.doc.visibilityState!=='hidden'&&!this.frozen){this.focused=true;this.publish();}}
  private publish(force=false){if(!this.stopped)for(const listener of this.listeners)listener(force);}
  private visibility=()=>this.publish();
  private blur=()=>{this.focused=false;this.publish();};
  private focus=()=>{this.focused=true;this.publish();};
  private suspend=()=>{if(this.frozen)return;this.frozen=true;this.publish();};
  private resume=(event:Event)=>{
    if(event.type==='pageshow'&&!(event as PageTransitionEvent).persisted&&!this.frozen)return;
    this.frozen=false;this.publish(true);
  };
  subscribe(listener:(force:boolean)=>void){if(this.stopped)return()=>{};this.listeners.add(listener);return()=>{this.listeners.delete(listener);};}
  stop(){
    if(this.stopped)return;this.stopped=true;this.listeners.clear();
    this.doc.removeEventListener('visibilitychange',this.visibility);
    this.doc.removeEventListener('freeze',this.suspend);
    this.doc.removeEventListener('resume',this.resume);
    this.view?.removeEventListener('blur',this.blur);
    this.view?.removeEventListener('focus',this.focus);
    this.view?.removeEventListener('pagehide',this.suspend);
    this.view?.removeEventListener('pageshow',this.resume);
  }
}
