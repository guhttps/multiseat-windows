#include "frame-readback.h"

#include <array>
#include <cstdio>

using Microsoft::WRL::ComPtr;
using multiseat::native::display::FrameDigest;
using multiseat::native::display::FrameReadback;

namespace {

unsigned checks = 0;

void Require(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        std::printf("FAIL: %s\n", message);
        ExitProcess(1);
    }
}

bool Empty(const FrameDigest& digest) {
    return !digest.width && !digest.height && !digest.sequence && !digest.checksum;
}

} // namespace

int wmain() {
    ComPtr<ID3D11Device> device, other;
    ComPtr<ID3D11DeviceContext> context;
    Require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        &device, nullptr, &context)), "create software D3D11 device");
    Require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        &other, nullptr, nullptr)), "create independent software device");

    HANDLE cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Require(cancel != nullptr, "create cancellation event");
    struct Close {
        HANDLE handle;
        ~Close() { CloseHandle(handle); }
    } close{cancel};

    constexpr UINT width = 64, height = 32, stride = width * 4 + 32;
    std::array<BYTE, stride * height> pixels{};
    pixels.fill(0xcc);
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width * 4; ++x)
            pixels[y * stride + x] = static_cast<BYTE>((x * 3 + y * 17) & 255);

    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    D3D11_SUBRESOURCE_DATA initial{pixels.data(), stride, 0};
    ComPtr<ID3D11Texture2D> source, foreign, oversize, unsupported, srgb, resized;
    Require(SUCCEEDED(device->CreateTexture2D(&description, &initial, &source)), "create source");
    Require(SUCCEEDED(other->CreateTexture2D(&description, &initial, &foreign)), "create foreign source");
    auto large = description;
    large.Width = 1920;
    large.Height = 1080;
    Require(SUCCEEDED(device->CreateTexture2D(&large, nullptr, &oversize)), "create size refusal fixture");
    auto rgba = description;
    rgba.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Require(SUCCEEDED(device->CreateTexture2D(&rgba, nullptr, &unsupported)), "create format refusal fixture");
    auto gamma = description;
    gamma.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    Require(SUCCEEDED(device->CreateTexture2D(&gamma, &initial, &srgb)), "create sRGB fixture");
    auto resized_description = description;
    resized_description.Width = 32;
    resized_description.Height = 16;
    Require(SUCCEEDED(device->CreateTexture2D(&resized_description, &initial, &resized)), "create changed dimensions fixture");

    // Independent fixture hash over logical pixels; sentinel padding excluded.
    std::uint64_t expected = 14695981039346656037ULL;
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width * 4; ++x) {
            expected ^= pixels[y * stride + x];
            expected *= 1099511628211ULL;
        }

    FrameReadback reader(device.Get());
    FrameDigest digest{};
    const auto Read = [&](ID3D11Texture2D* texture) {
        return reader.Read(texture, cancel, GetTickCount64() + 5000, digest);
    };
    Require(Read(source.Get()) == S_OK && digest.width == width && digest.height == height &&
            digest.sequence == 1 && digest.checksum == expected, "logical pixels and first sequence");
    pixels.fill(0x29);
    context->UpdateSubresource(source.Get(), 0, nullptr, pixels.data(), stride, 0);
    Require(Read(source.Get()) == S_OK && digest.sequence == 2 && digest.checksum != expected,
            "real pixel update changes digest");
    Require(Read(foreign.Get()) == E_INVALIDARG && Empty(digest), "reject foreign device");
    Require(Read(oversize.Get()) == E_INVALIDARG && Empty(digest), "reject oversized image");
    Require(Read(unsupported.Get()) == DXGI_ERROR_UNSUPPORTED && Empty(digest), "reject unsupported format");
    Require(Read(nullptr) == E_INVALIDARG && Empty(digest), "reject null source");
    Require(reader.Read(source.Get(), nullptr, GetTickCount64() + 5000, digest) == E_INVALIDARG &&
            Empty(digest), "reject null cancellation event");
    Require(reader.Read(source.Get(), INVALID_HANDLE_VALUE, GetTickCount64() + 5000, digest) ==
            E_INVALIDARG && Empty(digest), "reject invalid cancellation sentinel");
    FrameReadback no_device(nullptr);
    Require(no_device.Read(source.Get(), cancel, GetTickCount64() + 5000, digest) == E_INVALIDARG &&
            Empty(digest), "reject unavailable device");
    Require(reader.Read(source.Get(), cancel, GetTickCount64(), digest) ==
            HRESULT_FROM_WIN32(ERROR_TIMEOUT) && Empty(digest), "reject expired deadline");
    Require(SetEvent(cancel) != FALSE, "signal cancellation");
    Require(Read(source.Get()) == HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED) && Empty(digest),
            "reject cancellation");
    Require(ResetEvent(cancel) != FALSE, "reset cancellation");
    Require(Read(srgb.Get()) == S_OK && digest.sequence == 3 && digest.checksum == expected,
            "format replacement succeeds without counting failed copies");
    Require(Read(resized.Get()) == S_OK && digest.sequence == 4 && digest.width == 32 &&
            digest.height == 16, "staging texture follows dimension changes");
    Require(Read(source.Get()) == S_OK && digest.sequence == 5 && digest.width == width,
            "original dimensions can be restored");

    std::printf("PASS: %u checks on generated WARP textures; no desktop capture, driver or session changes.\n", checks);
    return 0;
}
