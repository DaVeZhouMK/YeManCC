#pragma once

#include <WebView2.h>
#include <wrl.h>
#include <json.hpp>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace ymcc::webview::wallpaperWitness {
using Json = nlohmann::json;
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

inline bool assetId(std::wstring_view value) {
    if(value.size()!=32)return false;
    for(const auto c:value)if(!((c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f')))return false;
    return true;
}
inline bool mainDocument(std::wstring_view source) {
    return source==L"https://app.localhost/index.html" || source.starts_with(L"https://app.localhost/index.html#");
}
inline std::string_view text(const Json& row,const char* key) {
    if(!row.is_object())return {};
    const auto it=row.find(key);
    return it!=row.end()&&it->is_string()?std::string_view(it->get_ref<const std::string&>()):std::string_view{};
}
inline std::string ascii(std::wstring_view value) {
    std::string result;
    result.reserve(value.size());
    for(const auto c:value)result.push_back(c<=127?static_cast<char>(c):'?');
    return result;
}

class Evidence {
    struct Decoder { std::string name,platform; bool changed=false; };
    std::string url_;
    std::set<std::string> matches_;
    std::set<std::string> retired_;
    std::map<std::string,Decoder> decoders_;
    bool saturated_=false;
public:
    explicit Evidence(std::wstring_view id) {
        const auto key=ascii(id);
        url_="https://game-media.localhost/video/"+key+".mp4?v="+key;
    }
    const std::string& url()const{return url_;}
    void add(const Json& row,bool properties) {
        if(!row.is_object())return;
        const auto player=text(row,"playerId");
        if(player.empty()||player.size()>128)return;
        const std::string id(player);
        if(retired_.contains(id))return;
        const auto items=row.find(properties?"properties":"events");
        if(items==row.end()||!items->is_array())return;
        if(properties){
            for(const auto& item:*items){
                const auto key=text(item,"name");
                if(key!="kVideoDecoderName"&&key!="kIsPlatformVideoDecoder")continue;
                const auto value=text(item,"value");
                if(value.empty()||value.size()>64)continue;
                auto it=decoders_.find(id);
                if(it==decoders_.end()){
                    if(decoders_.size()>=32){saturated_=true;return;}
                    it=decoders_.try_emplace(id).first;
                }
                auto& field=key=="kVideoDecoderName"?it->second.name:it->second.platform;
                if(!field.empty()&&field!=value)it->second.changed=true;
                field=value;
            }
        }else{
            for(const auto& item:*items){
                const auto value=text(item,"value");
                if(value.empty())continue;
                const auto event=Json::parse(value,nullptr,false);
                // Media.enable can replay a destroyed player and send older events after destruction.
                if(text(event,"event")=="kWebMediaPlayerDestroyed"){
                    if(retired_.size()>=128)saturated_=true;
                    else retired_.insert(id);
                    decoders_.erase(id);
                    return;
                }
                if(text(event,"url")==url_){
                    if(matches_.contains(id))continue;
                    if(matches_.size()>=32){saturated_=true;return;}
                    matches_.insert(id);
                }
            }
        }
    }
    Json result(const Json& sample)const {
        const std::string* match=nullptr;
        size_t matching=0,retiredMatching=0;
        for(const auto& id:matches_){
            if(retired_.contains(id)){++retiredMatching;continue;}
            match=&id;++matching;
        }
        Json out={{"status","sample-interrupted"},{"matchingPlayers",matching},{"retiredMatchingPlayers",retiredMatching}};
        if(text(sample,"url")!=url_)return out;
        const auto number=[&](const char* key,bool integer=false){
            const auto it=sample.find(key);
            return it!=sample.end()&&(integer?it->is_number_integer():it->is_number())?it->get<double>():-1.0;
        };
        const auto fps=number("fps"),count=number("frames",true),drops=number("steadyDrops",true);
        const auto sampleDrops=number("sampleDrops",true),width=number("width",true),height=number("height",true);
        if(!(fps>0&&fps<1000)||count<1||count>500||drops<0||drops>500||sampleDrops<0||sampleDrops>500||
            width<1||height<1||width>8192||height>8192)
            return out;
        out.update(Json{{"fps",fps},{"frames",static_cast<int>(count)},{"steadyDrops",static_cast<int>(drops)},
            {"sampleDrops",static_cast<int>(sampleDrops)},{"width",static_cast<int>(width)},{"height",static_cast<int>(height)}});
        if(saturated_||matching!=1){out["status"]="decoder-not-unique";return out;}
        const auto it=decoders_.find(*match);
        if(it==decoders_.end()||it->second.name.empty()||(it->second.platform!="true"&&it->second.platform!="false")){
            out["status"]="decoder-missing";return out;
        }
        const auto& decoder=it->second;
        if(decoder.changed){out["status"]="decoder-changed";return out;}
        const bool hardware=decoder.name=="D3D11VideoDecoder"&&decoder.platform=="true";
        out.update(Json{{"decoder",decoder.name},{"platformDecoder",decoder.platform=="true"},
            {"d3d11",hardware},{"status",hardware&&fps>=29&&fps<=31&&count>=60&&drops==0&&sampleDrops==0?"verified":"measured"}});
        return out;
    }
};

inline bool receiptMessage(const Json& data) {
    if(!data.is_object())return false;
    const auto marker=data.find("ymccWallpaperWitness");
    return marker!=data.end()&&marker->is_number_integer()&&*marker==1&&data.contains("sample");
}

// The script observes the existing video without changing playback. Its own
// deadline cleans up after an abandoned native request or a broken renderer.
inline std::wstring script(const std::string& url) {
    const auto literal=Json(url).dump();
    return L"(() => {const expected="+std::wstring(literal.begin(),literal.end())+LR"JS(;
      if(location.origin!=='https://app.localhost'||location.pathname!=='/index.html')return;
      const selector='video.user-background-video';
      let picked=null,phase,request,done=false,changed=false;
      const events=['pause','seeking','waiting','emptied','error'];
      const disturb=()=>{changed=true;};
      const visible=()=>{if(picked)disturb();else choose();};
      const stop=()=>{
        if(done)return;done=true;clearTimeout(deadline);clearTimeout(phase);
        document.removeEventListener('playing',choose,true);
        document.removeEventListener('visibilitychange',visible);
        if(picked){
          for(const event of events)picked.removeEventListener(event,disturb);
          if(request!==undefined)picked.cancelVideoFrameCallback?.(request);
        }
      };
      const emit=sample=>{stop();chrome.webview.postMessage({ymccWallpaperWitness:1,sample});};
      const ready=()=>!done&&!changed&&!document.hidden&&picked?.isConnected&&
        document.querySelectorAll(selector).length===1&&document.querySelector(selector)===picked&&
        picked.currentSrc===expected&&!picked.paused&&!picked.seeking&&picked.readyState>=3&&
        picked.getBoundingClientRect().width>0&&getComputedStyle(picked).visibility==='visible';
      function choose(event){
        const candidate=event?.target||document.querySelector(selector);
        if(done||picked||!candidate?.matches?.(selector)||candidate.currentSrc!==expected||candidate.paused||document.hidden)return;
        picked=candidate;
        if(!picked.requestVideoFrameCallback||!picked.getVideoPlaybackQuality){emit({});return;}
        phase=setTimeout(()=>{
          if(!ready()){emit({});return;}
          for(const type of events)picked.addEventListener(type,disturb);
          const firstQuality=picked.getVideoPlaybackQuality();
          phase=setTimeout(()=>{
            if(!ready()){emit({});return;}
            const sampleStart=picked.getVideoPlaybackQuality().droppedVideoFrames;
            let first,last;
            function next(now,metadata){
              if(!ready())return;
              last=[metadata.presentationTime??now,metadata.presentedFrames];first??=last;
              request=picked.requestVideoFrameCallback(next);
            }
            request=picked.requestVideoFrameCallback(next);
            phase=setTimeout(()=>{
              if(!ready()||!first||!last||last[0]<=first[0]){emit({});return;}
              const frames=last[1]-first[1];
              emit({url:expected,fps:1000*frames/(last[0]-first[0]),frames,
                steadyDrops:sampleStart-firstQuality.droppedVideoFrames,
                sampleDrops:picked.getVideoPlaybackQuality().droppedVideoFrames-sampleStart,
                width:picked.videoWidth,height:picked.videoHeight});
            },2650);
          },2400);
        },600);
      }
      const deadline=setTimeout(stop,90000);
      document.addEventListener('playing',choose,true);
      document.addEventListener('visibilitychange',visible);
      choose();
    })())JS";
}

class Session : public std::enable_shared_from_this<Session> {
    Evidence evidence_;
    ComPtr<ICoreWebView2> view_;
    std::array<ComPtr<ICoreWebView2DevToolsProtocolEventReceiver>,2> receivers_;
    std::array<EventRegistrationToken,2> tokens_{};
    std::array<bool,2> subscribed_{};
    std::function<void(Json)> receipt_;
    ComPtr<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler> ignoreProtocolResult_;
    bool mediaRequested_=false,active_=true,armed_=false;

    static std::wstring_view read(PCWSTR raw){return raw?std::wstring_view(raw):std::wstring_view{};}
    static Json parseBounded(PCWSTR raw,size_t max) {
        const auto value=read(raw);
        if(value.empty()||value.size()>max)return {};
        // The known media host, player IDs, property names, and report data are ASCII.
        return Json::parse(ascii(value),nullptr,false);
    }
    struct ComString { LPWSTR value=nullptr; ~ComString(){CoTaskMemFree(value);} };
    void finish(Json result) {
        if(!active_)return;
        close();
        try{receipt_(std::move(result));}catch(...){ }
    }
public:
    Session(ICoreWebView2* view,std::wstring_view id,std::function<void(Json)> receipt):
        evidence_(id),view_(view),receipt_(std::move(receipt)){}
    ~Session(){close();}
    bool active()const{return active_;}
    void close()noexcept {
        if(!active_)return;
        active_=false;
        for(size_t i=0;i<receivers_.size();++i){
            if(subscribed_[i])receivers_[i]->remove_DevToolsProtocolEventReceived(tokens_[i]);
            receivers_[i].Reset();
        }
        if(mediaRequested_&&view_)view_->CallDevToolsProtocolMethod(L"Media.disable",L"{}",ignoreProtocolResult_.Get());
        view_.Reset();
    }
    void timeout(){finish({{"status","timeout"}});}
    bool start(){
        constexpr std::array names{L"Media.playerPropertiesChanged",L"Media.playerEventsAdded"};
        if(!active_||!view_)return false;
        ignoreProtocolResult_=Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>([](HRESULT,PCWSTR)->HRESULT{return S_OK;});
        if(!ignoreProtocolResult_)return false;
        for(size_t i=0;i<names.size();++i){
            if(FAILED(view_->GetDevToolsProtocolEventReceiver(names[i],&receivers_[i]))||!receivers_[i])return false;
            const auto handler=Callback<ICoreWebView2DevToolsProtocolEventReceivedEventHandler>(
                [owner=weak_from_this(),i](ICoreWebView2*,ICoreWebView2DevToolsProtocolEventReceivedEventArgs* args)->HRESULT{
                    if(const auto self=owner.lock();self&&self->active_&&args){
                        ComString raw;
                        if(SUCCEEDED(args->get_ParameterObjectAsJson(&raw.value)))try{
                            self->evidence_.add(parseBounded(raw.value,65536),i==0);
                        }catch(...){ }
                    }
                    return S_OK;
                });
            if(!handler||FAILED(receivers_[i]->add_DevToolsProtocolEventReceived(handler.Get(),&tokens_[i])))return false;
            subscribed_[i]=true;
        }
        const auto complete=Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>([owner=weak_from_this()](HRESULT hr,PCWSTR)->HRESULT{
            if(const auto self=owner.lock();self&&FAILED(hr))try{
                self->finish({{"status","media-enable-failed"}});
            }catch(...){self->close();}
            return S_OK;
        });
        mediaRequested_=complete&&SUCCEEDED(view_->CallDevToolsProtocolMethod(L"Media.enable",L"{}",complete.Get()));return mediaRequested_;
    }
    void arm(ICoreWebView2* sender){
        if(!active_||armed_||sender!=view_.Get())return;
        ComString source;if(FAILED(view_->get_Source(&source.value))||!mainDocument(read(source.value)))return;
        armed_=true;
        const auto done=Callback<ICoreWebView2ExecuteScriptCompletedHandler>([owner=weak_from_this()](HRESULT hr,PCWSTR)->HRESULT{
            if(const auto self=owner.lock();self&&FAILED(hr))try{
                self->finish({{"status","script-failed"}});
            }catch(...){self->close();}
            return S_OK;
        });
        if(!done||FAILED(view_->ExecuteScript(script(evidence_.url()).c_str(),done.Get())))finish({{"status","script-failed"}});
    }
    bool message(ICoreWebView2* sender,ICoreWebView2WebMessageReceivedEventArgs* args,PCWSTR raw){
        if(!active_||!armed_||sender!=view_.Get()||!args)return false;
        const auto data=parseBounded(raw,4096);
        if(!receiptMessage(data))return false;
        ComString source;
        if(FAILED(args->get_Source(&source.value))||!mainDocument(read(source.value)))return false;
        finish(evidence_.result(data["sample"]));return true;
    }
};
} // namespace ymcc::webview::wallpaperWitness
