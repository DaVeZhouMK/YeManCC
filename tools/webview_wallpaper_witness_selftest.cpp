// Build on Windows against the repository WebView2 and nlohmann include paths.
#define NOMINMAX
#include <windows.h>
#include "webview_wallpaper_witness.h"
#include <iostream>
#include <stdexcept>

namespace witness = ymcc::webview::wallpaperWitness;
using witness::Evidence;
using witness::Json;
constexpr auto asset = L"54ec20aeb09c053ae63220918d00a3f5";

void require(bool ok, const char* description) {
    if (!ok) throw std::runtime_error(description);
}

Json properties(const char* decoder, const char* platform) {
    return {{"playerId", "p1"}, {"properties", Json::array({
        {{"name", "kVideoDecoderName"}, {"value", decoder}},
        {{"name", "kIsPlatformVideoDecoder"}, {"value", platform}}})}};
}

Json opened(const std::string& url, const char* id = "p1") {
    return {{"playerId", id}, {"events", Json::array({{{"value", Json({{"url", url}}).dump()}}})}};
}

Json lifecycle(const char* event, const char* id) {
    return {{"playerId", id}, {"events", Json::array({{{"value", Json({{"event", event}}).dump()}}})}};
}

int main() {
    try {
        require(witness::assetId(asset) && !witness::assetId(L"54EC20aeb09c053ae63220918d00a3f5") &&
            !witness::assetId(L"") && !witness::assetId(L"a/b"), "invalid launch arguments");
        require(witness::mainDocument(L"https://app.localhost/index.html#/steam") &&
            !witness::mainDocument(L"https://app.localhost/index.html?other=1") &&
            !witness::mainDocument(L"https://app.localhost.invalid/index.html") &&
            !witness::mainDocument(L"https://app.localhost/notice-output.html"), "wrong document source");
        require(witness::receiptMessage(Json::parse(R"({"sample": {}, "ymccWallpaperWitness": 1})")) &&
            !witness::receiptMessage(Json::parse(R"({"id": 1, "cmd": "settings.read"})")) &&
            !witness::receiptMessage({{"ymccWallpaperWitness", true}, {"sample", nullptr}}), "IPC shape or key order");

        Evidence hardware(asset);
        const auto url = hardware.url();
        Json sample = {{"url", url}, {"fps", 30.02}, {"frames", 78}, {"steadyDrops", 0},
            {"sampleDrops", 0}, {"width", 1280}, {"height", 720}};
        hardware.add(properties("D3D11VideoDecoder", "true"), true);
        for (int i = 0; i < 40; ++i) hardware.add(opened(url), false);
        require(hardware.result(sample).at("status") == "verified", "hardware, repeated event for same player");
        for (const auto& [key, value] : std::initializer_list<std::pair<const char*, Json>>{
                {"steadyDrops", 1}, {"sampleDrops", 1}, {"fps", 60.0}, {"frames", 59}}) {
            auto invalid = sample;
            invalid[key] = value;
            require(hardware.result(invalid).at("status") == "measured", key);
        }
        auto malformed = sample;
        malformed["height"] = "720";
        require(hardware.result(malformed).at("status") == "sample-interrupted", "malformed renderer data");
        malformed = sample;
        malformed["frames"] = 4294967374ULL;
        require(hardware.result(malformed).at("status") == "sample-interrupted", "overflow cannot qualify");
        auto wrongRevision = url;
        wrongRevision.back() = '0';
        Evidence other(asset);
        other.add(opened(wrongRevision), false);
        other.add(properties("D3D11VideoDecoder", "true"), true);
        require(other.result(sample).at("status") == "decoder-not-unique", "asset match requires exact revision");
        other.add(opened(url), false);
        require(other.result(sample).at("status") == "verified", "event after properties");
        other.add(opened(url, "p2"), false);
        other.add(lifecycle("kPause", "p2"), false);
        require(other.result(sample).at("status") == "decoder-not-unique", "repeated players cannot qualify");
        other.add(lifecycle("kWebMediaPlayerDestroyed", "p2"), false);
        other.add(opened(url, "p2"), false);
        require(other.result(sample).at("status") == "verified" &&
            other.result(sample).at("retiredMatchingPlayers") == 1, "destroyed player replay cannot invalidate a live match");
        other.add(lifecycle("kWebMediaPlayerDestroyed", "p3"), false);
        other.add(opened(url, "p3"), false);
        require(other.result(sample).at("status") == "verified", "late historical load cannot revive a destroyed player");
        other.add(lifecycle("kWebMediaPlayerDestroyed", "p1"), false);
        require(other.result(sample).at("status") == "decoder-not-unique", "destroyed target cannot qualify on old decoder evidence");
        hardware.add(properties("FFmpegVideoDecoder", "false"), true);
        require(hardware.result(sample).at("status") == "decoder-changed", "decoder transition cannot qualify");

        Evidence software(asset);
        software.add(opened(url), false);
        require(software.result(sample).at("status") == "decoder-missing", "no decoder evidence");
        software.add(properties("FFmpegVideoDecoder", "false"), true);
        const auto softwareResult = software.result(sample);
        require(softwareResult.at("status") == "measured" && softwareResult.at("d3d11") == false,
            "30 presented FPS alone is not a hardware pass");
        auto closed = std::make_shared<witness::Session>(nullptr, asset, [](Json) {});
        closed->close();
        require(!closed->start() && !closed->message(nullptr, nullptr, nullptr) && !closed->active(),
            "closed session cannot resume");
        std::cout << "WEBVIEW_WALLPAPER_WITNESS_SELFTEST_OK\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
