#pragma once
// The monitor discards its display controller on close. The main UI reuses
// its controller, so restore only our temporary child flags before returning it.
#include "monitor_hud_input_policy.h"
#include <array>
namespace ymcc::gameoverlayinput {
class DisplayTreeLease {
    struct Child{HWND window=nullptr;LONG_PTR addedEx=0;bool enabled=false;};
    std::array<Child,64> children_{};size_t count_=0;HWND root_=nullptr;
    static constexpr UINT_PTR subclassId=0x594D4844; // monitor_hud_input_policy canonical subclass
    static constexpr const wchar_t* property=L"YMCC.OverlayDisplayChildLease";
    bool remember(HWND child){
        DWORD pid=0;const auto thread=GetWindowThreadProcessId(child,&pid);
        if(pid!=GetCurrentProcessId()||thread!=GetCurrentThreadId())return true; // Original monitor helper separately validates foreign children.
        for(size_t i=0;i<count_;++i)if(children_[i].window==child&&GetPropW(child,property)==this)return true;
        if(count_>=children_.size()||GetPropW(child,property))return false;
        DWORD_PTR data=0;if(GetWindowSubclass(child,hudinput::displayChildProc,subclassId,&data))return false;
        if(!SetPropW(child,property,this))return false;
        children_[count_++]={child,hudinput::displayChildExStyle&~GetWindowLongPtrW(child,GWL_EXSTYLE),IsWindowEnabled(child)!=FALSE};
        return true;
    }
public:
    void begin(HWND root){root_=root;count_=0;children_={};}
    bool protect(){
        if(!root_||!IsWindow(root_))return false;
        if(count_==0&&!GetWindow(root_,GW_CHILD)){
            if(!hudinput::ensure(root_))return false;
            // Protection can synchronously create a helper. Register its
            // original state before falling back to the full child walk.
            if(!GetWindow(root_,GW_CHILD))return true;
        }
        struct Visit{DisplayTreeLease* self;bool ok=true;} visit{this};
        EnumChildWindows(root_,[](HWND child,LPARAM p)->BOOL{auto& v=*reinterpret_cast<Visit*>(p);v.ok=v.self->remember(child);return v.ok;},reinterpret_cast<LPARAM>(&visit));
        return visit.ok&&hudinput::ensureDisplayTree(root_);
    }
    bool restore() noexcept {
        bool ok=true;size_t retained=0;
        for(size_t i=0;i<count_;++i){const auto child=children_[i];const HWND h=child.window;
            if(!IsWindow(h)||GetPropW(h,property)!=this)continue;
            DWORD_PTR root=0;
            if(GetWindowSubclass(h,hudinput::displayChildProc,subclassId,&root)&&root==reinterpret_cast<DWORD_PTR>(root_))
                if(!RemoveWindowSubclass(h,hudinput::displayChildProc,subclassId)){children_[retained++]=child;ok=false;continue;}
            const auto desired=GetWindowLongPtrW(h,GWL_EXSTYLE)&~child.addedEx;
            SetWindowLongPtrW(h,GWL_EXSTYLE,desired);
            if(child.enabled)EnableWindow(h,TRUE);
            if(GetWindowLongPtrW(h,GWL_EXSTYLE)!=desired||IsWindowEnabled(h)!=(child.enabled?TRUE:FALSE)){
                children_[retained++]=child;ok=false;continue;
            }
            RemovePropW(h,property);
        }
        for(size_t i=retained;i<count_;++i)children_[i]={};
        count_=retained;if(!count_)root_=nullptr;return ok;
    }
};
}
