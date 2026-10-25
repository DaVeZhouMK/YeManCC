#define UNICODE
#define _UNICODE
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include "gesture_summon.h"
#include <iostream>
#include <limits>
using namespace ymcc::gesturesummon;
int main() {
    int checks=0,failures=0;
    auto check=[&](bool pass,const char* name){++checks;if(!pass){++failures;std::cerr<<"FAIL "<<name<<"\n";}};
    Recognizer r;
    auto swipe=[&](bool left,float scale,float inward,float vertical,unsigned duration){
        r.reset(left,scale);r.event(1,Phase::Down,10,50,100);
        return r.event(1,Phase::Up,10+(left?inward:-inward)*scale,50+vertical*scale,100+duration);
    };
    check(swipe(true,1,80,0,300),"left-edge inward swipe");
    check(swipe(false,1,80,0,300),"right-edge inward swipe");
    check(!swipe(true,1,79,0,300),"distance below threshold");
    check(!swipe(false,1,0,0,50),"tap rejected");
    check(!swipe(true,1,-100,0,300),"outward rejected");
    check(!swipe(true,1,5,100,300),"vertical swipe rejected");
    check(!swipe(true,1,80,49,300),"excess vertical drift rejected");
    check(swipe(true,1,80,48,1000),"inclusive threshold boundaries");
    check(!swipe(true,1,100,0,1001),"slow hold rejected");
    check(swipe(false,1.5f,80,0,300),"150 percent DPI");
    check(swipe(true,2,80,0,300),"200 percent DPI");
    r.reset(true,1);r.event(1,Phase::Down,10,20,100);
    check(!r.event(1,Phase::Move,100,20,200),"release required");
    check(r.event(1,Phase::Up,100,20,250),"release commits");
    check(!r.event(1,Phase::Up,100,20,260),"no duplicate release");
    r.reset(true,1);r.event(1,Phase::Down,10,20,100);
    check(!r.event(1,Phase::Cancel,100,20,200),"capture loss cancels");
    check(!r.event(1,Phase::Up,100,20,210),"cancelled release ignored");
    r.reset(true,1);r.event(1,Phase::Down,10,20,100);r.event(2,Phase::Down,10,20,110);
    check(!r.event(2,Phase::Up,100,20,150),"second finger rejected");
    check(!r.event(1,Phase::Up,100,20,200),"remaining finger stays rejected");
    r.event(3,Phase::Down,10,20,300);
    check(r.event(3,Phase::Up,100,20,400),"fresh single finger after overlap accepted");
    r.reset(true,1);r.event(1,Phase::Down,10,20,100);r.event(2,Phase::Down,10,20,110);
    r.event(2,Phase::Cancel,10,20,150);
    check(!r.event(1,Phase::Up,100,20,200),"overlap cancellation cannot rearm");
    r.reset(true,1);r.event(1,Phase::Down,10,20,100,false);
    check(!r.event(1,Phase::Up,100,20,200,false),"pen cannot summon");
    r.reset(true,1);check(!r.event(4,Phase::Up,100,20,200),"unowned up ignored");
    check(!r.hasContacts(),"unowned up creates no contact");
    r.event(1,Phase::Down,10,20,100);r.event(1,Phase::Move,-10,20,150);
    check(!r.event(1,Phase::Up,100,20,200),"outward detour cannot rearm");
    r.reset(true,1);r.event(1,Phase::Down,10,20,100);r.event(1,Phase::Move,30,100,150);
    check(!r.event(1,Phase::Up,100,20,200),"vertical detour cannot rearm");
    r.reset(true,1);r.event(1,Phase::Down,10,20,100);
    check(!r.event(1,Phase::Up,std::numeric_limits<float>::quiet_NaN(),20,200),"nonfinite rejected");
    r.reset(true,1);r.event(1,Phase::Down,10,20,100);r.reset(false,1);
    check(!r.event(1,Phase::Up,-100,20,200),"direction change cancels old contact");
    r.reset(true,1);r.event(1,Phase::Down,10,20,0xfffffff0u);
    check(r.event(1,Phase::Up,100,20,0x50u),"DWORD timestamp wrap");
    r.reset(true,1);r.event(1,Phase::Down,10,20,100);
    check(!r.event(1,Phase::Up,100,20,90),"backwards timestamp rejected");
    r.reset(true,0);r.event(1,Phase::Down,10,20,100);
    check(r.event(1,Phase::Up,100,20,200),"invalid DPI falls back");
    // Retired runtime must ignore saved/imported true values and never create
    // a touch surface. Pure recognition above is retained for future evaluation.
    check(!kFeatureEnabled,"gesture summon is inactive");
    check(std::string(kDisabledReason)=="影响 Windows 功能","inactive reason is recorded");
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);
    cls.lpszClassName=L"GestureSummonSelftestOwner";cls.lpfnWndProc=DefWindowProcW;RegisterClassW(&cls);
    HWND owner=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,cls.lpszClassName,L"",WS_POPUP,100,100,200,200,nullptr,nullptr,cls.hInstance,nullptr);
    check(owner!=nullptr,"hidden owner created");
    const auto hasOwnedEdge=[](){
        bool found=false;
        EnumThreadWindows(GetCurrentThreadId(),[](HWND hwnd,LPARAM context)->BOOL{
            wchar_t name[64]{};GetClassNameW(hwnd,name,64);
            if(lstrcmpW(name,L"YeManGestureSummonEdge")==0)*reinterpret_cast<bool*>(context)=true;
            return TRUE;
        },reinterpret_cast<LPARAM>(&found));
        return found;
    };
    {
        EdgeSurface edge;
        check(edge.refresh(owner,false,false),"disabled refresh succeeds");
        check(!edge.enabled()&&!edge.active()&&!hasOwnedEdge(),"default has no touch surface");
        check(edge.refresh(owner,true,false),"legacy enabled request handled");
        check(!edge.enabled()&&!edge.active()&&!hasOwnedEdge(),"legacy true cannot activate right edge");
        check(edge.refresh(owner,true,true,true),"forced side refresh handled");
        check(!edge.enabled()&&!edge.active()&&!hasOwnedEdge(),"forced refresh cannot activate left edge");
        check(!edge.accepts(0)&&!edge.accepts(1),"queued summon generations rejected");
        check(!edge.edgeHintApplied(),"no Windows edge suppression");
        check(edge.refresh(nullptr,true,false,true)&&!hasOwnedEdge(),"missing owner stays inactive");
        edge.shutdown();
        check(!edge.enabled()&&!edge.active()&&!hasOwnedEdge(),"shutdown leaves no surface");
    }
    {
        const auto left=centeredEdge(-1000,-200,1000,800,12,true),right=centeredEdge(-1000,-200,1000,800,12,false);
        check(left.left==-1000&&left.top==-100&&left.bottom==700&&left.right==-988,"left centered eighty percent on negative origin");
        check(right.right==1000&&right.top==-100&&right.bottom==700&&right.left==988,"right centered eighty percent");
    }
    DestroyWindow(owner);
    std::cout<<"{\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"touchInjected\":false}";
    return failures?1:0;
}
