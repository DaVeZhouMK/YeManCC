#pragma once
#include <windows.h>
#include <winioctl.h>
#include <usbioctl.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <json.hpp>
namespace ymcc::msi::usbName {
using Json=nlohmann::json;
struct IoHooks {
    std::function<HANDLE(const wchar_t*)> open;
    std::function<HANDLE()> event;
    std::function<void(HANDLE)> close;
    std::function<bool(HANDLE,DWORD,void*,DWORD,DWORD&,OVERLAPPED&)> issue;
    std::function<DWORD(HANDLE,DWORD)> wait;
    std::function<bool(HANDLE,OVERLAPPED&)> cancel;
    std::function<bool(HANDLE,OVERLAPPED&,DWORD&)> result;
    std::function<DWORD()> error;
    std::function<uint64_t()> tick;
    bool complete()const{return open&&event&&close&&issue&&wait&&cancel&&result&&error&&tick;}
    static IoHooks system(){return {
        [](const wchar_t*p){return CreateFileW(p,0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);},
        []{return CreateEventW(nullptr,TRUE,FALSE,nullptr);},[](HANDLE h){CloseHandle(h);},
        [](HANDLE h,DWORD c,void*b,DWORD n,DWORD&r,OVERLAPPED&o){return DeviceIoControl(h,c,b,n,b,n,&r,&o)!=FALSE;},
        [](HANDLE h,DWORD ms){return WaitForSingleObject(h,ms);},[](HANDLE h,OVERLAPPED&o){return CancelIoEx(h,&o)!=FALSE;},
        [](HANDLE h,OVERLAPPED&o,DWORD&n){return GetOverlappedResult(h,&o,&n,FALSE)!=FALSE;},[]{return GetLastError();},[]{return GetTickCount64();}};}
};
struct IoReply{bool ok=false;std::vector<uint8_t> bytes;Json receipt;};
// A pending kernel buffer/event/handle is never destroyed before completion.
// Unknown cancellation disables further reads; the existing observer process
// watchdog ultimately owns process teardown. No device reset/cycle fallback.
class ReadOnlyUsbIo {
    struct Job{HANDLE file=INVALID_HANDLE_VALUE,event=nullptr;OVERLAPPED overlapped{};std::vector<uint8_t> buffer;bool completionUnproven=false;};
    IoHooks hooks;std::vector<std::unique_ptr<Job>> retained;
    void destroy(Job&j){const HANDLE e=j.event;j.event=nullptr;if(e)hooks.close(e);const HANDLE f=j.file;j.file=INVALID_HANDLE_VALUE;if(f!=INVALID_HANDLE_VALUE)hooks.close(f);}
    static bool supported(DWORD code,const std::vector<uint8_t>& data){
        if(code==IOCTL_USB_GET_NODE_INFORMATION)return data.size()>=sizeof(USB_NODE_INFORMATION)&&reinterpret_cast<const USB_NODE_INFORMATION*>(data.data())->NodeType==UsbHub;
        if(code==IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME)return data.size()>=sizeof(USB_NODE_CONNECTION_DRIVERKEY_NAME)&&reinterpret_cast<const USB_NODE_CONNECTION_DRIVERKEY_NAME*>(data.data())->ConnectionIndex>0;
        if(code==IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX)return data.size()>=sizeof(USB_NODE_CONNECTION_INFORMATION_EX)&&reinterpret_cast<const USB_NODE_CONNECTION_INFORMATION_EX*>(data.data())->ConnectionIndex>0;
        if(code!=IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION||data.size()<offsetof(USB_DESCRIPTOR_REQUEST,Data))return false;
        const auto*r=reinterpret_cast<const USB_DESCRIPTOR_REQUEST*>(data.data());const unsigned type=r->SetupPacket.wValue>>8,index=r->SetupPacket.wValue&255;
        return r->ConnectionIndex>0&&r->SetupPacket.bmRequest==0x80&&r->SetupPacket.bRequest==6&&
            (type==1||type==2||type==3)&&r->SetupPacket.wLength>0&&r->SetupPacket.wLength<=16384&&
            r->SetupPacket.wLength<=data.size()-offsetof(USB_DESCRIPTOR_REQUEST,Data)&&
            (type==3?(index==0?r->SetupPacket.wIndex==0:r->SetupPacket.wIndex!=0):r->SetupPacket.wIndex==0)&&
            (type!=1||index==0);
    }
    bool complete(Job& j,DWORD waitState,DWORD& count,DWORD& err){const bool ok=hooks.result(j.file,j.overlapped,count);err=ok?0:hooks.error();
        return ok||err==ERROR_OPERATION_ABORTED||(waitState==WAIT_OBJECT_0&&err!=ERROR_IO_INCOMPLETE&&err!=ERROR_IO_PENDING&&err!=ERROR_INVALID_HANDLE);}
public:
    explicit ReadOnlyUsbIo(IoHooks h=IoHooks::system()):hooks(std::move(h)){if(!hooks.complete())throw std::runtime_error("incomplete-name-probe-platform-boundary");retained.reserve(1);}
    ~ReadOnlyUsbIo(){
        // Deliberately retain unproven asynchronous storage until process exit.
        // Closing an event/file or freeing its buffer here would erase ownership.
        for(auto&j:retained){if(j->completionUnproven)(void)j.release();else try{destroy(*j);}catch(...){/* Completion proven: OS owns remaining handle cleanup. */}}
    }
    size_t pending()const{return retained.size();}
    void reap(){for(auto i=retained.begin();i!=retained.end();){if(!(*i)->completionUnproven){destroy(**i);i=retained.erase(i);continue;}const DWORD w=hooks.wait((*i)->event,0);DWORD n=0,e=0;
        if(w==WAIT_OBJECT_0&&complete(**i,w,n,e)){(*i)->completionUnproven=false;destroy(**i);i=retained.erase(i);}else ++i;}}
    IoReply query(const std::wstring& hub,DWORD code,std::vector<uint8_t> data,uint64_t deadline){
        IoReply r;const uint64_t begin=hooks.tick();r.receipt={{"ioctl",code},{"beginTick",begin},{"deadlineTick",deadline},
            {"handleDesiredAccess",0},{"overlapped",true},{"attempted",false},{"cancelAttempted",false},{"bufferRetained",false},
            {"writesHardware",false},{"scope","allowlisted USB hub query/GetDescriptor IN only; driver readback, not wire or consumer proof"}};
        auto finish=[&](const char*s,DWORD e){r.receipt["state"]=s;r.receipt["error"]=e;r.receipt["endTick"]=hooks.tick();return r;};
        if(!supported(code,data))return finish("request-not-allowlisted",ERROR_NOT_SUPPORTED);
        if(hub.empty()||hub.size()>32760||data.size()>32768)return finish("invalid-query-bounds",ERROR_INVALID_PARAMETER);
        reap();if(!retained.empty())return finish("prior-read-completion-unproven",ERROR_IO_INCOMPLETE);
        if(begin>=deadline)return finish("capture-deadline-expired",WAIT_TIMEOUT);
        auto j=std::make_unique<Job>();j->buffer=std::move(data);j->file=hooks.open(hub.c_str());if(j->file==INVALID_HANDLE_VALUE)return finish("metadata-handle-denied",hooks.error());
        j->event=hooks.event();if(!j->event){const DWORD e=hooks.error();destroy(*j);return finish("event-unavailable",e);}j->overlapped.hEvent=j->event;
        const uint64_t beforeIssue=hooks.tick();if(beforeIssue>=deadline){destroy(*j);return finish("deadline-before-issue",WAIT_TIMEOUT);}
        // Capacity was reserved before any I/O; move ownership before issue can start.
        retained.push_back(std::move(j));Job& active=*retained.back();active.completionUnproven=true;
        DWORD count=0;const bool immediate=hooks.issue(active.file,code,active.buffer.data(),DWORD(active.buffer.size()),count,active.overlapped);if(immediate)active.completionUnproven=false;r.receipt["attempted"]=true;
        const DWORD startError=immediate?0:hooks.error();r.receipt["startError"]=startError;
        if(!immediate&&startError!=ERROR_IO_PENDING){active.completionUnproven=false;destroy(active);retained.clear();return finish("start-failed",startError);}
        bool terminal=immediate,success=immediate;DWORD err=0;
        if(!immediate){const uint64_t now=hooks.tick();const DWORD budget=now>=deadline?0:DWORD(std::min<uint64_t>(200,deadline-now));
            const DWORD w=hooks.wait(active.event,budget);r.receipt["waitState"]=w;r.receipt["waitBudgetMs"]=budget;
            if(w==WAIT_OBJECT_0){success=hooks.result(active.file,active.overlapped,count);err=success?0:hooks.error();terminal=success||err==ERROR_OPERATION_ABORTED||(err!=ERROR_IO_INCOMPLETE&&err!=ERROR_IO_PENDING&&err!=ERROR_INVALID_HANDLE);}
            if(!terminal){r.receipt["cancelAttempted"]=true;const bool cancelled=hooks.cancel(active.file,active.overlapped);r.receipt["cancelAccepted"]=cancelled;r.receipt["cancelError"]=cancelled?0:hooks.error();
                const DWORD settle=hooks.wait(active.event,50);r.receipt["cancelWaitState"]=settle;DWORD settledBytes=0,settledError=0;
                terminal=complete(active,settle,settledBytes,settledError);r.receipt["cancelCompletionError"]=settledError;
                // Bytes completed after timeout/cancel are not used for the join.
                success=false;err=WAIT_TIMEOUT;
            }
        }
        if(terminal)active.completionUnproven=false;
        if(!terminal){r.receipt["bufferRetained"]=true;return finish("completion-unproven-retained",ERROR_IO_INCOMPLETE);}
        if(success&&hooks.tick()>=deadline){success=false;err=WAIT_TIMEOUT;r.receipt["completionPastCaptureDeadline"]=true;}
        if(success&&count<=active.buffer.size()){r.ok=true;r.bytes.assign(active.buffer.begin(),active.buffer.begin()+count);r.receipt["returnedBytes"]=count;}
        else if(success)err=ERROR_INVALID_DATA;
        destroy(active);retained.clear();return finish(r.ok?"completed":"completed-without-admitted-data",err);
    }
};
}