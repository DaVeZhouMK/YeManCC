#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "../native/shared_game_media_frame_probe.h"
#include "../native/shared_game_media_hardware_video.h"
#include "../native/shared_game_media_playback30_deadline.h"
#include <iostream>

constexpr bool playbackDeadlineContract() {
    using namespace ymcc::media::playback30;
    Deadline stuck, live, reset;
    for(uint64_t at=50000;at<65000;at+=5000)stuck.observe(at,128);
    if(stuck.expires()!=65000||stuck.shouldObserve(65000))return false;
    for(uint64_t at=50000;at<185000;at+=5000)live.observe(at,at);
    if(live.expires()!=185000||live.shouldObserve(185000))return false;
    reset.observe(50000,128);reset.observe(55000,256);
    if(reset.expires()!=70000)return false;
    reset.observe(60000,std::nullopt);
    if(reset.expires()!=70000)return false;
    reset.observe(65000,64);
    return reset.expires()==65000&&!reset.shouldObserve(65000);
}
static_assert(playbackDeadlineContract());

static bool playbackDeadlineFileContract() {
    using namespace ymcc::media::playback30;
    const auto missing=std::make_error_code(std::errc::no_such_file_or_directory);
    const auto denied=std::make_error_code(std::errc::permission_denied);
    const auto noSize=(std::numeric_limits<uint64_t>::max)();
    Deadline late, unknown, empty, replaced, unchanged;
    late.observeFileSize(50000,noSize,missing);
    late.observeFileSize(55000,noSize,missing);
    late.observeFileSize(60000,128,{});
    if(late.expires()!=75000||!late.shouldObserve(65000))return false;
    late.observeFileSize(65000,256,{});
    if(late.expires()!=80000)return false;
    unknown.observeFileSize(50000,noSize,denied);
    unknown.observeFileSize(55000,noSize,denied);
    unknown.observeFileSize(60000,128,{});
    if(unknown.expires()!=65000)return false;
    empty.observeFileSize(50000,noSize,missing);
    empty.observeFileSize(55000,0,{});
    empty.observeFileSize(60000,0,{});
    if(empty.expires()!=65000)return false;
    replaced.observeFileSize(50000,32,{});
    replaced.observeFileSize(55000,128,{});
    replaced.observeFileSize(60000,noSize,missing);
    if(replaced.expires()!=65000)return false;
    unchanged.observeFileSize(50000,noSize,missing);
    unchanged.observeFileSize(55000,noSize,missing);
    unchanged.observeFileSize(60000,noSize,missing);
    return unchanged.expires()==65000;
}

int wmain(int argc,wchar_t** argv) {
    if(!playbackDeadlineFileContract()) {
        std::cerr<<"playback30 staging growth deadline contract failed\n";
        return 5;
    }
    if(argc==2&&std::wstring_view(argv[1])==L"--deadline-only") {
        std::cout<<"playback30 staging growth deadline contract passed\n";
        return 0;
    }
    if(argc!=4&&argc!=5)return 2;
    const std::filesystem::path input=argv[1],output=argv[2];
    std::error_code ec;
    if(output.extension()!=L".mp4"||std::filesystem::exists(output,ec)||ec)return 3;
    try {
        const auto before=ymcc::media::frameProbe::inspectMp4(input);
        if(!before.exceeds30())return 4;
        const auto start=GetTickCount64();
        unsigned checks=0;
        const bool longBudget=argc==5&&std::wstring_view(argv[4])==L"--long-budget";
        ymcc::media::playback30::Deadline budget;
        const auto adapter=static_cast<UINT>(std::stoul(argv[3]));
        const auto count=ymcc::media::hardwareVideo::transcode(input.c_str(),output.c_str(),adapter,[&]{
            const auto elapsed=GetTickCount64()-start;
            if(longBudget&&budget.shouldObserve(elapsed)) {
                std::error_code error;const auto bytes=std::filesystem::file_size(output,error);
                budget.observeFileSize(elapsed,bytes,error);
            }
            if(elapsed>=(longBudget?(std::min)(budget.expires(),ymcc::media::playback30::workerHardMs):60000)||
               (argc==5&&!longBudget&&++checks==60))throw std::runtime_error("fixture-cancelled");
        });
        const auto after=ymcc::media::frameProbe::inspectMp4(output);
        const double beforeSeconds=double(before.ticks)/before.timescale;
        const double afterSeconds=after?double(after.ticks)/after.timescale:0;
        if(!after||after.exceeds30()||after.averageFps()<29.99||after.samples!=count||
            std::abs(beforeSeconds-afterSeconds)>0.035)throw std::runtime_error("invalid-720p30-output-timeline");
        std::cout<<"adapter="<<adapter<<" hardware decoder and encoder verified; "<<after.samples<<" frames; "<<
            after.averageFps()<<" FPS; "<<GetTickCount64()-start<<" ms\n";
        return 0;
    }catch(const std::exception& e){
        std::filesystem::remove(output,ec);
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
