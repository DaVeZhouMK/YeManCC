#pragma once
#include <atomic>
#include <cstdint>
#include <utility>
#include "keyboard_modifier_snapshot.h"

// Per-command diagnostics only. No input injection, timers, hooks, device reads,
// shared SendInput owner, or waiting across a producer call.
namespace ymcc::taskViewEvidence {
using Json=nlohmann::json;
struct Context { bool active=false; std::uint64_t sequence=0; const char* command="unknown"; };
class Recorder {
    std::atomic<std::uint64_t> sequence_{0},failures_{0};
    static bool knownEntry(const Json& args) {
        if(!args.is_object())return false;
        const auto entry=args.find("diagnosticAction");
        return entry!=args.end()&&entry->is_string()&&
            entry->get_ref<const std::string&>()=="game-recognition-switch-program";
    }
    static Json caller(const Json& args) {
        Json result={{"entry","unobserved"},{"token",nullptr},{"tokenStatus","absent"},
            {"identityScope","caller-provided-correlation-only"},{"humanGestureProven",false}};
        if(!args.is_object()){result["tokenStatus"]="arguments-not-object";return result;}
        const auto entry=args.find("diagnosticAction");
        if(entry==args.end()||!entry->is_string()||entry->get_ref<const std::string&>()!="game-recognition-switch-program"){
            result["tokenStatus"]="entry-unrecognized";return result;
        }
        result["entry"]="game-recognition-switch-program";
        // A synchronous engine marker distinguishes our controller click from
        // a DOM synthetic click. These are caller observations, never authority
        // to change foreground/input policy or proof of a human gesture.
        result["activation"]="unknown";
        result["eventTrusted"]=nullptr;
        result["clickDetail"]=nullptr;
        const auto activation=args.find("diagnosticActivation");
        if(activation!=args.end()&&activation->is_string()){
            const auto& value=activation->get_ref<const std::string&>();
            for(const char* allowed:{"gamepad-engine","trusted-touch","trusted-pen","trusted-mouse","trusted-other","scripted","unknown"})
                if(value==allowed){result["activation"]=value;break;}
        }
        const auto trusted=args.find("diagnosticEventTrusted");
        if(trusted!=args.end()&&trusted->is_boolean())result["eventTrusted"]=*trusted;
        const auto detail=args.find("diagnosticClickDetail");
        if(detail!=args.end()&&detail->is_number_integer()){
            if(detail->is_number_unsigned()){
                const auto value=detail->get<Json::number_unsigned_t>();
                if(value<=3)result["clickDetail"]=value;
            }else{
                const auto value=detail->get<Json::number_integer_t>();
                if(value>=0&&value<=3)result["clickDetail"]=value;
            }
        }
        const auto token=args.find("diagnosticActionId");
        if(token==args.end())return result;
        if(!token->is_string()){result["tokenStatus"]="token-not-string";return result;}
        const auto& text=token->get_ref<const std::string&>();
        if(text.empty()||text.size()>80){result["tokenStatus"]="token-length-invalid";return result;}
        for(unsigned char c:text)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c==':'||c=='-'||c=='_'||c=='.')){
            result["tokenStatus"]="token-characters-invalid";return result;
        }
        result["token"]=text;result["tokenStatus"]="retained";return result;
    }
    template<class Sink> void emit(const Json& args,Context c,const char* phase,const char* outcome,
            DWORD errorHint,bool postKnown,bool postAccepted,Sink&& sink) noexcept {
        const DWORD savedError=GetLastError();
        try {
            const auto snapshot=keyboardEvidence::read();
            Json row={{"schemaVersion",1},{"nativeRequestSequence",c.sequence},{"command",c.command},
                {"phase",phase},{"operationOutcome",outcome},{"caller",caller(args)},
                {"keyboardModifiers",keyboardEvidence::toJson(snapshot)},
                {"win32ErrorHint",errorHint},{"errorHintIsDefinitive",false},
                {"postAccepted",postKnown?Json(postAccepted):Json(nullptr)},
                {"taskViewVisibleProven",false},{"minimizeCompletedProven",false},
                {"downstreamKeyboardReceiptProven",false},{"diagnosticFailures",failures_.load(std::memory_order_relaxed)},
                {"diagnosticFailureScope","caught-by-this-recorder-only"},{"logSinkReturnIsDurableReceipt",false},
                {"scope","Single command boundary observations only; snapshots are sequential, caller token is not identity, and command return/post is not UI completion or keyboard consumption."}};
            std::forward<Sink>(sink)(std::move(row));
        } catch(...) {failures_.fetch_add(1,std::memory_order_relaxed);}
        SetLastError(savedError);
    }
public:
    template<class Sink> Context begin(bool enabled,const Json& args,const char* command,Sink&& sink,bool callerRequired=false) noexcept {
        if(!enabled)return {};
        if(callerRequired){
            const DWORD savedError=GetLastError();bool eligible=false;
            try {eligible=knownEntry(args);}catch(...){failures_.fetch_add(1,std::memory_order_relaxed);}
            SetLastError(savedError);if(!eligible)return {};
        }
        Context c{true,sequence_.fetch_add(1,std::memory_order_relaxed)+1,command};
        emit(args,c,"before-call","not-issued",0,false,false,std::forward<Sink>(sink));return c;
    }
    template<class Sink> void end(bool enabled,const Json& args,Context c,const char* outcome,
            DWORD errorHint,bool postKnown,bool postAccepted,Sink&& sink) noexcept {
        if(!enabled||!c.active)return;
        emit(args,c,"after-call",outcome,errorHint,postKnown,postAccepted,std::forward<Sink>(sink));
    }
    std::uint64_t failures()const noexcept{return failures_.load(std::memory_order_relaxed);}
};
} // namespace ymcc::taskViewEvidence
