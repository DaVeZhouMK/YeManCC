// Source-extracted keyboard shortcut encoding; fake SendInput only.
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
namespace ymcc::keyboardEvents { enum class SourceId { NativeKeyboardShortcut }; }
namespace fake {
unsigned checks=0;
json failures=json::array();
std::vector<std::vector<INPUT>> batches;
UINT observedRequested=0,observedInserted=0;
bool observerMatches=false;
void check(bool okay,const std::string& label){++checks;if(!okay)failures.push_back(label);}
UINT send(UINT count,LPINPUT inputs,int size){
    check(size==sizeof(INPUT),"SendInput size");
    batches.emplace_back(inputs,inputs+count);
    return count;
}
}
static void keyboardModifierObserveSelfSend(ymcc::keyboardEvents::SourceId source,
    const INPUT* inputs,UINT requested,UINT inserted) noexcept {
    fake::observedRequested=requested;fake::observedInserted=inserted;
    fake::observerMatches=source==ymcc::keyboardEvents::SourceId::NativeKeyboardShortcut &&
        !fake::batches.empty() && fake::batches.back().size()==requested;
    if(fake::observerMatches)for(UINT i=0;i<requested;++i){
        const auto& sent=fake::batches.back()[i];
        if(inputs[i].type!=sent.type || inputs[i].ki.wVk!=sent.ki.wVk ||
           inputs[i].ki.wScan!=sent.ki.wScan || inputs[i].ki.dwFlags!=sent.ki.dwFlags)
            fake::observerMatches=false;
    }
}
// Packet encoding remains independently checked; the actual batch/identity
// wrapper is source-bound by keyboard_self_send_batch_selftest.
static UINT keyboardSelfSendWithEvidence(ymcc::keyboardEvents::SourceId source,UINT count,INPUT* inputs) {
    const UINT inserted=fake::send(count,inputs,sizeof(INPUT));
    keyboardModifierObserveSelfSend(source,inputs,count,inserted);return inserted;
}
#define SendInput fake::send
#include "actual_shortcut.inc"
#undef SendInput
int main(){
    struct Case {std::string action,code;WORD vk;bool shift;bool extended;};
    std::vector<Case> cases;
    for(char c='0';c<='9';++c)cases.push_back({"keyboard.numeric",std::string("Digit")+c,static_cast<WORD>(c),false,false});
    for(unsigned i=1;i<=12;++i)cases.push_back({"keyboard.numeric","F"+std::to_string(i),static_cast<WORD>(VK_F1+i-1),false,false});
    for(char c='A';c<='Z';++c)cases.push_back({"keyboard.letter",std::string("Key")+c,static_cast<WORD>(c),false,false});
    for(char c='0';c<='9';++c)cases.push_back({"keyboard.symbol",std::string("Digit")+c,static_cast<WORD>(c),true,false});
    for(const auto& c:std::vector<Case>{
        {"keyboard.symbol","Backquote",VK_OEM_3,true,false},{"keyboard.symbol","Minus",VK_OEM_MINUS,true,false},
        {"keyboard.symbol","Equal",VK_OEM_PLUS,true,false},{"keyboard.leftFunction","ShiftLeft",VK_LSHIFT,false,false},
        {"keyboard.leftFunction","AltLeft",VK_LMENU,false,false},{"keyboard.leftFunction","Enter",VK_RETURN,false,false},
        {"keyboard.leftFunction","MetaLeft",VK_LWIN,false,false},{"keyboard.leftFunction","CapsLock",VK_CAPITAL,false,false},
        {"keyboard.leftFunction","Escape",VK_ESCAPE,false,false},{"keyboard.leftFunction","ControlLeft",VK_LCONTROL,false,false},
        {"keyboard.leftFunction","Backspace",VK_BACK,false,false},{"keyboard.rightFunction","PrintScreen",VK_SNAPSHOT,false,true},
        {"keyboard.rightFunction","ScrollLock",VK_SCROLL,false,false},{"keyboard.rightFunction","Pause",VK_PAUSE,false,false},
        {"keyboard.rightFunction","Insert",VK_INSERT,false,true},{"keyboard.rightFunction","Home",VK_HOME,false,true},
        {"keyboard.rightFunction","PageUp",VK_PRIOR,false,true},{"keyboard.rightFunction","Delete",VK_DELETE,false,true},
        {"keyboard.rightFunction","End",VK_END,false,true},{"keyboard.rightFunction","PageDown",VK_NEXT,false,true}})cases.push_back(c);
    for(const auto& c:cases){
        const auto label=c.action+"/"+c.code;
        WORD vk=0;bool shift=false;
        fake::check(nativeKeyboardShortcutVirtualKey(c.action,c.code,vk,shift),label+" mapping rejected");
        fake::check(vk==c.vk && shift==c.shift,label+" mapping changed");
        fake::batches.clear();fake::observerMatches=false;
        nativeKeyboardShortcutSend(vk,shift);
        fake::check(fake::batches.size()==1,label+" expected one complete batch");
        if(fake::batches.size()!=1)continue;
        const auto& inputs=fake::batches.back();
        fake::check(inputs.size()==(c.shift?4u:2u),label+" batch size");
        fake::check(fake::observerMatches && fake::observedRequested==inputs.size() &&
                    fake::observedInserted==inputs.size(),label+" source/count observer");
        std::vector<INPUT> pending;
        for(const auto& e:inputs){
            fake::check(e.type==INPUT_KEYBOARD && e.ki.wScan==0,label+" unexpected event type/scan");
            const DWORD identity=e.ki.dwFlags&~DWORD(KEYEVENTF_KEYUP);
            if((e.ki.dwFlags&KEYEVENTF_KEYUP)==0){
                if(e.ki.wVk==c.vk)fake::check(identity==(c.extended?DWORD(KEYEVENTF_EXTENDEDKEY):0),label+" wrong DOWN identity flags");
                pending.push_back(e);
            }else{
                fake::check(!pending.empty(),label+" UP without DOWN");
                if(!pending.empty()){
                    const auto down=pending.back();pending.pop_back();
                    fake::check(down.ki.wVk==e.ki.wVk && down.ki.wScan==e.ki.wScan &&
                                (down.ki.dwFlags&~DWORD(KEYEVENTF_KEYUP))==identity,label+" DOWN/UP identity mismatch");
                }
            }
        }
        fake::check(pending.empty(),label+" unpaired input packet");
    }
    for(const auto& c:std::vector<Case>{{"keyboard.numeric","F13",0,false,false},{"keyboard.numeric","Digit10",0,false,false},
        {"keyboard.letter","Keya",0,false,false},{"keyboard.leftFunction","ControlRight",0,false,false},
        {"unknown","ControlLeft",0,false,false}}){
        WORD vk=0;bool shift=false;
        fake::check(!nativeKeyboardShortcutVirtualKey(c.action,c.code,vk,shift),"invalid mapping accepted: "+c.code);
    }
    json result={{"pass",fake::failures.empty()},{"checks",fake::checks},{"supportedMappings",cases.size()},
        {"failures",fake::failures},{"actualOSInput",false},{"scope","Actual mapping/flag/sender functions; packet identity only, no OS key state or delivery claim"},
        {"partialSendRecoveryTested",false},{"ctrlAltFieldProducerProven",false}};
    std::cout<<result.dump()<<'\n';return fake::failures.empty()?0:1;
}
