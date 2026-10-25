#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <atomic>
#include <algorithm>
#include "input_signal_activity_evidence.h"

// Only decodes supplied existing callback/batch data. No Win32 calls, injection,
// device query, clocks, dynamic allocation or logging. All ticks come from caller.
namespace ymcc::inputActivity {
inline constexpr std::size_t sourceCapacity=16;
struct BatchCounts {
    std::uint64_t batches=0,requested=0,returned=0,partial=0,zero=0,overReported=0;
    std::uint64_t inspected=0,uninspected=0,unicodeSkipped=0,invalidKey=0;
    DWORD lastErrorHint=0;
};
struct AdapterCounts {
    std::array<BatchCounts,sourceCapacity> sources{};
    std::uint64_t unknownSource=0,rawMouseUnobserved=0;
};
class WindowsAdapter {
    struct AtomicBatch {
        std::atomic<std::uint64_t> batches{0},requested{0},returned{0},partial{0},zero{0},overReported{0};
        std::atomic<std::uint64_t> inspected{0},uninspected{0},unicodeSkipped{0},invalidKey{0};
        std::atomic<DWORD> lastErrorHint{0};
    };
    std::array<AtomicBatch,sourceCapacity> sources_{};
    std::atomic<std::uint64_t> unknownSource_{0},rawMouseUnobserved_{0};
    Recorder& recorder_;
    void mouseFlags(Channel channel,DWORD flags,DWORD mouseData,std::uint64_t tick,std::uint64_t source)noexcept{
        auto record=[&](Kind kind,std::uint16_t code,bool down,std::int32_t delta=0){
            Event e{};e.channel=channel;e.kind=kind;e.code=code;e.down=down;e.delta=delta;e.tick=tick;e.sourceId=source;(void)recorder_.record(e);
        };
        for(const auto& pair:std::array<std::array<DWORD,3>,3>{{{{MOUSEEVENTF_LEFTDOWN,MOUSEEVENTF_LEFTUP,1}},{{MOUSEEVENTF_RIGHTDOWN,MOUSEEVENTF_RIGHTUP,2}},{{MOUSEEVENTF_MIDDLEDOWN,MOUSEEVENTF_MIDDLEUP,3}}}}){
            if(flags&pair[0])record(Kind::MouseButton,static_cast<std::uint16_t>(pair[2]),true);
            if(flags&pair[1])record(Kind::MouseButton,static_cast<std::uint16_t>(pair[2]),false);
        }
        for(unsigned button=0;button<2;++button)if(mouseData&(1u<<button)){
            if(flags&MOUSEEVENTF_XDOWN)record(Kind::MouseButton,static_cast<std::uint16_t>(4+button),true);
            if(flags&MOUSEEVENTF_XUP)record(Kind::MouseButton,static_cast<std::uint16_t>(4+button),false);
        }
        if(flags&MOUSEEVENTF_WHEEL)record(Kind::WheelVertical,0,false,static_cast<std::int32_t>(mouseData));
        if(flags&MOUSEEVENTF_HWHEEL)record(Kind::WheelHorizontal,0,false,static_cast<std::int32_t>(mouseData));
        if(flags&MOUSEEVENTF_MOVE)record(Kind::MouseMove,0,false);
    }
    void input(Channel channel,const INPUT& value,std::uint64_t tick,std::uint64_t source,AtomicBatch* counts)noexcept{
        if(value.type==INPUT_MOUSE){mouseFlags(channel,value.mi.dwFlags,value.mi.mouseData,tick,source);return;}
        if(value.type!=INPUT_KEYBOARD)return;
        if(value.ki.dwFlags&KEYEVENTF_UNICODE){if(counts)counts->unicodeSkipped.fetch_add(1,std::memory_order_relaxed);return;}
        Event e{};e.channel=channel;e.tick=tick;e.sourceId=source;
        const bool scan=(value.ki.dwFlags&KEYEVENTF_SCANCODE)!=0;
        e.kind=scan?Kind::KeyboardScanCode:Kind::Keyboard;
        e.code=scan?value.ki.wScan:value.ki.wVk;
        if(e.code>255){if(counts)counts->invalidKey.fetch_add(1,std::memory_order_relaxed);return;}
        e.extended=(value.ki.dwFlags&KEYEVENTF_EXTENDEDKEY)!=0;e.down=(value.ki.dwFlags&KEYEVENTF_KEYUP)==0;
        (void)recorder_.record(e);
    }
public:
    explicit WindowsAdapter(Recorder& recorder)noexcept:recorder_(recorder){}
    void lowLevelKeyboard(const KBDLLHOOKSTRUCT& kb,std::uint64_t tick,bool suppressed,bool decisionKnown=true)noexcept{
        if(!recorder_.enabled())return;
        Event e{};e.channel=Channel::LowLevelKeyboard;e.kind=Kind::Keyboard;
        e.code=kb.vkCode<=255?static_cast<std::uint16_t>(kb.vkCode):256;
        e.extended=(kb.flags&LLKHF_EXTENDED)!=0;e.down=(kb.flags&LLKHF_UP)==0;e.tick=tick;
        // A route/flag code, NOT a producer process ID. Its meaning is emitted in
        // the log. A swallowed event is still observed in this callback ledger.
        e.sourceId=(decisionKnown?(suppressed?2u:1u):3u)|((kb.flags&LLKHF_INJECTED)?0x10u:0u)|((kb.flags&LLKHF_LOWER_IL_INJECTED)?0x20u:0u);
        (void)recorder_.record(e);
    }
    void rawKeyboard(const RAWINPUT& raw,std::uint64_t tick)noexcept{
        if(!recorder_.enabled())return;
        if(raw.header.dwType!=RIM_TYPEKEYBOARD){if(raw.header.dwType==RIM_TYPEMOUSE)rawMouseUnobserved_.fetch_add(1,std::memory_order_relaxed);return;}
        const auto& kb=raw.data.keyboard;Event e{};e.channel=Channel::RawKeyboard;e.kind=Kind::Keyboard;e.code=kb.VKey;
        e.extended=(kb.Flags&(RI_KEY_E0|RI_KEY_E1))!=0;e.down=(kb.Flags&RI_KEY_BREAK)==0;e.tick=tick;
        e.sourceId=(kb.Flags&RI_KEY_E1)?2u:1u;e.deviceId=reinterpret_cast<std::uintptr_t>(raw.header.hDevice);
        (void)recorder_.record(e);
    }
    void mouseHook(const MSLLHOOKSTRUCT& ms,WPARAM message,std::uint64_t tick,bool suppressed)noexcept{
        if(!recorder_.enabled())return;
        Event e{};e.channel=Channel::MouseHook;e.tick=tick;
        e.sourceId=(suppressed?2u:1u)|((ms.flags&LLMHF_INJECTED)?0x10u:0u)|((ms.flags&LLMHF_LOWER_IL_INJECTED)?0x20u:0u);
        switch(message){
        case WM_LBUTTONDOWN:case WM_LBUTTONUP:e.kind=Kind::MouseButton;e.code=1;e.down=message==WM_LBUTTONDOWN;break;
        case WM_RBUTTONDOWN:case WM_RBUTTONUP:e.kind=Kind::MouseButton;e.code=2;e.down=message==WM_RBUTTONDOWN;break;
        case WM_MBUTTONDOWN:case WM_MBUTTONUP:e.kind=Kind::MouseButton;e.code=3;e.down=message==WM_MBUTTONDOWN;break;
        case WM_XBUTTONDOWN:case WM_XBUTTONUP:e.kind=Kind::MouseButton;e.code=HIWORD(ms.mouseData)==XBUTTON1?4:HIWORD(ms.mouseData)==XBUTTON2?5:0;e.down=message==WM_XBUTTONDOWN;break;
        case WM_MOUSEWHEEL:e.kind=Kind::WheelVertical;e.delta=static_cast<SHORT>(HIWORD(ms.mouseData));break;
        case WM_MOUSEHWHEEL:e.kind=Kind::WheelHorizontal;e.delta=static_cast<SHORT>(HIWORD(ms.mouseData));break;
        case WM_MOUSEMOVE:e.kind=Kind::MouseMove;break;
        default:return;
        }
        (void)recorder_.record(e);
    }
    void selfSend(const INPUT* inputs,UINT requested,UINT inserted,DWORD errorHint,std::uint64_t tick,unsigned source)noexcept{
        if(!recorder_.enabled()||!inputs)return;
        const unsigned slot=source<sourceCapacity?source:0;
        if(source>=sourceCapacity)unknownSource_.fetch_add(1,std::memory_order_relaxed);
        auto& c=sources_[slot];c.batches.fetch_add(1,std::memory_order_relaxed);c.requested.fetch_add(requested,std::memory_order_relaxed);c.returned.fetch_add(inserted,std::memory_order_relaxed);
        if(inserted!=requested){c.partial.fetch_add(1,std::memory_order_relaxed);c.lastErrorHint.store(errorHint,std::memory_order_relaxed);}
        if(!inserted)c.zero.fetch_add(1,std::memory_order_relaxed);
        if(inserted>requested)c.overReported.fetch_add(1,std::memory_order_relaxed);
        const UINT bound=(std::min)(requested,UINT(64));c.inspected.fetch_add(bound,std::memory_order_relaxed);c.uninspected.fetch_add(requested-bound,std::memory_order_relaxed);
        for(UINT i=0;i<bound;++i){
            input(Channel::SelfSendInputRequest,inputs[i],tick,static_cast<std::uint64_t>(slot)+1,&c);
            if(i<inserted)input(Channel::SelfSendInputReturnedPrefix,inputs[i],tick,static_cast<std::uint64_t>(slot)+1,nullptr);
        }
    }
    // Independent atomic windows, not a transaction with the key/mouse ledger.
    // Cumulative modular counters are drained only under the explicit log gate.
    AdapterCounts takeCounts()noexcept{
        AdapterCounts out{};
        for(std::size_t n=0;n<sourceCapacity;++n){auto& a=sources_[n];auto& b=out.sources[n];
            b.batches=a.batches.exchange(0);b.requested=a.requested.exchange(0);b.returned=a.returned.exchange(0);b.partial=a.partial.exchange(0);b.zero=a.zero.exchange(0);b.overReported=a.overReported.exchange(0);
            b.inspected=a.inspected.exchange(0);b.uninspected=a.uninspected.exchange(0);b.unicodeSkipped=a.unicodeSkipped.exchange(0);b.invalidKey=a.invalidKey.exchange(0);b.lastErrorHint=a.lastErrorHint.load();}
        out.unknownSource=unknownSource_.exchange(0);out.rawMouseUnobserved=rawMouseUnobserved_.exchange(0);return out;
    }
};
} // namespace ymcc::inputActivity
