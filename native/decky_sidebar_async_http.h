#pragma once
// Additive cancellable bootstrap IO. All WinHTTP calls/close are issued by ONE owner.
// Sequential ownership may transfer to the event watcher after startup.
// Async callbacks only record completions. Buffers outlive HANDLE_CLOSING.
// Reference: learn.microsoft.com/windows/win32/winhttp/concurrency-in-winhttp
#include <windows.h>
#include <winhttp.h>
#include <array>
#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <stdexcept>
#include <exception>
#include "steam_failure_diagnostics.h"
#pragma comment(lib,"winhttp.lib")
namespace ymcc::deckymirror {
class CancelToken {
    HANDLE event_ = nullptr;
    HANDLE observedOwner_ = nullptr; // Read-only cancellation from a distinct authority owner.
public:
    CancelToken():event_(CreateEventW(nullptr,TRUE,FALSE,nullptr)){
        if(!event_)throw std::runtime_error("mirror-cancel-event-failed");
    }
    // A binding worker duplicates its owner's inherited manual-reset event.
    // This wakes the existing WinHTTP wait directly; no relay thread or poller.
    // The caller retains its handle; this token closes only its own duplicate.
    explicit CancelToken(HANDLE externalEvent){
        if(!externalEvent || externalEvent==INVALID_HANDLE_VALUE ||
           !DuplicateHandle(GetCurrentProcess(),externalEvent,GetCurrentProcess(),&event_,
               SYNCHRONIZE|EVENT_MODIFY_STATE,FALSE,0))
            throw std::runtime_error("mirror-cancel-event-duplicate-failed");
    }
    // Delegated identity revocation is one-way: local watch retirement must
    // never signal the producer's event and suppress its already queued result.
    // Legacy one-argument shared-event callers keep their original contract.
    struct ObserveOwner {};
    CancelToken(HANDLE externalEvent,ObserveOwner):CancelToken(){
        if(!externalEvent||externalEvent==INVALID_HANDLE_VALUE||
           !DuplicateHandle(GetCurrentProcess(),externalEvent,GetCurrentProcess(),&observedOwner_,
               SYNCHRONIZE,FALSE,0))
            throw std::runtime_error("mirror-cancel-event-duplicate-failed");
    }
    ~CancelToken(){if(observedOwner_)CloseHandle(observedOwner_);CloseHandle(event_);}
    CancelToken(const CancelToken&)=delete;
    HANDLE event() const{return event_;}
    HANDLE ownerEvent() const noexcept{return observedOwner_;}
    void cancel(){SetEvent(event_);}
    bool canceled() const{return WaitForSingleObject(event_,0)==WAIT_OBJECT_0||
        (observedOwner_&&WaitForSingleObject(observedOwner_,0)==WAIT_OBJECT_0);}
};
class CancelSignal {
    std::mutex mutex_;
    std::shared_ptr<CancelToken> token_;
public:
    void renew(){
        auto next=std::make_shared<CancelToken>();std::shared_ptr<CancelToken> previous;
        {std::lock_guard lock(mutex_);previous=std::move(token_);token_=std::move(next);}
        // A watcher may still own the previous generation. Dropping our shared
        // reference is not cancellation: wake its actual WinHTTP wait as well.
        if(previous)previous->cancel();
    }
    std::shared_ptr<CancelToken> current(){std::lock_guard lock(mutex_);return token_;}
    void cancel(){std::lock_guard lock(mutex_);if(token_)token_->cancel();}
};
struct AsyncState {
    std::mutex mutex;
    HANDLE changed=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    DWORD completed=0,error=0,length=0;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE kind=WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
    bool websocket=false;
    bool finalNotification=false;
    std::array<char,8192> buffer{};
    std::string outbound;
    std::shared_ptr<AsyncState> callbackLifetime;
    // Caller-owned connection/session are kept with this exact request state,
    // including after the operation stack returns a move-only cleanup debt.
    std::shared_ptr<void> parentLifetime;
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
        if(status==WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING){
            state->finalNotification=true;
            state->callbackLifetime.reset();
            SetEvent(state->changed);
            return; // Local keep owns state through this callback's last access.
        }
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
// Carry only the numeric result already returned by this operation. Preserve
// what() because recovery routing consumes the existing reason, not this DTO.
class AsyncFailure : public std::runtime_error {
public:
    const DWORD code;
    const ymcc::steamfailure::SourceStage stage;
    const ymcc::steamfailure::Domain domain;
    const bool codeKnown;
    AsyncFailure(const char* reason,DWORD value,ymcc::steamfailure::SourceStage source,
        ymcc::steamfailure::Domain kind=ymcc::steamfailure::Domain::WinHttp,bool known=true)
        :std::runtime_error(reason),code(value),stage(source),domain(kind),codeKnown(known){}
};
// The operation's original exception stays separate from its cleanup result.
// No second close is attempted; this failure ends the original child scope.
class AsyncRetirementFailure final : public std::exception {
public:
    const bool closeAccepted,finalNotification;
    const DWORD error;
    const std::exception_ptr original;
    AsyncRetirementFailure(bool accepted,bool final,DWORD code,std::exception_ptr cause={}) noexcept
        :closeAccepted(accepted),finalNotification(final),error(code),original(std::move(cause)){}
    const char* what() const noexcept override {return "mirror-bootstrap-cleanup-incomplete";}
};
inline nlohmann::json bootstrapOriginalTransportFailure(const std::exception& error){
    const auto* failure=dynamic_cast<const AsyncFailure*>(&error);
    if(!failure)return nlohmann::json{};
    auto result=ymcc::steamfailure::native(failure->domain,failure->code,failure->stage);
    if(!failure->codeKnown){result["codeKnown"]=false;result.erase("code");}
    return result;
}
inline nlohmann::json bootstrapTransportFailure(const std::exception& error){
    if(const auto* cleanup=dynamic_cast<const AsyncRetirementFailure*>(&error);cleanup&&cleanup->original){
        try{std::rethrow_exception(cleanup->original);}catch(const std::exception& cause){return bootstrapOriginalTransportFailure(cause);}catch(...){}
    }
    return bootstrapOriginalTransportFailure(error);
}
// A constructor can publish context before setup fails. Its callback state may
// remain alive without a final notification. The original one-shot binding
// child must end; continuing discovery could otherwise keep that debt forever.
// Fixed stage literals avoid a separate message-string allocation; normal C++
// exception machinery is unchanged. API stage reasons remain the same. This is
// Decky-only, not a new shared Steam policy.
class AsyncConstructionFailure final : public std::exception {
    const char* reason_;
public:
    explicit AsyncConstructionFailure(const char* reason) noexcept:reason_(reason){}
    const char* what() const noexcept override {return reason_;}
};
class AsyncRequest {
    HINTERNET handle_=nullptr;
    std::shared_ptr<AsyncState> state_;
    std::shared_ptr<CancelToken> cancellation_;
    ULONGLONG deadline_;
    bool retirementAttempted_=false;
    DWORD retirementError_=0;
    bool prepared_=false,preparedAttempted_=false,preparedCallbackPublished_=false;
    const char* preparedFailure_=nullptr;
    DWORD preparedError_=0;
    void prepare(){check();std::lock_guard lock(state_->mutex);state_->completed=0;state_->error=0;state_->length=0;ResetEvent(state_->changed);}
    void wait(DWORD expected,ymcc::steamfailure::SourceStage stage){
        for(;;){
            check();
            {std::lock_guard lock(state_->mutex);if(state_->error)throw AsyncFailure("mirror-bootstrap-async-error",state_->error,stage);if(state_->completed==expected)return;}
            const auto now=GetTickCount64();if(deadline_&&now>=deadline_)throw std::runtime_error("mirror-bootstrap-deadline");
            HANDLE handles[3]{cancellation_->event(),state_->changed,cancellation_->ownerEvent()};
            const auto result=WaitForMultipleObjects(cancellation_->ownerEvent()?3:2,handles,FALSE,deadline_?static_cast<DWORD>((std::min)(deadline_-now,static_cast<ULONGLONG>(INFINITE-1))):INFINITE);
            if(result==WAIT_OBJECT_0||result==WAIT_OBJECT_0+2)throw std::runtime_error("mirror-bootstrap-canceled");
            if(result==WAIT_TIMEOUT)throw std::runtime_error("mirror-bootstrap-deadline");
            if(result!=WAIT_OBJECT_0+1)throw std::runtime_error("mirror-bootstrap-wait-failed");
        }
    }
public:
    AsyncRequest(HINTERNET handle,std::shared_ptr<CancelToken> cancellation,ULONGLONG deadline,bool websocket=false,
        std::shared_ptr<void> parentLifetime={})
        :handle_(handle),cancellation_(std::move(cancellation)),deadline_(deadline){
        if(!handle_)throw std::runtime_error("mirror-bootstrap-request-failed");
        const char* constructionStage="mirror-bootstrap-request-failed";
        try{
            state_=std::make_shared<AsyncState>();state_->websocket=websocket;
            state_->parentLifetime=std::move(parentLifetime);
            // Prepare the callback's strong lease before publishing its address.
            // No allocation, throwing lock or other fallible step follows a
            // successful callback registration. Caller supplied a fresh handle.
            state_->callbackLifetime=state_;
            DWORD_PTR pointer=reinterpret_cast<DWORD_PTR>(state_.get());
            constructionStage="mirror-bootstrap-context-failed";
            if(!WinHttpSetOption(handle_,WINHTTP_OPTION_CONTEXT_VALUE,&pointer,sizeof(pointer)))throw std::runtime_error("mirror-bootstrap-context-failed");
            constructionStage="mirror-bootstrap-callback-failed";
            const auto prior=WinHttpSetStatusCallback(handle_,mirrorHttpCallback,WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS|WINHTTP_CALLBACK_FLAG_HANDLES,0);
            if(prior==WINHTTP_INVALID_STATUS_CALLBACK)throw std::runtime_error("mirror-bootstrap-callback-failed");
        }catch(...){
            // An upgraded socket can inherit the request callback even when
            // this registration fails. No new business IO was initiated, but
            // published context must survive that callback's real final return.
            WinHttpCloseHandle(handle_);handle_=nullptr;
            // Context publication may have succeeded before registration failed.
            // A fresh upgraded socket can inherit this callback from its request.
            // Keep the existing self lease through its actual final notification;
            // a failure without a final callback ends in the original owned child.
            // Do not import this failure scope into long-lived main transports.
            throw AsyncConstructionFailure(constructionStage);
        }
    }
    // Private new caller route. The caller publishes this complete object into
    // its existing local scope BEFORE it creates the raw WinHTTP request.
    struct Prepared {};
    AsyncRequest(Prepared,std::shared_ptr<CancelToken> cancellation,ULONGLONG deadline,
        bool websocket,std::shared_ptr<void> parents)
        :cancellation_(std::move(cancellation)),deadline_(deadline),prepared_(true){

        state_=std::make_shared<AsyncState>();state_->websocket=websocket;
        state_->parentLifetime=std::move(parents);
    }
    // A WebSocket inherits our known callback. Give CompleteUpgrade the NEW
    // context at creation; do not first publish an inherited parent's pointer.
    DWORD_PTR preparedUpgradeContext(){
        if(!prepared_||preparedAttempted_||handle_)throw std::runtime_error("document-request-state-invalid");
        state_->callbackLifetime=state_;
        return reinterpret_cast<DWORD_PTR>(state_.get());
    }
    void attachPrepared(HINTERNET handle,bool contextAtCreation=false){
        if(!prepared_||preparedAttempted_||retirementAttempted_)throw std::runtime_error("document-request-state-invalid");
        preparedAttempted_=true;handle_=handle;
        const auto fail=[&](const char* reason,DWORD error){
            preparedFailure_=reason;preparedError_=error?error:ERROR_GEN_FAILURE;
            // This exact fresh request has not published our pointer. Its
            // callback context is zero, even if callback registration succeeded.
            if(!preparedCallbackPublished_)state_->callbackLifetime.reset();
            throw AsyncConstructionFailure(reason);
        };
        if(!handle_)fail("mirror-bootstrap-request-failed",GetLastError());
        preparedCallbackPublished_=contextAtCreation;
        if(contextAtCreation){
            // This callback is already inherited from the verified upgrade
            // request. A refused redundant registration still leaves it owned.
            if(WinHttpSetStatusCallback(handle_,mirrorHttpCallback,
                WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS|WINHTTP_CALLBACK_FLAG_HANDLES,0)==WINHTTP_INVALID_STATUS_CALLBACK)
                fail("mirror-bootstrap-callback-failed",GetLastError());
        }else{
            if(WinHttpSetStatusCallback(handle_,mirrorHttpCallback,
                WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS|WINHTTP_CALLBACK_FLAG_HANDLES,0)==WINHTTP_INVALID_STATUS_CALLBACK)
                fail("mirror-bootstrap-callback-failed",GetLastError());
            state_->callbackLifetime=state_;
            DWORD_PTR pointer=reinterpret_cast<DWORD_PTR>(state_.get());
            if(!WinHttpSetOption(handle_,WINHTTP_OPTION_CONTEXT_VALUE,&pointer,sizeof(pointer)))
                fail("mirror-bootstrap-context-failed",GetLastError());
            preparedCallbackPublished_=true;
        }
    }
    const char* preparedFailure()const noexcept{return preparedFailure_;}
    DWORD preparedError()const noexcept{return preparedError_;}

    ~AsyncRequest(){
        if(handle_&&!retirementAttempted_){WinHttpCloseHandle(handle_);handle_=nullptr;}
        // An explicitly rejected close is caller-owned debt. The finite caller
        // must retain this object/parent scope; do not silently retry in dtor.
    }
    AsyncRequest(const AsyncRequest&)=delete;
    HINTERNET handle() const{return handle_;}
    // Original sequential owner calls this only after the prior operation has
    // completed, before transfer into the existing watcher thread.
    void retainParents(std::shared_ptr<void> parents) {
        check();std::lock_guard lock(state_->mutex);
        if(!parents)throw std::runtime_error("mirror-bootstrap-parents-unavailable");
        if(state_->parentLifetime&&state_->parentLifetime!=parents)
            throw std::runtime_error("mirror-bootstrap-parents-already-owned");
        state_->parentLifetime=std::move(parents);
    }
    struct Retirement {
        bool attempted=false,closeAccepted=false,finalNotification=false;
        DWORD error=0;
        bool noCallbackPublished=false;
        bool returned() const{return finalNotification||(noCallbackPublished&&closeAccepted);} // Exact last notification is stronger than an earlier failed close result.
    };
    // Private finite-caller extension, not yet used by the product callers.
    // Exactly one owner, after async initiation returns. The supplied cleanup
    // deadline is absolute and independent of the canceled business wait.
    Retirement retire(ULONGLONG cleanupDeadline) noexcept {
        Retirement receipt;
        if(!state_)return receipt;
        if(!retirementAttempted_){
            retirementAttempted_=true;
            if(handle_){
                if(WinHttpCloseHandle(handle_))handle_=nullptr;
                else{const auto error=GetLastError();retirementError_=error?error:ERROR_GEN_FAILURE;}
            }
        }
        receipt.attempted=retirementAttempted_;receipt.closeAccepted=handle_==nullptr;receipt.error=retirementError_;
        if(prepared_&&!preparedCallbackPublished_){
            receipt.noCallbackPublished=true;return receipt;
        }
        // A previously failed close is never retried. Its original context may
        // still receive a real final notification; retain the initial error.
        if(!receipt.closeAccepted){
            std::lock_guard lock(state_->mutex);
            receipt.finalNotification=state_->finalNotification;
            return receipt;
        }
        for(;;){
            {
                std::lock_guard lock(state_->mutex);
                if(state_->finalNotification){receipt.finalNotification=true;return receipt;}
            }
            const auto now=GetTickCount64();
            if(!cleanupDeadline||now>=cleanupDeadline){receipt.error=WAIT_TIMEOUT;return receipt;}
            const auto delay=static_cast<DWORD>((std::min)(cleanupDeadline-now,static_cast<ULONGLONG>(INFINITE-1)));
            const auto wait=WaitForSingleObject(state_->changed,delay);
            if(wait==WAIT_OBJECT_0)continue;
            receipt.error=wait==WAIT_TIMEOUT?WAIT_TIMEOUT:GetLastError();return receipt;
        }
    }
    // Zero is an event/cancel-only idle wait; command/reply IO always has a finite budget.
    // Called only by the current sequential owner with no operation outstanding.
    void setDeadline(ULONGLONG value){deadline_=value;}
    std::weak_ptr<AsyncState> callbackState() const{return state_;} // Read-only verification/diagnostic, no extra owner.
    void check() const{
        if(prepared_&&(!preparedAttempted_||preparedFailure_))throw std::runtime_error("document-request-state-invalid");
        if(retirementAttempted_)throw std::runtime_error("mirror-bootstrap-retired");
        if(!cancellation_||cancellation_->canceled())throw std::runtime_error("mirror-bootstrap-canceled");
        if(deadline_&&GetTickCount64()>=deadline_)throw std::runtime_error("mirror-bootstrap-deadline");
    }
    void handshake(bool upgrade=false){
        if(upgrade&&!WinHttpSetOption(handle_,WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,nullptr,0))throw std::runtime_error("mirror-bootstrap-upgrade-failed");
        DWORD flags=WINHTTP_DISABLE_REDIRECTS;if(!WinHttpSetOption(handle_,WINHTTP_OPTION_DISABLE_FEATURE,&flags,sizeof(flags)))throw std::runtime_error("mirror-bootstrap-options-failed");
        prepare();
        if(!WinHttpSendRequest(handle_,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,reinterpret_cast<DWORD_PTR>(state_.get())))throw std::runtime_error("mirror-bootstrap-send-failed");
        wait(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE,ymcc::steamfailure::SourceStage::Send);
        prepare();if(!WinHttpReceiveResponse(handle_,nullptr))throw std::runtime_error("mirror-bootstrap-response-failed");wait(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE,upgrade?ymcc::steamfailure::SourceStage::Upgrade:ymcc::steamfailure::SourceStage::Discovery);
    }
    DWORD status(){check();DWORD code=0,size=sizeof(code);if(!WinHttpQueryHeaders(handle_,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&code,&size,nullptr))throw std::runtime_error("mirror-bootstrap-status-failed");return code;}
    std::string httpBody(size_t maximum){
        std::string body;
        for(;;){prepare();if(!WinHttpReadData(handle_,state_->buffer.data(),static_cast<DWORD>(state_->buffer.size()),nullptr))throw std::runtime_error("mirror-bootstrap-read-failed");wait(WINHTTP_CALLBACK_STATUS_READ_COMPLETE,ymcc::steamfailure::SourceStage::Receive);
            DWORD length;{std::lock_guard lock(state_->mutex);length=state_->length;}
            if(length>state_->buffer.size())throw std::runtime_error("mirror-bootstrap-buffer-invalid");
            if(!length)return body;body.append(state_->buffer.data(),length);if(body.size()>maximum)throw std::runtime_error("mirror-bootstrap-body-limit");}
    }
    void sendText(const std::string& text){
        prepare();state_->outbound=text;
        const auto error=WinHttpWebSocketSend(handle_,WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,state_->outbound.data(),static_cast<DWORD>(state_->outbound.size()));
        if(error!=NO_ERROR&&error!=ERROR_IO_PENDING)throw AsyncFailure("mirror-bootstrap-ws-send-failed",error,ymcc::steamfailure::SourceStage::Send);wait(WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE,ymcc::steamfailure::SourceStage::Send);
    }

    struct DocumentFragment {std::string text;WINHTTP_WEB_SOCKET_BUFFER_TYPE kind;};
    DocumentFragment documentFragment(){
        prepare();
        const auto error=WinHttpWebSocketReceive(handle_,state_->buffer.data(),static_cast<DWORD>(state_->buffer.size()),nullptr,nullptr);
        if(error!=NO_ERROR&&error!=ERROR_IO_PENDING)throw AsyncFailure("mirror-bootstrap-ws-receive-failed",error,ymcc::steamfailure::SourceStage::Receive);
        wait(WINHTTP_CALLBACK_STATUS_READ_COMPLETE,ymcc::steamfailure::SourceStage::Receive);
        std::lock_guard lock(state_->mutex);
        if(state_->length>state_->buffer.size())throw std::runtime_error("document-reply-invalid");
        return {std::string(state_->buffer.data(),state_->length),state_->kind};
    }
    std::string receiveText(size_t maximum){
        std::string text;
        for(;;){prepare();
            const auto error=WinHttpWebSocketReceive(handle_,state_->buffer.data(),static_cast<DWORD>(state_->buffer.size()),nullptr,nullptr);
            if(error!=NO_ERROR&&error!=ERROR_IO_PENDING)throw AsyncFailure("mirror-bootstrap-ws-receive-failed",error,ymcc::steamfailure::SourceStage::Receive);wait(WINHTTP_CALLBACK_STATUS_READ_COMPLETE,ymcc::steamfailure::SourceStage::Receive);
            DWORD length;WINHTTP_WEB_SOCKET_BUFFER_TYPE kind;
            {std::lock_guard lock(state_->mutex);length=state_->length;kind=state_->kind;}
            if(kind==WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE){
                // The receive has completed on this sequential owner. This
                // queries its existing close receipt, never probes the peer.
                // The API permits at most 123 reason bytes; none enter logs.
                USHORT status=0;DWORD received=0;std::array<char,123> reason{};
                const bool known=WinHttpWebSocketQueryCloseStatus(handle_,&status,reason.data(),static_cast<DWORD>(reason.size()),&received)==NO_ERROR;
                throw AsyncFailure("mirror-bootstrap-ws-closed",known?status:0,
                    ymcc::steamfailure::SourceStage::Receive,ymcc::steamfailure::Domain::WebSocketClose,known);
            }
            if(length>state_->buffer.size()||kind!=WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE&&kind!=WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)throw std::runtime_error("mirror-bootstrap-ws-invalid");
            text.append(state_->buffer.data(),length);if(text.size()>maximum)throw std::runtime_error("mirror-bootstrap-reply-limit");
            if(kind==WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)return text;
        }
    }
};
} // namespace ymcc::deckymirror
