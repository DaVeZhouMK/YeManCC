// Same production extraction code; only test fixtures are written under Validation.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "quick_app_file_icons.h"
#include <shobjidl.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <json.hpp>
#pragma comment(lib, "user32.lib")

using namespace ymcc::quick_app_icons;
using json = nlohmann::json;
static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

static json checkPng(const std::string& url) {
    const std::string prefix = "data:image/png;base64,";
    check(url.starts_with(prefix) && url.size() < 65536, "Expected bounded PNG data URL");
    const auto b64 = url.substr(prefix.size());
    DWORD length = 0;
    check(CryptStringToBinaryA(b64.c_str(), static_cast<DWORD>(b64.size()), CRYPT_STRING_BASE64, nullptr, &length, nullptr, nullptr), "base64 size");
    std::vector<BYTE> png(length);
    check(CryptStringToBinaryA(b64.c_str(), static_cast<DWORD>(b64.size()), CRYPT_STRING_BASE64, png.data(), &length, nullptr, nullptr), "base64 decode");
    check(length > 8 && png[0] == 137 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G', "PNG signature");
    ComPtr<IWICImagingFactory> factory;
    check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))), "WIC factory");
    ComPtr<IWICStream> stream;
    check(SUCCEEDED(factory->CreateStream(&stream)), "WIC stream");
    check(SUCCEEDED(stream->InitializeFromMemory(png.data(), length)), "PNG memory stream");
    ComPtr<IWICBitmapDecoder> decoder;
    check(SUCCEEDED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)), "PNG decode");
    ComPtr<IWICBitmapFrameDecode> frame;
    check(SUCCEEDED(decoder->GetFrame(0, &frame)), "PNG frame");
    UINT width = 0, height = 0;
    check(SUCCEEDED(frame->GetSize(&width, &height)) && width > 0 && height > 0 && width <= 64 && height <= 64, "Icon dimensions");
    ComPtr<IWICFormatConverter> converter;
    check(SUCCEEDED(factory->CreateFormatConverter(&converter)), "Decode converter");
    check(SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)), "BGRA decode");
    std::vector<BYTE> pixels(width * height * 4);
    check(SUCCEEDED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data())), "Read decoded pixels");
    bool visible = false, transparent = false;
    for (size_t i = 0; i < pixels.size(); i += 4) {
        check(pixels[i] == pixels[i + 1] && pixels[i] == pixels[i + 2], "All RGB channels must be equal (no color)");
        visible |= pixels[i + 3] > 0;
        transparent |= pixels[i + 3] < 255;
    }
    check(visible && transparent, "Icon shape and alpha must be preserved");
    return { {"width", width}, {"height", height}, {"pngBytes", length}, {"grayscale", true}, {"hasTransparency", true} };
}

int wmain(int argc, wchar_t** argv) {
    try {
        check(argc == 2, "Output directory argument required");
        const std::filesystem::path out(argv[1]);
        std::filesystem::create_directories(out);
        ComScope com;
        check(com.valid(), "Test COM initialization");
        std::vector<std::string> cases;
        std::vector<BYTE> colored = { 10, 180, 250, 47, 255, 0, 0, 255, 0, 0, 0, 0 };
        grayscalePixels(colored);
        check(colored[0] == colored[1] && colored[1] == colored[2] && colored[3] == 47 && colored[7] == 255 && colored[11] == 0, "grayscale/alpha conversion");
        cases.push_back("Grayscale conversion preserves alpha for opaque/transparent/partial-alpha pixels");
        for (const auto& path : { L"", L"relative.exe", L"https://example.com/app.exe", L"\\\\server\\share\\app.exe", L"C:\\Windows", L"C:\\nonexistent-ymcc-icon-fixture.exe" })
            check(fileIconDataUrl(path).empty(), "Unsupported/missing input should use fallback");
        check(fileIconDataUrl(std::wstring(L"C:\\Windows\\explorer.exe\0suffix", 31)).empty(), "Embedded NUL rejected");
        cases.push_back("Empty/missing/folder/relative/URL/network/NUL inputs return fallback without executing a target");

        wchar_t winDir[MAX_PATH]{}, sysDir[MAX_PATH]{};
        check(GetWindowsDirectoryW(winDir, MAX_PATH) && GetSystemDirectoryW(sysDir, MAX_PATH), "Windows directories");
        const std::wstring explorer = std::wstring(winDir) + L"\\explorer.exe";
        const std::wstring taskmgr = std::wstring(sysDir) + L"\\Taskmgr.exe";
        const auto first = fileIconDataUrl(explorer), second = fileIconDataUrl(taskmgr);
        const auto firstMeta = checkPng(first), secondMeta = checkPng(second);
        check(first != second, "Different real applications must not share a fixed icon");
        cases.push_back("Two real EXEs produce different source-derived PNGs; every decoded pixel is grayscale");

        const auto shortcut = out / L"图标测试' 快捷方式.lnk";
        ComPtr<IShellLinkW> link;
        check(SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))), "Create test shortcut");
        check(SUCCEEDED(link->SetPath(explorer.c_str())) && SUCCEEDED(link->SetIconLocation(taskmgr.c_str(), 0)), "Configure different shortcut icon");
        ComPtr<IPersistFile> persist;
        check(SUCCEEDED(link.As(&persist)) && SUCCEEDED(persist->Save(shortcut.c_str(), TRUE)), "Save isolated shortcut fixture");
        const auto shortcutIcon = fileIconDataUrl(shortcut.wstring());
        checkPng(shortcutIcon);
        check(shortcutIcon != first, "Shortcut must honor its configured icon instead of the target's fixed mark");
        cases.push_back("Unicode/apostrophe/space .lnk path uses its own configured icon without launching Explorer");

        const DWORD resourcesBefore = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        for (int i = 0; i < 20; i++) check(fileIconDataUrl(taskmgr) == second, "Repeated icon extraction");
        const DWORD resourcesAfter = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        check(resourcesAfter <= resourcesBefore + 1, "HICON handles must be released");
        cases.push_back("Twenty repeated extractions are stable and do not leak icon handles");
        std::vector<std::future<std::string>> jobs;
        for (int i = 0; i < 4; i++) jobs.push_back(std::async(std::launch::async, [taskmgr] { return fileIconDataUrl(taskmgr); }));
        for (auto& job : jobs) check(job.get() == second, "Worker extraction must initialize COM and remain deterministic");
        cases.push_back("Concurrent background threads own COM/encoder state and return identical grayscale output");

        json report = { {"suite", "Quick app native file icons"}, {"passed", cases.size()}, {"cases", cases}, {"explorer", firstMeta}, {"taskmgr", secondMeta}, {"scope", "Actual Windows Shell/WIC extraction, no app launch or installed settings writes"} };
        std::ofstream(out / "native-results.json") << report.dump(2);
        std::ofstream(out / "file-icon-fixtures.json") << json({ {"first", first}, {"second", second}, {"shortcut", shortcutIcon} }).dump(2);
        std::cout << report.dump(2) << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Quick app native icons selftest failed: " << e.what() << '\n';
        return 1;
    }
}

