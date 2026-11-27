#pragma once
#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <codecapi.h>
#include <wrl/client.h>
#include <bit>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include "shared_game_media_frame_probe.h"

#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"mfuuid.lib")
#pragma comment(lib,"ole32.lib")

namespace ymcc::media::hardwareVideo {
using Microsoft::WRL::ComPtr;

inline void must(HRESULT hr,const char* action) {
    if(SUCCEEDED(hr))return;
    throw std::runtime_error(std::string(action)+":"+std::to_string(static_cast<unsigned long>(hr)));
}

// Media Foundation is optional on Windows N. Do not make the library host
// depend on its DLLs at process startup.
class SystemModule {
    HMODULE handle_;
public:
    explicit SystemModule(const wchar_t* name):handle_(LoadLibraryExW(name,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        if(!handle_)throw std::runtime_error("media-hardware-foundation-unavailable");
    }
    ~SystemModule(){FreeLibrary(handle_);}
    SystemModule(const SystemModule&)=delete;
    SystemModule& operator=(const SystemModule&)=delete;
    template<class Fn> Fn get(const char* name)const {
        const auto address=GetProcAddress(handle_,name);
        if(!address)throw std::runtime_error("media-hardware-foundation-unavailable");
        return std::bit_cast<Fn>(address);
    }
};

class Foundation {
    SystemModule platform_{L"mfplat.dll"}, io_{L"mfreadwrite.dll"};
    decltype(&MFShutdown) shutdown_=platform_.get<decltype(&MFShutdown)>("MFShutdown");
public:
    const decltype(&MFCreateMediaType) createType=platform_.get<decltype(&MFCreateMediaType)>("MFCreateMediaType");
    const decltype(&MFCreateAttributes) createAttributes=platform_.get<decltype(&MFCreateAttributes)>("MFCreateAttributes");
    const decltype(&MFCreateDXGIDeviceManager) createManager=platform_.get<decltype(&MFCreateDXGIDeviceManager)>("MFCreateDXGIDeviceManager");
    const decltype(&MFCreateDXGISurfaceBuffer) createSurfaceBuffer=platform_.get<decltype(&MFCreateDXGISurfaceBuffer)>("MFCreateDXGISurfaceBuffer");
    const decltype(&MFCreateSample) createSample=platform_.get<decltype(&MFCreateSample)>("MFCreateSample");
    const decltype(&MFCreateSourceReaderFromURL) createReader=io_.get<decltype(&MFCreateSourceReaderFromURL)>("MFCreateSourceReaderFromURL");
    const decltype(&MFCreateSinkWriterFromURL) createWriter=io_.get<decltype(&MFCreateSinkWriterFromURL)>("MFCreateSinkWriterFromURL");
    Foundation(){must(platform_.get<decltype(&MFStartup)>("MFStartup")(MF_VERSION,MFSTARTUP_FULL),"MFStartup");}
    ~Foundation(){shutdown_();}
};

constexpr DWORD video=static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
constexpr LONGLONG second=10'000'000;
constexpr LONGLONG timestamp(LONGLONG index) {return index*second/30;}

inline ComPtr<IMFMediaType> videoType(const Foundation& mf,GUID format) {
    ComPtr<IMFMediaType> t;
    must(mf.createType(&t),"create type");
    must(t->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video),"video type");
    must(t->SetGUID(MF_MT_SUBTYPE,format),"subtype");
    return t;
}

struct Dimensions { UINT width,height; };
inline RECT visibleFrame(IMFMediaType* decoded,IMFMediaType* compressed,Dimensions coded) {
    for(const auto& key:{MF_MT_GEOMETRIC_APERTURE,MF_MT_MINIMUM_DISPLAY_APERTURE}) {
        for(auto* type:{decoded,compressed}) {
            MFVideoArea area{};UINT32 size=0;
            if(FAILED(type->GetBlob(key,reinterpret_cast<UINT8*>(&area),sizeof(area),&size)))continue;
            const LONG left=area.OffsetX.value,top=area.OffsetY.value;
            if(size!=sizeof(area)||area.OffsetX.fract||area.OffsetY.fract||left<0||top<0||
               (left&1)||(top&1)||area.Area.cx<=0||area.Area.cy<=0||
               uint64_t(left)+area.Area.cx>coded.width||uint64_t(top)+area.Area.cy>coded.height)
                throw std::runtime_error("media-hardware-source-aperture-unsupported");
            return {left,top,left+area.Area.cx,top+area.Area.cy};
        }
    }
    UINT32 width=0,height=0;
    if(SUCCEEDED(MFGetAttributeSize(compressed,MF_MT_FRAME_SIZE,&width,&height))&&
       width&&height&&width<=coded.width&&height<=coded.height&&coded.width-width<16&&coded.height-height<16)
        return {0,0,static_cast<LONG>(width),static_cast<LONG>(height)};
    return {0,0,static_cast<LONG>(coded.width),static_cast<LONG>(coded.height)};
}

inline Dimensions fit720(UINT width,UINT height) {
    if(width<64||height<64||width>4096||height>2160||(width&1u)||(height&1u))
        throw std::runtime_error("media-hardware-source-size-unsupported");
    // Treat 720p as a ceiling; do not enlarge a low-resolution source.
    if(width<=1280&&height<=720)return {width,height};
    const bool widthLimited=uint64_t(width)*720>uint64_t(height)*1280;
    // NV12 requires both axes to be even. Keep the display ratio instead of
    // stretching a tall or ultrawide game trailer into 16:9.
    const auto widthOut=widthLimited?1280u:((width*360+height/2)/height)*2;
    const auto heightOut=widthLimited?((height*640+width/2)/width)*2:720u;
    if(widthOut<64||heightOut<64)throw std::runtime_error("media-hardware-output-size-unsupported");
    return {widthOut,heightOut};
}

inline ComPtr<IMFMediaType> playbackType(const Foundation& mf,GUID format,Dimensions size) {
    auto t=videoType(mf,format);
    must(MFSetAttributeSize(t.Get(),MF_MT_FRAME_SIZE,size.width,size.height),"size");
    must(MFSetAttributeRatio(t.Get(),MF_MT_FRAME_RATE,30,1),"rate");
    must(MFSetAttributeRatio(t.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1),"pixel aspect");
    must(t->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive),"progressive");
    return t;
}

inline ComPtr<IMFMediaBuffer> surfaceBuffer(const Foundation& mf,ID3D11Texture2D* texture) {
    ComPtr<IMFMediaBuffer> buffer;
    must(mf.createSurfaceBuffer(__uuidof(ID3D11Texture2D),texture,0,FALSE,&buffer),"GPU sample buffer");
    ComPtr<IMF2DBuffer> surface;
    must(buffer.As(&surface),"GPU 2D buffer");
    DWORD length=0;
    must(surface->GetContiguousLength(&length),"GPU buffer length");
    must(buffer->SetCurrentLength(length),"GPU buffer size");
    return buffer;
}

class Scaler {
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11VideoDevice> video_;
    ComPtr<ID3D11VideoContext> context_;
    ComPtr<ID3D11VideoProcessorEnumerator> modes_;
    ComPtr<ID3D11VideoProcessor> processor_;
    Dimensions out_;
public:
    Scaler(ID3D11Device* device,Dimensions in,Dimensions out,const RECT& source,IMFMediaType* raw):device_(device),out_(out) {
        must(device_.As(&video_),"video processor device");
        ComPtr<ID3D11DeviceContext> graphics;
        device_->GetImmediateContext(&graphics);
        must(graphics.As(&context_),"video processor context");
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
        content.InputFrameFormat=D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        content.InputFrameRate={30,1};content.OutputFrameRate={30,1};
        content.InputWidth=in.width;content.InputHeight=in.height;
        content.OutputWidth=out.width;content.OutputHeight=out.height;
        content.Usage=D3D11_VIDEO_USAGE_OPTIMAL_SPEED;
        must(video_->CreateVideoProcessorEnumerator(&content,&modes_),"video scaling mode");
        UINT formats=0;
        must(modes_->CheckVideoProcessorFormat(DXGI_FORMAT_NV12,&formats),"video scaling surface support");
        if(!(formats&D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT)||!(formats&D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT))
            throw std::runtime_error("media-hardware-scaling-unavailable");
        must(video_->CreateVideoProcessor(modes_.Get(),0,&processor_),"video scaler");
        context_->VideoProcessorSetStreamAutoProcessingMode(processor_.Get(),0,FALSE);
        const RECT target{0,0,static_cast<LONG>(out.width),static_cast<LONG>(out.height)};
        context_->VideoProcessorSetStreamSourceRect(processor_.Get(),0,TRUE,&source);
        context_->VideoProcessorSetStreamDestRect(processor_.Get(),0,TRUE,&target);
        UINT32 matrix=MFVideoTransferMatrix_Unknown,range=MFNominalRange_Unknown;
        raw->GetUINT32(MF_MT_YUV_MATRIX,&matrix);raw->GetUINT32(MF_MT_VIDEO_NOMINAL_RANGE,&range);
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE color{};
        color.YCbCr_Matrix=matrix!=MFVideoTransferMatrix_BT601;
        color.Nominal_Range=range==MFNominalRange_0_255?D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255:D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
        context_->VideoProcessorSetStreamColorSpace(processor_.Get(),0,&color);
        context_->VideoProcessorSetOutputColorSpace(processor_.Get(),&color);
    }
    ComPtr<IMFSample> scale(const Foundation& mf,ID3D11Texture2D* texture,UINT slice)const {
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input{};
        input.ViewDimension=D3D11_VPIV_DIMENSION_TEXTURE2D;
        input.Texture2D.ArraySlice=slice;
        ComPtr<ID3D11VideoProcessorInputView> view;
        must(video_->CreateVideoProcessorInputView(texture,modes_.Get(),&input,&view),"video scaling input view");
        D3D11_TEXTURE2D_DESC textureDesc{};
        textureDesc.Width=out_.width;textureDesc.Height=out_.height;
        textureDesc.MipLevels=1;textureDesc.ArraySize=1;textureDesc.Format=DXGI_FORMAT_NV12;
        textureDesc.SampleDesc.Count=1;textureDesc.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> scaled;
        must(device_->CreateTexture2D(&textureDesc,nullptr,&scaled),"video scaling destination");
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output{};
        output.ViewDimension=D3D11_VPOV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11VideoProcessorOutputView> sink;
        must(video_->CreateVideoProcessorOutputView(scaled.Get(),modes_.Get(),&output,&sink),"video scaling output view");
        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable=TRUE;stream.pInputSurface=view.Get();
        must(context_->VideoProcessorBlt(processor_.Get(),sink.Get(),0,1,&stream),"hardware video scaling");
        auto buffer=surfaceBuffer(mf,scaled.Get());
        ComPtr<IMFSample> sample;
        must(mf.createSample(&sample),"scaled sample");
        must(sample->AddBuffer(buffer.Get()),"scaled sample attachment");
        return sample;
    }
};

inline void encoderProof(IMFSinkWriter* writer,DWORD stream) {
    ComPtr<IMFSinkWriterEx> ex;
    must(writer->QueryInterface(IID_PPV_ARGS(&ex)),"sink writer transform inspection");
    for(DWORD i=0;i<8;++i) {
        GUID category{};
        ComPtr<IMFTransform> mft;
        must(ex->GetTransformForStream(stream,i,&category,&mft),"find selected encoder");
        if(category!=MFT_CATEGORY_VIDEO_ENCODER)continue;
        ComPtr<IMFAttributes> a;
        must(mft->GetAttributes(&a),"encoder attributes");
        UINT32 n=0;
        must(a->GetStringLength(MFT_ENUM_HARDWARE_URL_Attribute,&n),"selected encoder hardware identity");
        if(!n)throw std::runtime_error("empty hardware identity");
        return;
    }
    throw std::runtime_error("No hardware encoder proof");
}

struct Vp9Timeline {
    frameProbe::Cadence cadence{static_cast<uint32_t>(second)};
    LONGLONG first=-1,last=-1,deficit=0;
    void observe(LONGLONG pts) {
        if(pts<0||(last>=0&&pts<=last)||cadence.samples==frameProbe::detail::sampleLimit)
            throw std::runtime_error("media-vp9-timeline-invalid");
        if(first<0)first=pts;
        if(pts-first>900*second)throw std::runtime_error("media-vp9-timeline-limit");
        if(last>=0){
            const auto step=second-(pts-last)*30;
            // Permit one millisecond of WebM timestamp rounding per window,
            // not per frame. Sustained faster spans still accumulate evidence.
            constexpr auto tolerance=(second/1000)*30;
            deficit=(std::max)(LONGLONG{0},deficit+step);
            if(step>tolerance)++cadence.shortSamples;
            cadence.fastWindow|=deficit>tolerance;
        }
        last=pts;
        ++cadence.samples;
    }
    frameProbe::Cadence finish(IMFSample* sample) {
        LONGLONG duration=0;
        if(!sample||FAILED(sample->GetSampleDuration(&duration))||duration<=0||duration>900*second-(last-first))return {};
        cadence.ticks=static_cast<uint64_t>(last-first+duration);
        return cadence;
    }
};

inline void verifyVp9Surface(IMFSample* sample) {
    ComPtr<IMFMediaBuffer> buffer;
    must(sample->GetBufferByIndex(0,&buffer),"VP9 buffer");
    ComPtr<IMFDXGIBuffer> dxgi;
    must(buffer.As(&dxgi),"VP9 decoded DXGI buffer");
    ComPtr<ID3D11Texture2D> texture;
    must(dxgi->GetResource(IID_PPV_ARGS(&texture)),"VP9 decoder texture");
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    if(desc.Format!=DXGI_FORMAT_NV12||!(desc.BindFlags&D3D11_BIND_DECODER))
        throw std::runtime_error("not a hardware VP9 NV12 decoder surface");
}

template<class Check>
unsigned transcode(const wchar_t* input,const wchar_t* output,UINT adapterIndex,const Check& check,
                   frameProbe::Cadence* webmPresentation=nullptr,
                   const std::function<const wchar_t*()>& earlyOutput={}) {
    check();
    must(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"initialize media apartment");
    struct Apartment{~Apartment(){CoUninitialize();}} apartment;
    const Foundation mf;
    ComPtr<IDXGIFactory1> factory;
    must(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"DXGI factory");
    ComPtr<IDXGIAdapter1> adapter;
    must(factory->EnumAdapters1(adapterIndex,&adapter),"adapter");
    DXGI_ADAPTER_DESC1 ad{};
    must(adapter->GetDesc1(&ad),"adapter description");
    if(ad.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)throw std::runtime_error("software adapter");
    ComPtr<ID3D11Device> device;
    must(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,
        D3D11_CREATE_DEVICE_VIDEO_SUPPORT|D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,
        &device,nullptr,nullptr),"D3D11 video device");
    ComPtr<ID3D11Multithread> mt;
    must(device.As(&mt),"D3D11 multithread");
    mt->SetMultithreadProtected(TRUE);
    ComPtr<IMFDXGIDeviceManager> dm;
    UINT token=0;
    must(mf.createManager(&token,&dm),"DXGI manager");
    must(dm->ResetDevice(device.Get(),token),"set DXGI device");

    ComPtr<IMFAttributes> ra;
    must(mf.createAttributes(&ra,3),"reader attrs");
    must(ra->SetUnknown(MF_SOURCE_READER_D3D_MANAGER,dm.Get()),"reader DXGI");
    must(ra->SetUINT32(MF_READWRITE_D3D_OPTIONAL,FALSE),"required DXGI");
    must(ra->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS,TRUE),"no decoder conversion");
    ComPtr<IMFSourceReader> reader;
    must(mf.createReader(input,ra.Get(),&reader),"reader");
    must(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS),FALSE),"deselect streams");
    must(reader->SetStreamSelection(video,TRUE),"select video");
    ComPtr<IMFMediaType> compressed;
    must(reader->GetNativeMediaType(video,0,&compressed),"native format");
    GUID codec{};
    must(compressed->GetGUID(MF_MT_SUBTYPE,&codec),"input codec");
    if(codec!=(webmPresentation?MFVideoFormat_VP90:MFVideoFormat_H264))
        throw std::runtime_error("media-hardware-source-codec-unsupported");
    auto nv12=videoType(mf,MFVideoFormat_NV12);
    must(reader->SetCurrentMediaType(video,nullptr,nv12.Get()),"decode to NV12");
    ComPtr<IMFSample> firstSample;
    DWORD firstFlags=0;
    LONGLONG firstPts=0;
    for(unsigned retry=0;!firstSample&&retry<16;++retry) {
        check();
        must(reader->ReadSample(video,0,nullptr,&firstFlags,&firstPts,&firstSample),"read initial decoded frame");
        if(firstFlags&(MF_SOURCE_READERF_ERROR|MF_SOURCE_READERF_ENDOFSTREAM))break;
    }
    if(!firstSample||(firstFlags&MF_SOURCE_READERF_ERROR))throw std::runtime_error("media-hardware-source-empty");
    ComPtr<IMFMediaType> raw;
    must(reader->GetCurrentMediaType(video,&raw),"decoder format");
    UINT32 w=0,h=0;
    must(MFGetAttributeSize(raw.Get(),MF_MT_FRAME_SIZE,&w,&h),"decoder size");
    UINT32 aspectX=1,aspectY=1,rotation=0;
    if((SUCCEEDED(MFGetAttributeRatio(raw.Get(),MF_MT_PIXEL_ASPECT_RATIO,&aspectX,&aspectY))&&(!aspectX||aspectX!=aspectY))||
        (SUCCEEDED(compressed->GetUINT32(MF_MT_VIDEO_ROTATION,&rotation))&&rotation))
        throw std::runtime_error("media-hardware-source-geometry-unsupported");
    const auto rect=visibleFrame(raw.Get(),compressed.Get(),{w,h});
    const auto size=fit720(static_cast<UINT>(rect.right-rect.left),static_cast<UINT>(rect.bottom-rect.top));
    const auto verifyDecoderType=[&] {
        UINT32 currentW=0,currentH=0;GUID subtype{};
        must(reader->GetCurrentMediaType(video,&raw),"changed decoder type");
        must(MFGetAttributeSize(raw.Get(),MF_MT_FRAME_SIZE,&currentW,&currentH),"changed decoder size");
        must(raw->GetGUID(MF_MT_SUBTYPE,&subtype),"changed decoder subtype");
        if(currentW!=w||currentH!=h||subtype!=MFVideoFormat_NV12)throw std::runtime_error("unexpected decoder change");
    };
    Vp9Timeline webmTimeline;
    ComPtr<IMFSample> secondSample;
    DWORD secondFlags=0;
    LONGLONG secondPts=0;
    if(!output){
        if(!webmPresentation)throw std::invalid_argument("VP9 probe requires a result");
        auto sample=std::move(firstSample);
        ComPtr<IMFSample> heldFirst;
        DWORD flags=firstFlags;LONGLONG pts=firstPts;
        frameProbe::Cadence observed;
        for(unsigned reads=0;reads<frameProbe::detail::sampleLimit;++reads){
            check();
            if(flags&(MF_SOURCE_READERF_ERROR|MF_SOURCE_READERF_STREAMTICK))
                throw std::runtime_error("media-vp9-probe-discontinuity");
            if(flags&(MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED|MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED))verifyDecoderType();
            if(sample){
                verifyVp9Surface(sample.Get());
                webmTimeline.observe(pts);
                observed=webmTimeline.finish(sample.Get());
                if(observed.exceeds30()){
                    *webmPresentation=observed;
                    // Reuse the decoder only on an early proof. Late proof
                    // must not pin an unbounded queue of decoder surfaces.
                    if(heldFirst&&observed.samples==2&&(output=earlyOutput())){
                        firstSample=std::move(heldFirst);
                        secondSample=std::move(sample);secondFlags=flags;secondPts=pts;
                        break;
                    }
                    return 0;
                }
                if(earlyOutput&&observed.samples==1)heldFirst=sample;
                else heldFirst.Reset();
            }
            if(flags&MF_SOURCE_READERF_ENDOFSTREAM){*webmPresentation=observed;return 0;}
            sample.Reset();
            must(reader->ReadSample(video,0,nullptr,&flags,&pts,&sample),"read VP9 presentation frame");
        }
        if(!output)throw std::runtime_error("media-vp9-probe-limit");
        webmTimeline={};
    }
    std::optional<Scaler> scaler;
    if(w!=size.width||h!=size.height||rect.left||rect.top||rect.right!=static_cast<LONG>(w)||rect.bottom!=static_cast<LONG>(h))
        scaler.emplace(device.Get(),Dimensions{w,h},size,rect,raw.Get());

    ComPtr<IMFAttributes> wa;
    must(mf.createAttributes(&wa,3),"writer attrs");
    must(wa->SetUnknown(MF_SINK_WRITER_D3D_MANAGER,dm.Get()),"writer DXGI");
    must(wa->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,TRUE),"writer hardware");
    must(wa->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING,TRUE),"write offline");
    ComPtr<IMFSinkWriter> writer;
    must(mf.createWriter(output,nullptr,wa.Get(),&writer),"writer");
    auto encoded=playbackType(mf,MFVideoFormat_H264,size);
    must(encoded->SetUINT32(MF_MT_AVG_BITRATE,2500000),"bitrate");
    must(encoded->SetUINT32(MF_MT_MPEG2_PROFILE,eAVEncH264VProfile_Main),"H264 main");
    DWORD stream=0;
    must(writer->AddStream(encoded.Get(),&stream),"encoded stream");
    auto encoderInput=playbackType(mf,MFVideoFormat_NV12,size);
    for(const auto& key:{MF_MT_VIDEO_PRIMARIES,MF_MT_TRANSFER_FUNCTION,MF_MT_YUV_MATRIX,MF_MT_VIDEO_NOMINAL_RANGE}) {
        UINT32 value=0;
        if(SUCCEEDED(raw->GetUINT32(key,&value)))must(encoderInput->SetUINT32(key,value),"input color metadata");
    }
    must(writer->SetInputMediaType(stream,encoderInput.Get(),nullptr),"encoder NV12");
    must(writer->BeginWriting(),"begin writing");
    encoderProof(writer.Get(),stream);

    LONGLONG first=-1,last=-1,previous=-1;
    ComPtr<IMFSample> heldPicture;
    const auto holdBuffer=[&](IMFSample* picture,bool independent) {
        ComPtr<IMFMediaBuffer> media;
        must(picture->GetBufferByIndex(0,&media),"held picture buffer");
        if(independent)return media;
        ComPtr<IMFDXGIBuffer> dxgi;
        must(media.As(&dxgi),"held decoder buffer");
        ComPtr<ID3D11Texture2D> source;
        must(dxgi->GetResource(IID_PPV_ARGS(&source)),"held decoder texture");
        UINT slice=0;
        must(dxgi->GetSubresourceIndex(&slice),"held decoder slice");
        D3D11_TEXTURE2D_DESC desc{};
        source->GetDesc(&desc);
        if(desc.MipLevels!=1||slice>=desc.ArraySize)throw std::runtime_error("media-hardware-held-surface-invalid");
        desc.ArraySize=1;desc.BindFlags=0;desc.MiscFlags=0;desc.CPUAccessFlags=0;desc.Usage=D3D11_USAGE_DEFAULT;
        ComPtr<ID3D11Texture2D> texture;
        must(device->CreateTexture2D(&desc,nullptr,&texture),"held texture");
        ComPtr<ID3D11DeviceContext> graphics;
        device->GetImmediateContext(&graphics);
        graphics->CopySubresourceRegion(texture.Get(),0,0,0,0,source.Get(),slice,nullptr);
        return surfaceBuffer(mf,texture.Get());
    };
    frameProbe::Cadence fullWebm;
    const auto writeFrame=[&](IMFSample* sample,LONGLONG frame) {
        const auto time=timestamp(frame);
        must(sample->SetSampleTime(time),"frame time");
        must(sample->SetSampleDuration(timestamp(frame+1)-time),"frame duration");
        must(writer->WriteSample(stream,sample),"write frame");
        last=frame;
    };

    for(;;) {
        check();
        ComPtr<IMFSample> inputSample=std::move(firstSample);
        DWORD flags=firstFlags;
        LONGLONG pts=firstPts;
        if(!inputSample&&secondSample){inputSample=std::move(secondSample);flags=secondFlags;pts=secondPts;}
        if(!inputSample)must(reader->ReadSample(video,0,nullptr,&flags,&pts,&inputSample),"read decoded frame");
        if(flags&MF_SOURCE_READERF_ERROR)throw std::runtime_error("decoder error");
        if(flags&MF_SOURCE_READERF_STREAMTICK)throw std::runtime_error("media-hardware-source-discontinuity");
        if(flags&(MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED|MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED))verifyDecoderType();
        if(inputSample) {
            if(webmPresentation){
                webmTimeline.observe(pts);
                fullWebm=webmTimeline.finish(inputSample.Get());
            }
            if(first<0)first=pts;
            if(pts<0||pts<previous||pts-first>900*second)throw std::runtime_error("media-hardware-source-timestamp-range");
            previous=pts;
            const auto frame=((pts-first)*30+second/4)/second;
            if(frame>last) {
                if(frame-last>30)throw std::runtime_error("media-hardware-source-timestamp-gap");
                if(last+1<frame) {
                    // The encoder can outlive the decoder sample. Give repeats
                    // a private GPU buffer and reuse it for this interval.
                    const auto heldSurface=holdBuffer(heldPicture.Get(),scaler.has_value()||webmPresentation);
                    while(last+1<frame) {
                        check();
                        ComPtr<IMFSample> repeat;
                        must(mf.createSample(&repeat),"held sample");
                        must(repeat->AddBuffer(heldSurface.Get()),"held sample attachment");
                        writeFrame(repeat.Get(),last+1);
                    }
                }
                heldPicture.Reset();
                ComPtr<IMFMediaBuffer> media;
                must(inputSample->GetBufferByIndex(0,&media),"decode buffer");
                ComPtr<IMFDXGIBuffer> dxgi;
                must(media.As(&dxgi),"decoded DXGI buffer, no CPU fallback");
                ComPtr<ID3D11Texture2D> texture;
                must(dxgi->GetResource(IID_PPV_ARGS(&texture)),"video texture");
                D3D11_TEXTURE2D_DESC textureDesc{};
                texture->GetDesc(&textureDesc);
                if(textureDesc.Format!=DXGI_FORMAT_NV12||!(textureDesc.BindFlags&D3D11_BIND_DECODER))
                    throw std::runtime_error("not a hardware NV12 decoder surface");
                if(scaler) {
                    UINT subresource=0;
                    must(dxgi->GetSubresourceIndex(&subresource),"decoder surface index");
                    if(textureDesc.MipLevels!=1||subresource>=textureDesc.ArraySize)
                        throw std::runtime_error("media-hardware-decode-surface-invalid");
                    inputSample=scaler->scale(mf,texture.Get(),subresource);
                }else if(webmPresentation){
                    // Sharing VP9 decoder-owned surfaces with the encoder lost
                    // input frames on both tested GPU vendors. Copy only the
                    // selected frames; the surface never leaves the GPU.
                    const auto own=holdBuffer(inputSample.Get(),false);
                    inputSample.Reset();dxgi.Reset();media.Reset();texture.Reset();
                    must(mf.createSample(&inputSample),"VP9 encoder sample");
                    must(inputSample->AddBuffer(own.Get()),"VP9 encoder surface");
                }
                writeFrame(inputSample.Get(),frame);
                heldPicture=std::move(inputSample);
            }
        }
        if(flags&MF_SOURCE_READERF_ENDOFSTREAM)break;
    }
    if(last<1)throw std::runtime_error("media-hardware-output-empty");
    if(webmPresentation){
        if(!fullWebm.exceeds30())throw std::runtime_error("media-vp9-timeline-not-confirmed");
        *webmPresentation=fullWebm;
    }
    check();
    must(writer->Finalize(),"finish MP4");
    check();
    return static_cast<unsigned>(last+1);
}
} // namespace ymcc::media::hardwareVideo
