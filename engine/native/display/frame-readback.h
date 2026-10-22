#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <wrl.h>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace multiseat::native::display {

// Diagnostic checksum, not an authentication tag. Contains no image bytes.
struct FrameDigest {
    UINT width = 0;
    UINT height = 0;
    std::uint64_t sequence = 0;
    std::uint64_t checksum = 0;
};

// Reads an already supplied D3D11 texture. Does not acquire a desktop, load a
// driver, export pixels or authorize the caller to observe another session.
// One owner serializes all calls and access to the device's immediate context.
class FrameReadback final {
public:
    static constexpr UINT MaxWidth = 1280;
    static constexpr UINT MaxHeight = 720;

    explicit FrameReadback(ID3D11Device* device) noexcept : device_(device) {
        if (device_) device_->GetImmediateContext(&context_);
    }

    FrameReadback(const FrameReadback&) = delete;
    FrameReadback& operator=(const FrameReadback&) = delete;

    // The caller retains source/cancel through this call. cancel must be an
    // event with SYNCHRONIZE access. deadline is an absolute GetTickCount64()
    // value. Failure clears output and does not advance the sequence.
    // Polling and CPU work check cancellation/deadline. A driver call itself
    // can stall; this API cannot guarantee a hard deadline for a faulty GPU.
    [[nodiscard]] HRESULT Read(ID3D11Texture2D* source, HANDLE cancel,
                               ULONGLONG deadline, FrameDigest& output) noexcept {
        output = {};
        if (!source || !device_ || !context_ || !cancel || cancel == INVALID_HANDLE_VALUE)
            return E_INVALIDARG;
        HRESULT result = Check(cancel, deadline);
        if (FAILED(result)) return result;
        if (sequence_ == (std::numeric_limits<std::uint64_t>::max)())
            return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);

        D3D11_TEXTURE2D_DESC description{};
        source->GetDesc(&description);
        if (!description.Width || description.Width > MaxWidth ||
            !description.Height || description.Height > MaxHeight ||
            description.ArraySize != 1 || description.MipLevels != 1 ||
            description.SampleDesc.Count != 1 || description.SampleDesc.Quality != 0)
            return E_INVALIDARG;
        if (description.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
            description.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
            return DXGI_ERROR_UNSUPPORTED;

        Microsoft::WRL::ComPtr<ID3D11Device> owner;
        Microsoft::WRL::ComPtr<IUnknown> actual, expected;
        source->GetDevice(&owner);
        result = owner.As(&actual);
        if (FAILED(result)) return result;
        result = device_.As(&expected);
        if (FAILED(result)) return result;
        if (actual.Get() != expected.Get()) return E_INVALIDARG;

        if (!staging_ || current_.Width != description.Width ||
            current_.Height != description.Height || current_.Format != description.Format) {
            description.Usage = D3D11_USAGE_STAGING;
            description.BindFlags = 0;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            description.MiscFlags = 0;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> replacement;
            result = device_->CreateTexture2D(&description, nullptr, &replacement);
            if (FAILED(result)) return result;
            staging_ = replacement;
            current_ = description;
        }

        context_->CopyResource(staging_.Get(), source);
        context_->Flush();
        D3D11_MAPPED_SUBRESOURCE mapped{};
        for (;;) {
            result = Check(cancel, deadline);
            if (FAILED(result)) return result;
            result = context_->Map(staging_.Get(), 0, D3D11_MAP_READ,
                                   D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
            if (result != DXGI_ERROR_WAS_STILL_DRAWING) break;
            // Bound the wait independently; Check handles the exact deadline.
            if (WaitForSingleObject(cancel, 1) != WAIT_TIMEOUT)
                return HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
        }
        if (FAILED(result)) return result;

        struct Unmap {
            ID3D11DeviceContext* context;
            ID3D11Texture2D* texture;
            ~Unmap() { context->Unmap(texture, 0); }
        } unmap{context_.Get(), staging_.Get()};

        const UINT row_bytes = description.Width * 4;
        if (!mapped.pData || mapped.RowPitch < row_bytes) return E_UNEXPECTED;
        std::uint64_t hash = 14695981039346656037ULL;
        for (UINT y = 0; y < description.Height; ++y) {
            result = Check(cancel, deadline);
            if (FAILED(result)) return result;
            const auto row = static_cast<const BYTE*>(mapped.pData) +
                             static_cast<std::size_t>(y) * mapped.RowPitch;
            for (UINT x = 0; x < row_bytes; ++x) {
                hash ^= row[x];
                hash *= 1099511628211ULL;
            }
        }
        result = Check(cancel, deadline);
        if (FAILED(result)) return result;
        output = {description.Width, description.Height, ++sequence_, hash};
        return S_OK;
    }

private:
    static HRESULT Check(HANDLE cancel, ULONGLONG deadline) noexcept {
        if (WaitForSingleObject(cancel, 0) != WAIT_TIMEOUT)
            return HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
        return GetTickCount64() >= deadline ? HRESULT_FROM_WIN32(ERROR_TIMEOUT) : S_OK;
    }

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_;
    D3D11_TEXTURE2D_DESC current_{};
    std::uint64_t sequence_ = 0;
};

} // namespace multiseat::native::display
