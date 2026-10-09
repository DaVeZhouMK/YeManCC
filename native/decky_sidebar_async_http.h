#pragma once
// Additive cancellable bootstrap IO. All WinHTTP calls/close are issued by ONE owner.
// Async callbacks only record completions. Buffers outlive HANDLE_CLOSING.
// Reference: learn.microsoft.com/windows/win32/winhttp/concurrency-in-winhttp
#include <windows.h>
#include <winhttp.h>
#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <stdexcept>
#pragma comment(lib,"winhttp.lib")
namespace ymcc::deckymirror {
class CancelToken {
    HANDLE event_ = CreateEventW(nullptr,TRUE,FALSE,nullptr);
public:
    CancelToken(){if(!event_)throw std::runtime_error("mirror-cancel-event-failed");}
    ~CancelToken(){CloseHandle(event_);}
    CancelToken(const CancelToken&)=delete;
    HANDLE event() const{return event_;}
    void cancel(){SetEvent(event_);}
    bool canceled() const{return WaitForSingleObject(event_,0)==WAIT_OBJECT_0;}
};
class CancelSignal {
    std::mutex mutex_;
    std::shared_ptr<CancelToken> token_;
public:
    void renew(){auto token=std::make_shared<CancelToken>();std::lock_guard lock(mutex_);token_=std::move(token);}
    std::shared_ptr<CancelToken> current(){std::lock_guard lock(mutex_);return token_;}
    void cancel(){auto token=current();if(token)token->cancel();}
};
struct AsyncState {
    std::mutex mutex;
    HANDLE changed=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    DWORD completed=0,error=0,length=0;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE kind=WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
    bool websocket=false;
    std::array<char,8192> buffer{};
    std::string outbound;
    std::shared_ptr<AsyncState> callbackLifetime;
    AsyncState(){if(!changed)throw std::runtime_error("mirror-async-event-failed");}
    ~AsyncState(){CloseHandle(changed);}
};
inline void CALLBACK mirrorHttpCallback(HINTERNET,DWORD_PTR context,DWORD status,void* information,DWORD length) {
    if(!context)return;
    auto* state=reinterpret_cast<AsyncState*>(context);
    std::shared_ptr<AsyncState> keep;
    {
        std::lock_guard lock(state->mutex);
        keep=state->callbackLifetime;
        if(status==WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING){state->callbackLifetime.reset();return;}
        if(status==WINHTTP_CALLBACK_STATUS_REQUEST_ERROR){
            state->error=length>=sizeof(WINHTTP_ASYNC_RESULT)?reinterpret_cast<WINHTTP_ASYNC_RESULT*>(information)->dwError:ERROR_WINHTTP_INTERNAL_ERROR;
        }else if(status==WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE||status==WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE||
                 status==WINHTTP_CALLBACK_STATUS_READ_COMPLETE||status==WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE){
            state->completed=status;
            if(status==WINHTTP_CALLBACK_STATUS_READ_COMPLETE){
                if(state->websocket){
                    if(!information||length<sizeof(WINHTTP_WEB_SOCKET_STATUS)){state->error=ERROR_INVALID_DATA;}
                    else{const auto* value=reinterpret_cast<WINHTTP_WEB_SOCKET_STATUS*>(information);state->length=value->dwBytesTransferred;state->kind=value->eBufferType;}
                }else state->length=length;
            }
        }else return;
    }
    SetEvent(state->changed);
}
class AsyncRequest {
    HINTERNET handle_=nullptr;
    std::shared_ptr<AsyncState> state_;
    std::shared_ptr<CancelToken> cancellation_;
    ULONGLONG deadline_;
    void prepare(){check();std::lock_guard lock(state_->mutex);state_->completed=0;state_->error=0;state_->length=0;ResetEvent(state_->changed);}
    void wait(DWORD expected){
        for(;;){
            check();
            {std::lock_guard lock(state_->mutex);if(state_->error)throw std::runtime_error("mirror-bootstrap-async-error");if(state_->completed==expected)return;}
            const auto now=GetTickCount64();if(now>=deadline_)throw std::runtime_error("mirror-bootstrap-deadline");
            HANDLE handles[2]{cancellation_->event(),state_->changed};
            const auto result=WaitForMultipleObjects(2,handles,FALSE,static_cast<DWORD>(deadline_-now));
            if(result==WAIT_OBJECT_0)throw std::runtime_error("mirror-bootstrap-canceled");
            if(result==WAIT_TIMEOUT)throw std::runtime_error("mirror-bootstrap-deadline");
            if(result!=WAIT_OBJECT_0+1)throw std::runtime_error("mirror-bootstrap-wait-failed");
        }
    }
public:
    AsyncRequest(HINTERNET handle,std::shared_ptr<CancelToken> cancellation,ULONGLONG deadline,bool websocket=false)
        :handle_(handle),cancellation_(std::move(cancellation)),deadline_(deadline){
        if(!handle_)throw std::runtime_error("mirror-bootstrap-request-failed");
        try{
            state_=std::make_shared<AsyncState>();state_->websocket=websocket;
            DWORD_PTR pointer=reinterpret_cast<DWORD_PTR>(state_.get());
            if(!WinHttpSetOption(handle_,WINHTTP_OPTION_CONTEXT_VALUE,&pointer,sizeof(pointer)))throw std::runtime_error("mirror-bootstrap-context-failed");
            const auto prior=WinHttpSetStatusCallback(handle_,mirrorHttpCallback,WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS|WINHTTP_CALLBACK_FLAG_HANDLES,0);
            if(prior==WINHTTP_INVALID_STATUS_CALLBACK)throw std::runtime_error("mirror-bootstrap-callback-failed");
            {std::lock_guard lock(state_->mutex);state_->callbackLifetime=state_;}
        }catch(...){WinHttpSetStatusCallback(handle_,nullptr,0,0);WinHttpCloseHandle(handle_);handle_=nullptr;throw;}
    }
    ~AsyncRequest(){if(handle_){WinHttpCloseHandle(handle_);handle_=nullptr;}/* callback lifetime owns pending buffers until final callback */}
    AsyncRequest(const AsyncRequest&)=delete;
    HINTERNET handle() const{return handle_;}
    std::weak_ptr<AsyncState> callbackState() const{return state_;} // Read-only verification/diagnostic, no extra owner.
    void check() const{
        if(!cancellation_||cancellation_->canceled())throw std::runtime_error("mirror-bootstrap-canceled");
        if(GetTickCount64()>=deadline_)throw std::runtime_error("mirror-bootstrap-deadline");
    }
    void handshake(bool upgrade=false){
        if(upgrade&&!WinHttpSetOption(handle_,WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,nullptr,0))throw std::runtime_error("mirror-bootstrap-upgrade-failed");
        DWORD flags=WINHTTP_DISABLE_REDIRECTS;if(!WinHttpSetOption(handle_,WINHTTP_OPTION_DISABLE_FEATURE,&flags,sizeof(flags)))throw std::runtime_error("mirror-bootstrap-options-failed");
        prepare();
        if(!WinHttpSendRequest(handle_,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,reinterpret_cast<DWORD_PTR>(state_.get())))throw std::runtime_error("mirror-bootstrap-send-failed");
        wait(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE);
        prepare();if(!WinHttpReceiveResponse(handle_,nullptr))throw std::runtime_error("mirror-bootstrap-response-failed");wait(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE);
    }
    DWORD status(){check();DWORD code=0,size=sizeof(code);if(!WinHttpQueryHeaders(handle_,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&code,&size,nullptr))throw std::runtime_error("mirror-bootstrap-status-failed");return code;}
    std::string httpBody(size_t maximum){
        std::string body;
        for(;;){prepare();if(!WinHttpReadData(handle_,state_->buffer.data(),static_cast<DWORD>(state_->buffer.size()),nullptr))throw std::runtime_error("mirror-bootstrap-read-failed");wait(WINHTTP_CALLBACK_STATUS_READ_COMPLETE);
            DWORD length;{std::lock_guard lock(state_->mutex);length=state_->length;}
            if(length>state_->buffer.size())throw std::runtime_error("mirror-bootstrap-buffer-invalid");
            if(!length)return body;body.append(state_->buffer.data(),length);if(body.size()>maximum)throw std::runtime_error("mirror-bootstrap-body-limit");}
    }
    void sendText(const std::string& text){
        prepare();state_->outbound=text;
        const auto error=WinHttpWebSocketSend(handle_,WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,state_->outbound.data(),static_cast<DWORD>(state_->outbound.size()));
        if(error!=NO_ERROR&&error!=ERROR_IO_PENDING)throw std::runtime_error("mirror-bootstrap-ws-send-failed");wait(WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE);
    }
    std::string receiveText(size_t maximum){
        std::string text;
        for(;;){prepare();
            const auto error=WinHttpWebSocketReceive(handle_,state_->buffer.data(),static_cast<DWORD>(state_->buffer.size()),nullptr,nullptr);
            if(error!=NO_ERROR&&error!=ERROR_IO_PENDING)throw std::runtime_error("mirror-bootstrap-ws-receive-failed");wait(WINHTTP_CALLBACK_STATUS_READ_COMPLETE);
            DWORD length;WINHTTP_WEB_SOCKET_BUFFER_TYPE kind;
            {std::lock_guard lock(state_->mutex);length=state_->length;kind=state_->kind;}
            if(length>state_->buffer.size()||kind!=WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE&&kind!=WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)throw std::runtime_error("mirror-bootstrap-ws-invalid");
            text.append(state_->buffer.data(),length);if(text.size()>maximum)throw std::runtime_error("mirror-bootstrap-reply-limit");
            if(kind==WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)return text;
        }
    }
};
} // namespace ymcc::deckymirror
