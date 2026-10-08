#pragma once
// Read-only Windows file/shortcut icon extraction. No target execution or disk writes.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <wincrypt.h>
#include <algorithm>
#include <string>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "crypt32.lib")

namespace ymcc::quick_app_icons {
using Microsoft::WRL::ComPtr;

struct ComScope {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); }
    bool valid() const { return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE; }
};
struct IconScope {
    HICON value = nullptr;
    ~IconScope() { if (value) DestroyIcon(value); }
};

// Preserve the source shape, contrast and transparency, but strip all color.
// Pixels are straight-alpha BGRA, not premultiplied; alpha is left unchanged.
inline void grayscalePixels(std::vector<BYTE>& pixels) {
    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
        const BYTE gray = static_cast<BYTE>((29u * pixels[i] + 150u * pixels[i + 1] +
                                            77u * pixels[i + 2] + 128u) >> 8);
        pixels[i] = pixels[i + 1] = pixels[i + 2] = gray;
    }
}

// Caller owns a COM apartment and the icon. All encoder state stays local.
inline std::string monochromePngDataUrl(HICON icon) {
    if (!icon) return {};
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory)))) return {};
    ComPtr<IWICBitmap> bitmap;
    if (FAILED(factory->CreateBitmapFromHICON(icon, &bitmap))) return {};
    UINT width = 0, height = 0;
    if (FAILED(bitmap->GetSize(&width, &height)) || !width || !height || width > 256 || height > 256) return {};
    ComPtr<IWICBitmapSource> source = bitmap;
    // Bound response size regardless of the machine's DPI/icon metrics.
    if (width > 64 || height > 64) {
        ComPtr<IWICBitmapScaler> scaler;
        const UINT maxSize = (std::max)(width, height);
        width = (std::max)(1u, width * 64u / maxSize);
        height = (std::max)(1u, height * 64u / maxSize);
        if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
            FAILED(scaler->Initialize(bitmap.Get(), width, height, WICBitmapInterpolationModeFant))) return {};
        source = scaler;
    }
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppBGRA,
                                    WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return {};
    const UINT stride = width * 4;
    std::vector<BYTE> pixels(stride * height);
    if (FAILED(converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data()))) return {};
    grayscalePixels(pixels);

    ComPtr<IStream> stream;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return {};
    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return {};
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize(width, height))) return {};
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&format)) || !IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA) ||
        FAILED(frame->WritePixels(height, stride, static_cast<UINT>(pixels.size()), pixels.data())) ||
        FAILED(frame->Commit()) || FAILED(encoder->Commit())) return {};

    STATSTG info{};
    if (FAILED(stream->Stat(&info, STATFLAG_NONAME)) || !info.cbSize.QuadPart || info.cbSize.QuadPart > 32768) return {};
    LARGE_INTEGER beginning{};
    if (FAILED(stream->Seek(beginning, STREAM_SEEK_SET, nullptr))) return {};
    std::vector<BYTE> png(static_cast<size_t>(info.cbSize.QuadPart));
    ULONG count = 0;
    if (FAILED(stream->Read(png.data(), static_cast<ULONG>(png.size()), &count)) || count != png.size()) return {};
    DWORD length = 0;
    const DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
    if (!CryptBinaryToStringA(png.data(), count, flags, nullptr, &length)) return {};
    std::string encoded(length, '\0');
    if (!CryptBinaryToStringA(png.data(), count, flags, encoded.data(), &length)) return {};
    if (!encoded.empty() && encoded.back() == '\0') encoded.pop_back();
    return "data:image/png;base64," + encoded;
}

inline std::string fileIconDataUrl(std::wstring path) {
    // Don't resolve shell URLs, UNC/mapped network drives or missing files just
    // to paint a card. Unsupported locations fall back without blocking saves.
    if (path.size() < 3 || path.size() >= MAX_PATH || path.find(L'\0') != std::wstring::npos ||
        !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) ||
        path[1] != L':' || (path[2] != L'\\' && path[2] != L'/')) return {};
    std::replace(path.begin(), path.end(), L'/', L'\\');
    const std::wstring drive = path.substr(0, 3);
    const UINT driveType = GetDriveTypeW(drive.c_str());
    if (driveType != DRIVE_FIXED && driveType != DRIVE_REMOVABLE && driveType != DRIVE_RAMDISK) return {};
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return {};

    ComScope com;
    if (!com.valid()) return {};
    SHFILEINFOW fileInfo{};
    // SHGFI_ICON honors a shortcut's configured icon and its target's icon.
    // This only reads the shell icon; it never launches the application.
    const auto result = SHGetFileInfoW(path.c_str(), 0, &fileInfo, sizeof(fileInfo), SHGFI_ICON | SHGFI_LARGEICON);
    IconScope icon{fileInfo.hIcon};
    if (!result || !icon.value) return {};
    return monochromePngDataUrl(icon.value);
}
} // namespace ymcc::quick_app_icons
